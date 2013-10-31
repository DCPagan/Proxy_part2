#include"proxy.h"

//int tap_fd = -1;
//int eth_fd = -1;

/**************************************************
  * allocate_tunnel:
  * open a tun or tap device and returns the file
  * descriptor to read/write back to the caller
  *****************************************/
int allocate_tunnel(char *dev, int flags) {
	int fd, error;
	struct ifreq ifr;
	char *device_name = "/dev/net/tun";
	if( (fd = open(device_name , O_RDWR)) < 0 ) {
		perror("error opening /dev/net/tun");
		return fd;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = flags;
	if (*dev) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}if( (error = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
		perror("ioctl on tap failed");
		close(fd);
		return error;
	}
	strcpy(dev, ifr.ifr_name);
	return fd;
}

int open_listenfd(unsigned short port){
	int listenfd;
	int optval = 1;
	struct sockaddr_in serveraddr;
	if((listenfd=socket(AF_INET, SOCK_STREAM, 0))<0){
		perror("error creating socket\n");
		return -1;
	}	
	/* avoid EADDRINUSE error on bind() */
	if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		(char *)&optval, sizeof(optval)) < 0) {
		perror("setsockopt()");
		exit(-1);
	}
	if(listen(listenfd, BACKLOG)<0){
		perror("error making socket a listening socket\n");
		return -1;
	}
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));
	serveraddr.sin_family=AF_INET;
	serveraddr.sin_addr.s_addr=htonl(INADDR_ANY);
	serveraddr.sin_port=port;
	if(bind(listenfd, (struct sockaddr *)&serveraddr,
		sizeof(struct sockaddr_in))<0){
		perror("error binding socketfd to port\n");
		return -1;
	}
	return listenfd;
}

int open_clientfd(char *hostname, unsigned short port){
	int clientfd;
	struct hostent *hp;
	struct in_addr addr;
	struct sockaddr_in serveraddr;
	if((clientfd=socket(AF_INET, SOCK_STREAM, 0))<0){
		perror("error opening socket\n");
		return -1;
	}
	if((inet_aton(hostname, &addr))!=0)
		hp=gethostbyaddr((const char *)&addr, sizeof(struct in_addr), AF_INET);
	else if((hp=gethostbyname(hostname))==NULL){
		perror("error retrieving host information\n");
		return -1;
	}
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));
	serveraddr.sin_family=AF_INET;
	memcpy(&serveraddr.sin_addr, hp->h_addr_list, hp->h_length);
	serveraddr.sin_port=htons(port);
	if(connect(clientfd, (struct sockaddr *)&serveraddr,
		sizeof(struct sockaddr_in))<0){
		perror("error connecting to server\n");
		return -1;
	}
	return clientfd;
}

void *eth_thread(thread_param *tp){
	rio_t *rio_eth, *rio_tap;
	ssize_t size;
	unsigned int short type, length;
	char buffer[MTU_L2];
	void *bufptr;
	proxy_header prxyhdr;
	while(1){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2);
		//	Read the proxy header first.
		if((size=rio_read(rio_eth, bufptr, PROXY_HEADER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
		}
		//	Increment the buffer pointer.
		bufptr+=size;
		//	Parse and evaluate the proxy header.
		prxyhdr.type=ntohs(((proxy_header *)bufptr)->type);
		prxyhdr.length=ntohs(((proxy_header *)bufptr)->length);
		if(prxyhdr.type!=0xABCD){
			fprintf(stderr, "error, incorrect type");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
		}
		//	Read the rest of the payload.
		if((size=rio_read(rio_eth, bufptr, prxyhdr.length))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
		}
		write(tp->tapfd, bufptr, prxyhdr.length);
		rio_resetBuffer(rio_eth);
		rio_resetBuffer(rio_tap);
	}
}

void *tap_thread(thread_param *tp){
	rio_t *rio_eth, *rio_tap;
	ssize_t size;
	char buffer[MTU_L2+PROXY_HEADER_SIZE];
	void *bufptr;
	unsigned short length;
	proxy_header prxyhdr;
	int optval;
	rio_readinit(rio_eth, tp->ethfd);
	rio_readinit(rio_tap, tp->tapfd);
	while(1){
		bufptr=buffer;
		memset(buffer, 0, MTU_L2);
		//	Get the ethernet header first.
		if((size=rio_read(rio_tap, bufptr, FRAME_HEADER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
		}
		bufptr+=FRAME_HEADER_SIZE;
		//	Get the IP header. Assume IPv4.
		if((size=rio_read(rio_tap, bufptr, IPv4_HEADER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
		}
		/**
	  	  * Increment the buffer pointer by the amount read, which is the
		  * IPv4 header size.
		  */
		bufptr+=size;

		/**
		  * Check if the IHL field is greater than 5, and read the rest of
		  * the IPv4 packet if necessary.
		  */
		if(((struct iphdr *)bufptr)->ihl>5&&
			(size=rio_read(rio_tap, bufptr,
				((struct iphdr *)bufptr)->ihl-size))<0){
			fprintf(stderr, "error reading from the tap.\n");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
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
			if(setsockopt(tp->ethfd, IPPROTO_IP, IP_DONTFRAG,
				&optval, sizeof(optval))<0){
				fprintf(stderr, "error setting DF socket option.\n");
				close(tp->ethfd);
				close(tp->tapfd);
				exit(-1);
			}
		  */
		/**
		  * All of that trouble, just for one bit. LOL.
		  *
		  * All other fields of the IP packet that the ethernet device will
		  * construct are implied by the TCP/IP protocol that this program
		  * uses to send packets between proxies.
		  */
		//	The type field of the proxy header is always set to 0xABCD.
		prxyhdr.type=htons(0xABCD);
		/**
		  * The length of the IP payload is equal to the value of the total
		  * length field of the IPv4 header minus the length of the IPv4
		  * header in bytes. The IHL field valued in 4-byte words.
		  */
		prxyhdr.length=length=((struct iphdr *)bufptr)->tot_len
			-4*((struct iphdr *)bufptr)->ihl;
		/**
		  * type, length and buffer are stored in the stack
		  * adjacent to each other in order.
		  */
		memcpy(bufptr, &prxyhdr, PROXY_HEADER_SIZE);
		bufptr+=PROXY_HEADER_SIZE;
		//	Get the rest of the frame payload, and the frame footer.
		if((size=rio_read(rio_tap, bufptr, length+FRAME_FOOTER_SIZE))<0){
			fprintf(stderr, "error reading from the tap device.\n");
			close(tp->ethfd);
			close(tp->tapfd);
			exit(-1);
		}
		bufptr-=PROXY_HEADER_SIZE;
		//	Write the modified IP payload to the ethernet socket.
		write(tp->ethfd, bufptr, length+PROXY_HEADER_SIZE);
		rio_resetBuffer(rio_eth);
		rio_resetBuffer(rio_tap);
	}
} 
