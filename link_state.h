#include "proxy.h"

typedef struct Config{
	char mac[6];
	int listen_port;
	int link_period;
	int link_timeout;
	int tap;
	int quit_timer;
} Config;

typedef struct Peer{
	link_state ls;
	pthread_mutex_t *lock;
	UT_hash_handle hh;
} Peer;

/*creates a head node for each list and initialized it to null*/
/*Not needed for part 2*/
//List *ll_create();
//void ll_add(List list, List *node);
//void ll_remove(List list, List *node);

void remove_member(char* Mac, Peer *node);
//void remove_expired_member(char* mac, List *node)nk;
void add_member(char* Mac, Peer *node);
