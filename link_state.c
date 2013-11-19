#include "proxy.h"
#include "link_state.h"

List *hash_table = NULL;

void remove_member(char* mac, List node){
	List *tmp;
	HASH_FIND_STR(hash_table, mac, tmp);
	HASH_DEL(hash_table, tmp);
}

void add_member(char* mac, List node){
	List *tmp;
	HASH_FIND_STR( hash_table, mac, tmp);
	if(tmp == NULL){
		HASH_ADD_STR(hash_table, mac, node);
	}
}
