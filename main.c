#include"proxy.h"

int main(int argc, char **argv){
	int listenfd;
	unsigned short port;
	struct sockaddr_in clientaddr;
	struct hostent hostinfo;
	thread_param tp;
	pthread_t eth_tid, tap_tid;
	if(argc!=3&&argc!=4){
		fprintf(stderr, "Error\n\t Usage for first proxy:\n\t\t"
			"cs352proxy <port> <local interface> \n\t"
			"Usage for second proxy: \n\t\t"
			"cs352proxy <remote host> <remote port> <local interface>\n");
	return -1;
	}

	switch(argc){
		//	server case
		case 3:
			//	Get the port number from the argument list.
			port=get_port(argv[1]);
			//	Set up a socket to listen to clients' connection requests.
			if((listenfd=open_listenfd(port))<0){
				fprintf(stderr, "error opening listening socket\n");
				exit(-1);
			}
			//	Accept a connection request.
			if((tp.ethfd=accept(listenfd, &clientaddr,
				sizeof(clientaddr)))<0){
				fprintf(stderr, "error opening socket to client\n");
				exit(-1);
			}
			printf("Successfully connected to client at I.P. address %s.\n",
				inet_ntoa(clientaddr.sin_addr));
			//	Set up the tap device.
			if((tp.tapfd=allocate_tunnel(argv[2], IFF_TAP|IFF_NO_PI))<0){
				fprintf(stderr, "error opening tap device\n");
				exit(-1);
			}
			break;

		//	client case
		case 4:
			//	Get the port number from the argument list.
			port=get_port(argv[2]);
			//	Connect to the server.
			if((tp.ethfd=open_clientfd(argv[1], port))<0){
				fprintf(stderr, "error opening ethernet device\n");
				exit(-1);
			}
			//	Set up the tap device.
			if((tp.tapfd=allocate_tunnel(argv[3], IFF_TAP|IFF_NO_PI))<0){
				fprintf(stderr, "error opening tap device\n");
				exit(-1);
			}
			break;
		default:
			fprintf(stderr, "ERROR: invalid parameters.\n");
			exit(-1);
	}
	/**
	  * 1st thread listens to TCP socket
	  * 2nd thread listens to tap device
	  */
	pthread_create(&eth_tid, NULL, eth_thread, &tp);
	pthread_create(&tap_tid, NULL, tap_thread, &tp);
	pthread_join(eth_tid, NULL);
	pthread_join(tap_tid, NULL);
	close(tp.ethfd);
	close(tp.tapfd);
	return 0;
}
