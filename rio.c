#include"rio.h"

void rio_readinit(rio_t *rp, int fd){
	rp->fd=fd;
	rp->cnt=0;
	rp->bufp=rp->buf;
	memset(rp->buf, 0, MTU_L2);
}

void rio_resetBuffer(rio_t *rp){
	memset(rp->buf, 0, MTU_L2);
	rp->bufp=rp->buf;
}

ssize_t rio_read(rio_t *rp, void *usrbuf, size_t n){
	int cnt;
	while(rp->cnt<=0){
		rp->cnt=read(rp->fd, rp->buf, sizeof(rp->buf));
		if(rp->cnt<0){
			//	signal interrupt case
			if(errno!=EINTR)
				rp->cnt=0;
			//	error case
			else return -1;
		}
		//	EOF case
		else if(rp->cnt==0)
			return 0;
		//	no error
		else
			rp->bufp=rp->buf;
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

ssize_t rio_write(rio_t *rp, void *usrbuf, size_t n){
	size_t nleft=n;
	ssize_t nwritten;
	char *bufp=usrbuf;
	while(nleft>0){
		if((nwritten=write(rp->fd, bufp, nleft))<0){
			//	interrupted by signal handler
			if(errno==EINTR)
				nwritten=0;
			//	interrupted by write()
			else
				return -1;
		}
		//	decrement number of bytes remaining by number of bytes written
		nleft-=nwritten;
		//	increment buffer pointer by number of bytes written
		bufp+=nwritten;
	}
	//	n will always equal number of bytes finally written
	return (n-nleft);
}

ssize_t rio_readline(rio_t *rp, void *usrbuf, size_t n){
	int i, cnt;
	char c;
	char *bufp=usrbuf;
	for(i=1; i<n; i++){
		if((cnt=rio_read(rp, &c, 1))==1){
			*bufp++=c;
			if(c=='\n')
				break;
		} else if(cnt==0){
			//	EOF, no data read
			if(n==1)
				return 0;
			//	EOF, some data was read
			else
				break;
		} else	//	error
			return -1;
	}
	//	null terminator
	*bufp=0;
	//	return number of bytes read, including null terminator
	return i;
}

ssize_t readn(int fd, void *usrbuf, size_t n){
	ssize_t nread;
	size_t nleft=n;
	char *bufp=usrbuf;
	while(nleft>0){
		if((nread=read(fd, bufp, nleft))<0){
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
