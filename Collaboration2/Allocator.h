/***********************************************************************
Allocator - Special-purpose allocator for medium-sized memory blocks
that are allocated and then released after a short time.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef ALLOCATOR_INCLUDED
#define ALLOCATOR_INCLUDED

#define ALLOCATOR_USE_SPINLOCK 1

#include <stddef.h>
#include <stdlib.h>
#if ALLOCATOR_USE_SPINLOCK
#include <Threads/Spinlock.h>
#else
#include <Threads/Atomic.h>
#endif

class Allocator
	{
	/* Embedded classes: */
	private:
	struct BlockHeader // Structure at the beginning of used or unused memory blocks
		{
		/* Elements: */
		union
			{
			BlockHeader* succ; // Pointer to next free block if the block is unused
			size_t poolIndex; // Index of the block pool whence a used block came
			};
		};
	
	/* Elements: */
	private:
	static Allocator theAllocator; // Static instance of the allocator class
	size_t granularity; // Granularity of memory block sizes
	#if ALLOCATOR_USE_SPINLOCK
	Threads::Spinlock poolMutex; // Mutex serializing access to the block pool
	#else
	Threads::Atomic<bool> poolLock; // Atomic counter for DIY spin locking
	#endif
	size_t numPools; // Number of block pools currently allocated
	BlockHeader** pools; // List of block pools, each containing a stack of unused memory blocks
	
	/* Private methods: */
	void growPool(size_t newNumPools); // Grows the pool to the given new size
	
	/* Constructors and destructors: */
	public:
	Allocator(size_t sGranularity =128); // Creates an empty allocator with the given granularity
	~Allocator(void); // Destroys the allocator and releases all unused memory
	
	/* Methods: */
	static void* allocate(size_t size) // Returns a memory block of the requested size
		{
		#if ALLOCATOR_USE_SPINLOCK
		Threads::Spinlock::Lock poolLock(theAllocator.poolMutex);
		#else
		/* Enter critical section: */
		while(theAllocator.poolLock.compareAndSwap(false,true))
			;
		#endif
		
		/* Find the index of the pool containing blocks of the requested size: */
		size_t poolIndex=(size+sizeof(BlockHeader))/theAllocator.granularity; // Add room for a size marker (yes, I know malloc has it, but I need it)
		
		/* Grow the pool if necessary: */
		if(poolIndex>=theAllocator.numPools)
			theAllocator.growPool(poolIndex+1);
		
		/* Check if there is a block of the requested size already available: */
		BlockHeader* block;
		if(theAllocator.pools[poolIndex]!=0)
			{
			/* Remove the unused block from the pool and return it: */
			block=theAllocator.pools[poolIndex];
			theAllocator.pools[poolIndex]=theAllocator.pools[poolIndex]->succ;
			}
		else
			{
			/* Allocate and return a new block: */
			block=static_cast<BlockHeader*>(malloc((poolIndex+1)*theAllocator.granularity));
			}
		
		#if !ALLOCATOR_USE_SPINLOCK
		/* Leave critical section: */
		theAllocator.poolLock.compareAndSwap(true,false);
		#endif
		
		/* Remember the pool index whence the block came and return it: */
		block->poolIndex=poolIndex;
		return block+1;
		}
	static void release(void* block) // Puts the given memory block back into the pool for re-use
		{
		#if ALLOCATOR_USE_SPINLOCK
		Threads::Spinlock::Lock poolLock(theAllocator.poolMutex);
		#else
		/* Enter critical section: */
		while(theAllocator.poolLock.compareAndSwap(false,true))
			;
		#endif
		
		/* Access the block's header: */
		BlockHeader* blockHeader=static_cast<BlockHeader*>(block)-1;
		
		/* Put the block back into the pool: */
		size_t poolIndex=blockHeader->poolIndex;
		blockHeader->succ=theAllocator.pools[poolIndex];
		theAllocator.pools[poolIndex]=blockHeader;
		
		#if !ALLOCATOR_USE_SPINLOCK
		/* Leave critical section: */
		theAllocator.poolLock.compareAndSwap(true,false);
		#endif
		}
	};

#endif
