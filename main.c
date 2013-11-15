#include"proxy.h"

int main(int argc, char **argv){
	int i, listenfd;	//	listening socket descriptor
	int addrlen=sizeof(struct sockaddr_in);
	unsigned short port;
	struct sockaddr_in clientaddr;
	struct hostent hostinfo;
	char buf[64];
	char *bufp=buf;
	if(argc!=3&&argc!=4){
		fprintf(stderr, "Error\n\t Usage for first proxy:\n\t\t"
			"cs352proxy <port> <local interface> \n\t"
			"Usage for second proxy: \n\t\t"
			"cs352proxy <remote host> <remote port> <local interface>\n");
		return -1;
	}
	//	Initialize all socket descriptors as -1.
	memset(buf, 0, sizeof(buf));
	memset(&rio_eth, 0, sizeof(rio_eth));
	for(i=0; i<CONNECTION_MAX; i++){
		connections[i]=-1;
		eth_tid[i]=0;
	}
	i=0;
	max_conn=-1;
	next_conn=0;
	switch(argc){
		//	server case
		case 3:
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(argv[2], IFF_TAP|IFF_NO_PI))<0){
				fprintf(stderr, "error opening tap device\n");
				close(listenfd);
				exit(-1);
			}
			//	Get the port number from the argument list.
			port=get_port(argv[1]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
				fprintf(stderr, "error opening listening socket\n");
				close(tapfd);
				exit(-1);
			}
			/**
			  *	Same code as listen_handler, but the tap cannot be read from
			  *	until the first connection is made.
			  */
			if((connections[0]=accept(listenfd,
				(struct sockaddr *)&clientaddr, &addrlen))<0){
				fprintf(stderr, "error opening socket to client: %s\n",
					strerror(errno));
				close(listenfd);
				close(tapfd);
				exit(-1);
			}
			printf("Successfully connected to host at I.P. address %s.\n",
				inet_ntoa(clientaddr.sin_addr));
			rio_readinit(&rio_tap, tapfd);
			rio_readinit(&rio_eth[0], connections[0]);
			++max_conn;
			++next_conn;
			break;

		//	client case
		case 4:
			//	Set up the tap device.
			if((tapfd=allocate_tunnel(argv[3], IFF_TAP|IFF_NO_PI))<0){
				fprintf(stderr, "error opening tap device\n");
				exit(-1);
			}
			//	Get the port number from the argument list.
			port=get_port(argv[2]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
				fprintf(stderr, "error opening listening socket\n");
				close(tapfd);
				exit(-1);
			}
			//	Connect to the server.
			if((connections[0]=open_clientfd(argv[1], port))<0){
				fprintf(stderr, "error opening ethernet device\n");
				close(tapfd);
				exit(-1);
			}
			rio_readinit(&rio_tap, tapfd);
			rio_readinit(&rio_eth[0], connections[0]);
			++max_conn;
			++next_conn;
			break;
		default:
			fprintf(stderr, "error: invalid parameters\n");
			exit(-1);
	}
	pthread_create(&tap_tid, NULL, tap_handler, &tapfd);
	pthread_create(&eth_tid[0], NULL, eth_handler, connections);
	pthread_create(&listen_tid, NULL, listen_handler, &listenfd);
	/**
	  *	The main thread will be dedicated to prompting the user for the next
	  *	proxy to connect to.
	for(i=next_conn; next_conn<CONNECTION_MAX;){
		unsigned int j, k=0;
		do{
			printf("enter address and port of proxy to connect to.\n");
			fgets(buf, 64, stdin);
			strtok(buf, " \n");
			bufp=strtok(NULL, " \n");
			j=strtoul(bufp, NULL, 10);
			if(j==ULONG_MAX&&errno==ERANGE
				||j<1024||j>65535){
				fprintf(stderr, "error: invalid port parameter\n");
				k=0;
				continue;
			}
			port=(unsigned short)j;
			if((connections[i]=open_clientfd(buf, port))<0){
				fprintf(stderr, "error opening ethernet device\n");
				k=0;
				continue;
			}
			k=1;
		}while(!k);

	  	  *	If this thread created a connection with a higher index than
		  *	max_conn.

		if(i>max_conn)
			max_conn=i;
		//	If next_conn has not changed due to a client disconnection.
		if(i==next_conn)
			++i;
	}
	  */
	pthread_join(tap_tid, NULL);
	pthread_join(eth_tid[0], NULL);
	pthread_join(listen_tid, NULL);
	close(connections[0]);
	close(tapfd);
	return 0;
}
