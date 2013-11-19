q
#include "proxy.h"
#include "link_state.h"

List *hash_table = NULL;

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

void remove_member(char* mac, List *node){
	List *tmp;
	HASH_FIND_STR(hash_table, mac, tmp);
	HASH_DEL(hash_table, tmp);
}

void add_member(char* mac, List *node){
	List *tmp;
	HASH_FIND_STR( hash_table, mac, tmp);
	if(tmp == NULL){
		HASH_ADD_STR(hash_table, mac, node);
	}
}
