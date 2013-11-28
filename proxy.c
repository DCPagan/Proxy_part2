#include"proxy.h"

int tapfd=-1;
pthread_t tap_tid;
rio_t rio_tap;
Config config;
link_state linkState;
Peer *hash_table = NULL;
int readcount, writecount;
pthread_mutex_t mutex1=PTHREAD_MUTEX_INITIALIZER,
	mutex2=PTHREAD_MUTEX_INITIALIZER,
	mutex3=PTHREAD_MUTEX_INITIALIZER,
	r=PTHREAD_MUTEX_INITIALIZER,
	w=PTHREAD_MUTEX_INITIALIZER;

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

int getMAC(char *dev, char *local_mac){
	char buffer[64];
	// Get device MAC address //
	sprintf(buffer,"/sys/class/net/%s/address",dev);
	FILE* f = fopen(buffer,"r");
	fgets(buffer, 64, f);
	fclose(f);
	sscanf(buffer,"%hhX:%hhX:%hhX:%hhX:%hhX:%hhX",
		local_mac,local_mac+1,local_mac+2,
		local_mac+3,local_mac+4,local_mac+5);
	return 0;
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
	initial_join_client(pp);
	add_member(pp);
	return pp;
}

/**
  *	Client side of link-state exchange.
  *	Send link-state packet, then wait for a response from the server.
  */
Peer *initial_join_client(Peer *pp){
	initial_join_packet pkt;
	size_t size;
	/**
	  *	Calculate the length of the packet to send by adding the values
	  *	of the single-record server holding this proxy's connection
	  *	information, and the product of the number of neighbors times the
	  *	size of a neighbor's record.
	  */
	pkt.prxyhdr.type=htons(LINK_STATE);
	pkt.prxyhdr.length=htons(sizeof(unsigned short)	//	number of neighbors
		+sizeof(link_state_source));	//	source/origin link-state
	/**
	  *	Allocate memory for a structure to hold the struct for a
	  *	constant-sized single-record link-state structure by declaring
	  *	such an automatic structure variable.
	  */
	pkt.ls=linkState;
	pkt.ls.listenPort=htons(pkt.ls.listenPort);
	readBegin();
	pkt.numNbrs1=pkt.numNbrs2=htons(HASH_COUNT(hash_table));
	readEnd();
	//	Write, then read, because this is on the client side.
	if((size=rio_write(&pp->rio, &pkt, sizeof(pkt)))<=0){
		/**
		  *	error condition; program accordingly.
		  */
		return NULL;
	}
	if((size=rio_readnb(&pp->rio, &pkt, sizeof(pkt)))<=0){
		/**
		  *	error condition; program accordingly.
		  */
		return NULL;
	}
	/**
	  *	Only check if the type is correct, and copy the connection
	  *	information.
	  */
	if(ntohs(pkt.prxyhdr.type)!=LINK_STATE){
		/**
		  *	Link-state error condition
		  */
		return NULL;
	}
	pp->ls=pkt.ls;
	rio_resetBuffer(&pp->rio);
	return pp;
}

/**
  *	Server side of link-state exchange.
  *	Wait for link-state packet, then send a response to the client.
  */
Peer *initial_join_server(Peer *pp){
	initial_join_packet pkt;
	size_t size;
	/**
	  *	Read, then write, because this is on the server side.
	  *	Read and evaluate the proxy header of the incoming packet.
	  */
	if((size=rio_readnb(&pp->rio, &pkt, sizeof(pkt)))<=0){
		/**
		  *	Link-state error condition
		  */
		return NULL;
	}
	/**
	  *	Only check if the type is correct, and copy the connection
	  *	information.
	  */
	if(ntohs(pkt.prxyhdr.type)!=LINK_STATE){
		/**
		  *	Link-state error condition
		  */
		return NULL;
	}
	pkt.ls.listenPort=ntohs(pkt.ls.listenPort);
	pp->ls=pkt.ls;
	//	Write the local link-state packet after receiving from the client.
	pkt.prxyhdr.type=htons(LINK_STATE);
	pkt.prxyhdr.length=htons(sizeof(unsigned short)	//	number of neighbors
		+sizeof(link_state_source));	//	source/origin link-state
	pkt.ls=linkState;
	pkt.ls.listenPort=htons(pkt.ls.listenPort);
	readBegin();
	pkt.numNbrs1=pkt.numNbrs2=htons(HASH_COUNT(hash_table));
	readEnd();
	if((size=rio_write(&pp->rio, &pkt, sizeof(pkt)))<=0){
		/**
		  *	Link-state error condition
		  */
		return NULL;
	}
	rio_resetBuffer(&pp->rio);
	return pp;
}

