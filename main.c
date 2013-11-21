#include"proxy.h"
#include"link_state.h"

int main(int argc, char **argv){
	int *connfdptr, listenfd;	//	listening socket descriptor
	struct sockaddr_in clientaddr;
	unsigned int addrlen=sizeof(struct sockaddr_in);
	char buf[256];
	rio_t *rp;
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
	memset(&linkState, 0, sizeof(linkState));
	memset(&config, 0, sizeof(config));
	while(!feof(fp)){
		char buf[64];
		if(fgets(buf, 256, fp)!=buf)
			break;
		if(!strncmp(buf, "listenPort", 10)){
			sscanf(buf, "%*s %hu\n", &config.listen_port);
			printf("%s %hu\n", config.listen_port);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(config.listen_port))<0){
				perror("error opening listening socket");
				exit(-1);
			}
			linkState.listenPort=config.listen_port;
		}else if(!strncmp(buf, "linkPeriod", 10)){
			sscanf(buf, "%*s %hu\n", buf1, &config.link_period);
			printf("%s %hu\n", buf1, config.link_period);
		}else if(!strncmp(buf, "linkTimeout", 11)){
			sscanf(buf, "%*s %hu\n", &config.link_timeout);
			printf("%s %hu\n", config.link_timeout);
		}else if(!strncmp(buf, "peer", 4)){
			sscanf(buf, "%*s %s %hu\n", buf, &port);
			printf("%s %s %hu\n", buf, port);
			//	Connect to the server.
			if((ethfd=open_clientfd(argv[1], config.listen_port))<0){
				perror("error opening ethernet device");
				exit(-1);
			}
		}else if(!strncmp(buf, "quitAfter", 9)){
			sscanf(buf, "%*s %hu\n", &config.quit_timer);
			printf("%s %hu\n", config.quit_timer);
		}else if(!strncmp(buf, "tapDevice", 9)){
			sscanf(buf, "%*s %s\n", buf);
			printf("%s %s\n", buf);
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(buf, IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device");
				exit(-1);
			}
			rio_readinit(&rio_tap, tapfd);
			pthread_create(&tid, NULL, eth_handler, connfdptr);
		}
	}
	fclose(fp);
	//	Initialize all socket descriptors as -1.
	pthread_create(&tap_tid, NULL, tap_handler, &tapfd);
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
	}
	return 0;
}
