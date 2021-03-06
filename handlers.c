#include"proxy.h"

void *tap_handler(int *fd){
	ssize_t size;
	char buffer[ETH_FRAME_LEN+PROXY_HLEN];
	proxy_header prxyhdr;
	Peer *pp, *tmp;
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	for(;;){
		memset(buffer, 0, ETH_FRAME_LEN+PROXY_HLEN);
		/**
	  	  *	Read the entire Ethernet frame.
		  ****************************************************************
		  *
		  *	IMPORTANT NOTICE, PLEASE READ
		  *
		  ****************************************************************
		  *	For whatever reason, the Ethernet frames of the tap device do
		  *	not include the frame checksum. Because of this, when an extra
		  * four bytes are read from the tap device, it instead reads from
		  *	the first four bytes of the next frame. If the tap device does
		  *	pass frame checksums at the end of frames, then add the macro
		  *	ETH_FCS_LEN (valued at 4) to the third parameter of the
		  *	following reading procedure.
		  */
		if((size=rio_read(&rio_tap, buffer+PROXY_HLEN,
			PROXY_HLEN+ETH_FRAME_LEN))<=0){
			perror("error reading from the tap device.\n");
			exit(-1);
		}
		/**
		  * Write the proxy header in network byte-order to the front of
		  *	the buffer.
		  *
		  * The type field of the proxy header is always set to DATA.
		  *	The size is given by the length field of the Ethernet frame
		  *	header.
		  */
		prxyhdr.type=htons(DATA);
		prxyhdr.length=htons(size);
		memcpy(buffer, &prxyhdr, PROXY_HLEN);
		/**
		  *	Evaluate MAC addresses here. Dereference bufptr as
		  *	(struct ethhdr *), and consult linux/if_ether.h.
		  *
		  *	The length of the payload cannot be evaluated from reading the
		  *	two-octet field as expected; it must be derived from the IPv4
		  *	packet header.
		  */
		writeBegin();
		if(!memcmp(&((struct ethhdr *)(buffer+PROXY_HLEN))->h_dest,
			BROADCAST_ADDR, ETH_ALEN)){
			HASH_ITER(hh, hash_table, pp, tmp){
				if((size=rio_write(&pp->rio, buffer,
					PROXY_HLEN+ntohs(prxyhdr.length)))<=0){
					//	Remove member while holding the mutex.
					pthread_cancel(pp->timeout_tid);
					HASH_DEL(hash_table, pp);
					close(pp->rio.fd);
					pthread_cancel(pp->tid);
					free(pp);
				}
			}
		}else{
			/**
			  *	For part 3, we will have to consult a forwarding table
			  *	instead of a membership list. Substitute accordingly.
			  */
			HASH_FIND(hh, hash_table, &((link_state *)buffer)->tapMAC,
				ETH_ALEN, pp);
			//	Write the whole buffer to the Ethernet device.
			if(pp!=NULL&&(size=rio_write(&pp->rio, buffer,
				ntohs(prxyhdr.length)+PROXY_HLEN))<=0){
				//	Remove member while holding the mutex.
				pthread_cancel(pp->timeout_tid);
				HASH_DEL(hash_table, pp);
				close(pp->rio.fd);
				pthread_cancel(pp->tid);
				free(pp);
			}
		}
		writeEnd();
	}
}

