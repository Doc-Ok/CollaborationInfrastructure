/***********************************************************************
Allocator - Special-purpose allocator for medium-sized memory blocks
that are allocated and then released after a short time.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Allocator.h>

#include <string.h>

/**********************************
Static elements of class Allocator:
**********************************/

Allocator Allocator::theAllocator;

/**************************
Methods of class Allocator:
**************************/

void Allocator::growPool(size_t newNumPools)
	{
	/* Create a larger pool list: */
	BlockHeader** newPools=new BlockHeader*[newNumPools];
	
	/* Copy over the old pool list: */
	memcpy(newPools,pools,numPools*sizeof(BlockHeader*));
	
	/* Initialize the new pools to empty: */
	memset(newPools+numPools*sizeof(BlockHeader*),0,(newNumPools-numPools)*sizeof(BlockHeader*));
	
	/* Replace the old pool list: */
	numPools=newNumPools;
	delete[] pools;
	pools=newPools;
	}

Allocator::Allocator(size_t sGranularity)
	:granularity(sGranularity),
	 #if !ALLOCATOR_USE_SPINLOCK
	 poolLock(0),
	 #endif
	 numPools(0),pools(0)
	{
	}

Allocator::~Allocator(void)
	{
	/* Release all unused memory blocks: */
	BlockHeader** pEnd=pools+numPools;
	for(BlockHeader** pPtr=pools;pPtr!=pEnd;++pPtr)
		{
		BlockHeader* head=*pPtr;
		while(head!=0)
			{
			BlockHeader* succ=head->succ;
			free(head);
			head=succ;
			}
		}
	
	/* Destroy the pool list: */
	delete[] pools;
	}
