/**
  *	WARNING: do not replace old membership list interface with this new
  *	one until all testing is finished; keep old code.
  */
#include"proxy.h"

graph *network=NULL;

int cmp(graph_llnode *x, graph_llnode *y){
	return memcmp(x->ls.tapMAC, y->tapMAC, ETH_ALEN);
}

void evaluate_record(link_state_record *lsr){
	graph peer, *pp;
	graph_llnode *nbr;
	writeBegin();
	HASH_FIND(hh, graph, &lsr->proxy1.tapMAC, ETH_ALEN, pp);
	if(pp==NULL){
		pp=(graph *)malloc(sizeof(graph));
		nbr=(edge *)malloc(sizeof(edge));
		pp->ls=lsr->proxy1;
		pp->nbrs=NULL;
		nbr->ls=lsr->proxy2;
		HASH_ADD(hh, network, ls.tapMAC, ETH_ALEN, pp);
		HASH_ADD(hh, pp->nbrs, ls.tapMAC, ETH_ALEN, nbr);
	}else{
		HASH_FIND(hh, pp->nbrs, &lsr->proxy2.tapMAC, ETH_ALEN, nbr);
		if(nbr==NULL){
			nbr=(graph_llnode *)malloc(sizeof(graph_llnode));
			nbr->ls=lsr->proxy2;
			HASH_ADD(hh, pp->nbrs, ls.tapMAC, ETH_ALEN, nbr);
		}
	}
	pp->timestamp.tv_sec=ntohl(lsr->ID.tv_sec);
	pp->timestamp.tv_nsec=ntohl(lsr->ID.tv_nsec);
	nbr->linkWeight=ntohl(lsr->linkWeight);
	writeEnd();
	return;
}

void remove_from_network(Peer *pp){
	graph *node, *gtmp;
	edge *nbr, *etmp;
	writeBegin();
	HASH_FIND(hh, network, ls.tapMAC, ETH_ALEN, node);
	if(node==NULL)
		return;
	HASH_ITER(hh, node->nbrs, nbr, etmp){
		HASH_DELETE(hh, node->nbrs, nbr);
		free(nbr);
	}
	HASH_DELETE(hh, network, node);
	free(node);
	HASH_ITER(hh, network, node, gtmp){
		HASH_FIND(hh, node->nbr, ls.tapMAC, ETH_ALEN, nbr);
		if(nbr!=NULL){
			HASH_DELETE(hh, node->nbrs, nbr);
			free(nbr);
		}
	}
	writeEnd();
	return;
}
