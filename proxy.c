#include"proxy.h"

int tapfd=-1;
pthread_t tap_tid;
rio_t rio_tap;
Config config;
link_state linkState;
struct timespec timestamp;
Peer *hash_table = NULL;
llnode *llhead=NULL;
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

int getMAC(char *dev, unsigned char *local_mac){
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

uint16_t get_port(char *s){
	uint16_t port;
	unsigned long x;
	x=strtoul(s, NULL, 10);
	//	Check for overflow error
	if(x==ULONG_MAX&&errno==ERANGE
		||x<1024||x>65535){
		perror("error: invalid port parameter");
		exit(1);
	}
	port=(uint16_t)x;
	return port;
}

int open_listenfd(uint16_t port){
	int listenfd;
	struct sockaddr_in serveraddr;
	int optval=1;
	if((listenfd=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))<0){
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
	if(bind(listenfd, &serveraddr, sizeof(serveraddr))<0){
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

Peer *connectbyname(char *hostname, char *port){
	int clientfd;
	int optval=1;
	struct sockaddr_in addr;
	static socklen_t addrlen=sizeof(addr);
	static struct addrinfo hints=
		{0, AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};
	struct addrinfo *res;
	int error;
	Peer *pp;
	if((clientfd=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))<0){
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
	  *	getaddrinfo() is thread-safe, and multiple threads may be making
	  *	TCP connection requests.
	  */
	if((error=getaddrinfo(hostname, port, &hints, &res))!=0){
		perror("error retrieving host information");
		close(clientfd);
		return NULL;
	}
	if(connect(clientfd, res->ai_addr, res->ai_addrlen)<0){
		perror("error connecting to server");
		close(clientfd);
		return NULL;
	}
	freeaddrinfo(res);
	if(linkState.IPaddr.s_addr==-1){
		if(getsockname(clientfd, &addr, &addrlen)<0){
			perror("error: getsockname()");
			exit(-1);
		}
		linkState.IPaddr=addr.sin_addr;
	}
	//	Initialize local tap MAC address
	pp=(Peer *)malloc(sizeof(Peer));
	memset(pp, 0, sizeof(Peer));
	rio_readinit(&pp->rio, clientfd);
	initial_join_client(pp);
	add_member(pp);
	return pp;
}

Peer *connectbyaddr(uint32_t addr, uint16_t port){
	struct sockaddr_in peeraddr;
	int peerfd;
	static int optval=1;
	Peer *pp;
	if(peerfd=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)){
		perror("error opening socket");
		return NULL;
	}
	if(setsockopt(peerfd, SOL_SOCKET, SO_REUSEADDR,
		&optval, sizeof(optval))<0){
		perror("setsockopt() error");
		close(peerfd);
		return NULL;
	}
	memset(&peeraddr, 0, sizeof(peeraddr));
	peeraddr.sin_family=AF_INET;
	peeraddr.sin_addr.s_addr=addr;
	peeraddr.sin_port=port;
	if(connect(peerfd, &peeraddr, sizeof(peeraddr))<0){
		perror("error connecting to peer");
		close(peerfd);
		return NULL;
	}
	//	Commence the link-state packet exchange with the peer.
	pp=(Peer *)malloc(sizeof(Peer));
	memset(pp, 0, sizeof(Peer));
	rio_readinit(&pp->rio, peerfd);
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
	pkt.prxyhdr.length=htons(sizeof(uint16_t)	//	number of neighbors
		+sizeof(link_state_source));	//	source/origin link-state
	/**
	  *	Allocate memory for a structure to hold the struct for a
	  *	constant-sized single-record link-state structure by declaring
	  *	such an automatic structure variable.
	  */
	pkt.ls=linkState;
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
	pkt.ls.listenPort=ntohs(pkt.ls.listenPort);
	pp->ls=pkt.ls;
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
	pkt.prxyhdr.length=htons(sizeof(uint16_t)	//	number of neighbors
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
	return pp;
}

int Data(void *data, uint16_t length){
	ssize_t size;
	//	Write the payload to the tap device.
	if((size=rio_write(&rio_tap, data, length))<=0){
		fprintf(stderr, "error writing to tap device\n");
		return -1;
	}
	return 0;
}

int Leave(void *data, uint16_t length){
	Peer *pp;
	readBegin();
	HASH_FIND(hh, hash_table, &((link_state *)data)->tapMAC,
		ETH_ALEN, pp);
	readEnd();
	if(pp!=NULL)
		remove_member(pp);
	return 0;
}

int Quit(void *data, uint16_t length){
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

int Link_State(void *data, uint16_t length){
	Peer *pp;
	void *ptr;
	uint16_t N;
	ptr=data;
	N=ntohs(*(uint16_t *)ptr);
	ptr+=sizeof(uint16_t);
	//	Verify correct length of packet.
	if(2*sizeof(N)+sizeof(link_state)
		+(N*N+N)*sizeof(link_state_record)!=length){
		return -1;
	}
	/**
	  *	Check if the link-state packet came from this proxy.
	  *	Same code as in the loop.
	  */
	if(!memcmp(((link_state *)ptr)->tapMAC, linkState.tapMAC, ETH_ALEN))
		return 0;
	writeBegin();
	HASH_FIND(hh, hash_table,
		&((link_state *)ptr)->tapMAC, ETH_ALEN, pp);
	/**
	  *	If the peer is not in the list, connect to it with the address
	  *	Connection information is supplied in the packet.
	  */
	if(pp==NULL){
		pp=connectbyaddr(((link_state *)ptr)->IPaddr.s_addr,
			ntohs(((link_state *)ptr)->listenPort));
		clock_gettime(CLOCK_REALTIME, &pp->timestamp);
	}
	ptr+=sizeof(link_state_source);
	/**
	  *	ptr now points to the beginning of the membership list.
	  *	Iterate through the records
	  */
	for(N=N*N+N; N>0; N--, ptr+=sizeof(link_state_record)){
		//	If the tap MAC address is the same as this proxy, continue.
		if(!memcmp(((link_state_record *)ptr)->proxy1.tapMAC,
			linkState.tapMAC, ETH_ALEN))
			continue;
		HASH_FIND(hh, hash_table,
			&((link_state_record *)ptr)->proxy1.tapMAC, ETH_ALEN, pp);
		/**
		  *	If the peer is not in the list, connect to it with the
		  *	address information supplied in the packet.
		  */
		if(pp==NULL){
			pp=connectbyaddr(((link_state *)ptr)->IPaddr.s_addr,
				ntohs(((link_state *)ptr)->listenPort));
			clock_gettime(CLOCK_REALTIME, &pp->timestamp);
		//	Otherwise, update the timestamp.
		}else{
			pthread_mutex_lock(&pp->timeout_mutex);
			pp->timestamp.tv_sec=
				ntohl(((link_state_record *)ptr)->ID.tv_sec);
			pp->timestamp.tv_nsec=
				ntohl(((link_state_record *)ptr)->ID.tv_nsec);
			pthread_mutex_unlock(&pp->timeout_mutex);
		}
	}
	writeEnd();
	return 0;
}

int RTT_Probe_Request(void *data, uint16_t length){
	return 0;
}

int RTT_Probe_Response(void *data, uint16_t length){
	return 0;
}

int Proxy_Public_Key(void *data, uint16_t length){
	return 0;
}

int Signed_Data(void *data, uint16_t length){
	return 0;
}

int Proxy_Secret_Key(void *data, uint16_t length){
	return 0;
}

int Encrypted_Data(void *data, uint16_t length){
	return 0;
}

int Encrypted_Link_State(void *data, uint16_t length){
	return 0;
}

int Signed_Link_State(void *data, uint16_t length){
	return 0;
}

int Bandwidth_Probe_Request(void *data, uint16_t length){
	return 0;
}

int Bandwidth_Probe_Response(void *data, uint16_t length){
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
	return;
}

void writeBegin(){
	pthread_mutex_lock(&mutex2);
	if(++writecount==1)
		pthread_mutex_lock(&r);
	pthread_mutex_unlock(&mutex2);
	pthread_mutex_lock(&w);
	return;
}

void writeEnd(){
	pthread_mutex_unlock(&w);
	pthread_mutex_lock(&mutex2);
	if(--writecount==0)
		pthread_mutex_unlock(&r);
	pthread_mutex_unlock(&mutex2);
	return;
}
void add_member(Peer *pp){
	Peer *tmp;
	writeBegin();
	HASH_FIND(hh, hash_table, &pp->ls.tapMAC, ETH_ALEN, tmp);
	if(tmp == NULL){
		HASH_ADD(hh, hash_table, ls.tapMAC, ETH_ALEN, pp);
	}
	memset(&pp->timestamp, 0, sizeof(struct timespec));
	pthread_mutex_init(&pp->timeout_mutex, NULL);
	pthread_cond_init(&pp->timeout_cond, NULL);
	pthread_create(&pp->tid, NULL, eth_handler, pp);
	pthread_create(&pp->timeout_tid, NULL, timeout_handler, pp);
	writeEnd();
	return;
}

//	Signal the timeout thread of the respective peer.
void remove_member(Peer *pp){
	writeBegin();
	HASH_FIND(hh, hash_table, &pp->ls.tapMAC, ETH_ALEN, pp);
	if(pp==NULL)
		return;
	pthread_mutex_lock(&pp->timeout_mutex);
	pthread_cond_signal(&pp->timeout_cond);
	pthread_mutex_unlock(&pp->timeout_mutex);
	writeEnd();
	return;
}
