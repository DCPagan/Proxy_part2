/**
  *	WARNING: do not replace old membership list interface with this new
  *	one until all testing is finished; keep old code.
  */
#include"proxy.h"

graph *network=NULL;

int cmp(graph_llnode *x, graph_llnode *y){
	return memcmp(x->ls.tapMAC, y->tapMAC, ETH_ALEN);
}

void add_member_network(link_state_record *lsr){
	graph peer, *pp;
	graph_llnode *nbr;
	writeBegin();
	HASH_FIND(hh, graph, &lsr->proxy1.tapMAC, ETH_ALEN, pp);
	if(pp==NULL){
		pp=(graph *)malloc(sizeof(graph));
		pp->peer.ls=
	}else{
		LL_SEARCH(pp->nbrs, nbr, lsr->proxy2.tapMAC, cmp);
		if(nbrp==NULL){
			nbr=(graph_llnode *)malloc(sizeof(graph_llnode));
			nbr->ls=lsr->proxy2;
			LL_PREPEND(pp->nbrs, nbr);
		}
			nbr->linkWeight=lsr->linkWeight;
	}
	writeEnd();
}
