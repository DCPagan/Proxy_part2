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
	rio_readinit(&rio_eth, ethfd);
	rio_readinit(&rio_tap, tapfd);
	while(1){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2);
		//	Read the proxy header first.
		if((size=rio_read(&rio_eth, bufptr, PROXY_HEADER_SIZE))<0){
			if(size<0)
				fprintf(stderr,
					"error reading from the ethernet device.\n");
			else
				fprintf(stderr, "connection severed\n");
			close(*ethfd);
			*ethfd=-1;
			if(*ethfd<next_conn)
				next_conn=*ethfd;
			return NULL;
		}
		//	Parse and evaluate the proxy header.
		if(((proxy_header *)bufptr)->type!=0xABCD){
			fprintf(stderr, "error, incorrect type\n");
			close(*ethfd);
			*ethfd=-1;
			if(*ethfd<next_conn)
				next_conn=*ethfd;
			return NULL;
		}
		bufptr+=size;
		//	Read the rest of the payload.
		if((size=rio_read(&rio_eth, bufptr,
			((proxy_header *)bufptr)->length))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the ethernet device.\n");
			else
				fprintf(stderr, "connection severed\n");
			close(*ethfd);
			*ethfd=-1;
			if(*ethfd<next_conn)
				next_conn=*ethfd;
			return NULL;
		}
		//	Write the payload to the tap device.
		if((size=writen(tapfd, bufptr,
			((proxy_header *)bufptr)->length))<0){
			fprintf(stderr, "error writing to tap device\n");
			close(*ethfd);
			*ethfd=-1;
			if(*ethfd<next_conn)
				next_conn=*ethfd;
			return NULL;
		}
		printf("received %d bytes\n", size);
		rio_resetBuffer(&rio_eth);
		rio_resetBuffer(&rio_tap);
	}
}

void *tap_handler(int *tfd){
	ssize_t size;
	char buffer[MTU_L2+PROXY_HEADER_SIZE];
	void *bufptr;
	int length;
	proxy_header prxyhdr;
	int optval;
	while(1){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2);
		//	Get the ethernet header first.
		if((size=rio_read(&rio_tap, bufptr, FRAME_HEADER_SIZE))<0){
			if(size<0)
				fprintf(stderr, "error reading from the tap device.\n");
			else
				fprintf(stderr, "connection severed\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		//	Parse MAC addresses here.
		bufptr+=size;
		//	Get the IP header. Assume IPv4.
		if((size=rio_read(&rio_tap, bufptr, IPv4_HEADER_SIZE))<0){
			if(size<0)
				fprintf(stderr, "error reading from the tap device.\n");
			else
				fprintf(stderr, "connection severed\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		/**
		  *	Check whether the I.P. packet is IPv4.
		  *	If the version is not 4, then terminate the program. This may
		  * be editted later to accommodate IPv6 in the program.
		  */
		if(((struct iphdr *)bufptr)->version!=4){
			fprintf(stderr, "error: I.P. packet not version 4\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		/**
		  * Check if the IHL field is greater than 5, and read the rest of
		  * the IPv4 packet if necessary.
		  */
		if(((struct iphdr *)bufptr)->ihl>5){
			bufptr+=size;
			if((size=rio_read(&rio_tap, bufptr,
				((struct iphdr *)bufptr)->ihl-size))<0){
				if(size<0)
					fprintf(stderr, "error reading from the tap device.\n");
				else
					fprintf(stderr, "connection severed\n");
				close(connections[0]);
				connections[0]=-1;
				return NULL;
			}
		}
		bufptr+=size;
		/**
		  * Having trouble setting the Don't Fragment option on the ethernet
		  * socket.
		  *
		  * The ethernet socket's fragmentation option must be the same as
		  * the packet. This is essential for ping to operate correctly.
		  * The DF flag is the 15-th most significant bit if the frag_off
		  * field, which is written in big-endian order.

		if(optval=((struct iphdr *)bufptr)->frag_off&ntohs(0x4000)?1:0){
			//	IP_DONTFRAG is only applicable to datagram and raw sockets.
			if(setsockopt(ethfd, IPPROTO_IP, IP_DONTFRAG,
				&optval, sizeof(optval))<0){
				fprintf(stderr, "error setting DF socket option.\n");
				close(connections[0]);
				;
				return NULL;
			}
		}
		  */
		/**
		  * All of that trouble, just for one bit. LOL.
		  *
		  * All other fields of the IP packet that the ethernet device will
		  * construct are implied by the TCP/IP protocol that this program
		  * uses to send packets between proxies.
		  */
		/**
		  * Write the proxy header.
		  * The type field of the proxy header is always set to 0xABCD.
		  * The length of the IP payload is equal to the value of the total
		  * length field of the IPv4 header minus the length of the IPv4
		  * header in bytes. The IHL field valued in 4-byte words.
		  */
		prxyhdr.type=htons(0xABCD);
		prxyhdr.length=length=((struct iphdr *)bufptr)->tot_len
			-4*((struct iphdr *)bufptr)->ihl;
		/**
		  * type, length and buffer are stored in the stack
		  * adjacent to each other in order.
		  */
		memcpy(bufptr, &prxyhdr, PROXY_HEADER_SIZE);
		bufptr+=PROXY_HEADER_SIZE;
		//	Get the rest of the frame payload, and the frame footer.
		if((size=rio_read(&rio_tap, bufptr, length+FRAME_FOOTER_SIZE))<=0){
			if(size<0)
				fprintf(stderr, "error reading from the tap device.\n");
			else
				fprintf(stderr, "connection severed\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		bufptr-=PROXY_HEADER_SIZE;
		//	Write the modified IP payload to the ethernet socket.
		if((size=writen(connections[0],
			bufptr, length+PROXY_HEADER_SIZE))<0){
			fprintf(stderr, "error writing to ethernet device\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		printf("sent %d bytes\n", size);
		rio_resetBuffer(&rio_eth);
		rio_resetBuffer(&rio_tap);
	}
}
