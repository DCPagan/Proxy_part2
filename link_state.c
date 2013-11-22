#include "proxy.h"

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

void remove_member(Peer *node){
	Peer *tmp;
	/**
	  *	This is a write operation on shared data; to solve the
	  *	readers/writers problem, use mutual exclusion to privilege
	  *	the writer by granting exclusive access to the hash_table.
	  */
	HASH_FIND(hh, hash_table, &node->ls.MAC, ETH_ALEN ,tmp);
	if(tmp != NULL){
		pthread_mutex_lock(&node->lock);
		HASH_DEL(hash_table, ls.MAC);
	/**
	  *	Upon removing a peer from the membership list, terminate the
	  *	thread associated with the connection, close its file descriptor,
	  *	and free its memory.
	  */
		pthread_mutex_unlock(&node->lock);
		pthread_mutex_destroy(&node->lock);
	}
	pthread_cancel(node->tid);
	close(node->rio.fd);
	free(node);
	return;
}

/**
  *	Lock memory accordingly in this procedure as well.
  */
void add_member(Peer *node){
	Peer *tmp;
	pthread_mutex_lock(&node->lock);
	HASH_FIND(hh, hash_table, &node->ls.MAC, ETH_ALEN, tmp);
	if(tmp == NULL){
		HASH_ADD(hh, hash_table, ls.MAC, ETH_ALEN,node);
	}
	pthread_mutex_unlock(&node->lock);
}
