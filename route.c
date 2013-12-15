/**
  *	WARNING: do not replace old membership list interface with this new
  *	one until all testing is finished; keep old code.
  */
#include"proxy.h"

graph *network=NULL;
ForwardingTable *table; 

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
	graph *node, *vtmp;
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
	HASH_ITER(hh, network, node, vtmp){
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
	graph *node, *tmpNode;
	edge *nbr, *tmp;
	Visited *visited = NULL, *tmpVisit;
	int distance = 0;
	writeBegin();
	HASH_FIND(hh, network, &linkState.tapMAC, ETH_ALEN, node);
	if(node==NULL)
		return;
	enqueue(q, node);
	//insert source node into visited
	tmpVisit = (Visited *)malloc(sizeof(Visited));
	tmpVisit->node = q->node;
	tmpVisit->prev = NULL;
	tmpVisit->dist = distance++;
	HASH_ADD(hh, visited, node, sizeof(graph), tmpVisit);
	while(q!=NULL){
		tmpNode = dequeue(q);
		//go through each node in graph until the dest node is found
		HASH_ITER(hh, tmpNode->nbrs, nbr, tmp){
			if(nbr->node == dest){
				/**
				  *	destination node found return shortest path
				  *	prepare the routing table here as a linked list
				  *
				  * Here is the code for a routing table
				  * If you want the fowarding table, break up the routing
				  *	table into individual pieces
				  * I do not know what you want to do with this
				  * I do not know why you want this global, since
				  *	fowarding tables are specific to a given route
				  * 
				  * This code gives you the destination part of the
				  *	fowarding table as a linked list
				  * The list starts at the source node and points to the
				  *	next hop in the route
				  * The table  contains a field for Destination, Distance
				  *	from the source, and Previous Hop, the source's
				  *	node's previous hop is NULL
				  */
				table = prepare_forwarding_table(visited,
					nbr->node, tmpNode, dest);
				/*
					TODO Make the next hop here.....with what func?
				*/
				writeEnd();
				return; 
			}
			//find out if this proxy has already been visited
			HASH_FIND(hh, visited, nbr->node, sizeof(graph), node);
			if(node == NULL){ //has not been visited yet
				// add new node to struct visited
				tmpVisit->node = node;
				tmpVisit->prev = nbr->node;
				tmpVisit->dist = distance++;
				HASH_ADD(hh, visited, node, ETH_ALEN, tmpVisit);
				// enqueue its neighbors
				HASH_ITER(hh, tmpVisit->node->nbrs, nbr, tmp){
					enqueue(q, nbr->node);
				}
			}
		}
	}
	writeEnd();
	printf("no such paths exists for given destination");
	return;
}

