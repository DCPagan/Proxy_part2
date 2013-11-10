#include"proxy.h"

int main(int argc, char **argv){
	int i, listenfd;	//	listening socket descriptor
	int addrlen=sizeof(struct sockaddr_in);
	unsigned short port;
	struct sockaddr_in clientaddr;
	struct hostent hostinfo;
	thread_param tp[CONNECTION_MAX];	//	parameters for thread handlers
	pthread_t tap_tid, eth_tid[CONNECTION_MAX];	//	thread identifiers
	if(argc!=3&&argc!=4){
		fprintf(stderr, "Error\n\t Usage for first proxy:\n\t\t"
			"cs352proxy <port> <local interface> \n\t"
			"Usage for second proxy: \n\t\t"
			"cs352proxy <remote host> <remote port> <local interface>\n");
		return -1;
	}
	//	initialize all socket descriptors as -1 for error-handling.
	for(i=0; i<CONNECTION_MAX; i++)
		connections[i]=-1;
	i=0;
	switch(argc){
		//	server case
		case 3:
			//	Set up the tap device.
			if((tapfd=tp[0].tapfd=allocate_tunnel(argv[2], IFF_TAP|IFF_NO_PI))<0){
				fprintf(stderr, "error opening tap device\n");
				close(listenfd);
				exit(-1);
			}
			//	Get the port number from the argument list.
			port=get_port(argv[1]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
				fprintf(stderr, "error opening listening socket\n");
				close(tp[0].tapfd);
				exit(-1);
			}
			if((connections[0]=tp[0].ethfd=accept(
				listenfd, (struct sockaddr *)&clientaddr, &addrlen))<0){
				fprintf(stderr, "error opening socket to client: %s\n",
					strerror(errno));
				close(listenfd);
				close(tp[0].tapfd);
				exit(-1);
			}
			printf("Successfully connected to host at I.P. address %s.\n",
				inet_ntoa(clientaddr.sin_addr));
			break;

		//	client case
		case 4:
			//	Set up the tap device.
			if((tapfd=tp[0].tapfd=
				allocate_tunnel(argv[3], IFF_TAP|IFF_NO_PI))<0){
				fprintf(stderr, "error opening tap device\n");
				exit(-1);
			}
			//	Get the port number from the argument list.
			port=get_port(argv[2]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
				fprintf(stderr, "error opening listening socket\n");
				close(tp[0].tapfd);
				exit(-1);
			}
			//	Connect to the server.
			if((connections[0]=tp[0].ethfd=open_clientfd(argv[1], port))<0){
				fprintf(stderr, "error opening ethernet device\n");
				close(tp[0].tapfd);
				exit(-1);
			}
			break;
		default:
			fprintf(stderr, "error: invalid parameters\n");
			exit(-1);
	}
	pthread_create(&tap_tid, NULL, tap_thread, &tp[0]);
	pthread_create(&eth_tid, NULL, eth_thread, &tp[0]);
	/**
	for(i=1; i<CONNECTION_MAX; i++){
		//	Accept a connection request.
		if((connections[i]=tp.ethfd[i]=accept(
			listenfd, (struct sockaddr *)&clientaddr, &addrlen))<0){
			fprintf(stderr, "error opening socket to client: %s\n",
				strerror(errno));
			close(listenfd);
			exit(-1);
		}
		printf("Successfully connected to host at I.P. address %s.\n",
			inet_ntoa(clientaddr.sin_addr));
		pthread_create(&eth_tid, NULL, eth_thread, &tp);
	}
	*/
	close(tp[0].ethfd);
	close(tp[0].tapfd);
	return 0;
}