void *tap_handler(int *fd){
	ssize_t size;
	char buffer[ETH_FRAME_LEN+PROXY_HLEN];
	proxy_header prxyhdr;
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
		if((size=rio_read(&rio_tap, buffer+PROXY_HLEN,
			PROXY_HLEN+ETH_FRAME_LEN))<=0){
			perror("error reading from the tap device.\n");
			exit(-1);
		}
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
		/**
		  *	Evaluate MAC addresses here. Dereference bufptr as
		  *	(struct ethhdr *), and consult linux/if_ether.h.
		  *
		  *	The length of the payload cannot be evaluated from reading the
		  *	two-octet field as expected; it must be derived from the IPv4
		  *	packet header.
		  */
		if(!memcmp(&((struct ethhdr *)(buffer+PROXY_HLEN))->h_dest,
			BROADCAST_ADDR, ETH_ALEN)){
			readBegin();
			HASH_ITER(hh, hash_table, pp, tmp){
				if((size=rio_write(&pp->rio, buffer,
					PROXY_HLEN+ntohs(prxyhdr.length)))<=0){
					remove_member(pp);
					return NULL;
				}
			}
			readEnd();
		}else{
			readBegin();
			HASH_FIND(hh, hash_table, &((link_state *)buffer)->tapMAC,
				ETH_ALEN, pp);
			//	Write the whole buffer to the Ethernet device.
			if(pp!=NULL&&(size=rio_write(&pp->rio, buffer,
				ntohs(prxyhdr.length)+PROXY_HLEN))<=0){
				remove_member(pp);
				readEnd();
				return NULL;
			}
			readEnd();
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
					writeBegin();
					remove_member(pp);
					writeEnd();
					return NULL;
		}
		free(buffer);
	}
}

/**
  *	This handles terminal signals and other terminal conditions.
  *	Before exiting, the proxy must broadcast a leave packet.
  */
void leave_handler(int signo){
	Peer *pp, *tmp;
	size_t size;
	leave_packet lvpkt;
	readBegin();
	lvpkt.prxyhdr.type=htons(LEAVE);
	lvpkt.lv.localIP=linkState.IPaddr;
	lvpkt.lv.localListenPort=htons(linkState.listenPort);
	memcpy(&lvpkt.lv.localMAC, &linkState.tapMAC, ETH_ALEN);
	clock_gettime(CLOCK_MONOTONIC, &lvpkt.lv.ID);
	lvpkt.lv.ID.tv_sec=htonl(lvpkt.lv.ID.tv_sec);
	lvpkt.lv.ID.tv_nsec=htonl(lvpkt.lv.ID.tv_nsec);
	HASH_ITER(hh, hash_table, pp, tmp){
		//	Write the leave packet.
		if((size=rio_write(&pp->rio, &lvpkt,
			sizeof(leave_packet)))<0){
			/**
			  *	error broadcasting leave packet
			  */
		}
		//	Remove the peer from the system entirely.
		HASH_DEL(hash_table, pp);
		pthread_cancel(pp->tid);
		close(pp->rio.fd);
		free(pp);
	}
	readEnd();
	exit(0);
}

/**
  *	@param	int, void (*)(int): signal number, and the new signal handler
  *	@return	void (*)(int):		the old handler for that signal.
  */
void (*Signal(int signo, void (*sig_handler)(int)))(int){
	struct sigaction act, oact;	//	sigaction, old sigaction
	sigemptyset(&act.sa_mask);
	act.sa_flags=0;
	/**
	  *	Interrupt system calls when an alarm signal is received, as the
	  *	purpose of the alarm is to place a timeout on an I/O operation.
	  *	Otherwise, restart the system call.
	  */
	if(signo==SIGALRM){
#ifdef SA_INTERRUPT
		act.sa_flags|=SA_INTERRUPT;
#endif
	}else{
#ifdef SA_RESTART
		act.sa_flags|=SA_RESTART;
#endif
	}
	if(sigaction(signo, &act, &oact)<0)
		return SIG_ERR;
	return oact.sa_handler;
}

