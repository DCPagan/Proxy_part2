#include"proxy.h"

int tapfd=-1;
pthread_t tap_tid;
rio_t rio_tap;
Config config;
link_state linkState;
Peer *hash_table = NULL;

const char BROADCAST_ADDR[ETH_ALEN]=
	{'\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF'};

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
		perror("error opening /dev/net/tun");
		return fd;
	}
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags=flags;
	if(*dev){
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}if((error=ioctl(fd, TUNSETIFF, (void *)&ifr))<0){
		perror("ioctl on tap failed");
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
		perror("error: invalid port parameter");
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
		perror("error creating socket");
		return -1;
	}
	/* avoid EADDRINUSE error on bind() */
	if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
		(char *)&optval, sizeof(optval)) < 0) {
		perror("setsockopt()");
		close(listenfd);
		exit(-1);
	}
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family=AF_INET;
	serveraddr.sin_addr.s_addr=htonl(INADDR_ANY);
	serveraddr.sin_port=htons(port);
	if(bind(listenfd, (struct sockaddr *)&serveraddr,
		sizeof(serveraddr))<0){
		perror("error binding socketfd to port");
		close(listenfd);
		return -1;
	}
	if(listen(listenfd, BACKLOG)<0){
		perror("error making socket a listening socket");
		close(listenfd);
		return -1;
	}
	return listenfd;
}

Peer *open_clientfd(char *hostname, unsigned short port){
	int clientfd;
	int optval=1;
	struct hostent *hp;
	struct in_addr addr;
	struct sockaddr_in serveraddr;
	Peer *pp;
	if((clientfd=socket(AF_INET, SOCK_STREAM, 0))<0){
		perror("error opening socket");
		return NULL;
	}
	/* avoid EADDRINUSE error on bind() */
	if(setsockopt(clientfd, SOL_SOCKET, SO_REUSEADDR,
		(char *)&optval, sizeof(optval)) < 0) {
		perror("setsockopt()");
		close(clientfd);
		return NULL;
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
		perror("error retrieving host information");
		close(clientfd);
		return NULL;
	}
	printf("Connecting to host at I.P. address %s...\n",
		inet_ntoa(**(struct in_addr **)hp->h_addr_list));
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family=AF_INET;
	memcpy(&serveraddr.sin_addr, *hp->h_addr_list, hp->h_length);
	serveraddr.sin_port=htons(port);
	if(connect(clientfd, (struct sockaddr *)&serveraddr,
		sizeof(serveraddr))<0){
		perror("error connecting to server");
		close(clientfd);
		return NULL;
	}
	printf("Successfully connected to host at I.P. address %s.\n",
		inet_ntoa(serveraddr.sin_addr));
	/**
	  *	Commence the link-state packet exchange with the peer.
	  *	First, fields must be filled out.
	  */
	pp=(Peer *)malloc(sizeof(Peer));
	memset(pp, 0, sizeof(Peer));
	rio_readinit(&pp->rio, clientfd);
	pthread_mutex_init(&pp->lock, NULL);
	link_state_exchange_client(pp);
	add_member(pp);
	return pp;
}

/**
  *	Client side of link-state exchange.
  *	Send link-state packet, then wait for a response from the server.
  */
Peer *link_state_exchange_client(Peer *pp){
	proxy_header prxyhdr;
	char buffer[ETH_FRAME_LEN];
	void *bufptr=buffer;
	unsigned short N=HASH_COUNT(hash_table);	//	number of neighbors
	Peer *pp1, *pp2;
	size_t size;
	/**
	  *	Calculate the length of the packet to send by adding the values
	  *	of the single-record server holding this proxy's connection
	  *	information, and the product of the number of neighbors times the
	  *	size of a neighbor's record.
	  */
	prxyhdr.type=ntohs(LINK_STATE);
	prxyhdr.length=2*sizeof(unsigned short)	//	2 numbers for neighbors
		+sizeof(link_state_source)	//	1 source/origin link-state
		+N*sizeof(link_state_record);	// N records
	/**
	  *	Allocate memory for a structure to hold the struct for a
	  *	constant-sized single-record link-state structure by declaring
	  *	such an automatic structure variable.
	  */
	memcpy(bufptr, &prxyhdr, PROXY_HLEN);
	bufptr+=PROXY_HLEN;
	memcpy(bufptr, N, sizeof(N));
	//	Iterate through the membership list.
	HASH_ITER(hh, hash_table, pp1, pp2){
		/**
		  *	Write link-state records for each neighbor here.
		  *	Consult the struct link_state_record for information on how to
		  *	write records on the buffer.
		  */
		bufptr+=sizeof(link_state_record);
	}
	if((size=rio_write(&pp->rio, buffer, prxyhdr.length+PROXY_HLEN))<0){
		/**
		  *	error condition; program accordingly.
		  */
		free(buffer);
		return NULL;
	}
	/**
	  *	Read the proxy header first, and then the rest of the packet.
	  */
	if((size=rio_read(&pp->rio, &prxyhdr, PROXY_HLEN))<0){
		/**
		  *	error condition; program accordingly.
		  */
		free(buffer);
		return NULL;
	}
	free(buffer);
	return pp;
}

