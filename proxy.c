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
  	  *	ethfd points to the socket descriptor to the Ethernet device that
	  *	only this thread can read from. It is set to -1 after the thread
	  *	closes.
	  */
	ssize_t size;
	char buffer[MTU_L2];
	void *bufptr;
	int i=(int)(ethfd-connections);	//	index of ethfd at connections
	for(;;){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2);
		//	Read the proxy header first.
		if((size=rio_readnb(&rio_eth[0], bufptr, PROXY_HEADER_SIZE))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the Ethernet device.\n");
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
		if((size=rio_readnb(&rio_eth[0], bufptr,
			ntohs(((proxy_header *)bufptr)->length)))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the Ethernet device.\n");
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
	int i;
	for(;;){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2+PROXY_HEADER_SIZE);
		//	Get the Ethernet header first.
		if((size=rio_readnb(&rio_tap, bufptr, ETH_HLEN))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		//	Print Ethernet frame header fields.
		printf("size of ethernet frame header read: %d\n", size);
		printf("source MAC address: ");
		for(i=0; i<ETH_ALEN-1; i++)
			printf("%0.2X:", ((struct ethhdr *)bufptr)->h_source[i]);
		printf("%0.2X\n", ((struct ethhdr *)bufptr)->h_source[i]);
		printf("destination MAC address: ");
		for(i=0; i<ETH_ALEN-1; i++)
			printf("%0.2X:", ((struct ethhdr *)bufptr)->h_dest[i]);
		printf("%0.2X\n", ((struct ethhdr *)bufptr)->h_dest[i]);
		printf("ethertype: %#0.4X\n",
			ntohs(((struct ethhdr *)bufptr)->h_proto));
		/**
		  *	Parse MAC addresses here. Dereference bufptr as
		  *	(struct ethhdr *), and consult linux/if_ether.h.
		  *
		  *	Read the Wikipedia article concerning Ethertype. The final,
		  *	two-octet field in the Ethernet frame header is used to
		  *	indicate which protocol is encapsulated in the payload of the
		  *	Ethernet frame. The value usually starts with a value of
		  *	0x0800, which explains why the value of the field is greater
		  *	than that value upon dereferencing and converting to host byte-
		  *	order.
		  *
		  *	The length of the payload cannot be evaluated from reading the
		  *	two-octet field as expected; it must be derived from the IPv4
		  *	packet header.
		  *
		  *	Move bufptr by the size of the proxy header, and read the IPv4
		  *	header. Then, write the proxy header into the buffer before the
		  *	IPv4 header, and read the rest of the Ethernet frame after the
		  *	IPv4 packet header.
		  */
		bufptr+=size+PROXY_HEADER_SIZE;
		if((size=rio_readnb(&rio_tap, bufptr, IPv4_HEADER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		//	Print IPv4 packet header frames.
		printf("size of IPv4 packet header read: %d\n", size);
		printf("IP version: %d\n", ((struct iphdr *)bufptr)->version);
		printf("IP header length: %d\n",
			((struct iphdr *)bufptr)->ihl<<2);
		printf("packet size: %#0.4X\n",
			ntohs(((struct iphdr *)bufptr)->tot_len));
		printf("protocol: %#0.2X\n", ((struct iphdr *)bufptr)->protocol);
		printf("size of IPv4 packet header read: %d\n", size);
		printf("packet size: %d\n",
			htons(((struct iphdr *)bufptr)->tot_len));
		/**
		  *	bufptr now points to the beginning of the IPv4 packet header;
		  *	one may add code here to output IPv4 packet information.
		  *	Consult linux/ip.h to find the fields of struct iphdr.
		  */
		/**
		  * Write the proxy header in network byte-order.
		  * The type field of the proxy header is always set to 0xABCD.
		  *	The length field is given by the length field of the Ethernet
		  *	frame header.
		  */
		prxyhdr.type=htons(0xABCD);
		prxyhdr.length=((struct iphdr *)bufptr)->tot_len;
		bufptr-=PROXY_HEADER_SIZE;
		memcpy(bufptr, &prxyhdr, PROXY_HEADER_SIZE);
		//	Now read the rest of the Ethernet frame.
		if((size=rio_readnb(&rio_tap,
			bufptr+PROXY_HEADER_SIZE+IPv4_HEADER_SIZE,
			ntohs(prxyhdr.length)-IPv4_HEADER_SIZE+ETH_FCS_LEN))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		//	Write the modified IP payload to the Ethernet socket.
		if((size=rio_write(&rio_eth[0], bufptr,
			ntohs(prxyhdr.length)+PROXY_HEADER_SIZE))<0){
			fprintf(stderr, "error writing to Ethernet device\n");
			close(connections[0]);
			connections[0]=-1;
			return NULL;
		}
		printf("sent %d bytes\n", size);
		rio_resetBuffer(&rio_eth[0]);
		rio_resetBuffer(&rio_tap);
	}
}
