/***********************************************************************
NonBlockSocket - Class representing a socket for non-blocking I/O.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/NonBlockSocket.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <stdexcept>
#include <Misc/PrintInteger.h>
#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Comm/ListeningTCPSocket.h>

#include <Collaboration2/MessageBuffer.h>

/*******************************
Methods of class NonBlockSocket:
*******************************/

void NonBlockSocket::init(size_t readBufferSize)
	{
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
		Misc::throwStdErr("NonBlockSocket::init: Unable to set socket to non-blocking mode due to error %d (%s)",error,strerror(error));
		}
	
	/* Disable socket-level buffering on the socket: */
	int nodelay=1;
	if(setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay))<0)
		{
		/* Close the socket again: */
		int error=errno;
		close(fd);
		Misc::throwStdErr("NonBlockSocket::init: Unable to disable socket-level buffering due to error %d (%s)",error,strerror(error));
		}
	
	/* Initialize socket state: */
	peerClosed=false;
	
	/* Create the read buffer: */
	swapOnRead=false;
	readBuffer=new char[readBufferSize];
	readBufferEnd=readBuffer+readBufferSize;
	writePtr=readBuffer;
	readPtr=readBuffer;
	unread=0;
	
	/* Create the write queue: */
	sendQueueSize=0;
	sent=0;
	}

NonBlockSocket::NonBlockSocket(void)
	:fd(-1),
	 readBuffer(0),
	 sendQueue(4)
	{
	}

NonBlockSocket::NonBlockSocket(Comm::ListeningTCPSocket& listenSocket,size_t readBufferSize)
	:fd(-1),
	 readBuffer(0),
	 sendQueue(4)
	{
	/* Initialize the connection: */
	accept(listenSocket,readBufferSize);
	}

NonBlockSocket::NonBlockSocket(const char* peerHostName,int peerPortId,size_t readBufferSize)
	:fd(-1),
	 readBuffer(0),
	 sendQueue(4)
	{
	/* Initialize the connection: */
	connect(peerHostName,peerPortId,readBufferSize);
	}

NonBlockSocket::~NonBlockSocket(void)
	{
	/* Close the socket: */
	if(fd>=0)
		close(fd);
	
	/* Delete the read buffer: */
	delete[] readBuffer;
	
	/* Release all messages still in the send queue: */
	for(SendQueue::iterator sqIt=sendQueue.begin();sqIt!=sendQueue.end();++sqIt)
		(*sqIt)->unref();
	}

void NonBlockSocket::accept(Comm::ListeningTCPSocket& listenSocket,size_t readBufferSize)
	{
	/* Accept the listening socket's first pending connection: */
	socklen_t peerAddressLen=sizeof(peerAddress);
	fd=::accept(listenSocket.getFd(),reinterpret_cast<sockaddr*>(&peerAddress),&peerAddressLen);
	
	/* Check for errors: */
	if(fd<0)
		{
		if(errno==EAGAIN||errno==EWOULDBLOCK)
			throw std::runtime_error("NonBlockSocket::accept: Spurious connection attempt");
		else
			Misc::throwStdErr("NonBlockSocket::accept: Unable to accept connection due to error %d (%s)",errno,strerror(errno));
		}
	if(peerAddressLen>sizeof(peerAddress))
		{
		/* Close the socket again: */
		close(fd);
		throw std::runtime_error("NonBlockSocket::accept: Unsupported peer address format");
		}
	
	/* Initialize the rest of the socket: */
	init(readBufferSize);
	}

void NonBlockSocket::connect(const char* peerHostName,int peerPortId,size_t readBufferSize)
	{
	/* Convert port ID to string for getaddrinfo (awkward!): */
	if(peerPortId<0||peerPortId>65535)
		Misc::throwStdErr("NonBlockSocket::connect: Invalid port %d",peerPortId);
	char peerPortIdBuffer[6];
	char* peerPortIdString=Misc::print(peerPortId,peerPortIdBuffer+5);
	
	/* Lookup host's IP address: */
	struct addrinfo hints;
	memset(&hints,0,sizeof(struct addrinfo));
	hints.ai_family=AF_UNSPEC;
	hints.ai_socktype=SOCK_STREAM;
	hints.ai_flags=AI_NUMERICSERV|AI_ADDRCONFIG;
	hints.ai_protocol=0;
	struct addrinfo* addresses;
	int aiResult=getaddrinfo(peerHostName,peerPortIdString,&hints,&addresses);
	if(aiResult!=0)
		Misc::throwStdErr("NonBlockSocket::connect: Unable to resolve peer host name %s due to error %s",peerHostName,gai_strerror(aiResult));
	
	/* Try all returned addresses in order until one successfully connects: */
	for(struct addrinfo* aiPtr=addresses;aiPtr!=0;aiPtr=aiPtr->ai_next)
		{
		/* Open a socket: */
		fd=socket(aiPtr->ai_family,aiPtr->ai_socktype,aiPtr->ai_protocol);
		if(fd<0)
			continue;
		
		/* Connect to the remote host and bail out if successful: */
		if(::connect(fd,reinterpret_cast<struct sockaddr*>(aiPtr->ai_addr),aiPtr->ai_addrlen)>=0)
			{
			/* Store the peer's address: */
			if(aiPtr->ai_family==AF_INET)
				peerAddress=Comm::IPSocketAddress(*reinterpret_cast<struct sockaddr_in*>(aiPtr->ai_addr));
			else if(aiPtr->ai_family==AF_INET6)
				peerAddress=Comm::IPSocketAddress(*reinterpret_cast<struct sockaddr_in6*>(aiPtr->ai_addr));
			
			break;
			}
		
		/* Close the socket and try the next address: */
		close(fd);
		fd=-1;
		}
	
	/* Release the returned addresses: */
	freeaddrinfo(addresses);
	
	if(fd>=0)
		{
		/* Initialize the rest of the socket: */
		init(readBufferSize);
		}
	else
		Misc::throwStdErr("NonblockSocket::connect: Unable to connect to peer %s:%d",peerHostName,peerPortId);
	}