/**
  *	Server side of link-state exchange.
  *	Wait for link-state packet, then send a response to the client.
  */
Peer *link_state_exchange_server(Peer *pp){
	proxy_header prxyhdr;
	void *buffer, *bufptr;
	unsigned short N;	//	number of neighbors
	N=HASH_COUNT(hash_table);
	return pp;
}

void *tap_handler(int *fd){
	ssize_t size;
	char buffer[ETH_FRAME_LEN+PROXY_HLEN];
	proxy_header prxyhdr;
	int i;
	pthread_detach(pthread_self());
	Peer *pp, *tmp;
	for(;;){
		memset(buffer, 0, ETH_FRAME_LEN+PROXY_HLEN);
		/**
	  	  *	Read the entire Ethernet frame.
		  ****************************************************************
		  *
		  *	IMPORTANT NOTICE, PLEASE READ
		  *
		  ****************************************************************
		  *	For whatever reason, the Ethernet frames of the tap device do
		  *	not include the frame checksum. Because of this, when an extra
		  * four bytes are read from the tap device, it instead reads from
		  *	the first four bytes of the next frame. If the tap device does
		  *	pass frame checksums at the end of frames, then add the macro
		  *	ETH_FCS_LEN (valued at 4) to the third parameter of the following
		  *	reading procedure.
		  */
		if((size=rio_read(&rio_tap, buffer+PROXY_HLEN, ETH_FRAME_LEN))<=0){
			perror("error reading from the tap device.\n");
			exit(-1);
		}
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
		  */
		/**
		  * Write the proxy header in network byte-order to the front of
		  *	the buffer.
		  *
		  * The type field of the proxy header is always set to DATA.
		  *	The size is given by the length field of the Ethernet frame
		  *	header.
		  */
		prxyhdr.type=htons(DATA);
		prxyhdr.length=htons(size);
		memcpy(buffer, &prxyhdr, PROXY_HLEN);
		//	Check if the packet received is a broadcast packet.
		if(memcmp(((struct ethhdr *)(buffer+PROXY_HLEN))->h_dest,
			BROADCAST_ADDR, ETH_ALEN)){
			HASH_ITER(hh, hash_table, pp, tmp){
				if((size=rio_write(&pp->rio, buffer,
					ntohs(prxyhdr.length)+PROXY_HLEN))<0){
					remove_member(pp);
					return NULL;
				}
			}
		}else{
			HASH_FIND(hh, hash_table, &((link_state *)buffer)->tapMAC,
				ETH_ALEN, pp);
			//	Write the whole buffer to the Ethernet device.
			if((size=rio_write(&pp->rio, buffer,
				ntohs(prxyhdr.length)+PROXY_HLEN))<0){
				remove_member(pp);
				return NULL;
			}
		}
		rio_resetBuffer(&rio_tap);
	}
} 

