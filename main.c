#include"proxy.h"

int main(int argc, char **argv){
	char *c;
	unsigned short port;
	thread_param tp;
	pthread_t eth_tid, tap_tid;
	
	char *remote_host;
	int remote_port;
	char *local_interface;
	if(argc!=3&&argc!=4){
		printf("Error\n\t Usage for first proxy:\n\t\t"
			"cs352proxy <port> <local interface> \n\t"
			"Usage for second proxy: \n\t\t"
			"cs352proxy <remote host> <remote port> <local interface>\n");
	return -1;
	}

	switch(argc){
		// 1st proxy
		case 3:
			for(c=argv[1]; isdigit(*c)||*c=='\0'; c++);
			port=get_port(argv[1]);
			if((tp.ethfd=open_listenfd(port))<0){
				perror("error opening ethernet device\n");
				exit(1);
			}
			if((tp.tapfd=allocate_tunnel(argv[2], IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device\n");
				exit(1);
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
			break;

		// 2nd proxy
		case 4:
			for(c=argv[2]; isdigit(*c)||*c=='\0'; c++);
			if(*c=='\0'){
				tp.ethfd=atoi(argv[2]);
				if(tp.ethfd<1024||tp.ethfd>65535){
					perror("ERROR: port must be from "
						"1024-65535.\n");
					exit(1);
				}
				port=(unsigned short)tp.ethfd;
			}
			else{
				perror("ERROR: port parameter "
					"not a decimal number.\n");
				exit(1);
			}
			if((tp.ethfd=open_clientfd(argv[1], port))<0){
				perror("error opening ethernet device\n");
				exit(1);
			}
			if((tp.tapfd=allocate_tunnel(argv[3], IFF_TAP|IFF_NO_PI))<0){
				perror("error opening tap device\n");
				exit(1);
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
			break;
		default:
			perror("ERROR: invalid parameters.\n");
			exit(1);
	}
	return 0;
}
