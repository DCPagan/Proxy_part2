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
	Config config;
	int port;
	List *next;
} List;

typedef struct Graph{
	char *Mac;
	List *list;
	UT_hash_handle hh;
	pthread_mutex_t lock;
} Graph;

void remove_member(Graph graph, List node);
void remove_expired_member(Graph graph, List node);
void add_member(Graph graph, List node);
