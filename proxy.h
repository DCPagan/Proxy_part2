#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdint.h>
#include<unistd.h>
#include<signal.h>
#include<time.h>
#include<limits.h>
#include<errno.h>
#include<fcntl.h>
#include<poll.h>			//	for poll()
#include<pthread.h>
#include<netdb.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<sys/select.h>
#include<sys/time.h>
#include<time.h>
#include<net/if.h>
#include<arpa/inet.h>
#include<linux/if_ether.h>	//	for Ethernet structures and macros
#include<linux/if_tun.h>
#include"rio.h"
#include"uthash.h"
#include"utlist.h"

#define BACKLOG 16
/**
  * The MTU at layer 2, including the sizes of the ethernet frame header
  * and the layer 3 payload, is given in linux/if_ether.h is ETH_FRAME_LEN.
  */
#define PROXY_HLEN 4
/**
  * Each packet structure is used to dereference specific fields, such as
  *	the length of the payloads for the ethernet frames or the TCP/IP 
  *	packets. The structures for headers for Ethernet frames, IPv4 packets
  *	and ICMP segments can be found in linux/if_ether.h, linux/ip.h and
  *	linux/icmp.h, respectively.
  */
//	List of all packet type numbers
#define DATA 0XABCD						//	part 2
#define LEAVE 0XAB01					//	part 2
#define QUIT 0XAB12						//	part 2
#define LINK_STATE 0XABAC				//	part 2
#define RTT_PROBE_REQUEST 0XAB34		//	part 3
#define RTT_PROBE_RESPONSE 0XAB35		//	part 3
#define PROXY_PUBLIC_KEY 0XAB21			//	extra credit
#define SIGNED_DATA 0XABC1				//	extra credit
#define PROXY_SECRET_KEY 0XAB2			//	extra credit
#define ENCRYPTED_DATA 0XABC2			//	extra credit
#define ENCRYPTED_LINK_STATE 0XABAB		//	extra credit
#define SIGNED_LINK_STATE 0XABAD		//	extra credit
#define BANDWIDTH_PROBE_REQUEST 0XAB45	//	extra credit
#define BANDWIDTH_RESPONSE 0XAB46		//	extra credit

#define QUIT_LEN 20

typedef struct __attribute__((packed)){
	uint16_t type;
	uint16_t length;
} proxy_header;

typedef struct __attribute__((packed)){
	struct in_addr localIP;
	uint16_t localListenPort;
	unsigned char localMAC[ETH_ALEN];
	struct timespec ID;
} leave;

typedef struct __attribute__((packed)){
	proxy_header prxyhdr;
	leave lv;
} leave_packet;

typedef struct __attribute__((packed)){ 
	struct in_addr IPaddr;
	uint16_t listenPort;
	unsigned char tapMAC[ETH_ALEN];
	unsigned char ethMAC[ETH_ALEN];
} link_state;

typedef struct{
	proxy_header prxyhdr;
	struct timespec ID;
} probe_req;

typedef struct __attribute__((packed)){
	link_state ls;
	uint16_t numNbrs;
} link_state_source;

typedef struct __attribute__((packed)){
	struct timespec ID;
	link_state proxy1;
	link_state proxy2;
	uint32_t linkWeight;
} link_state_record;

typedef struct __attribute__((packed)){
	proxy_header prxyhdr;
	uint16_t numNbrs1;
	link_state ls;
	uint16_t numNbrs2;
}initial_join_packet;

typedef struct{
	struct timespec timestamp;
	link_state ls;
	rio_t rio;
	pthread_t tid;
	pthread_t timeout_tid;
	pthread_mutex_t timeout_mutex;
	pthread_cond_t timeout_cond;
	struct timespec probe_timestamp;
	UT_hash_handle hh;
} Peer;

/**
  *	The graph structure will have all the information necessary to
  *	construct a link-state proxy.
  *
  *	Let pp be a graph struct pointer in the graph hash table, and
  *	let nbr be an edge struct pointer in the edge hash table pointed to
  *	by pp->nbrs.
  *
  *	nbr is an edge from the proxy at pp->ls to the proxy at pp->nbr->ls.
  *	pp->timestamp is the timestamp of all records of edges from pp.
  *	nbr->linkWeight is the weight of that edge.
 */
typedef struct graph graph;
typedef struct edge edge;
typedef struct Visited Visited;
typedef struct Queue Queue;
typedef struct ForwardingTable ForwardingTable;

struct edge{
	struct graph *node;
	uint32_t linkWeight;
	UT_hash_handle hh;
};

struct graph{
	struct timespec timestamp;
	link_state ls;
	struct edge *nbrs;
	UT_hash_handle hh;
};

struct Queue{
	graph *node;
	struct Queue *next;
};

