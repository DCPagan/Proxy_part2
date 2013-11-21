typedef struct{
	char mac[6];
	unsigned short listen_port;
	unsigned int link_period;
	unsigned int link_timeout;
	unsigned int quit_timer;
	int tap;
} Config;

typedef struct{
	link_state ls;
	pthread_t tid;
	rio_t rio;
	pthread_mutex_t lock;
	UT_hash_handle hh;
} Peer;

/*creates a head node for each list and initialized it to null*/
/*Not needed for part 2*/
//List *ll_create();
//void ll_add(List list, List *node);
//void ll_remove(List list, List *node);

void remove_member(Peer *);
//void remove_expired_member(char* mac, List *node)nk;
void add_member(Peer *);
