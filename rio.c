#include"rio.h"

void rio_readinit(rio_t *rp, int fd){
	int flags;
	rp->fd=fd;
	rp->cnt=0;
	rp->bufp=rp->buf;
	memset(rp->buf, 0, MTU_L2);
	/**
	  *	Set the file flags to include non-blocking.
	  */
	if(flags=fcntl(rp->fd, F_GETFL, 0)<0){
		fprintf(stderr, "F_GETFL error\n");
		exit(-1);
	}
	flags|=O_NONBLOCK;
	if(fcntl(rp->fd, F_SETFL, flags)<0){
		fprintf(stderr, "F_SETFL error\n");
		exit(-1);
	}
}

void rio_resetBuffer(rio_t *rp){
	memset(rp->buf, 0, MTU_L2);
	rp->bufp=rp->buf;
}

ssize_t rio_read(rio_t *rp, void *usrbuf, size_t n){
	/**
  	  *	flock structures are necessary for file-locking via fcntl().
	  *	These structures are constant for all executions of this function.
	  */
	static struct flock lock={F_RDLCK, 0, 0, 0, 0};
	static struct flock unlock={F_UNLCK, 0, 0, 0, 0};
	//	poll structure for waiting until the file descriptor is ready.
	struct pollfd pfd={rp->fd, POLLIN, 0};
	int cnt;
	while(rp->cnt<=0){
		/**
		  * Wait indefinitely until the socket is ready for reading.
		  *	Receive the packet into the buffer.
		  *	Returning from poll() implies that the socket has data to be
		  *	received.
		  *	read() terminates at the end of data with the tap device, but
		  *	blocks with a socket.
		  *	Use fcntl() to set the file descriptor to non-blocking instead.
		  *
		  *	Also use fcntl() to lock the file descriptor for thread-safe
		  *	access to I/O operations.
		  *
		  *	Wrap locking procedure over error-handling in case fcntl() is
		  *	interrupted by a signal or encounters an error.
		  *	This error-handling code is copied in rio_write().
		  */
		poll(&pfd, 1, -1);
		do{
			if(fcntl(rp->fd, F_SETLKW, &lock)<0
				&&errno!=EINTR){
				fprintf(stderr, "F_SETLKW error\n");
				exit(-1);
			}
		}while(errno==EINTR);
		rp->cnt=read(rp->fd, rp->buf, sizeof(rp->buf));
		if(rp->cnt<0){
			//	signal interrupt case
			if(errno!=EINTR)
				return -1;
		}
		//	EOF case
		else if(rp->cnt==0)
			return 0;
		//	no error
		else
			rp->bufp=rp->buf;
		//	error-wrapped file unlock
		do{
			if(fcntl(rp->fd, F_SETLKW, &unlock)<0
				&&errno!=EINTR){
				fprintf(stderr, "F_SETLKW error\n");
				exit(-1);
			}
		}while(errno==EINTR);
	}
	cnt=n;
	//	if bytes read were less than demanded
	if(rp->cnt<n)
		cnt=rp->cnt;
	//	copy read bytes into memory
	memcpy(usrbuf, rp->bufp, cnt);
	//	increment buffer pointer to first unread byte
	rp->bufp+=cnt;
	//	decrement count of number of unread bytes
	rp->cnt-=cnt;
	//	return number of bytes read
	return cnt;
}

ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n){
	ssize_t nread;
	size_t nleft=n;
	void *bufp=usrbuf;
	while(nleft>0){
		if((nread=rio_read(rp, bufp, nleft))<0){
			// signal handler interrupt
			if(errno==EINTR)
				nread=0;
			// read() error
			else
				return -1;
		}
		// EOF
		else if(nread==0)
			break;
		//	Decrement number of bytes left by number of bytes read.
		nleft-=nread;
		//	Increment buffer pointer by number of bytes read.
		bufp+=nread;
	}
	//	return total number of bytes read
	return (n-nleft);
}

ssize_t rio_write(rio_t *rp, void *usrbuf, size_t n){
	/**
  	  *	flock structures are necessary for file-locking via fcntl().
	  *	These structures are constant for all executions of this function.
	  */
	static struct flock lock={F_WRLCK, 0, 0, 0, 0};
	static struct flock unlock={F_UNLCK, 0, 0, 0, 0};
	//	poll structure for waiting until the file descriptor is ready.
	struct pollfd pfd={rp->fd, POLLIN, 0};
	size_t nleft=n;
	ssize_t nwritten;
	char *bufp=usrbuf;
	while(nleft>0){
		poll(&pfd, 1, -1);
		//	error-wrapped file lock
		do{
			if(fcntl(rp->fd, F_SETLKW, &lock)<0
				&&errno!=EINTR){
				fprintf(stderr, "F_SETLKW error\n");
				exit(-1);
			}
		}while(errno==EINTR);
		if((nwritten=write(rp->fd, bufp, nleft))<0){
			//	interrupted by signal handler
			if(errno==EINTR)
				nwritten=0;
			//	interrupted by write()
			else
				return -1;
		}
		//	error-wrapped file unlock
		do{
			if(fcntl(rp->fd, F_SETLKW, &unlock)<0
				&&errno!=EINTR){
				fprintf(stderr, "F_SETLKW error\n");
				exit(-1);
			}
		}while(errno==EINTR);
		//	decrement number of bytes remaining by number of bytes written
		nleft-=nwritten;
		//	increment buffer pointer by number of bytes written
		bufp+=nwritten;
	}
	//	n will always equal number of bytes finally written
	return (n-nleft);
}

ssize_t writen(int fd, void *usrbuf, size_t n){
	size_t nleft=n;
	ssize_t nwritten;
	char *bufp=usrbuf;
	while(nleft>0){
		if((nwritten=write(fd, bufp, nleft))<0){
			//	signal handler interrupt
			if(errno==EINTR)
				nwritten=0;
			//	write() error
			else
				return -1;
		}
		//	Decrement number of bytes left by number of bytes written.
		nleft-=nwritten;
		//	Increment buffer pointer by number of bytes written.
		bufp+=nwritten;
	}
	//	Return total number of bytes written.
	return (n-nleft);
}
