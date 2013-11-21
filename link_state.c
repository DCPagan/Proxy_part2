#include "link_state.h"

Peer *hash_table = NULL;
/*
 *These functions not needed for part 2
 *
List *ll_create(){
	List *head = malloc(sizeof(*head));
	head = NULL;
	return head;
}

void ll_add(List *list, List *node){
	LL_APPEND(list, node);
}

void ll_remove(List *list, List *node){
	LL_DELETE(list, node);
}
*/

void remove_member(char* Mac,Peer *node){
	Peer *tmp;
	HASH_FIND_STR(hash_table, &Mac, tmp);
	if(tmp != NULL){
		HASH_DEL(hash_table, tmp);
	}
}

void add_member(char* Mac, Peer *node){
	Peer *tmp;
	HASH_FIND_STR( hash_table, &Mac, tmp);
	if(tmp == NULL){
		HASH_ADD_STR(hash_table, mac, node);
	}
}
