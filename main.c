#include"proxy.h"

int main(int argc, char **argv){
	int i, listenfd;	//	listening socket descriptor
	int addrlen=sizeof(struct sockaddr_in);
	unsigned short port;
	struct sockaddr_in clientaddr;
	struct hostent hostinfo;
	if(argc!=3&&argc!=4){
		fprintf(stderr, "Error\n\t Usage for first proxy:\n\t\t"
			"cs352proxy <port> <local interface> \n\t"
			"Usage for second proxy: \n\t\t"
			"cs352proxy <remote host> <remote port> <local interface>\n");
		return -1;
	}
	//	Initialize all socket descriptors as -1.
	for(i=0; i<CONNECTION_MAX; i++){
		connections[i]=-1;
		eth_tid[i]=0;
		memset(&rio_eth, 0, sizeof(rio_eth));
	}
	i=0;
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
			break;
		default:
			fprintf(stderr, "error: invalid parameters\n");
			exit(-1);
	}
	max_conn=0;
	next_conn=1;
	pthread_create(&tap_tid, NULL, tap_handler, &tapfd);
	pthread_create(&eth_tid[0], NULL, eth_handler, connections);
	pthread_create(&listen_tid, NULL, listen_handler, &listenfd);
	pthread_join(tap_tid, NULL);
	pthread_join(eth_tid[0], NULL);
	pthread_join(listen_tid, NULL);
	close(connections[0]);
	close(tapfd);
	return 0;
}
