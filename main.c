#include"proxy.h"

int main(int argc, char **argv){
	int *connfdptr, listenfd;	//	listening socket descriptor
	struct sockaddr_in clientaddr;
	unsigned int addrlen=sizeof(struct sockaddr_in);
	unsigned short port;
	char buf[256];
	pthread_t tap_tid, eth_tid, tid;	//	thread identifiers
	if(argc!=3&&argc!=4){
		perror("Error\n\t Usage for first proxy:\n\t\t"
			"cs352proxy <port> <local interface> \n\t"
			"Usage for second proxy: \n\t\t"
			"cs352proxy <remote host> <remote port> <local interface>\n");
		return -1;
	}
	/**
	  *	Test code for reading and parsing the tap device.
	  *	Use this code as an example to interpret the configuration file.
	  *	
	  *	Possible implementation: push data of a certain type that is
	  *	expected to have multiple entries in the configuration file to its
	  *	respective list. For example, for every line that lists a peer,
	  *	input the host name or host address, and the port into a struct,
	  *	and push that struct into a list. Return an error for multiple
	  *	lines of data pertaining to a type that is expected to only have
	  *	one line, such as the listenPort or tapDevice entries.
	  */
	/**
	if((fp=fopen("proxy.conf", "r"))!=NULL)
		printf("proxy.conf opened\n");
	else{
		perror("error opening proxy.conf");
		exit(-1);
	}
	while(!feof(fp)){
		unsigned short x;
		char buf1[32], buf2[32];
		if(fgets(buf, 256, fp)!=buf)
			break;
		if(!strncmp(buf, "listenPort", 10)){
			sscanf(buf, "%s %hu\n", buf1, &x);
			printf("%s %hu\n", buf1, x);
		}else if(!strncmp(buf, "linkPeriod", 10)){
			sscanf(buf, "%s %hu\n", buf1, &x);
			printf("%s %hu\n", buf1, x);
		}else if(!strncmp(buf, "linkTimeout", 11)){
			sscanf(buf, "%s %hu\n", buf1, &x);
			printf("%s %hu\n", buf1, x);
		}else if(!strncmp(buf, "peer", 4)){
			sscanf(buf, "%s %s %hu\n", buf1, buf2, &x);
			printf("%s %s %hu\n", buf1, buf2, x);
		}else if(!strncmp(buf, "quitAfter", 9)){
			sscanf(buf, "%s %hu\n", buf1, &x);
			printf("%s %hu\n", buf1, x);
		}else if(!strncmp(buf, "tapDevice", 9)){
			sscanf(buf, "%s %s\n", buf1, buf2);
			printf("%s %s\n", buf1, buf2);
		}
	}
	fclose(fp);
	*/
	//	Initialize all socket descriptors as -1.
	memset(buf, 0, sizeof(buf));
	memset(&rio_eth, 0, sizeof(rio_eth));
	switch(argc){
		//	server case
		case 3:
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(argv[2], IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device");
				exit(-1);
			}
			//	Get the port number from the argument list.
			port=get_port(argv[1]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
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
			//	Get the port number from the argument list.
			port=get_port(argv[2]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
				perror("error opening listening socket");
				close(tapfd);
				exit(-1);
			}
			//	Connect to the server.
			if((ethfd=open_clientfd(argv[1], port))<0){
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