int Link_State_Broadcast(int signo){
	void *buffer, *ptr;
	Peer *pp, *tmp;
	proxy_header prxyhdr;
	unsigned short N;
	size_t size;
	/**
	  *	Broadcasting requires reading, but not writing, the membership
	  *	list, so reader mutual exclusion procedures must be invoked.
	  */
	//	If there are no neighbors, then return.
	if(hash_table==NULL)
		return 0;
	readBegin();
	N=HASH_COUNT(hash_table);
	//	Write the fields of the proxy header.
	prxyhdr.type=ntohs(LINK_STATE);
	prxyhdr.length=sizeof(unsigned short)	//	number of neighbors
		+sizeof(link_state_source)	//	source/origin link-state
		+N*(N+1)*sizeof(link_state_record);	// N records
	//	Allocate just enough data to write the link-state packet.
	buffer=ptr=malloc(PROXY_HLEN+ntohs(prxyhdr.length));
	/**
	  *	Write the header, number of neighbors (twice), and the local
	  *	proxy information.
	  */
	*(proxy_header *)ptr=prxyhdr;
	ptr+=sizeof(proxy_header);
	*(unsigned short *)ptr=N;
	ptr+=sizeof(unsigned short);
	*(link_state *)ptr=linkState;
	ptr+=sizeof(link_state);
	*(unsigned short *)ptr=N;
	ptr+=sizeof(unsigned short);
	/**
	  *	First write the link-state records of the edges from the origin
	  *	proxy and its neighbors.
	  */
	HASH_ITER(hh, hash_table, pp, tmp){
		clock_gettime(CLOCK_MONOTONIC, 
			&((link_state_record *)ptr)->ID);
		((link_state_record *)ptr)->proxy1=linkState;
		((link_state_record *)ptr)->proxy2=pp->ls;
		((link_state_record *)ptr)->linkWeight=ntohl(1);
		ptr+=sizeof(link_state_record);
	}
	/**
	  *	Loop on all pairs between hosts to write neighbor records.
	  *	According to an email, there are N^2 records, one for each
	  *	edge.
	  *
	  *	Read uthash.h to better understand the for-loop implementation.
	  */
	for(pp=hash_table; pp->hh.next!=NULL; pp=pp->hh.next){
		/**
		  *	Write the link-state record of the edge from the neighbor to
		  * the origin proxy before looping again through the neighbor
		  *	list.
		  */
		clock_gettime(CLOCK_MONOTONIC,
			&((link_state_record *)ptr)->ID);
		((link_state_record *)ptr)->proxy1=pp->ls;
		((link_state_record *)ptr)->proxy2=linkState;
		((link_state_record *)ptr)->linkWeight=ntohl(1);
		ptr+=sizeof(link_state_record);
		/**
		  *	This loop will cover all connections between all neighbors,
		  *	excluding the origin proxy.
		  */
		for(tmp=hash_table; tmp!=NULL; tmp=tmp->hh.next){
			if(pp==tmp)
				continue;
			clock_gettime(CLOCK_MONOTONIC,
				&((link_state_record *)ptr)->ID);
			((link_state_record *)ptr)->proxy1=pp->ls;
			((link_state_record *)ptr)->proxy2=tmp->ls;
			((link_state_record *)ptr)->linkWeight=ntohl(1);
			ptr+=sizeof(link_state_record);
		}
	}
	/**
	  *	Write the packet to all neighbors.
	  */
	HASH_ITER(hh, hash_table, pp, tmp){
		if((size=rio_write(&pp->rio, buffer,
			PROXY_HLEN+ntohs(prxyhdr.length)))<0){
			/**
			  *	Link-state error condition.
			  */
			free(buffer);
			return -1;
		}
	}
	readEnd();
	free(buffer);
	alarm(config.link_period);
	return -1;
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
	if((size=rio_write(&rio_tap, data, length))<=0){
		fprintf(stderr, "error writing to tap device\n");
		return -1;
	}
	return 0;
}

int Leave(void *data, unsigned short length){
	Peer *pp;
	writeBegin();
	HASH_FIND(hh, hash_table, &((link_state *)data)->tapMAC,
		ETH_ALEN, pp);
	if(pp!=NULL)
		HASH_DEL(hash_table, pp);
	writeEnd();
	pthread_cancel(pp->tid);
	close(pp->rio.fd);
	free(pp);
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
	writeBegin();
	HASH_ITER(hh, hash_table, pp, tmp){
		rio_write(&pp->rio, buffer, PROXY_HLEN+QUIT_LEN);
		remove_member(pp);
	}
	writeEnd();
	pthread_cancel(tap_tid);
	exit(0);
}

