#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<poll.h>

#define MTU_L2 1518

/**
  *	Robust I/O structure and functions allow for application-level buffering,
  *	which facilitates network I/O operations.
  */

typedef struct{
	int fd;
	int cnt;
	char *bufp;
	char buf[MTU_L2];
	struct pollfd pfd;
} rio_t;

extern void rio_readinit(rio_t *rp, int fd);
/**
  *	First read fills entire buffer
  *	subsequent reads read from the buffer
  */
extern ssize_t rio_read(rio_t *rp, void *usrbuf, size_t n);
extern ssize_t rio_write(rio_t *rp, void *usrbuf, size_t n);
extern ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t n);
//	buffered read
extern ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
extern ssize_t readn(int, void *, size_t);
extern ssize_t writen(int, void *, size_t);
