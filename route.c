/**
  *	WARNING: do not replace old membership list interface with this new
  *	one until all testing is finished; keep old code.
  */
#include"proxy.h"

graph *network=NULL;

void evaluate_record(link_state_record *lsr){
	graph *pp;
	edge *nbr;
	writeBegin();
	HASH_FIND(hh, network, &lsr->proxy1.tapMAC, ETH_ALEN, pp);
	if(pp==NULL){
		pp=(graph *)malloc(sizeof(graph));
		nbr=(edge *)malloc(sizeof(edge));
		pp->ls=lsr->proxy1;
		pp->nbrs=NULL;
		nbr->node->ls=lsr->proxy2;
		HASH_ADD(hh, network, ls.tapMAC, ETH_ALEN, pp);
		HASH_ADD(hh, pp->nbrs, node->ls.tapMAC, ETH_ALEN, nbr);
	}else{
		HASH_FIND(hh, pp->nbrs, &lsr->proxy2.tapMAC, ETH_ALEN, nbr);
		if(nbr==NULL){
			nbr=(edge *)malloc(sizeof(edge));
			nbr->node->ls=lsr->proxy2;
			HASH_ADD(hh, pp->nbrs, node->ls.tapMAC, ETH_ALEN, nbr);
		}
	}
	pp->timestamp.tv_sec=ntohl(lsr->ID.tv_sec);
	pp->timestamp.tv_nsec=ntohl(lsr->ID.tv_nsec);
	nbr->linkWeight=ntohl(lsr->linkWeight);
	writeEnd();
	return;
}

void remove_from_network(graph *pp){
	graph *node, *gtmp;
	edge *nbr, *etmp;
	writeBegin();
	HASH_FIND(hh, network, &pp->ls.tapMAC, ETH_ALEN, node);
	if(node==NULL)
		writeEnd();
		return;
	HASH_ITER(hh, node->nbrs, nbr, etmp){
		HASH_DELETE(hh, node->nbrs, nbr);
		free(nbr);
	}
	HASH_DELETE(hh, network, node);
	free(node);
	HASH_ITER(hh, network, node, gtmp){
		HASH_FIND(hh, node->nbrs, &pp->ls.tapMAC, ETH_ALEN, nbr);
		if(nbr!=NULL){
			HASH_DELETE(hh, node->nbrs, nbr);
			free(nbr);
		}
	}
	writeEnd();
	return;
}

void shortest_path(graph *dest){
	Queue *q;
	graph *node;
	edge *nbr, *tmp;
	Visited *visited = NULL;
	writeBegin();
	HASH_FIND(hh, network, &linkState.tapMAC, ETH_ALEN, node);
	if(node==NULL||node==dest)
		return;
	enqueue(q, node);
	while(q!=NULL){
		graph *tmpNode;
		tmpNode = dequeue(q);
		Visited *tmpVisit;
		tmpVisit = (Visited *)malloc(sizeof(Visited));
		tmpVisit->node = q->node;
		node = q->node;
		tmpVisit->prev = NULL;
		HASH_ADD(hh, visited, node, sizeof(Visited), tmpVisit);
		//go through each node in graph until the dest node is found
		HASH_ITER(hh, tmpNode->nbrs, nbr, tmp){
			if(nbr->node == dest){ //destination node found return shortest path
				//prepare the routing table here as linked list, note table does not contain source node.
				Queue *routing_table = prepare_routing_table(visited, nbr->node, tmpNode);
				/*
					Make the next hop here.....with what func?
				*/
				writeEnd();
				return; 
			}
			HASH_FIND(hh, visited, nbr->node, ETH_ALEN, node); //find out if this proxy has already been visited
			if(node == NULL){ //has not been visited add to struct visited and enqueue its neighbors
				tmpVisit->node = node;
				tmpVisit->prev = nbr->node;
				HASH_ADD(hh, visited, node, ETH_ALEN, tmpVisit);
				HASH_ITER(hh, tmpVisit->node->nbrs, nbr, tmp){
					enqueue(q, nbr->node);
				}
			}
		}
	}
	printf("no such paths exists for given destination");
}

/*
This will be used instead of reverse path fowarding
Basically a copy of shortest_path()
Will return a linked list of proxies that our source can send packets to	
*/
Visited* bfs(){
	char buffer[ETH_FRAME_LEN+PROXY_HLEN];
	proxy_header prxyhdr;
	Queue *q;
	graph *node;
	edge *nbr, *tmp;
	Visited *visited = NULL;
	writeBegin();
	HASH_FIND(hh, network, &linkState.tapMAC, ETH_ALEN, node);
	enqueue(q, node);
	while(q!=NULL){
		graph *tmpNode;
		tmpNode = dequeue(q);
		Visited *tmpVisit;
		tmpVisit = (Visited *)malloc(sizeof(Visited));
		tmpVisit->node = q->node;
		node = q->node;
		tmpVisit->prev = NULL;
		HASH_ADD(hh, visited, node, sizeof(Visited), tmpVisit);
		//go through each node in graph until the dest node is found
		HASH_ITER(hh, tmpNode->nbrs, nbr, tmp){
			HASH_FIND(hh, visited, nbr->node, ETH_ALEN, node); //find out if this proxy has already been visited
			if(node == NULL){ //has not been visited add to struct visited and enqueue its neighbors
				/*
					Broadcast the packet here
				*/
				tmpVisit->node = node;
				tmpVisit->prev = nbr->node;
				HASH_ADD(hh, visited, node, ETH_ALEN, tmpVisit);
				HASH_ITER(hh, tmpVisit->node->nbrs, nbr, tmp){
					enqueue(q, nbr->node);
				}
			}
		}
	}
	writeEnd();
	printf("done");
	return visited;
}