int Link_State(void *data, unsigned short length){
	Peer *pp;
	char addr[16];
	void *ptr;
	unsigned short N;
	/**
	  * Check if you are connected to the host that sent the packet.
	  *	Define a struct to dereference the tap MAC address correctly.
	  *
	  *	Finding data in a shared resource is a read operation, and mutual
	  *	exclusion of the shared membership list must be
	  *	writer-preferential.
	  */
	ptr=data;
	N=*(unsigned short *)ptr;
	if(2*sizeof(N)+sizeof(link_state)
		+(N*N+N)*sizeof(link_state_record)!=length){
		/**
		  *	The length of the packet must equal the size of the number
		  *	of neighbors, the size of the source/origin information,
		  *	and the size of all N*N+N link-state records.
		  *
		  *	If this is not the case, then an error has occured.
		  */
		return -1;
	}
	ptr+=sizeof(unsigned short);
	/**
	  *	The source is connected to N neighbors.
	  *	Therefore, the number of edges between all nodes, the source plus
	  *	neighbors (N+1) equals (N+1)*N. There are N*N+N records in the
	  *	packet.
	  *
	  *	If the peer is not in the membership list, then connect to it.
	  */
	writeBegin();
	HASH_FIND(hh, hash_table,
		&((link_state *)ptr)->tapMAC, ETH_ALEN, pp);
	if(pp==NULL){
		inet_ntoa_r((unsigned int)
			((link_state *)ptr)->IPaddr.s_addr,
			addr);
		pp=open_clientfd(addr, ((link_state *)ptr)->listenPort);
		clock_gettime(CLOCK_MONOTONIC, &pp->timestamp);
	}
	for(N=N*N+N; N>0; N--){
		HASH_FIND(hh, hash_table,
			&((link_state_record *)ptr)->proxy1.tapMAC, ETH_ALEN, pp);
		if(pp==NULL){
			inet_ntoa_r((unsigned int)
				((link_state_record *)ptr)->proxy1.IPaddr.s_addr,
				addr);
			pp=open_clientfd(addr, ((link_state *)ptr)->listenPort);
			clock_gettime(CLOCK_MONOTONIC, &pp->timestamp);
		}else{
			//	Compare the packet's timestamp with the saved timestamp.
			pp->timestamp.tv_sec=
				ntohs(((link_state_record *)ptr)->ID.tv_sec);
			pp->timestamp.tv_nsec=
				ntohs(((link_state_record *)ptr)->ID.tv_nsec);
		}
		ptr+=sizeof(link_state_source);
	}
	writeEnd();
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
  *	Lock with writer's preference; in the instance of receiving new
  *	information pertaining to the network, pre-existing data may be
  *	old and false.
  *
  *	Because of the danger of an interleaving between a reader finding
  *	a peer in the membership list and a writer deleting that peer from
  *	the membership list, the mutex cannot be stored inside of the peer
  *	structure; it may be necessary for the mutexes to be stored outside
  *	of the hash table.
  */

void readBegin(){
	pthread_mutex_lock(&mutex3);
	pthread_mutex_lock(&r);
	pthread_mutex_lock(&mutex1);
	if(++readcount==1)
		pthread_mutex_lock(&w);
	pthread_mutex_unlock(&mutex1);
	pthread_mutex_unlock(&r);
	pthread_mutex_unlock(&mutex3);
	return;
}

void readEnd(){
	pthread_mutex_lock(&mutex1);
	if(--readcount==0)
		pthread_mutex_unlock(&w);
	pthread_mutex_unlock(&mutex1);
	return; }


void writeBegin(){
	pthread_mutex_lock(&mutex2);
	if(++writecount==1)
		pthread_mutex_lock(&r);
	pthread_mutex_unlock(&mutex2);
	return;
}

void writeEnd(){
	pthread_mutex_unlock(&mutex2);
	if(--writecount==0)
		pthread_mutex_unlock(&r);
	pthread_mutex_lock(&mutex2);
	return;
}
void add_member(Peer *node){
	Peer *tmp;
	writeBegin();
	HASH_FIND(hh, hash_table, &node->ls.tapMAC, ETH_ALEN, tmp);
	if(tmp == NULL){
		HASH_ADD(hh, hash_table, ls.tapMAC, ETH_ALEN,node);
	}
	writeEnd();
	return;
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
	writeBegin();
	HASH_FIND(hh, hash_table, &node->ls.tapMAC, ETH_ALEN ,tmp);
	if(tmp != NULL){
		HASH_DEL(hash_table, node);
	}
	writeEnd();
	/**
	  *	Upon removing a peer from the membership list, terminate the
	  *	thread associated with the connection, close its file descriptor,
	  *	and free its memory.
	  */
	pthread_cancel(node->tid);
	close(node->rio.fd);
	free(node);
	return;
}

int make_timer(Peer *peer, int timout){
	struct sigevent te;
	struct itimerspec its;
	struct sigaction sa;
	int sigNo = SIGRTMIN;

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = timer_handler;
	sigemptyset(&sa.sa_mask);
	if(sigaction(sigNo, &sa, NULL) == -1){
		//Failed to set up signal
		return -1;
	}

	/*set and arm alarm*/
	te.sigev_notify = SIGEV_SIGNAL;
	te.sigev_signo = sigNo;
	te.sigev_value.sival_ptr = peer;
	//te.sigev_notify_thread_id = peer->tid;
	timer_create(CLOCK_REALTIME, &te, (time_t)peer->timerID);

	its.it_interval.tv_sec = 1;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = timout;
	its.it_value.tv_nsec = 0;
	timer_settime(peer->timerID, 0, &its, NULL);

	return 0;
}

void timer_handler( int sig, siginfo_t *si, void *uc){
    Peer *peerID;
    peerID = si->si_value.sival_ptr;
	remove_member(peerID);
}
