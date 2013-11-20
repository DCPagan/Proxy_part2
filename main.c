#include"proxy.h"
#include"link_state.h"

int main(int argc, char **argv){
	int *connfdptr, listenfd;	//	listening socket descriptor
	struct sockaddr_in clientaddr;
	unsigned int addrlen=sizeof(struct sockaddr_in);
	char buf[256];
	pthread_t tap_tid, eth_tid, tid;	//	thread identifiers
	FILE *fp;
	/**
	  *	The old parameter scheme may become redundant when we include
	  *	code for handling the configuration file. Revise all instances
	  *	of arguments accordingly.
	  */
	if(argc!=3){
		fprintf(stderr, "proxy configuration file not specified\n");
		return -1;
	}
	if((fp=fopen(argv[2], "r"))==NULL){
		perror("error opening proxy.conf");
		exit(-1);
	}
	while(!feof(fp)){
		char buf1[64], buf2[64];
		if(fgets(buf, 256, fp)!=buf)
			break;
		if(!strncmp(buf, "listenPort", 10)){
			sscanf(buf, "%s %hu\n", buf1, &config.listen_port);
			printf("%s %hu\n", buf1, config.listen_port);
		}else if(!strncmp(buf, "linkPeriod", 10)){
			sscanf(buf, "%s %hu\n", buf1, &config.link_period);
			printf("%s %hu\n", buf1, config.link_period);
		}else if(!strncmp(buf, "linkTimeout", 11)){
			sscanf(buf, "%s %hu\n", buf1, &config.link_timeout);
			printf("%s %hu\n", buf1, config.link_timeout);
		}else if(!strncmp(buf, "peer", 4)){
			sscanf(buf, "%s %s %hu\n", buf1, buf2, &x);
			printf("%s %s %hu\n", buf1, buf2, x);
		}else if(!strncmp(buf, "quitAfter", 9)){
			sscanf(buf, "%s %hu\n", buf1, &x);
			printf("%s %hu\n", buf1, x);
		}else if(!strncmp(buf, "tapDevice", 9)){
			sscanf(buf, "%s %s\n", buf1, buf2);
			printf("%s %s\n", buf1, buf2);
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(buf2, IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device");
				exit(-1);
			}
		}
	}
	fclose(fp);
	//	Initialize all socket descriptors as -1.
	memset(buf, 0, sizeof(buf));
	memset(&rio_eth, 0, sizeof(rio_eth));
	switch(argc){
		//	server case
		case 3:
			//	Get the config.listen_port number from the argument list.
			config.listen_port=get_config.listen_port(argv[1]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(config.listen_port))<0){
				perror("error opening listening socket");
				close(tapfd);
				exit(-1);
			}
			/**
			  *	Same code as listen_handler, but the tap cannot be read from
			  *	until the first connection is made.
			  */
			if((ethfd=accept(listenfd,
				(struct sockaddr *)&clientaddr, &addrlen))<0){
				perror("error opening socket to client");
				close(tapfd);
				close(listenfd);
				exit(-1);
			}
			printf("Successfully connected to host at I.P. address %s.\n",
				inet_ntoa(clientaddr.sin_addr));
			rio_readinit(&rio_tap, tapfd);
			rio_readinit(&rio_eth, ethfd);
			break;

		//	client case
		case 4:
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(argv[3], IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device");
				exit(-1);
			}
			//	Get the config.listen_port number from the argument list.
			config.listen_port=get_config.listen_port(argv[2]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(config.listen_port))<0){
				perror("error opening listening socket");
				close(tapfd);
				exit(-1);
			}
			//	Connect to the server.
			if((ethfd=open_clientfd(argv[1], config.listen_port))<0){
				perror("error opening ethernet device");
				close(tapfd);
				close(listenfd);
				exit(-1);
			}
			break;
		default:
			perror("error: invalid parameters");
			exit(-1);
	}
	rio_readinit(&rio_tap, tapfd);
	rio_readinit(&rio_eth, ethfd);
	pthread_create(&tap_tid, NULL, tap_handler, &tapfd);
	pthread_create(&eth_tid, NULL, eth_handler, &ethfd);
	for(;;){
		//	Accept a connection request.
		connfdptr=(int *)malloc(sizeof(int));
		if((*connfdptr=accept(listenfd,
			(struct sockaddr *)&clientaddr, &addrlen))<0){
			perror("error opening socket to client");
			close(listenfd);
			exit(-1);
		}
		printf("Successfully connected to host at I.P. address %s.\n",
			inet_ntoa(clientaddr.sin_addr));
		pthread_create(&tid, NULL, eth_handler, connfdptr);
	}
	return 0;
}
