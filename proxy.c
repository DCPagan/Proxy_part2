#include"proxy.h"

pthread_t tap_tid, listen_tid,
	eth_tid[CONNECTION_MAX];	//	thread identifiers
int tapfd=-1;
int connections[CONNECTION_MAX];	//	list of connections of the socket
int max_conn;	//	maximum index of open socket descriptors
int next_conn;	//	least index of unopened socket descriptors
rio_t rio_tap;
rio_t rio_eth[CONNECTION_MAX];

/**************************************************
  * allocate_tunnel:
  * open a tun or tap device and returns the file
  * descriptor to read/write back to the caller
  *****************************************/
int allocate_tunnel(char *dev, int flags) {
	int fd, error;
	struct ifreq ifr;
	char *device_name="/dev/net/tun";
	if((fd=open(device_name , O_RDWR))<0) {
		fprintf(stderr, "error opening /dev/net/tun: %s\n",
			strerror(errno));
		return fd;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags=flags;
	if(*dev){
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}if((error=ioctl(fd, TUNSETIFF, (void *)&ifr))<0){
		fprintf(stderr, "ioctl on tap failed: %s\n",
			strerror(errno));
		close(fd);
		return error;
	}
	strcpy(dev, ifr.ifr_name);
	return fd;
}

unsigned short get_port(char *s){
	unsigned short port;
	unsigned long x;
	x=strtoul(s, NULL, 10);
	//	Check for overflow error
	if(x==ULONG_MAX&&errno==ERANGE
		||x<1024||x>65535){
		fprintf(stderr, "error: invalid port parameter: %s\n",
			strerror(errno));
		exit(1);
	}
	port=(unsigned short)x;
	return port;
}

int open_listenfd(unsigned short port){
	int listenfd;
	struct sockaddr_in serveraddr;
	int optval=1;
	if((listenfd=socket(AF_INET, SOCK_STREAM, 0))<0){
		fprintf(stderr, "error creating socket: %s\n",
			strerror(errno));
		return -1;
	}	
	/* avoid EADDRINUSE error on bind() */
	if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		(char *)&optval, sizeof(optval)) < 0) {
		fprintf(stderr, "setsockopt(): %s\n",
			strerror(errno));
		close(listenfd);
		exit(-1);
	}
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family=AF_INET;
	serveraddr.sin_addr.s_addr=htonl(INADDR_ANY);
	serveraddr.sin_port=htons(port);
	if(bind(listenfd, (struct sockaddr *)&serveraddr,
		sizeof(serveraddr))<0){
		fprintf(stderr, "error binding socketfd to port: %s\n",
			strerror(errno));
		close(listenfd);
		return -1;
	}
	if(listen(listenfd, BACKLOG)<0){
		fprintf(stderr, "error making socket a listening socket: %s\n",
			strerror(errno));
		close(listenfd);
		return -1;
	}
	return listenfd;
}

int open_clientfd(char *hostname, unsigned short port){
	int clientfd;
	int optval=1;
	struct hostent *hp;
	struct in_addr addr;
	struct sockaddr_in serveraddr;
	if((clientfd=socket(AF_INET, SOCK_STREAM, 0))<0){
		fprintf(stderr, "error opening socket: %s\n",
			strerror(errno));
		return -1;
	}
	/* avoid EADDRINUSE error on bind() */
	if(setsockopt(clientfd, SOL_SOCKET, SO_REUSEADDR,
		(char *)&optval, sizeof(optval)) < 0) {
		fprintf(stderr, "setsockopt(): %s\n",
			strerror(errno));
		close(clientfd);
		exit(-1);
	}
	/**
	  * If the given hostname is an I.P. address in dotted decimal notation,
	  * then parse it via gethostbyaddr().
	  * Otherwise, parse the hostname via gethostbyname().
	  */
	if(inet_aton(hostname, &addr)!=0
		&&(hp=gethostbyaddr((char *)&addr,
			sizeof(struct in_addr), AF_INET))==NULL
		||(hp=gethostbyname(hostname))==NULL){
		fprintf(stderr, "error retrieving host information\n");
		close(clientfd);
		return -1;
	}
	printf("Connecting to host at I.P. address %s...\n",
		inet_ntoa(**(struct in_addr **)hp->h_addr_list));
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family=AF_INET;
	memcpy(&serveraddr.sin_addr, *hp->h_addr_list, hp->h_length);
	serveraddr.sin_port=htons(port);
	if(connect(clientfd, (struct sockaddr *)&serveraddr,
		sizeof(serveraddr))<0){
		fprintf(stderr, "error connecting to server: %s\n",
			strerror(errno));
		close(clientfd);
		return -1;
	}
	printf("Successfully connected to host at I.P. address %s.\n",
		inet_ntoa(serveraddr.sin_addr));
	return clientfd;
}

