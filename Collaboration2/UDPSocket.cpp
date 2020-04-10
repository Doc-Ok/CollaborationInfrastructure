/***********************************************************************
UDPSocket - Class to send and receive packets over a UDP socket in non-
blocking mode.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/UDPSocket.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdexcept>
#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>

#include <Collaboration2/MessageBuffer.h>

/**************************
Methods of class UDPSocket:
**************************/

UDPSocket::UDPSocket(int portId)
	:fd(-1),
	 sendQueue(4),sendQueueSize(0)
	{
	/* Create a datagram socket for the IPv4 domain: */
	fd=socket(AF_INET,SOCK_DGRAM,0);
	if(fd<0)
		Misc::throwStdErr("UDPSocket::UDPSocket: Unable to create socket due to error %d (%s)",errno,strerror(errno));
	
	/* Set the socket to non-blocking mode: */
	int flags=fcntl(fd,F_GETFL,0);
	bool ok=flags>=0;
	if(ok)
		ok=fcntl(fd,F_SETFL,flags|O_NONBLOCK)>=0;
	if(!ok)
		{
		/* Close the socket again: */
		int error=errno;
		close(fd);
		Misc::throwStdErr("UDPSocket::UDPSocket: Unable to set socket to non-blocking mode due to error %d (%s)",error,strerror(error));
		}
	
	/* Bind the socket to the given port ID: */
	Comm::IPv4SocketAddress socketAddress(portId);
	if(bind(fd,(struct sockaddr*)&socketAddress,sizeof(struct Comm::IPv4SocketAddress))<0)
		{
		/* Close the socket again: */
		int error=errno;
		close(fd);
		Misc::throwStdErr("UDPSocket::UDPSocket: Unable to bind socket to port %d due to error %d (%s)",portId,error,strerror(error));
		}
	}

UDPSocket::~UDPSocket(void)
	{
	/* Close the socket: */
	close(fd);
	
	/* Release all messages still in the send queue: */
	for(SendQueue::iterator sqIt=sendQueue.begin();sqIt!=sendQueue.end();++sqIt)
		sqIt->message->unref();
	}

MessageBuffer* UDPSocket::readFromSocket(UDPSocket::Address& senderAddress)
	{
	/* Retrieve the size of the next pending message in bytes: */
	int messageSize;
	if(ioctl(fd,FIONREAD,&messageSize)<0)
		Misc::throwStdErr("UDPSocket::readFromSocket: Unable to query message size due to error %d (%s)",errno,strerror(errno));
	
	/* Create a message buffer of the required size: */
	MessageBuffer* result=MessageBuffer::create(messageSize);
	
	/* Read into the message buffer: */
	socklen_t senderAddressLen=sizeof(Address);
	ssize_t recvResult=recvfrom(fd,result->getBuffer(),result->getBufferSize(),0,(struct sockaddr*)&senderAddress,&senderAddressLen);
	if(recvResult>=0)
		{
		/* Update the message buffer's size with the amount of data actually received: */
		result->setBufferSize(size_t(recvResult));
		
		/* Check if the sender address was read successfully: */
		if(senderAddressLen!=sizeof(Address))
			{
			/* Discard the message: */
			result->unref();
			throw std::runtime_error("UDPSocket::readFromSocket: Invalid sender address");
			}
		}
	else if(errno==EAGAIN||errno==EWOULDBLOCK)
		{
		/* Socket didn't have data (this shouldn't happen in event-driven I/O): */
		result->unref();
		result=0;
		Misc::logWarning("UDPSocket::readFromSocket: Nothing to read");
		}
	else
		{
		/* There was an error: */
		result->unref();
		Misc::throwStdErr("UDPSocket::readFromSocket: Error %d (%s)",errno,strerror(errno));
		}
	
	return result;
	}

size_t UDPSocket::writeToSocket(void)
	{
	/* Bail out if the send queue is empty (this shouldn't happen in event-driven I/O): */
	if(sendQueue.empty())
		{
		Misc::logWarning("UDPSocket::writeToSocket: Nothing to write");
		return 0;
		}
	
	/* Write the first queued message to the socket: */
	SendQueueEntry& sqf=sendQueue.front();
	MessageBuffer* head=sqf.message;
	ssize_t sendResult=sendto(fd,head->getBuffer(),head->getBufferSize(),0,(const struct sockaddr*)&sqf.receiverAddress,sizeof(Address));
	if(sendResult>=0)
		{
		/* Check if the entire message was sent: */
		if(size_t(sendResult)!=head->getBufferSize())
			Misc::throwStdErr("UDPSocket::writeToSocket: Packet was truncated; %u of %u bytes sent",(unsigned int)(sendResult),(unsigned int)(head->getBufferSize()));
		
		/* Remove the sent packet from the send queue: */
		sendQueue.pop_front();
		sendQueueSize-=head->getBufferSize();
		head->unref();
		}
	else if(errno==EAGAIN||errno==EWOULDBLOCK)
		{
		/* Socket is busy (this shouldn't happen in event-driven I/O): */
		Misc::logWarning("UDPSocket::writeToSocket: Socket is busy");
		}
	else
		{
		/* Remove the offending packet, or this will probably just happen again: */
		sendQueue.pop_front();
		sendQueueSize-=head->getBufferSize();
		head->unref();
		
		Misc::throwStdErr("UDPSocket::writeToSocket: Error %d (%s)",errno,strerror(errno));
		}
	
	return sendQueueSize;
	}

size_t UDPSocket::queueMessage(const UDPSocket::Address& receiverAddress,MessageBuffer* message)
	{
	size_t result=sendQueueSize;
	
	/* Append the message to the end of the send queue: */
	message->ref();
	SendQueueEntry sqe;
	sqe.receiverAddress=receiverAddress;
	sqe.message=message;
	sendQueue.push_back(sqe);
	sendQueueSize+=message->getBufferSize();
	
	return result;
	}
