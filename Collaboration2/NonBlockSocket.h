/***********************************************************************
NonBlockSocket - Class representing a socket for non-blocking I/O.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef NONBLOCKSOCKET_INCLUDED
#define NONBLOCKSOCKET_INCLUDED

#include <stddef.h>
#include <string.h>
#include <Misc/Endianness.h>
#include <Misc/RingBuffer.h>
#include <Comm/IPSocketAddress.h>

/* Forward declarations: */
namespace Comm {
class ListeningTCPSocket;
}
class MessageBuffer;

class NonBlockSocket
	{
	/* Embedded classes: */
	private:
	typedef Misc::RingBuffer<MessageBuffer*> SendQueue; // Type for queues of messages waiting to be sent
	
	/* Elements: */
	private:
	int fd; // Socket file descriptor
	Comm::IPSocketAddress peerAddress; // IP address and TCP port of the connected socket
	bool peerClosed; // Flag whether the peer closed the connection
	
	/* Reading interface state: */
	bool swapOnRead; // Flag if binary data from the other end must be endianness-swapped
	char* readBuffer; // A ring buffer to read data from the socket
	char* readBufferEnd; // Pointer to the end of the ring buffer
	char* writePtr; // Position at which new data from the socket will be written into the buffer
	char* readPtr; // Position from which data will be read from the buffer
	size_t unread; // Amount of unread data currently in the buffer
	
	/* Writing interface state: */
	SendQueue sendQueue; // Queue of messages waiting to be sent
	size_t sendQueueSize; // Total size of all messages currently in the send queue
	size_t sent; // Amount of already-sent data from the first message in the queue
	
	/* Private methods: */
	void init(size_t readBufferSize); // Initializes the socket after connect or accept
	
	/* Constructors and destructors: */
	public:
	NonBlockSocket(void); // Creates an unconnected TCP socket
	NonBlockSocket(Comm::ListeningTCPSocket& listenSocket,size_t readBufferSize =8192); // Creates a TCP socket by accepting the next pending connection request on the given listening socket
	NonBlockSocket(const char* peerHostName,int peerPortId,size_t readBufferSize =8192); // Creates a TCP socket by connecting to the given IP address and port number
	~NonBlockSocket(void); // Closes the socket
	
	/* Methods: */
	bool isConnected(void) const // Returns true if the socket is connected to a peer
		{
		return fd>=0;
		}
	void accept(Comm::ListeningTCPSocket& listenSocket,size_t readBufferSize =8192); // Creates a TCP socket by accepting the next pending connection request on the given listening socket
	void connect(const char* peerHostName,int peerPortId,size_t readBufferSize =8192); // Creates a TCP socket by connecting to the given IP address and port number
	int getFd(void) const // Returns the socket's file descriptor
		{
		return fd;
		}
	const Comm::IPSocketAddress& getPeerAddress(void) const // Returns the peer's socket address
		{
		return peerAddress;
		}
	void shutdown(bool read,bool write); // Shuts down one or both directions of the connection in preparation for a close
	
	/* Read methods: */
	size_t readFromSocket(void); // Reads more data into the buffer; returns the new total amount of unread data in the buffer
	void readRaw(void* destPtr,size_t destSize) // Reads the given number of bytes into the given destination
		{
		/* Check if the read straddles the read buffer end: */
		size_t endSpace=size_t(readBufferEnd-readPtr);
		if(destSize>endSpace)
			{
			/* Copy from the read pointer to the end of the read buffer first: */
			memcpy(destPtr,readPtr,endSpace);
			destPtr=reinterpret_cast<char*>(destPtr)+endSpace;
			destSize-=endSpace;
			
			/* Wrap around the read pointer: */
			readPtr=readBuffer;
			unread-=endSpace;
			}
		
		/* Copy from the read pointer: */
		memcpy(destPtr,readPtr,destSize);
		readPtr+=destSize;
		if(readPtr==readBufferEnd)
			readPtr=readBuffer;
		unread-=destSize;
		}
	bool eof(void) const // Returns true if the peer closed the connection and there is no more unread data
		{
		return peerClosed&&unread==0;
		}
	
	/* Type- and endianness-safe read methods: */
	bool getSwapOnRead(void) const // Returns true if data needs to be endianness-swapped when being read from this socket
		{
		return swapOnRead;
		}
	void setSwapOnRead(bool newSwapOnRead); // Sets the endianness swapping flag
	size_t getUnread(void) const // Returns the amount of data that can be read without blocking
		{
		return unread;
		}
	template <class DataParam>
	DataParam read(void) // Reads a single value of the given data type; assumes there is enough data in the buffer
		{
		DataParam result;
		readRaw(&result,sizeof(DataParam));
		if(swapOnRead)
			Misc::swapEndianness(result);
		return result;
		}
	template <class DataParam>
	DataParam& read(DataParam& data) // Ditto
		{
		readRaw(&data,sizeof(DataParam));
		if(swapOnRead)
			Misc::swapEndianness(data);
		return data;
		}
	template <class DataParam>
	void read(DataParam* items,size_t numItems) // Reads an array of values of the given data type; assumes there is enough data in the buffer
		{
		readRaw(items,numItems*sizeof(DataParam));
		if(swapOnRead)
			Misc::swapEndianness(items,numItems);
		}
	
	/* Write methods: */
	size_t writeToSocket(void); // Writes data from the current buffer to the socket; returns the new total amount of unsent data
	size_t getUnsent(void) const // Returns the total amount of data that still needs to be written
		{
		return sendQueueSize-sent;
		}
	size_t queueMessage(MessageBuffer* message); // Queues the given message for sending; returns the previous total amount of unsent data
	};

#endif
