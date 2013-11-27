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
#include<linux/ip.h>		//	for IPv4 structures and macros
#include<linux/icmp.h>		//	for ICMP structures and macros
#include<linux/if_tun.h>
#include"rio.h"
#include"uthash.h"
#include"utlist.h"

#define CONNECTION_MAX 16
#define BACKLOG 16
/**
  * The MTU at layer 2, including the sizes of the ethernet frame header
  * and the layer 3 payload, is given in linux/if_ether.h is ETH_FRAME_LEN.
  */
/**
  *	This IPv4 header size only applies to basic IPv4 headers such that
  *	IHL==5; if IHL>5, then the packet must be treated accordingly.
  */
#define IPv4_HLEN 20
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
	unsigned short type;
	unsigned short length;
} proxy_header;

typedef struct __attribute__((packed)){
	struct in_addr localIP;
	unsigned short localListenPort;
	unsigned char localMAC[ETH_ALEN];
	struct timespec ID;
} leave;

typedef struct __attribute__((packed)){ 
	struct in_addr IPaddr;
	unsigned short listenPort;
	unsigned char tapMAC[ETH_ALEN];
	unsigned char ethMAC[ETH_ALEN];
} link_state;

typedef struct __attribute__((packed)){
	link_state ls;
	unsigned short numNbrs;
} link_state_source;

typedef struct __attribute__((packed)){
	struct timespec ID;
	link_state proxy1;
	link_state proxy2;
	unsigned int linkWeight;
} link_state_record;

typedef struct{
	link_state ls;
	pthread_t tid;
	rio_t rio;
	unsigned long long timerID;
	struct timespec timestamp;
	UT_hash_handle hh;
} Peer;

typedef struct{
	char mac[6];
	unsigned short listen_port;
	unsigned int link_period;
	unsigned int link_timeout;
	unsigned int quit_timer;
	int tap;
} Config;

extern int allocate_tunnel(char *, int);
extern int getMAC(char *, char *);
extern unsigned short get_port(char *);
extern int open_listenfd(unsigned short);
extern Peer *open_clientfd(char *, unsigned short);
extern Peer *initial_join_client(Peer *pp);
extern Peer *initial_join_server(Peer *pp);
extern void *tap_handler(int *);
extern void *eth_handler(Peer *);
extern int Link_State_Broadcast(int);
extern void leave_handler(int);

/**
  *	@param	int, void (*)(int): signal number, and the new signal handler
  *	@return	void (*)(int):		the old handler for that signal.
  */
extern void (*Signal(int, void (*)(int)))(int);

extern void inet_ntoa_r(unsigned int addr, char *s);
extern void printEthernet(struct ethhdr *);
extern void printIP(struct iphdr *);
extern void printARP(void *);
extern void printICMP(struct icmphdr *);

extern int Data(void *, unsigned short);
extern int Leave(void *, unsigned short);
extern int Quit(void *, unsigned short);
extern int Link_State(void *, unsigned short);
extern int RTT_Probe_Request(void *, unsigned short);
extern int RTT_Probe_Response(void *, unsigned short);
extern int Proxy_Public_Key(void *, unsigned short);
extern int Signed_Data(void *, unsigned short);
extern int Proxy_Secret_Key(void *, unsigned short);
extern int Encrypted_Data(void *, unsigned short);
extern int Encrypted_Link_State(void *, unsigned short);
extern int Signed_Link_State(void *, unsigned short);
extern int Bandwidth_Probe_Request(void *, unsigned short);
extern int Bandwidth_Probe_Response(void *, unsigned short);

//get time
unsigned long long curr_time();

/**
  *	Simple interface to writer-preferential mutual exclusion.
  *	See the Readers-Writers Problem.
  */

extern void readBegin();
extern void readEnd();
extern void writeBegin();
extern void writeEnd();

/*creates a head node for each list and initialized it to null*/
/*Not needed for part 2*/
//List *ll_create();
//void ll_add(List list, List *node);
//void ll_remove(List list, List *node);

//void remove_expired_member(char* mac, List *node)nk;
extern void add_member(Peer *);
extern void remove_member(Peer *);

extern int tapfd;
extern pthread_t tap_tid;
extern rio_t rio_tap;	//	Robust I/O struct for the tap device
extern Peer *hash_table;
extern Config config;
extern link_state linkState;
extern const char BROADCAST_ADDR[ETH_ALEN];
//	5 mutexes are required for write-preferential mutual exclusion
extern int readcount, writecount;
extern pthread_mutex_t mutex1, mutex2, mutex3, r, w;

//timer
extern int make_timer(Peer *peer, int timout);
extern void timer_handler(int sig, siginfo_t *si, void *uc);
