/***********************************************************************
MessageBuffer - Class for buffers containing complete messages, to form
a queue at a sending socket or receive data from a datagram socket.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef MESSAGEBUFFER_INCLUDED
#define MESSAGEBUFFER_INCLUDED

#define MESSAGEBUFFER_USE_ALLOCATOR 0

#include <stddef.h>
#include <Threads/Atomic.h>

#if MESSAGEBUFFER_USE_ALLOCATOR
#include <Collaboration2/Allocator.h>
#else
#include <stdlib.h>
#endif

#include <Collaboration2/Protocol.h>

class MessageBuffer
	{
	/* Elements: */
	private:
	Threads::Atomic<unsigned int> refCount; // Number of objects referencing this message buffer; will be deleted when there are zero references
	// unsigned int refCount; // Number of objects referencing this message buffer; will be deleted when there are zero references
	unsigned int messageId; // ID of this message, replicated in network format at the beginning of actual buffer
	size_t bufferSize; // Size of the actual message buffer in bytes
	
	/* Constructors and destructors: */
	public:
	static MessageBuffer* create(size_t sBufferSize) // Creates a buffer for an ID-less message of the given size with a reference count of 1
		{
		/* Allocate a raw buffer holding a header and message body: */
		#if MESSAGEBUFFER_USE_ALLOCATOR
		void* buffer=Allocator::allocate(sizeof(MessageBuffer)+sBufferSize);
		#else
		void* buffer=malloc(sizeof(MessageBuffer)+sBufferSize);
		#endif
		
		/* Construct a message buffer header at the beginning of the buffer: */
		return new(buffer) MessageBuffer(~0x0U,sBufferSize);
		}
	static MessageBuffer* create(unsigned int sMessageId,size_t sBufferSize) // Creates a buffer for a message of the given ID and size, excluding the message ID, with a reference count of 1
		{
		/* Add room for the message ID to the buffer: */
		sBufferSize+=sizeof(MessageID);
		
		/* Allocate a raw buffer holding a header and message body: */
		#if MESSAGEBUFFER_USE_ALLOCATOR
		void* buffer=Allocator::allocate(sizeof(MessageBuffer)+sBufferSize);
		#else
		void* buffer=malloc(sizeof(MessageBuffer)+sBufferSize);
		#endif
		
		/* Construct a message buffer header at the beginning of the buffer: */
		return new(buffer) MessageBuffer(sMessageId,sBufferSize);
		}
	MessageBuffer(unsigned int sMessageId,size_t sBufferSize)
		:refCount(1),messageId(sMessageId),bufferSize(sBufferSize)
		{
		}
	
	/* Methods: */
	MessageBuffer* ref(void) // Increments the reference count and returns a pointer to the message buffer
		{
		/* Increment the reference counter: */
		// ++refCount;
		refCount.preAdd(1);
		return this;
		}
	MessageBuffer* unref(void) // Decrements the reference count and returns a pointer to the message buffer; destroys the message buffer and returns null if the count hits zero
		{
		if(refCount.preSub(1)==0)
		// if(--refCount==0)
			{
			#if MESSAGEBUFFER_USE_ALLOCATOR
			Allocator::release(this);
			#else
			free(this);
			#endif
			return 0;
			}
		else
			return this;
		}
	unsigned int getMessageId(void) const // Returns the message ID
		{
		return messageId;
		}
	const char* getBuffer(void) const // Returns the message buffer
		{
		return reinterpret_cast<const char*>(this+1);
		}
	char* getBuffer(void) // Ditto
		{
		return reinterpret_cast<char*>(this+1);
		}
	size_t getBufferSize(void) const // Returns the buffer size
		{
		return bufferSize;
		}
	void setMessageId(unsigned int newMessageId) // Sets the message ID in the message's header
		{
		messageId=newMessageId;
		}
	void setBufferSize(size_t newBufferSize) // Sets the buffer size
		{
		bufferSize=newBufferSize;
		}
	};

#endif
