/***********************************************************************
UDPSocket - Class to send and receive packets over a UDP socket in non-
blocking mode.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef UDPSOCKET_INCLUDED
#define UDPSOCKET_INCLUDED

#include <stddef.h>
#include <Misc/RingBuffer.h>
#include <Comm/IPv4SocketAddress.h>

/* Forward declarations: */
class MessageBuffer;

class UDPSocket
	{
	/* Embedded classes: */
	public:
	typedef Comm::IPv4SocketAddress Address; // Type for sender and receiver addresses
	
	struct SendQueueEntry // Structure for entries in the send queue
		{
		/* Elements: */
		public:
		Address receiverAddress; // Socket address to which to send the message
		MessageBuffer* message; // Message to send to the receiver address
		};
	
	typedef Misc::RingBuffer<SendQueueEntry> SendQueue; // Type for queues of messages waiting to be sent
	
	/* Elements: */
	private:
	int fd; // Socket file descriptor
	
	/* Writing interface state: */
	SendQueue sendQueue; // Queue of messages waiting to be sent
	size_t sendQueueSize; // Total size of all messages currently in the send queue
	
	/* Constructors and destructors: */
	public:
	UDPSocket(int portId); // Opens a UDP socket on the given UDP port
	~UDPSocket(void); // Closes the UDP socket
	
	/* Methods: */
	int getFd(void) const // Returns the UDP socket's file descriptor
		{
		return fd;
		}
	
	/* Read methods: */
	MessageBuffer* readFromSocket(Address& senderAddress); // Returns the next pending message on the socket's input buffer and fills in sender address; returns null if there is no pending message; otherwise result will have ref count of 1
	
	/* Write methods: */
	size_t writeToSocket(void); // Writes data from the send queue to the socket; returns the new total amount of unsent data
	size_t getUnsent(void) const // Returns the total amount of data that still needs to be written
		{
		return sendQueueSize;
		}
	size_t queueMessage(const Address& receiverAddress,MessageBuffer* message); // Queues the given message for the given receiver address for sending; returns the previous total amount of unsent data
	};

#endif