void NonBlockSocket::shutdown(bool read,bool write)
	{
	if(read&&write)
		::shutdown(fd,SHUT_RDWR);
	else if(read)
		::shutdown(fd,SHUT_RD);
	else if(write)
		::shutdown(fd,SHUT_WR);
	}

size_t NonBlockSocket::readFromSocket(void)
	{
	/* Calculate how much data can be read at once: */
	size_t space;
	if(writePtr>=readPtr)
		{
		/* Check if the read buffer is full: */
		if(writePtr==readPtr&&unread!=0)
			throw std::runtime_error("NonBlockSocket::readFromSocket: Read buffer is full");
		
		/* Can write until the end of the buffer: */
		space=readBufferEnd-writePtr;
		}
	else
		{
		/* Can write until the read pointer: */
		space=readPtr-writePtr;
		}
	
	/* Read into the buffer: */
	ssize_t readSize=::read(fd,writePtr,space);
	if(readSize>0)
		{
		/* Increase the amount of unread data: */
		writePtr+=readSize;
		if(writePtr==readBufferEnd)
			writePtr=readBuffer;
		unread+=size_t(readSize);
		}
	else if(readSize==0)
		{
		/* The peer closed the connection: */
		peerClosed=true;
		}
	else if(errno==EAGAIN||errno==EWOULDBLOCK)
		Misc::logWarning("NonBlockSocket::readFromSocket: Nothing to read");
	else
		Misc::throwStdErr("NonBlockSocket::readFromSocket: Error %d (%s)",errno,strerror(errno));
	
	return unread;
	}

void NonBlockSocket::setSwapOnRead(bool newSwapOnRead)
	{
	swapOnRead=newSwapOnRead;
	}

size_t NonBlockSocket::writeToSocket(void)
	{
	/* Bail out if the send queue is empty (this shouldn't happen in event-driven I/O): */
	if(sendQueue.empty())
		{
		Misc::logWarning("NonBlockSocket::writeToSocket: Nothing to write");
		return 0;
		}
	
	/* Try sending all messages in the send queue en bloc, hopefully combining small messages into larger IP packets: */
	size_t numMessages=sendQueue.size();
	iovec* iovecs=new iovec[numMessages];
	iovec* iovPtr=iovecs;
	
	/* Send what remains of the first packet in the queue: */
	SendQueue::iterator sqIt=sendQueue.begin();
	iovPtr->iov_base=(*sqIt)->getBuffer()+sent;
	iovPtr->iov_len=(*sqIt)->getBufferSize()-sent;
	
	/* Send all other queued packets in their entirety: */
	for(++sqIt,++iovPtr;sqIt!=sendQueue.end();++sqIt,++iovPtr)
		{
		iovPtr->iov_base=(*sqIt)->getBuffer();
		iovPtr->iov_len=(*sqIt)->getBufferSize();
		}
	
	/* Send the queued-up data: */
	ssize_t writeSize=writev(fd,iovecs,int(numMessages));
	delete[] iovecs;
	if(writeSize>=0)
		{
		/* Remove all messages that were completely sent from the send queue: */
		sent+=writeSize;
		while(!sendQueue.empty()&&sent>=sendQueue.front()->getBufferSize())
			{
			/* Release the completely-sent message: */
			sent-=sendQueue.front()->getBufferSize();
			sendQueueSize-=sendQueue.front()->getBufferSize();
			sendQueue.front()->unref();
			sendQueue.pop_front();
			}
		}
	else if(errno==EAGAIN||errno==EWOULDBLOCK)
		{
		/* Socket is busy (this shouldn't happen in event-driven I/O): */
		Misc::logWarning("NonBlockSocket::writeToSocket: Socket is busy");
		}
	else
		Misc::throwStdErr("NonBlockSocket::writeToSocket: Error %d (%s)",errno,strerror(errno));
	
	return sendQueueSize-sent;
	}

size_t NonBlockSocket::queueMessage(MessageBuffer* message)
	{
	size_t result=sendQueueSize-sent;
	
	/* Append the message to the end of the send queue: */
	message->ref();
	sendQueue.push_back(message);
	sendQueueSize+=message->getBufferSize();
	
	return result;
	}
