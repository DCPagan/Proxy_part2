#include"proxy.h"

int main(int argc, char **argv){
	int connfd, listenfd;	//	listening socket descriptor
	struct sockaddr_in clientaddr;
	socklen_t addrlen=sizeof(clientaddr);
	char buf[256];
	Peer *pp, *tmp;
	FILE *fp;
	llnode *add, *lltmp;
	/**
	  *	The old parameter scheme may become redundant when we include
	  *	code for handling the configuration file. Revise all instances
	  *	of arguments accordingly.
	  */
	if(argc!=2){
		fprintf(stderr, "proxy configuration file not specified\n");
		return -1;
	}
	if((fp=fopen(argv[1], "r"))==NULL){
		perror("error opening proxy.conf");
		exit(-1);
	}
	memset(&linkState, -1, sizeof(linkState));
	memset(&config, -1, sizeof(config));
	memset(&add, 0, sizeof(add));
	while(!feof(fp)){
		char buf1[64];
		if(fgets(buf, 256, fp)!=buf)
			break;
		if(!strncmp(buf, "listenPort", 10)){
			sscanf(buf, "%*s %hu\n", &config.listen_port);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(config.listen_port))<0){
				perror("error opening listening socket");
				exit(-1);
			}
			linkState.listenPort=htons(config.listen_port);
		}else if(!strncmp(buf, "linkPeriod", 10)){
			sscanf(buf, "%*s %u\n", &config.link_period);
		}else if(!strncmp(buf, "linkTimeout", 11)){
			sscanf(buf, "%*s %u\n", &config.link_timeout);
		/**
		  *	ERROR HERE: cannot commence the initial join packet
		  *	exchange until the local MAC addresses have been
		  *	acquired.
		  *
		  *	Use a linked list data structure to list the peers,
		  *	and connect to them after the configuration file has
		  *	been read.
		  */
		}else if(!strncmp(buf, "peer", 4)){
			add=(llnode *)malloc(sizeof(llnode));
			sscanf(buf, "%*s %s %hu\n", &add->hostname, &add->port);
			LL_PREPEND(llhead, add);
		}else if(!strncmp(buf, "quitAfter", 9)){
			sscanf(buf, "%*s %u\n", &config.quit_timer);
		}else if(!strncmp(buf, "tapDevice", 9)){
			sscanf(buf, "%*s %s\n", buf1);
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(buf1, IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device");
				exit(-1);
			}
			rio_readinit(&rio_tap, tapfd);
			getMAC(buf1, &linkState.tapMAC);
			getMAC("eth0", &linkState.ethMAC);
		}
	}
	fclose(fp);
	LL_FOREACH_SAFE(llhead, add, lltmp){
		if((pp=open_clientfd(add->hostname, add->port))==NULL){
			perror("error opening ethernet device");
			exit(-1);
		}
	}
	writeBegin();
	HASH_ITER(hh, hash_table, pp, tmp){
		pp->timeout_mutex=PTHREAD_MUTEX_INITIALIZER;
		pp->timeout_cond=PTHREAD_COND_INITIALIZER;
		pthread_create(&pp->tid, NULL, eth_handler, pp);
		pthread_create(&pp->timeout_tid, NULL, timeout_handler, pp);
	}
	writeEnd();
	pthread_create(&tap_tid, NULL, tap_handler, &tapfd);
	/**
	  *	Associate a signal handler to the termination signal to
	  *	construct and broadcast the leave packet.
	  *
	  *	For some reason, I am encountering a segmentation fault every
	  *	time that I send a keyboard termination signal.
	  *
	signal(SIGINT, leave_handler);
	signal(SIGTERM, leave_handler);
	  */
	signal(SIGINT, leave_handler);
	signal(SIGTERM, leave_handler);
	/**
	  *	set up a timer to periodically broadcast link-state packets.
	  */
	signal(SIGALRM, Link_State_Broadcast);
	alarm(config.link_period);
	for(;;){
		//	Accept a connection request.
		if((connfd=accept(listenfd,
			(struct sockaddr *)&clientaddr, &addrlen))<0){
			perror("error opening socket to client");
			close(listenfd);
			exit(-1);
		}
		printf("Successfully connected to host at I.P. address %s.\n",
			inet_ntoa(clientaddr.sin_addr));
		/**
		  *	Get local IP address via getsockname() if this is the first
		  *	socket.
		  */
		if(hash_table==NULL){
			if(getsockname(connfd,
				(struct sockaddr *)&clientaddr, &addrlen)<0){
				perror("error: getsockname()");
				exit(-1);
			}
			linkState.IPaddr=clientaddr.sin_addr;
		}
		pp=(Peer *)malloc(sizeof(Peer));
		memset(pp, 0, sizeof(Peer));
		rio_readinit(&pp->rio, connfd);
		initial_join_server(pp);
		add_member(pp);
		pthread_create(&pp->tid, NULL, eth_handler, pp);
		//	make_timer(pp, config.link_timeout);
	}
	return 0;
}
