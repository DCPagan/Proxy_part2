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
  *	For rio_read(), the first read fills entire buffer, and subsequent
  *	reads read from the buffer.
  *	The buffer resets after all data stored in the buffer is read.
  *
  *	rio_readnb() makes iterative calls to rio_read() until all data
  *	requested is read.
  */
extern ssize_t rio_readnb(rio_t *, void *, size_t);
extern ssize_t rio_read(rio_t *, void *, size_t);
extern ssize_t rio_write(rio_t *, void *, size_t);