struct Visited{
	graph *node;
	graph *prev;
	int dist;
	UT_hash_handle hh;
};

struct ForwardingTable{
	Peer *nextHop;
	graph dest;
	int dist;
	UT_hash_handle hh;
};

typedef struct{
	graph *node;
	graph *prev;
	uint32_t dist;
	uint8_t visited;
} heapent;

typedef struct{
	graph *node;
	uint32_t index;
	UT_hash_handle hh;
} heapindex;

typedef struct{
	uint32_t size;
	uint32_t max;
	heapent **heap;
	heapindex *index;
} Heap;

typedef struct{
	uint16_t listen_port;
	uint32_t link_period;
	uint32_t link_timeout;
	uint32_t quit_timer;
	int tap;
} Config;

typedef struct{
	char hostname[64];
	char port[8];
	struct llnode *next;
} llnode;

extern int allocate_tunnel(char *, int);
extern int getMAC(char *, unsigned char *);
extern uint16_t get_port(char *);
extern int open_listenfd(uint16_t);
extern Peer *connectbyname(char *, char *);
extern Peer *connectbyaddr(uint32_t addr, uint16_t port);

/**
  *	The difference between the client-side and the server-side of the
  *	initial join procedures is that the client sends the packet first,
  *	and then the server sends the packet.
  */
extern Peer *initial_join_client(Peer *pp);
extern Peer *initial_join_server(Peer *pp);

/**
  *	thread handlers for listening to the tap device, and every ethernet
  *	socket.
  */
extern void *tap_handler(int *);
extern void *eth_handler(Peer *);
extern void *timeout_handler(Peer *);

/**
  *	Link_State_Broadcast(int) handles the alarm signal for periodic
  *	packet broadcasts.
  *
  *	leave_handler(int) handles termination signals so as to broadcast
  *	leave packets and TCP FIN packets after shutting down connections.
  */
extern void Link_State_Broadcast(int);
extern void leave_handler(int);

/**
  *	@param	int, void (*)(int): signal number, and the new signal handler
  *	@return	void (*)(int):		the old handler for that signal.
  */
extern void (*Signal(int, void (*)(int)))(int);

/**
  *	All of the procedures for handling different packet types.
  *	Not all of them are implemented; the unimplemented ones merely
  *	return, doing nothing.
  */
extern int Data(void *, uint16_t);
extern int Leave(void *, uint16_t);
extern int Quit(void *, uint16_t);
extern int Link_State(void *, uint16_t);
extern int RTT_Probe_Request(void *, uint16_t, Peer *);
extern int RTT_Probe_Response(void *, uint16_t);
extern int Proxy_Public_Key(void *, uint16_t);
extern int Signed_Data(void *, uint16_t);
extern int Proxy_Secret_Key(void *, uint16_t);
extern int Encrypted_Data(void *, uint16_t);
extern int Encrypted_Link_State(void *, uint16_t);
extern int Signed_Link_State(void *, uint16_t);
extern int Bandwidth_Probe_Request(void *, uint16_t, Peer *);
extern int Bandwidth_Probe_Response(void *, uint16_t);

/**
  *	Simple interface to writer-preferential mutual exclusion.
  *	See the Readers-Writers Problem.
  */
extern void readBegin();
extern void readEnd();
extern void writeBegin();
extern void writeEnd();

//	Membership list interface
extern void add_member(Peer *);
extern void remove_member(Peer *);

//	Graph and routing interface
extern void evaluate_record(link_state_record *);
extern void remove_from_network(graph *);
extern void Dijkstra(graph *); //unused
extern void shortest_path(graph *dest);
extern Visited* bfs();
extern ForwardingTable* prepare_forwarding_table(Visited *v, graph *curr, graph *previous, graph *dest);

//	Heap interface
// unused
extern Heap *heap_alloc(uint32_t);
extern void heap_free(Heap *hp);
extern uint32_t heap_insert(Heap *, graph *, uint32_t);
extern heapent *heap_delete(Heap *);
extern void upheap(Heap *, uint32_t);
extern void downheap(Heap *);

// Queue Interface
extern void enqueue(Queue *q, graph *peer);
extern graph* dequeue(Queue *q);
extern void add2visited(Visited *visted, Visited *v);

//	Global variables
extern int tapfd;
extern pthread_t tap_tid;
extern rio_t rio_tap;	//	Robust I/O struct for the tap device
extern Peer *hash_table;
extern llnode *llhead;
extern Config config;
extern link_state linkState;
extern struct timespec timestamp;
extern const char BROADCAST_ADDR[ETH_ALEN];
//	5 mutexes are required for write-preferential mutual exclusion
extern int readcount, writecount;
extern pthread_mutex_t mutex1, mutex2, mutex3, r, w;
extern sigset_t sigset;
extern graph *network;
extern ForwardingTable *table;
