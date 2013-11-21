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

void remove_member(Peer *node){
	Peer *tmp;
	/**
	  *	This is a write operation on shared data; to solve the
	  *	readers/writers problem, use mutual exclusion to privilege
	  *	the writer by granting exclusive access to the hash_table.
	  */
	/**
	  * Lock here. I don't know how to use MUTEX's yet.
	  *	I don't know how to use MUTEX's yet, so please do this for me,
	  *	John. Delete these last two lines of comments for me as well.
	  */
	HASH_FIND(hash_table, &node->ls.MAC, tmp);
	if(tmp != NULL){
		HASH_DEL(hash_table, ls.MAC, tmp);
	}
	/**
	  *	Upon removing a peer from the membership list, terminate the
	  *	thread associated with the connection, close its file descriptor,
	  *	and free its memory.
	  */
	pthread_cancel(pp->tid);
	close(pp->rio.fd);
	free(pp);
	//	Unlock here.
	return;
}

void add_member(Peer *node){
	Peer *tmp;
	HASH_FIND(hash_table, &node->ls.MAC, tmp);
	if(tmp == NULL){
		HASH_ADD(hash_table, ls.MAC, node);
	}
}