void *listen_handler(int *listenfd){
	struct sockaddr_in clientaddr;
	int addrlen=sizeof(struct sockaddr_in);
	int i;
	//	Store next_conn value into i to prevent a race.
	for(i=next_conn; next_conn<CONNECTION_MAX;){
		//	Accept a connection request.
		if((connections[i]=accept(*listenfd,
			(struct sockaddr *)&clientaddr, &addrlen))<0){
			fprintf(stderr, "error opening socket to client: %s\n",
				strerror(errno));
			close(*listenfd);
			*listenfd=-1;
			exit(-1);
		}
		printf("Successfully connected to host at I.P. address %s.\n",
			inet_ntoa(clientaddr.sin_addr));
		rio_readinit(&rio_eth[i], connections[i]);
		pthread_create(&eth_tid[i], NULL, eth_handler, &connections[i]);
		/**
	  	  *	If this thread created a connection with a higher index than
		  *	max_conn.
		  */
		if(i>max_conn)
			max_conn=i;
		//	If next_conn has not changed due to a client disconnection.
		if(i==next_conn)
			++i;
	}
	printf("maximum number of connections reached.\n");
	return NULL;
}

void *eth_handler(int *ethfd){
	/**
  	  *	ethfd points to the socket descriptor to the ethernet device that
	  *	only this thread can read from. It is set to -1 after the thread
	  *	closes.
	  */
	ssize_t size;
	char buffer[MTU_L2];
	void *bufptr;
	int i=(int)(ethfd-connections);	//	index of ethfd at connections
	while(1){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2);
		//	Read the proxy header first.
		if((size=rio_readnb(&rio_eth, bufptr, PROXY_HEADER_SIZE))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the ethernet device.\n");
			else
				fprintf(stderr, "connection #%d severed\n", i);
			close(*ethfd);
			*ethfd=-1;
			if(i<next_conn)
				next_conn=i;
			return NULL;
		}
		//	Parse and evaluate the proxy header.
		if(((proxy_header *)bufptr)->type!=htons(0xABCD)){
			fprintf(stderr, "error, incorrect type\n");
			close(*ethfd);
			*ethfd=-1;
			if(i<next_conn)
				next_conn=i;
			return NULL;
		}
		bufptr+=size;
		//	Read the rest of the payload.
		if((size=rio_readnb(&rio_eth, bufptr,
			ntohs(((proxy_header *)bufptr)->length)))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the ethernet device.\n");
			else
				fprintf(stderr, "connection severed\n");
			close(*ethfd);
			*ethfd=-1;
			if(i<next_conn)
				next_conn=i;
			return NULL;
		}
		//	Write the payload to the tap device.
		if((size=writen(tapfd, bufptr,
			((proxy_header *)bufptr)->length))<0){
			fprintf(stderr, "error writing to tap device\n");
			close(*ethfd);
			*ethfd=-1;
			if(i<next_conn)
				next_conn=i;
			return NULL;
		}
		printf("received %d bytes\n", size);
		rio_resetBuffer(&rio_eth[0]);
		rio_resetBuffer(&rio_tap);
	}
}

void *tap_handler(int *tfd){
	ssize_t size;
	char buffer[MTU_L2+PROXY_HEADER_SIZE];
	void *bufptr;
	proxy_header prxyhdr;
	int optval;
	while(1){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2+PROXY_HEADER_SIZE);
		//	Get the ethernet header first.
		if((size=rio_readnb(&rio_tap, bufptr, FRAME_HEADER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		printf("frame payload size: %d\n",
			ntohs(((frame_header *)bufptr)->length));
		//	Parse MAC addresses here.
		bufptr+=size;
		/**
		  * Write the proxy header in network byte-order.
		  * The type field of the proxy header is always set to 0xABCD.
		  *	The length field is given by the length field of the ethernet
		  *	frame header.
		  */
		prxyhdr.type=htons(0xABCD);
		prxyhdr.length=((frame_header *)bufptr)->length;
		/**
		  * type, length and buffer are stored in the stack
		  * adjacent to each other in order.
		  */
		memcpy(bufptr, &prxyhdr, PROXY_HEADER_SIZE);
		bufptr+=PROXY_HEADER_SIZE;
		//	Read the frame payload and the frame footer to the buffer.
		if((size=rio_readnb(&rio_tap, bufptr,
			ntohs(prxyhdr.length)+FRAME_FOOTER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		//	Write the modified IP payload to the ethernet socket.
		if((size=writen(connections[0],
			bufptr, ntohs(prxyhdr.length)+PROXY_HEADER_SIZE))<0){
			fprintf(stderr, "error writing to ethernet device\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		printf("sent %d bytes\n", size);
		rio_resetBuffer(&rio_eth[0]);
		rio_resetBuffer(&rio_tap);
	}
}