void *eth_handler(Peer *pp){
	/**
  	  *	ethfd points to the socket descriptor to the Ethernet device that
	  *	only this thread can read from. It is set to -1 after the thread
	  *	closes.
	  */
	ssize_t size;
	void *buffer;
	proxy_header prxyhdr;
	Peer *tmp;
	for(;;){
		//	Read the proxy type directly into the proxy header structure.
		if((size=rio_readnb(&pp->rio, &prxyhdr, PROXY_HLEN))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the Ethernet device.\n");
			else
				perror("connection severed\n");
			close(pp->rio.fd);
			return NULL;
		}
		/**
	  	  *	Parse and evaluate the proxy header.
		  *	Dynamically allocate memory for the data so that each packet
		  *	could be evaluated concurrently. Free the dynamically allocated
		  *	memory at the called helper functions.
		  */
		prxyhdr.type=ntohs(prxyhdr.type);
		prxyhdr.length=ntohs(prxyhdr.length);
		buffer=malloc(prxyhdr.length);
		if((size=rio_readnb(&pp->rio, buffer, prxyhdr.length))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the Ethernet device.\n");
			else
				perror("connection severed\n");
			free(buffer);
			remove_member(pp);
			return NULL;
		}
		rio_resetBuffer(&pp->rio);
		switch(prxyhdr.type){
			case DATA:
				if(Data(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Leave (part 2)
			case LEAVE:
				if(Leave(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Quit
			case QUIT:
				if(Quit(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Link-state (part 2)
			case LINK_STATE:
				if(Link_State(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	RTT Probe Request (part 3)
			case RTT_PROBE_REQUEST:
				if(RTT_Probe_Request(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	RTT Probe Response (part 3)
			case RTT_PROBE_RESPONSE:
				if(RTT_Probe_Response(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Proxy Public Key (extra credit)
			case PROXY_PUBLIC_KEY:
				if(Proxy_Public_Key(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Signed Data (extra credit)
			case SIGNED_DATA:
				if(Signed_Data(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Proxy Secret key (extra credit)
			case PROXY_SECRET_KEY:
				if(Proxy_Secret_Key(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Encrypted Data (extra credit)
			case ENCRYPTED_DATA:
				if(Encrypted_Data(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Encrypted Link State (extra credit)
			case ENCRYPTED_LINK_STATE:
				if(Encrypted_Link_State(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Signed link-state (extra credit)
			case SIGNED_LINK_STATE:
				if(Signed_Link_State(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Bandwidth Probe Request
			case BANDWIDTH_PROBE_REQUEST:
				if(Bandwidth_Probe_Request(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Bandwidth Response
			case BANDWIDTH_RESPONSE:
				if(Bandwidth_Probe_Response(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			default:
				fprintf(stderr, "error, incorrect type\n");
				TYPE_ERROR:
					free(buffer);
					remove_member(pp);
					return NULL;
		}
		free(buffer);
	}
}

/**
  *	Thread-safe implentation of inet_ntoa()
  *	Instead of returning a character pointer pointing to a string of the
  *	I.P. address, it writes the I.P. address as a character string to the
  *	given character pointer.
  */
void inet_ntoa_r(unsigned int addr, char *s){
	sprintf(s, "%hhu:%hhu:%hhu:%hhu",
		*(unsigned char *)&addr,
		*((unsigned char *)&addr+1),
		*((unsigned char *)&addr+2),
		*((unsigned char *)&addr+3));
	return;
}

void printEthernet(struct ethhdr *data){
	int i;
	//	Print Ethernet frame header fields.
	printf("%-25s ", "source MAC address:");
	for(i=0; i<ETH_ALEN-1; i++)
		printf("%.2x:", data->h_source[i]);
	printf("%.2x\n", data->h_source[i]);
	printf("%-25s ", "destination MAC address:");
	for(i=0; i<ETH_ALEN-1; i++)
		printf("%.2x:", data->h_dest[i]);
	printf("%.2x\n", data->h_dest[i]);
	printf("%-25s %#.4x\n",
		"Ethertype:", ntohs(data->h_proto));
	switch(ntohs(data->h_proto)){
		//	IPv4
		case ETH_P_IP:
			printIP((void *)data+ETH_HLEN);
			break;
		//	ARP
		case ETH_P_ARP:
			printARP((void *)data+ETH_HLEN);
			break;
		//	Unknown or unimplemented Ethertype
		default: break;
	}
	putchar('\n');
}

void printIP(struct iphdr *data){
	/**
	  *	data now points to the beginning of the IPv4 packet header;
	  *	one may add code here to output IPv4 packet information.
	  *	Consult linux/ip.h to find the fields of struct iphdr.
	  */
	char s[16];
	printf("%-25s %u\n", "IP version:", data->version);
	printf("%-25s %u\n", "IP packet size:", ntohs(data->tot_len));
	printf("%-25s %#.2x\n", "protocol:", data->protocol);
	inet_ntoa_r(data->saddr, s);
	printf("%-25s %s\n", "source I.P. address:", s);
	inet_ntoa_r(data->daddr, s);
	printf("%-25s %s\n", "destination I.P. address:", s);
	//	if the segment is an ICMP segment, print its fields.
	switch(data->protocol){
		//	ICMP protocol number
		case 0x01:
			printICMP((void *)data+IPv4_HLEN);
			break;
		//	Unknown or unimplemented protocol
		default: break;
	}
	return;
}

void printARP(void *data){
	return;
}

void printICMP(struct icmphdr *data){
	printf("%-25s %#.2x\n", "ICMP type:", data->type);
	printf("%-25s %#.2x\n", "ICMP code:", data->code);
	printf("%-25s %#.4x\n", "ICMP checksum:", ntohs(data->checksum));
	printf("%-25s %#.4x\n", "ICMP identifier:", ntohs(data->un.echo.id));
	printf("%-25s %#.4x\n", "ICMP sequence:",
		ntohs(data->un.echo.sequence));
	return;
}

int Data(void *data, unsigned short length){
	ssize_t size;
	//	Write the payload to the tap device.
	if((size=rio_write(&rio_tap, data, length))<0){
		fprintf(stderr, "error writing to tap device\n");
		return -1;
	}
	return 0;
}

int Leave(void *data, unsigned short length){
	Peer *pp;
	HASH_FIND(hh, hash_table, &((link_state *)data)->tapMAC,
		ETH_ALEN, pp);
	if(pp!=NULL)
		remove_member(pp);
	return 0;
}

int Quit(void *data, unsigned short length){
	proxy_header prxyhdr= {ntohs(QUIT), ntohs(QUIT_LEN)};
	char buffer[PROXY_HLEN+QUIT_LEN];
	Peer *pp, *tmp;
	if(length!=QUIT_LEN){
		fprintf(stderr, "error: quit packet has wrong size\n");
		return -1;
	}
	memcpy(buffer, &prxyhdr, PROXY_HLEN);
	memcpy(buffer+PROXY_HLEN, data, QUIT_LEN);
	HASH_ITER(hh, hash_table, pp, tmp){
		rio_write(&pp->rio, buffer, PROXY_HLEN+QUIT_LEN);
		remove_member(pp);
	}
	pthread_cancel(tap_tid);
	exit(0);
}

int Link_State(void *data, unsigned short length){
	Peer *pp;
	char *addr;
	/**
	  * Check if you are connected to the host that sent the packet.
	  *	Define a struct to dereference the tap MAC address correctly.
	  */
	HASH_FIND(hh, hash_table, &((link_state *)data)->tapMAC,
		ETH_ALEN, pp);
	if(pp==NULL){
		inet_ntoa_r((unsigned int)((link_state *)data)->IPaddr.s_addr,
			addr);
		open_clientfd(addr, ((link_state *)data)->listenPort);
	}
	return 0;
}

int RTT_Probe_Request(void *data, unsigned short length){
	return 0;
}

int RTT_Probe_Response(void *data, unsigned short length){
	return 0;
}

int Proxy_Public_Key(void *data, unsigned short length){
	return 0;
}

int Signed_Data(void *data, unsigned short length){
	return 0;
}

int Proxy_Secret_Key(void *data, unsigned short length){
	return 0;
}

int Encrypted_Data(void *data, unsigned short length){
	return 0;
}

int Encrypted_Link_State(void *data, unsigned short length){
	return 0;
}

int Signed_Link_State(void *data, unsigned short length){
	return 0;
}

int Bandwidth_Probe_Request(void *data, unsigned short length){
	return 0;
}

int Bandwidth_Probe_Response(void *data, unsigned short length){
	return 0;
}

/**
  *	Lock accordingly in this process as well.
  */
void add_member(Peer *node){
	Peer *tmp;
	HASH_FIND(hh, hash_table, &node->ls.tapMAC, ETH_ALEN, tmp);
	if(tmp == NULL){
		HASH_ADD(hh, hash_table, ls.tapMAC, ETH_ALEN,node);
	}
}

void remove_member(Peer *node){
	Peer *tmp;
	/**
	  *	This is a write operation on shared data; to solve the
	  *	readers/writers problem, use mutual exclusion to privilege
	  *	the writer by granting exclusive access to the hash_table.
	  */
	/**
	  * Lock here. I don't know how to use MUTEX's yet.
	  *	I don't know how to use MUTEX's yet, so please do this for me,
	  *	John. Delete these last two lines of comments for me as well.
	  */
	pthread_mutex_lock(node);
	HASH_FIND(hh, hash_table, &node->ls.tapMAC, ETH_ALEN ,tmp);
	if(tmp != NULL){
		HASH_DEL(hash_table, node);
	}
	/**
	  *	Upon removing a peer from the membership list, terminate the
	  *	thread associated with the connection, close its file descriptor,
	  *	and free its memory.
	  */
	pthread_cancel(node->tid);
	close(node->rio.fd);
	pthread_mutex_unlock(node);
	free(node);
	//	Unlock here.
	return;
}