/*
This will be used instead of reverse path fowarding
Basically a copy of shortest_path()
Will return a hash table of proxies that our source can send packets to	
*/
Visited* bfs(){
	char buffer[ETH_FRAME_LEN+PROXY_HLEN];
	proxy_header prxyhdr;
	Queue *q;
	graph *node, *tmpNode;;
	edge *nbr, *tmp;
	Visited *visited = NULL, *tmpVisit;
	int distance = 0;
	writeBegin();
	HASH_FIND(hh, network, &linkState.tapMAC, ETH_ALEN, node);
	if(node == NULL){
		return NULL;
	}
	//insert source node into Visited
	tmpVisit = (Visited *)malloc(sizeof(Visited));
	tmpVisit->node = q->node;
	tmpVisit->prev = NULL;
	tmpVisit->dist = distance++;
	HASH_ADD(hh, visited, node, sizeof(graph), tmpVisit);
	enqueue(q, node);
	while(q!=NULL){
		tmpNode = dequeue(q);
		//go through each node in graph until the dest node is found
		HASH_ITER(hh, tmpNode->nbrs, nbr, tmp){
			HASH_FIND(hh, visited, nbr->node, ETH_ALEN, node); //find out if this proxy has already been visited
			if(node == NULL){ //has not been visited yet
				/*
					TODO Broadcast the packet here
				*/
				//add new node to struct visited
				tmpVisit->node = node;
				tmpVisit->prev = nbr->node;
				tmpVisit->dist = distance++;
				HASH_ADD(hh, visited, node, sizeof(graph), tmpVisit);
				// enqueue its neighbors
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
	LL_APPEND(q, newQ);
}

graph* dequeue(Queue *q){
	Queue *tmp, *returnTmp; //not sure if these are needed, what happens when head of list is deleted?
	tmp = q->next;
	returnTmp = q;
	LL_DELETE(q,q);
	q = tmp;
	return returnTmp->node;
}

// helper function to prepare the fowarding table
ForwardingTable* prepare_forwarding_table(
	Visited *visited, graph *curr, graph *previous, graph *destination){
	ForwardingTable *table, *tmpFT;
	Visited *v;
	tmpFT = (ForwardingTable *)malloc(sizeof(ForwardingTable));
	// initialize destination node
	tmpFT->node = curr;
	tmpFT->prevHop = NULL;
	tmpFT->dist = 0;
	tmpFT->destNode = destination;
	LL_PREPEND(table, tmpFT);
	HASH_FIND(hh, visited, previous, sizeof(graph), v);
	if(v == NULL){
		printf("error node not in visited table\n");
		return NULL;
	}
	while(v != NULL){
		tmpFT->node = v->node;
		tmpFT->prevHop = v->prev;
		tmpFT->dist = v->dist;
		// use prepend here so that list will be inorder from source
		LL_PREPEND(table, tmpFT);
		//find the next previous hop
		HASH_FIND(hh, visited, v->prev, sizeof(graph), v);
	}
	return table;
}

/**
  *	Link-state routing algorithm based on Dijkstra's algorithm.
  *	Write the next hop to a routing table, which will be consulted by
  *	any procedure that needs to forward packets to specific destinations.
  *
  *	UNUSED
  */
void Dijkstra(graph *dest){
	Heap *hp;
	graph *node, *vtmp;
	edge *nbr, *ntmp;
	heapent *ent;
	heapindex *hi;
	uint32_t i;
	/**
	  *	in Dijkstra's algorithm, each node in a graph has three values:
	  *	whether it is visited, its distance from the source, and its
	  *	previous hop in the shortest path. The distance is used as a key
	  *	for the heap.
	  */
	writeBegin();
	HASH_FIND(hh, network, &linkState.tapMAC, ETH_ALEN, node);
	if(node==NULL)
		return;
	hp=heap_alloc(HASH_CNT(hh, network));
	/**
	  *	Start with all nodes in the heap set to maximum distance,
	  *	unvisited and with no previous hop.
	  */
	HASH_ITER(hh, network, node, vtmp){
		i=heap_insert(hp, node, -1);
		hp->heap[i]->prev=NULL;
		hp->heap[i]->visited=0;
	}
	//	Find the source proxy
	HASH_FIND(hh, hp->index, &linkState.tapMAC, ETH_ALEN, hi);
	if(hi==NULL){
		heap_free(hp);
		return;
	}
	i=hi->index;
	//	Initialize it, and commence the minimum-priority BFS.
	hp->heap[i]->dist=0;
	hp->heap[i]->visited=1;
	//	swap the source node with the top of the heap.
	ent=hp->heap[0];
	hp->heap[0]=hp->heap[i];
	hp->heap[i]=ent;
	while(hp->size>0){
		ent=heap_delete(hp);
		//	Iterate through its neighbors
		HASH_ITER(hh, node->nbrs, node, vtmp){
		}
	}
	writeEnd();
	return;
	//return visited //if you want to see the entire dijkstra's table
	//visited has fields Distance from source, previous hop, and current node
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

uint32_t heap_insert(Heap *hp, graph *node, uint32_t dist){
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
