#include <pthread.h>
#include "utlist.h"
#include "uthash.h"

typedef struct Config{
	int listen_port;
	int link_period;
	int link_timeout;
	char* tap;
	int quit_timer;
} Config;

typedef struct List{
	char* hostname;
	char* Mac;
	Config config;
	int port;
	List *next;
	UT_hash_handle hh;
} List;

typedef struct Graph{
	List *list;
	pthread_mutex_t lock;
} Graph;

void remove_member(char* mac, List node);
//void remove_expired_member(char* mac, List node);
void add_member(char* mac, List node);
