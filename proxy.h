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

#define CONNECTION_MAX 16
#define BACKLOG 16
/**
  * The MTU at layer 2, including the sizes of the ethernet frame header,
  * the ethernet footer, and the layer 3 payload, given in linux/if_ether.h.
  */
#define MTU_L2 ETH_DATA_LEN + ETH_FCS_LEN
/**
  *	This IPv4 header size only applies to basic IPv4 headers such that
  *	IHL==5; if IHL>5, then the packet must be treated accordingly.
  */
#define IPv4_HEADER_SIZE 20
#define PROXY_HEADER_SIZE 4
/**
  * Each packet structure is used to dereference specific fields, such as the
  *	length of the payloads for the ethernet frames or the TCP/IP packets.
  */

typedef struct{
	unsigned long long srcMACAddr : 48;
	unsigned long long dstMACAddr : 48;
	unsigned short length;
} frame_header;

typedef struct proxy_header{
	unsigned short type;
	unsigned short length;
} proxy_header;

extern int allocate_tunnel(char *, int);
extern unsigned short get_port(char *s);
extern int open_listenfd(unsigned short);
extern int open_clientfd(char *, unsigned short);
extern void *eth_handler(int *);
extern void *tap_handler(int *);
extern void *listen_handler(int *);

extern pthread_t tap_tid, listen_tid,
	eth_tid[CONNECTION_MAX];	//	thread identifiers
extern int tapfd;
extern int connections[CONNECTION_MAX];	//	list of connections
extern int max_conn;	//	maximum index of open socket descriptors
extern int next_conn;	//	least index of unopened socket descriptors
extern rio_t rio_tap;	//	Robust I/O struct for the tap device
extern rio_t rio_eth[CONNECTION_MAX];	//	list of Robust I/O structs
