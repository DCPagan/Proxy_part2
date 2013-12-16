#include"rio.h"

void rio_readinit(rio_t *rp, int fd){
	int flags;
	rp->fd=fd;
	rp->cnt=0;
	rp->bufp=rp->buf;
	memset(rp->buf, 0, ETH_FRAME_LEN+PROXY_HLEN);
	/**
	  *	Use fcntl() to set the flags of the file descriptor to include
	  *	non-blocking.
	  */
	if(flags=fcntl(rp->fd, F_GETFL, 0)<0){
		perror("F_GETFL error");
		exit(-1);
	}
	flags|=O_NONBLOCK;
	if(fcntl(rp->fd, F_SETFL, flags)<0){
		perror("F_SETFL error");
		exit(-1);
	}
}

int Wait(int fd, int events){
	/**
	  *	poll structure for waiting until the file descriptor is ready.
	  *	Initialization upon declaration of a static variable implies that
	  *	the variable is initialized only once.
	  */
	static struct pollfd pfd={0, 0, 0};
	pfd.fd=fd;
	pfd.events=events;
	//	Loop blocking poll call in case of signal interrupt.
	do{
		if(poll(&pfd, 1, -1)<0){
			if(errno!=EINTR){
				perror("poll() error");
				return -1;
			}
		}
	}while(errno==EINTR);
	return 0;
}

int Lock(int fd, int type){
	/**
	  *	flock structures are necessary for file-locking via fcntl().
	  *	Initialization upon declaration of a static variable implies that
	  *	the variable is initialized only once.
	  */
	static struct flock lock={0, 0, 0, 0, 0};
	lock.l_type=type;
	//	Loop a blocking lock call in case of signal interruption.
	do
		if(fcntl(fd, F_SETLKW, &lock)<0)
			//	If fcntl() encounters an error, return error status.
			if(errno!=EINTR)
				return -1;
	while(errno==EINTR);
	return 0;
}

ssize_t rio_read(rio_t *rp, void *usrbuf, size_t n){
	int cnt;
	/**
	  * Wait indefinitely until the socket is ready for reading.
	  *
	  *	Returning from Wait() implies that the socket has data to be
	  *	received.
	  *
	  *	Returning from Lock() implies that the file descriptor is locked
	  *	or unlocked, allowing for exclusive access to file descriptors.
	  *
	  *	Receive the packet into the buffer.
	  *
	  *	Wrap locking procedure over error-handling in case fcntl() is
	  *	interrupted by a signal or encounters an error. This error-
	  *	handling code is copied in rio_write().
	  */
	/*
	if(Lock(rp->fd, F_RDLCK)<0)
		return -1;
		*/
	while(rp->cnt<=0){
		if(Wait(rp->fd, POLLIN)<0)
			return -1;
		rp->cnt=read(rp->fd, rp->buf, sizeof(rp->buf));
		if(rp->cnt<0){
			//	signal interrupt case
			if(errno!=EINTR&&errno!=EAGAIN){
				//Lock(rp->fd, F_UNLCK);
				return -1;
			}
		}
		//	EOF case
		else if(rp->cnt==0){
			//Lock(rp->fd, F_UNLCK);
			return 0;
		}
		//	no error
		else
			rp->bufp=rp->buf;
	}
	/*
	if(Lock(rp->fd, F_UNLCK)<0)
		return -1;
		*/
	cnt=n;
	//	if bytes read were less than demanded
	if(rp->cnt<n)
		cnt=rp->cnt;
	//	copy read bytes into memory
	memcpy(usrbuf, rp->bufp, cnt);
	//	decrement count of number of unread bytes
	rp->cnt-=cnt;
	//	Reset buffer pointer to beginning if necessary.
	if(rp->cnt<=0)
		rp->bufp=rp->buf;
	//	increment buffer pointer to first unread byte
	else
		rp->bufp+=cnt;
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
	size_t nleft=n;
	ssize_t nwritten;
	char *bufp=usrbuf;
	if(Lock(rp->fd, F_WRLCK)<0)
		return -1;
	while(nleft>0){
		if(Wait(rp->fd, POLLOUT)<0)
			return -1;
		if((nwritten=write(rp->fd, bufp, nleft))<0){
			//	interrupted by signal handler
			if(errno==EINTR)
				nwritten=0;
			//	interrupted by write()
			else{
				if(Lock(rp->fd, F_UNLCK)<0)
					return -1;
				return -1;
			}
		}
		//	decrement number of bytes remaining by number of bytes written
		nleft-=nwritten;
		//	increment buffer pointer by number of bytes written
		bufp+=nwritten;
	}
	if(Lock(rp->fd, F_UNLCK)<0)
		return -1;
	//	n will always equal number of bytes finally written
	return (n-nleft);
}
