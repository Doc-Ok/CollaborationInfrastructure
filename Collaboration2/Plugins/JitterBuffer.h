/***********************************************************************
JitterBuffer - Class to sort a stream of incoming audio packets
suffering duplication, drop, and out-of-order delivery into a decoding
queue.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef JITTERBUFFER_INCLUDED
#define JITTERBUFFER_INCLUDED

#include <Misc/SizedTypes.h>

#include <Collaboration2/MessageBuffer.h>

// DEBUGGING
#include <iostream>

class JitterBuffer
	{
	/* Embedded classes: */
	public:
	typedef Misc::SInt16 Sequence; // Type for packet sequence numbers; must be signed for simpler computation
	typedef MessageBuffer* Entry; // Type for buffer entries
	
	/* Elements: */
	private:
	int numSlots; // Maximum number of packets in the buffer at any given time
	Entry* slots; // Array of buffer slots
	int headIndex; // Slot index of the current buffer head
	Sequence headSequence; // Sequence number at the head of the buffer
	
	/* Housekeeping counters for debugging: */
	size_t numQueueds; // Total number of packets that have been enqueued
	size_t numDuplicates; // Number of duplicate packets
	size_t numTooLates; // Number of packets that were not entered into the buffer for arriving too late
	size_t numTooEarlies; // Number of packets that were not entered into the buffer for arriving too early
	size_t numAbsents; // Number of absent buffer slots that were dequeued
	
	/* Constructors and destructors: */
	public:
	JitterBuffer(int sNumSlots) // Creates an empty jitter buffer of the given size
		:numSlots(sNumSlots),slots(new Entry[numSlots]),
		 headIndex(0),headSequence(0),
		 numQueueds(0),numDuplicates(0),numTooLates(0),numTooEarlies(0),numAbsents(0)
		{
		/* Initialize all buffer slots to "absent:" */
		for(int i=0;i<numSlots;++i)
			slots[i]=Entry(0);
		}
	private:
	JitterBuffer(const JitterBuffer& source); // Prohibit copy constructor
	JitterBuffer& operator=(const JitterBuffer& source); // Prohibit assignment operator
	public:
	~JitterBuffer(void) // Destroys the jitter buffer
		{
		/* Destroy all present buffer entries: */
		for(int i=0;i<numSlots;++i)
			if(slots[i]!=0)
				slots[i]->unref();
		
		/* Destroy the buffer: */
		delete[] slots;
		
		/* Print buffer stats: */
		// DEBUGGING
		std::cout<<"JitterBuffer: "<<numQueueds<<" packets queued"<<std::endl;
		std::cout<<"JitterBuffer: "<<double(numDuplicates)*100.0/double(numQueueds+numAbsents)<<"% duplicates"<<std::endl;
		std::cout<<"JitterBuffer: "<<double(numTooLates)*100.0/double(numQueueds+numAbsents)<<"% too late"<<std::endl;
		std::cout<<"JitterBuffer: "<<double(numTooEarlies)*100.0/double(numQueueds+numAbsents)<<"% too early"<<std::endl;
		std::cout<<"JitterBuffer: "<<double(numAbsents)*100.0/double(numQueueds+numAbsents)<<"% absent"<<std::endl;
		}
	
	/* Methods: */
	int getNumSlots(void) const // Returns the number of slots in the jitter buffer
		{
		return numSlots;
		}
	JitterBuffer& setNumSlots(int newNumSlots,bool clearBuffer =false) // Resizes the jitter buffer; discards newer entries if size will become too small or discards all entries if clearBuffer is true
		{
		/* Allocate the new buffer slot array and copy entries from the old array starting with the oldest: */
		Entry* newSlots=new Entry[newNumSlots];
		int numCopySlots=clearBuffer?0:Misc::min(numSlots,newNumSlots);
		for(int offset=0;offset<numCopySlots;++offset)
			newSlots[offset]=slots[(headIndex+offset)%numSlots];
		
		/* Clear new buffer entries: */
		for(int offset=numCopySlots;offset<newNumSlots;++offset)
			newSlots[offset]=Entry(0);
		
		/* Discard remaining buffer entries: */
		for(int offset=numCopySlots;offset<numSlots;++offset)
			if(slots[(headIndex+offset)%numSlots]!=Entry(0))
				{
				++numTooEarlies;
				slots[(headIndex+offset)%numSlots]->unref();
				}
		
		/* Install the new buffer slot array: */
		delete[] slots;
		numSlots=newNumSlots;
		slots=newSlots;
		headIndex=0;
		
		return *this;
		}
	JitterBuffer& init(Sequence firstSequence,Entry firstEntry) // Enqueues the first entry with the given sequence number; takes over caller's message buffer reference
		{
		/* Destroy all present buffer entries: */
		for(int i=0;i<numSlots;++i)
			if(slots[i]!=0)
				{
				slots[i]->unref();
				slots[i]=0;
				}
		
		/* Restart the buffer by inserting the first entry at the beginning: */
		++numQueueds;
		headIndex=0;
		headSequence=firstSequence;
		slots[0]=firstEntry;
		
		return *this;
		}
	JitterBuffer& enqueue(Sequence newSequence,Entry newEntry) // Enqueues the new entry with the given sequence number; takes over caller's message buffer reference
		{
		++numQueueds;
		
		/* Calculate the new entry's offset to the head of the buffer: */
		int offset=int(Sequence(newSequence-headSequence));
		
		/* Discard the new entry if it is too old or too new: */
		if(offset<0)
			{
			++numTooLates;
			newEntry->unref();
			return *this;
			}
		else if(offset>=numSlots)
			{
			++numTooEarlies;
			newEntry->unref();
			return *this;
			}
		
		/* Calculate the new entry's buffer slot: */
		int index=(headIndex+offset)%numSlots;
		
		/* Check if the slot is empty: */
		if(slots[index]==Entry(0))
			{
			/* Fill the slot: */
			slots[index]=newEntry;
			}
		else
			{
			/* It's a duplicate; discard the new entry: */
			++numDuplicates;
			newEntry->unref();
			}
		
		return *this;
		}
	Sequence getHeadSequence(void) const // Returns the sequence number of the buffer entry to be dequeued next, even if it is absent
		{
		return headSequence;
		}
	Entry dequeue(void) // Dequeues the next due buffer entry; returns null if the entry is absent
		{
		/* Return the current buffer head, absent or not: */
		Entry result=slots[headIndex];
		
		/* Dequeue the buffer head: */
		slots[headIndex]=0;
		headIndex=(headIndex+1)%numSlots;
		++headSequence;
		
		if(result==0)
			++numAbsents;
		return result;
		}
	};

#endif
