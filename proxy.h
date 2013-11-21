#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<limits.h>
#include<unistd.h>
#include<stdarg.h>
#include<errno.h>
#include<fcntl.h>
#include<pthread.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<sys/ioctl.h>
#include<sys/select.h>
#include<sys/time.h>
#include<poll.h>			//	for poll()
#include<net/if.h>
#include<arpa/inet.h>
#include<linux/if_ether.h>	//	for Ethernet structures and macros
#include<linux/ip.h>		//	for IPv4 structures and macros
#include<linux/icmp.h>		//	for ICMP structures and macros
#include<linux/if_tun.h>
#include<netdb.h>
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
  * Each packet structure is used to dereference specific fields, such as the
  *	length of the payloads for the ethernet frames or the TCP/IP packets.
  *	The structures for headers for Ethernet frames, IPv4 packets and ICMP
  *	segments can be found in linux/if_ether.h, linux/ip.h and linux/icmp.h,
  *	respectively.
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

typedef struct proxy_header{
	unsigned short type;
	unsigned short length;
} proxy_header;

typedef struct{
	struct in_addr localIP;
	unsigned short localListenPort;
	unsigned char localMAC[ETH_ALEN];
	unsigned long long ID;
} __attribute__((packed)) leave;

typedef struct{
	struct in_addr IPaddr;
	unsigned short listenPort;
	unsigned char MAC[ETH_ALEN];
} __attribute__((packed)) link_state;

typedef struct{
	struct in_addr localIP;
	unsigned short localListenPort;
	unsigned char localMAC[ETH_ALEN];
	struct in_addr remoteIP;
	unsigned short remoteListenPort;
	unsigned char remoteMAC[ETH_ALEN];
	unsigned int averageRTT;
}  __attribute__((packed)) link_state_Neighbor;

extern int allocate_tunnel(char *, int);
extern unsigned short get_port(char *);
extern int open_listenfd(unsigned short);
extern int open_clientfd(char *, unsigned short);
extern void *tap_handler(int *);
extern void *eth_handler(int *);

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

extern int tapfd;
extern int ethfd;
extern rio_t rio_tap;	//	Robust I/O struct for the tap device
extern rio_t rio_eth;	//	list of Robust I/O structs
