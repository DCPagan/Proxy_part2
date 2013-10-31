#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<stdarg.h>
#include<errno.h>
#include<fcntl.h>
#include<pthread.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<net/if.h>
#include<arpa/inet.h>
#include<linux/ip.h>	// for struct iphdr
#include<linux/if_tun.h>
#include<sys/socket.h>
#include<netdb.h>
#include<sys/ioctl.h>
#include<sys/select.h>
#include<sys/time.h>
#include"rio.h"

#define BACKLOG 16
/**
  * The MTU at layer 2, including the sizes of the ethernet frame header,
  * the ethernet footer, and the layer 3 payload.
  */
#define MTU_L2 1518
/**
  * The MTU at layer 3, including only the IP packet header and the IP
  * payload.
  */
#define MTU_L3 1500
/**
  *	Includese the source MAC address, the destination MAC address, and the
  *	length of the frame payload.
  */
#define FRAME_HEADER_SIZE 14
/**
  * Includes the checksum of the frame payload.
  */
#define FRAME_FOOTER_SIZE 4
/**
  *	This IPv4 header size only applies to basic IPv4 headers such that
  *	IHL==5; if IHL>5, then the packet must be treated accordingly.
  */
#define IPv4_HEADER_SIZE 20
#define IPv6_HEADER_SIZE 40
#define TCP_HEADER_SIZE 20
/**
  *	This TCP header size assumes, as does the IPv4 header size, that data
  *	offset equals 5; the packet should be treated according to the value of
  * that field.
  */
#define PROXY_HEADER_SIZE 4

/**
  * Each packet structure is used to dereference specific fields, such as the
  *	length of the payloads for the ethernet frames or the TCP/IP packets.
  */

typedef struct thread_param{
	int ethfd;
	int tapfd;
} thread_param;

typedef struct frame_header{
	unsigned long long srcMACAddr : 48;
	unsigned long long dstMACAddr : 48;
	unsigned short length;
} frame_header;

typedef struct proxy_header{
	unsigned short type;
	unsigned short length;
} proxy_header;

extern int allocate_tunnel(char *, int);
extern int open_listenfd(unsigned short);
extern int open_clientfd(char *, unsigned short);
extern void *eth_thread(thread_param *tp);
extern void *tap_thread(thread_param *tp);