void *eth_handler(Peer *pp){
	/**
  	  *	ethfd points to the socket descriptor to the Ethernet device that
	  *	only this thread can read from. It is set to -1 after the thread
	  *	closes.
	  */
	ssize_t size;
	void *buffer;
	proxy_header prxyhdr;
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	for(;;){
		//	Read the proxy type directly into the proxy header structure.
		if((size=rio_readnb(&pp->rio, &prxyhdr, PROXY_HLEN))<=0){
			pthread_testcancel();
			if(size<0)
				fprintf(stderr,
					"error reading from the Ethernet device.\n");
			else
				perror("connection severed\n");
			remove_member(pp);
		}
		pthread_testcancel();
		/**
	  	  *	Parse and evaluate the proxy header.
		  *	Dynamically allocate memory for the data so that each packet
		  *	could be evaluated concurrently. Free the dynamically allocated
		  *	memory at the called helper functions.
		  */
		prxyhdr.type=ntohs(prxyhdr.type);
		prxyhdr.length=ntohs(prxyhdr.length);
		buffer=malloc(prxyhdr.length);
		if((size=rio_readnb(&pp->rio, buffer, prxyhdr.length))<=0){
			if(size<0)
				fprintf(stderr,
					"error reading from the Ethernet device.\n");
			else
				perror("connection severed\n");
			free(buffer);
			remove_member(pp);
			pthread_testcancel();
		}
		switch(prxyhdr.type){
			case DATA:
				if(Data(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Leave (part 2)
			case LEAVE:
				if(Leave(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Quit
			case QUIT:
				if(Quit(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Link-state (part 2)
			case LINK_STATE:
				if(Link_State(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	RTT Probe Request (part 3)
			case RTT_PROBE_REQUEST:
				if(RTT_Probe_Request(buffer, prxyhdr.length, pp)<0)
					goto TYPE_ERROR;
				break;
			//	RTT Probe Response (part 3)
			case RTT_PROBE_RESPONSE:
				if(RTT_Probe_Response(buffer, prxyhdr.length, pp)<0)
					goto TYPE_ERROR;
				break;
			//	Proxy Public Key (extra credit)
			case PROXY_PUBLIC_KEY:
				if(Proxy_Public_Key(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Signed Data (extra credit)
			case SIGNED_DATA:
				if(Signed_Data(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Proxy Secret key (extra credit)
			case PROXY_SECRET_KEY:
				if(Proxy_Secret_Key(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Encrypted Data (extra credit)
			case ENCRYPTED_DATA:
				if(Encrypted_Data(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Encrypted Link State (extra credit)
			case ENCRYPTED_LINK_STATE:
				if(Encrypted_Link_State(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Signed link-state (extra credit)
			case SIGNED_LINK_STATE:
				if(Signed_Link_State(buffer, prxyhdr.length)<0)
					goto TYPE_ERROR;
				break;
			//	Bandwidth Probe Request
			case BANDWIDTH_PROBE_REQUEST:
				if(Bandwidth_Probe_Request(buffer, prxyhdr.length, pp)<0)
					goto TYPE_ERROR;
				break;
			//	Bandwidth Response
			case BANDWIDTH_PROBE_RESPONSE:
				if(Bandwidth_Probe_Response(buffer, prxyhdr.length, pp)<0)
					goto TYPE_ERROR;
				break;
			default:
				fprintf(stderr, "error, incorrect type\n");
				TYPE_ERROR:
					free(buffer);
					remove_member(pp);
					pthread_testcancel();
		}
		free(buffer);
		pthread_testcancel();
	}
}

/**
  *	Thread handler that closes a link upon link timeout.
  */
void *timeout_handler(Peer *pp){
	struct timespec ts, tscmp;
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec+=config.link_timeout;
	/**
	  *	pthread_cond_timedwait() returns 0 if the condition pointed to by
	  *	the first parameter is signaled within time timeout period, and
	  *	returns a positive Exxx value upon error, such as the timeout.
	  *
	  *	The condition is only signaled if the peer is ready to be removed
	  *	from the membership list. Therefore, the loop should break if the
	  *	function returns 0. Otherwise, evaluate whether or not the link
	  *	has been refreshed by a received link-state packet by comparing
	  *	the current time with the latest timestamp.
	  *
	  *	Threads calling pthread_cond_timedwait() sleep and release the
	  *	mutex lock, and hold the mutex again upon returning from the
	  *	sleep.
	  */
	pthread_mutex_lock(&pp->timeout_mutex);
	while(pthread_cond_timedwait(&pp->timeout_cond,
		&pp->timeout_mutex, &ts)>0){
		pthread_testcancel();
		/**
		  *	Compare the current time to the timestamp of the connection's
		  *	latest timestamp.
		  */
		clock_gettime(CLOCK_REALTIME, &tscmp);
		tscmp.tv_sec-=pp->timestamp.tv_sec;
		tscmp.tv_nsec-=pp->timestamp.tv_nsec;
		/**
		  *	In case the nanoseconds field is negative, decrement the
		  *	seconds field and add one billion to the nanoseconds field.
		  */
		if(tscmp.tv_nsec<0){
			tscmp.tv_sec--;
			tscmp.tv_nsec+=1000000000;
		}
		/**
		  *	If the difference exceeds the link timeout, then sever the
		  *	connection.
		  */
		if(tscmp.tv_sec>=config.link_timeout)
			break;
		/**
		  *	Otherwise, calculate the absolute time when the link expires
		  *	and restart the timed wait.
		  */
		memcpy(&ts, &pp->timestamp, sizeof(struct timespec));
		ts.tv_sec+=config.link_timeout;
	}
	pthread_mutex_unlock(&pp->timeout_mutex);
	pthread_testcancel();
	writeBegin();
	HASH_DEL(hash_table, pp);
	close(pp->rio.fd);
	pthread_cancel(pp->tid);
	free(pp);
	writeEnd();
	return NULL;
}

/**
  *	This handles terminal signals and other terminal conditions.
  *	Before exiting, the proxy must broadcast a leave packet.
  */
void leave_handler(int signo){
	Peer *pp, *tmp;
	ssize_t size;
	leave_packet lvpkt;
	lvpkt.prxyhdr.type=htons(LEAVE);
	lvpkt.prxyhdr.length=htons(QUIT_LEN);
	lvpkt.lv.localIP=linkState.IPaddr;
	lvpkt.lv.localListenPort=htons(linkState.listenPort);
	memcpy(&lvpkt.lv.localMAC, &linkState.tapMAC, ETH_ALEN);
	lvpkt.lv.ID.tv_sec=htonl(timestamp.tv_sec);
	lvpkt.lv.ID.tv_nsec=htonl(timestamp.tv_nsec);
	HASH_ITER(hh, hash_table, pp, tmp){
		pthread_cancel(pp->timeout_tid);
		if((size=rio_write(&pp->rio, &lvpkt,
			sizeof(leave_packet)))<=0){
			/**
			  *	error broadcasting leave packet
			  */
		}
		HASH_DEL(hash_table, pp);
		close(pp->rio.fd);
		pthread_cancel(pp->tid);
		free(pp);
	}
	exit(0);
}

void Link_State_Broadcast(int signo){
	void *buffer, *ptr;
	Peer *pp, *tmp;
	proxy_header prxyhdr;
	uint16_t N;
	ssize_t size;
	probe_req bwreq;
	alarm(config.link_period);
	clock_gettime(CLOCK_REALTIME, &timestamp);
	//	If there are no neighbors, then return.
	if(hash_table==NULL){
		return;
	}
	N=HASH_COUNT(hash_table);
	//	Write the fields of the proxy header.
	prxyhdr.type=htons(LINK_STATE);
	prxyhdr.length=htons(sizeof(uint16_t)	//	number of neighbors
		+sizeof(link_state_source)	//	source/origin link-state
		+N*(N+1)*sizeof(link_state_record));	// N records
	//	Allocate just enough data to write the link-state packet.
	buffer=ptr=malloc(PROXY_HLEN+ntohs(prxyhdr.length));
	/**
	  *	Write the header, number of neighbors (twice), and the local
	  *	proxy information.
	  *
	  *	Mind the byte-order
	  */
	*(proxy_header *)ptr=prxyhdr;
	ptr+=sizeof(proxy_header);
	*(uint16_t *)ptr=htons(N);
	ptr+=sizeof(N);
	*(link_state *)ptr=linkState;
	ptr+=sizeof(link_state);
	*(uint16_t *)ptr=htons(N);
	ptr+=sizeof(N);
	/**
	  *	First write the link-state records of the edges from the origin
	  *	proxy and its neighbors. Include the timestamp of the origin
	  *	proxy.
	  */
	HASH_ITER(hh, hash_table, pp, tmp){
		((link_state_record *)ptr)->ID.tv_sec=htonl(timestamp.tv_sec);
		((link_state_record *)ptr)->ID.tv_nsec=htonl(timestamp.tv_nsec);
		((link_state_record *)ptr)->proxy1=linkState;
		((link_state_record *)ptr)->proxy2=pp->ls;
		((link_state_record *)ptr)->linkWeight=ntohl(1);
		ptr+=sizeof(link_state_record);
	}
	/**
	  *	Loop on all pairs between hosts to write neighbor records.
	  *	According to an email, there are N^2 records, one for each
	  *	edge.
	  *
	  *	Read uthash.h to better understand the for-loop implementation.
	  */
	for(pp=hash_table; pp!=NULL; pp=pp->hh.next){
		/**
		  *	Write the link-state record of the edge from the neighbor to
		  * the origin proxy before looping again through the neighbor
		  *	list.
		  */
		pthread_mutex_lock(&pp->timeout_mutex);
		((link_state_record *)ptr)->ID.tv_sec=
			htonl(pp->timestamp.tv_sec);
		((link_state_record *)ptr)->ID.tv_nsec=
			htonl(pp->timestamp.tv_nsec);
		pthread_mutex_unlock(&pp->timeout_mutex);
		((link_state_record *)ptr)->ID.tv_sec=htonl(timestamp.tv_sec);
		((link_state_record *)ptr)->ID.tv_nsec=htonl(timestamp.tv_nsec);
		((link_state_record *)ptr)->proxy1=pp->ls;
		((link_state_record *)ptr)->proxy2=linkState;
		((link_state_record *)ptr)->linkWeight=ntohl(1);
		ptr+=sizeof(link_state_record);
		/**
		  *	This loop will cover all connections between all neighbors,
		  *	excluding the origin proxy.
		  */
		for(tmp=hash_table; tmp!=NULL; tmp=tmp->hh.next){
			if(pp==tmp)
				continue;
			pthread_mutex_lock(&pp->timeout_mutex);
			((link_state_record *)ptr)->ID.tv_sec=
				htonl(pp->timestamp.tv_sec);
			((link_state_record *)ptr)->ID.tv_nsec=
				htonl(pp->timestamp.tv_nsec);
			pthread_mutex_unlock(&pp->timeout_mutex);
			((link_state_record *)ptr)->proxy1=pp->ls;
			((link_state_record *)ptr)->proxy2=tmp->ls;
			((link_state_record *)ptr)->linkWeight=ntohl(1);
			ptr+=sizeof(link_state_record);
		}
	}
	/**
	  *	Write the packet to all neighbors.
	  */
	HASH_ITER(hh, hash_table, pp, tmp){
		if((size=rio_write(&pp->rio, buffer,
			PROXY_HLEN+ntohs(prxyhdr.length)))<0){
			/**
			  *	Link-state error condition.
			  */
			HASH_DEL(hash_table, pp);
			close(pp->rio.fd);
			pthread_cancel(pp->timeout_tid);
			pthread_cancel(pp->tid);
			free(pp);
		}
	}
	//	extra credit: broadcast bandwidth probe requests
	bwreq.prxyhdr.type=htons(BANDWIDTH_PROBE_REQUEST);
	bwreq.prxyhdr.length=htons(8);
	HASH_ITER(hh, hash_table, pp, tmp){
		clock_gettime(CLOCK_REALTIME, &pp->probe_ts);
		bwreq.ID.tv_sec=htonl(pp->probe_ts.tv_sec);
		bwreq.ID.tv_nsec=htonl(pp->probe_ts.tv_nsec);
		if((size=rio_write(&pp->rio, &bwreq, 12))<0){
			/**
			  *	Link-state error condition.
			  */
			HASH_DEL(hash_table, pp);
			close(pp->rio.fd);
			pthread_cancel(pp->timeout_tid);
			pthread_cancel(pp->tid);
			free(pp);
		}
	}
	free(buffer);
	return;
}

/**
  *	@param	int, void (*)(int): signal number, and the new signal handler
  *	@return	void (*)(int):		the old handler for that signal.
  */
void (*Signal(int signo, void (*sig_handler)(int)))(int){
	static struct sigaction act, oact;
	act.sa_handler=sig_handler;
	act.sa_mask=sigset;
	act.sa_flags=0;
#ifdef SA_RESTART
	act.sa_flags|=SA_RESTART;
#endif
	if(sigaction(signo, &act, &oact)<0)
		return SIG_ERR;
	return oact.sa_handler;
}
