#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<poll.h>
#include<fcntl.h>
#include<linux/if_ether.h>

#ifndef PROXY_HLEN
#define PROXY_HLEN 4
#endif

/**
  *	Robust I/O structure and functions allow for application-level buffering,
  *	which facilitates network I/O operations.
  */

typedef struct{
	int fd;
	int cnt;
	char *bufp;
	char buf[ETH_FRAME_LEN + PROXY_HLEN];
} rio_t;

extern void rio_readinit(rio_t *, int);
extern void rio_resetBuffer(rio_t *);
/**
  *	First read fills entire buffer
  *	subsequent reads read from the buffer
  */
extern ssize_t rio_read(rio_t *, void *, size_t);
extern ssize_t rio_write(rio_t *, void *, size_t);
//	buffered read
extern ssize_t rio_readnb(rio_t *, void *, size_t);
extern ssize_t readn(int, void *, size_t);
extern ssize_t writen(int, void *, size_t);