void enqueue(Queue *q, graph *peer){
	Queue *newQ;
	newQ=(Queue *)malloc(sizeof(Queue));
	newQ->node = peer;
	LL_PREPEND(q, newQ);
}

graph* dequeue(Queue *q){
	Queue *tmp, *returnTmp; //not sure if these are needed, what happens when head of list is deleted?
	tmp = q->next;
	returnTmp = q;
	LL_DELETE(q,q);
	q = tmp;
	return returnTmp->node;
}

Queue* prepare_routing_table(Visited *visited, graph *curr, graph *previous){
	Queue *routing_table, *tmpQ;
	Visited *v;
	tmpQ = (Queue *)malloc(sizeof(Queue));
	tmpQ->node = curr;
	LL_PREPEND(routing_table, tmpQ);
	HASH_FIND(hh, visited, previous, ETH_ALEN, v);
	if(v == NULL){
		printf("error node not in visited table\n");
	}
	while(v->prev != NULL){
		tmpQ->node = v->node;
		LL_PREPEND(routing_table, tmpQ);
		Visited *anotherTmp;
		anotherTmp->node = v->prev;
		HASH_FIND(hh, visited, anotherTmp->node, ETH_ALEN, v);
		if(v == NULL){
			printf("error node not in visited\n");
		}
	}
	return routing_table;
}

/**
  *	Link-state routing algorithm based on Dijkstra's algorithm.
  *	Write the next hop to a routing table, which will be consulted by
  *	any procedure that needs to forward packets to specific destinations.
  */
void Dijkstra(graph *dest){
	Heap *hp;
	graph *node;
	edge *nbr, *tmp;
	writeBegin();
	HASH_FIND(hh, network, &linkState.tapMAC, ETH_ALEN, node);
	if(node==NULL||node==dest)
		return;
	hp=heap_alloc(HASH_CNT(hh, network));
	//	Insert the neighbors of the local proxy to the heap.
	HASH_ITER(hh, node->nbrs, nbr, tmp){
		heap_insert(hp, nbr->node, nbr->linkWeight);
	}
	/**
	  *	Iterate through the heap until there are no entries left.
	  *
	  *	Define a data structure that defines whether a node in the
	  *	network is visited by Dijkstra's algorithm.
	  *
	  *	Associate each node with the previous hop from the local proxy
	  *	to the destination. Use this for the routing table.
	  */
	while(hp->size>0){
	}
	writeEnd();
	heap_free(hp);
	return;
}

Heap *heap_alloc(uint32_t max){
	Heap *hp;
	hp=(Heap *)malloc(sizeof(Heap));
	hp->size=0;
	hp->max=max;
	hp->heap=(heapent **)calloc(max, sizeof(heapent *));
	hp->index=NULL;
	return hp;
}

void heap_free(Heap *hp){
	free(hp->heap);
	free(hp);
	return;
}

int heap_insert(Heap *hp, graph *node, uint32_t dist){
	heapent *ent;
	if(hp->size>=hp->max)
		return -1;
	ent=(heapent *)malloc(sizeof(heapent));
	ent->node=node;
	ent->dist=dist;
	hp->heap[hp->size]=ent;
	upheap(hp, hp->size);
	hp->size++;
	return 0;
}

heapent *heap_delete(Heap *hp){
	heapent *ent;
	if(hp->size<=0)
		return NULL;
	--hp->size;
	ent=hp->heap[0];
	hp->heap[0]=hp->heap[hp->size];
	downheap(hp);
	return ent;
}

void upheap(Heap *hp, uint32_t index){
	uint32_t parent;
	heapent *ent;
	heapindex *hip;
	for(parent=(index-1)/2;
		hp->heap[index]->dist<hp->heap[parent]->dist;
		index=parent, parent=(parent-1)/2){
		ent=hp->heap[index];
		hp->heap[index]=hp->heap[parent];
		hp->heap[parent]=ent;
		//	Update the index of the entry that bubbled down.
		HASH_FIND(hh, hp->index, &hp->heap[parent]->node->ls.tapMAC,
			ETH_ALEN, hip);
		if(hip!=NULL)
			hip->index=parent;
	}
	//	Update the index of the entry that bubbled up.
	HASH_FIND(hh, hp->index, &hp->heap[index]->node->ls.tapMAC,
		ETH_ALEN, hip);
	if(hip!=NULL)
		hip->index=index;
	return;
} 

void downheap(Heap *hp){
	uint32_t index, left, right;
	heapent *ent;
	heapindex *hip;
	index=0;
	left=1;
	right=2;
	while(left<hp->size&&hp->heap[index]->dist>hp->heap[left]->dist||
		right<hp->size&&hp->heap[index]->dist>hp->heap[right]->dist){
		left=left<right?left:right;
		ent=hp->heap[index];
		hp->heap[index]=hp->heap[left];
		hp->heap[left]=ent;
		//	Update the index of the entry that bubbled up.
		HASH_FIND(hh, hp->index, &hp->heap[left]->node->ls.tapMAC,
			ETH_ALEN, hip);
		if(hip!=NULL)
			hip->index=left;
		index=left;
		left=2*index+1;
		right=2*index+2;
	}
	//	Update the index of the entry that bubbled down.
	HASH_FIND(hh, hp->index, &hp->heap[index]->node->ls.tapMAC,
		ETH_ALEN, hip);
	if(hip!=NULL)
		hip->index=index;
	return;
}
