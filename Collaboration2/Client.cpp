/***********************************************************************
Client - Class representing a collaboration client.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Client.h>

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <openssl/md5.h>
#include <utility>
#include <stdexcept>
#include <Misc/Utility.h>
#include <Misc/PrintInteger.h>
#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/CompoundValueCoders.h>

#include <Collaboration2/Config.h>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>

/*************************************
Methods of class Client::RemoteClient:
*************************************/

Client::RemoteClient::RemoteClient(unsigned int sId,const std::string& sName)
	:id(sId),name(sName)
	{
	}

Client::RemoteClient::~RemoteClient(void)
	{
	/* Delete all plug-in client states in reverse order: */
	for(PluginClientList::reverse_iterator pIt=plugins.rbegin();pIt!=plugins.rend();++pIt)
		delete *pIt;
	}

void Client::RemoteClient::setPlugin(unsigned int pluginIndex,PluginClient::RemoteClient* newPlugin)
	{
	/* Throw an exception if the plug-in protocol state structure is already set: */
	if(plugins[pluginIndex]!=0)
		{
		/* Delete the new structure: */
		delete newPlugin;
		throw std::runtime_error("Client::RemoteClient::setPlugin: Remote client plug-in state structure already set");
		}
	
	/* Set the plug-in protocol state structure: */
	plugins[pluginIndex]=newPlugin;
	}

PluginClient::RemoteClient* Client::RemoteClient::releasePlugin(unsigned int pluginIndex)
	{
	/* Return the currently set plug-in: */
	PluginClient::RemoteClient* result=plugins[pluginIndex];
	
	/* Reset the plug-in protocol state structure: */
	plugins[pluginIndex]=0;
	
	return result;
	}

/*******************************
Static elements of class Client:
*******************************/

Client* Client::theClient=0;

/***********************
Methods of class Client:
***********************/

bool Client::socketEvent(Threads::EventDispatcher::ListenerKey,int eventTypeMask)
	{
	try
		{
		if(eventTypeMask&Threads::EventDispatcher::Read)
			{
			/* Read data from the socket: */
			size_t unread=socket.readFromSocket();
			
			/* Process as much unread data as possible: */
			bool readAgain;
			do
				{
				readAgain=false;
				switch(state)
					{
					case ReadingMessageID:
						
						/* Check if there is enough unread data to read a message ID: */
						if(unread>=sizeof(MessageID))
							{
							/* Retrieve the message ID: */
							messageId=socket.read<MessageID>();
							
							/* Check if the message ID is valid: */
							MessageContinuationHandler& mh=tcpMessageHandlers[messageId];
							if(messageId>=tcpMessageHandlers.size()||mh.handler==0)
								throw std::runtime_error("Invalid message ID");
							
							/* Check if the message handler requires a minimum message body: */
							if(mh.minUnread>socket.getUnread())
								{
								/* Read the message body: */
								state=ReadingMessageBody;
								}
							else
								{
								/* Handle the message: */
								continuation=mh.handler(messageId,0,mh.handlerUserData);
								if(continuation==0)
									{
									/* Handler is done processing the message; start reading the next one: */
									state=ReadingMessageID;
									
									/* If there is unread data in the socket buffer at this point, read again: */
									readAgain=(unread=socket.getUnread())>0;
									}
								else
									{
									/* Handler is not done processing the message; continue calling the message handler: */
									state=HandlingMessage;
									}
								}
							}
						
						break;
					
					case ReadingMessageBody:
						{
						/* Check if there is enough unread data for the message handler: */
						MessageContinuationHandler& mh=tcpMessageHandlers[messageId];
						if(unread>=mh.minUnread)
							{
							/* Handle the message: */
							continuation=mh.handler(messageId,0,mh.handlerUserData);
							if(continuation==0)
								{
								/* Handler is done processing the message; start reading the next one: */
								state=ReadingMessageID;
								
								/* If there is unread data in the socket buffer at this point, read again: */
								readAgain=(unread=socket.getUnread())>0;
								}
							else
								{
								/* Handler is not done processing the message; continue calling the message handler: */
								state=HandlingMessage;
								}
							}
						
						break;
						}
					
					case HandlingMessage:
						{
						/* Handle the message: */
						MessageContinuationHandler& mh=tcpMessageHandlers[messageId];
						continuation=mh.handler(messageId,continuation,mh.handlerUserData);
						if(continuation==0)
							{
							/* Handler is done processing the message; start reading the next one: */
							state=ReadingMessageID;
							
							/* If there is unread data in the socket buffer at this point, read again: */
							readAgain=(unread=socket.getUnread())>0;
							}
						
						break;
						}
					
					case ReadingPasswordRequest:
						
						/* Check if there is enough unread data to read a password request message: */
						if(unread>=PasswordRequestMsg::size)
							{
							/* Extract the endianness marker: */
							Misc::UInt32 endiannessMarker=socket.read<Misc::UInt32>();
							if(endiannessMarker==0x78563412U)
								{
								socket.setSwapOnRead(true);
								swapOnRead=true;
								}
							else if(endiannessMarker!=0x12345678U)
								throw std::runtime_error("Invalid endianness marker in password request");
							
							/* Extract the protocol version: */
							Misc::UInt32 serverProtocolVersion=socket.read<Misc::UInt32>();
							if(serverProtocolVersion!=protocolVersion)
								Misc::throwStdErr("Invalid protocol version %u",serverProtocolVersion);
							
							/* Create the session password hash: */
							MD5_CTX md5Context;
							MD5_Init(&md5Context);
							
							/* Hash the nonce sent by the server: */
							Byte nonce[PasswordRequestMsg::nonceLength];
							socket.read(nonce,PasswordRequestMsg::nonceLength);
							MD5_Update(&md5Context,nonce,PasswordRequestMsg::nonceLength);
							
							/* Hash the session password: */
							if(!sessionPassword.empty())
								MD5_Update(&md5Context,sessionPassword.data(),sessionPassword.size());
							
							/* Retrieve the hash value: */
							Byte hash[ConnectRequestMsg::hashLength];
							MD5_Final(hash,&md5Context);
							
							/* Queue a connect request message to the server: */
							{
							MessageWriter connectRequest(ConnectRequestMsg::createMessage(plugins.size()));
							connectRequest.write(Misc::UInt32(0x12345678U));
							connectRequest.write(Misc::UInt32(protocolVersion));
							connectRequest.write(hash,ConnectRequestMsg::hashLength);
							stringToCharBuffer(clientName,connectRequest,ConnectRequestMsg::nameLength);
							connectRequest.write(Misc::UInt16(plugins.size()));
							for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
								{
								stringToCharBuffer((*pIt)->getName(),connectRequest,ConnectRequestMsg::ProtocolRequest::nameLength);
								connectRequest.write(Misc::UInt32((*pIt)->getVersion()));
								}
							queueMessage(connectRequest.getBuffer());
							}
							
							/* Start processing messages: */
							state=ReadingMessageID;
							}
						
						break;
					
					default:
						; // Do nothing
					}
				}
			while(readAgain);
			
			/* Check if the server closed the connection: */
			if(state<Disconnecting&&socket.eof())
				{
				Misc::formattedLogWarning("Client: Server %s at address %s closed the connection",serverName.c_str(),serverAddress.c_str());
				state=Disconnecting;
				}
			}
		
		if((eventTypeMask&Threads::EventDispatcher::Write)&&state<Disconnecting)
			{
			/* Write any pending data to the socket: */
			if(socket.writeToSocket()==0)
				{
				/* There is no more pending data; stop dispatching write events on the socket: */
				dispatcher.setIOEventListenerEventTypeMaskFromCallback(socketKey,Threads::EventDispatcher::Read);
				}
			}
		}
	catch(const std::runtime_error& err)
		{
		/* There was a fatal error; shut down the connection: */
		Misc::formattedLogWarning("Client: Disconnecting from server %s at address %s due to exception %s",serverName.c_str(),serverAddress.c_str(),err.what());
		state=Disconnecting;
		}
	
	/* Stop the event dispatcher if the server disconnected: */
	if(state==Disconnecting)
		{
		dispatcher.stop();
		state=Disconnected;
		}
	
	return state==Disconnected;
	}

bool Client::udpSocketEvent(Threads::EventDispatcher::ListenerKey,int eventTypeMask)
	{
	try
		{
		if(eventTypeMask&Threads::EventDispatcher::Read)
			{
			/* Get the next pending message: */
			UDPSocket::Address senderAddress;
			MessageReader message(udpSocket.readFromSocket(senderAddress));
			if(message.getBuffer()!=0)
				{
				/* Check if the message is from the server and contains at least a message ID: */
				message.setSwapOnRead(swapOnRead);
				if(senderAddress==udpServerAddress&&message.getUnread()>=sizeof(MessageID))
					{
					/* Read the message ID and check if it is valid: */
					unsigned int messageId=message.read<MessageID>();
					const MessageReaderHandler& mh=udpMessageHandlers[messageId];
					if(messageId>=udpMessageHandlers.size()||mh.handler==0)
						throw std::runtime_error("Invalid message ID");
					
					/* Dispatch the message: */
					mh.handler(messageId,message,mh.handlerUserData);
					}
				}
			}
		
		if(eventTypeMask&Threads::EventDispatcher::Write)
			{
			/* Write to the socket: */
			if(udpSocket.writeToSocket()==0)
				{
				/* There is no more pending data; stop dispatching write events on the UDP socket: */
				dispatcher.setIOEventListenerEventTypeMaskFromCallback(udpSocketKey,Threads::EventDispatcher::Read);
				}
			}
		}
	catch(const std::runtime_error& err)
		{
		/* This is UDP! Print an error message and carry on: */
		Misc::formattedLogError("Client::udpSocketEvent: Caught exception %s",err.what());
		}
	
	/* Keep listening: */
	return false;
	}

bool Client::sendUDPConnectRequestCallback(Threads::EventDispatcher::ListenerKey eventKey)
	{
	/* Stop trying if too many attempts failed: */
	if(--numUDPConnectRequests==0)
		{
		Misc::logWarning("Client: Unable to establish UDP connection to server");
		
		// DEBUGGING
		#if 1
		/* Create a timer to send ping requests at regular intervals: */
		Threads::EventDispatcher::Time interval(1,0);
		Threads::EventDispatcher::Time first=Threads::EventDispatcher::Time::now();
		first+=interval;
		dispatcher.addTimerEventListener(first,interval,Threads::EventDispatcher::wrapMethod<Client,&Client::sendPingRequestCallback>,this);
		#endif
		
		/* Stop listening: */
		return true;
		}
	
	/* Send a connection request message to the server's UDP socket: */
	{
	MessageWriter udpConnectRequest(UDPConnectRequestMsg::createMessage());
	udpConnectRequest.write(ClientID(id));
	udpConnectRequest.write(udpConnectionTicket);
	queueUDPMessage(udpConnectRequest.getBuffer());
	}
	
	/* Keep listening: */
	return false;
	}

bool Client::messageSignalCallback(Threads::EventDispatcher::ListenerKey signalKey,void* signalData)
	{
	/* Access the message: */
	MessageBuffer* message=static_cast<MessageBuffer*>(signalData);
	
	/* Send it to the server and release it: */
	queueMessage(message);
	message->unref();
	
	/* Keep listening: */
	return false;
	}

MessageContinuation* Client::connectReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		PluginList::iterator pcIt;
		unsigned int numUnreadProtocols;
		};
	
	/* Check if this is a new connect reply: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Extract the server name: */
		serverName.clear();
		charBufferToString(socket,ConnectReplyMsg::nameLength,serverName);
		
		/* Extract the client ID: */
		id=socket.read<ClientID>();
		
		/* Extract the assigned client name: */
		clientName.clear();
		charBufferToString(socket,ConnectReplyMsg::nameLength,clientName);
		
		/* Extract the UDP connection ticket: */
		udpConnectionTicket=socket.read<Misc::UInt32>();
		
		/* Check the number of protocol reply sub-messages contained in the connect reply: */
		if(socket.read<Misc::UInt16>()!=plugins.size())
			throw std::runtime_error("Mismatching number of protocol replies in connect reply");
		
		/* Create a timer to send connect requests to the server's UDP socket at regular intervals, until a reply is received: */
		Threads::EventDispatcher::Time interval(0,100000); // Send a request every 0.1s
		Threads::EventDispatcher::Time first=Threads::EventDispatcher::Time::now();
		udpConnectRequestTimerKey=dispatcher.addTimerEventListener(first,interval,Threads::EventDispatcher::wrapMethod<Client,&Client::sendUDPConnectRequestCallback>,this);
		
		/* Prepare to read the protocol replies: */
		cont=new Cont;
		cont->pcIt=plugins.begin();
		cont->numUnreadProtocols=plugins.size();
		}
	
	/* Read a chunk of protocols: */
	unsigned int numProtocols=Misc::min((unsigned int)(socket.getUnread()/ConnectReplyMsg::ProtocolReply::size),cont->numUnreadProtocols);
	for(unsigned int i=0;i<numProtocols;++i,++cont->pcIt)
		{
		PluginClient*& pc=*cont->pcIt;
		
		unsigned int replyStatus=socket.read<Misc::UInt8>();
		unsigned int protocolVersion=socket.read<Misc::UInt32>();
		unsigned int protocolIndex=socket.read<Misc::UInt16>();
		unsigned int clientMessageBase=socket.read<MessageID>();
		unsigned int serverMessageBase=socket.read<MessageID>();
		
		/* Check the negotiation status: */
		if(replyStatus==ConnectReplyMsg::ProtocolReply::Success)
			{
			/* Remember the protocol's server index and message ID bases: */
			pc->setServerIndex(protocolIndex);
			pc->setMessageBases(clientMessageBase,serverMessageBase);
			}
		else
			{
			/* Cancel the protocol: */
			if(replyStatus==ConnectReplyMsg::ProtocolReply::UnknownProtocol)
				Misc::formattedUserWarning("Client: Plug-in protocol %s unsupported by server",pc->getName());
			else if(replyStatus==ConnectReplyMsg::ProtocolReply::WrongVersion)
				Misc::formattedUserWarning("Client: Plug-in protocol %s version %u unsupported by server; needs version %u",pc->getName(),pc->getVersion(),protocolVersion);
			else
				throw std::runtime_error("Illformed protocol reply in connect reply");
			
			/* Destroy the plug-in protocol: */
			if(pluginLoader.isManaged(pc))
				pluginLoader.destroyObject(pc);
			else
				delete pc;
			pc=0;
			}
		}
	cont->numUnreadProtocols-=numProtocols;
	
	/* Check whether all protocols have been read: */
	if(cont->numUnreadProtocols==0)
		{
		Misc::formattedLogNote("Client: Connection to server %s at address %s established",serverName.c_str(),serverAddress.c_str());
		
		/* Compact the list of plug-in protocols: */
		PluginList newPlugins;
		for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
			if(*pIt!=0)
				{
				(*pIt)->setClientIndex(newPlugins.size());
				newPlugins.push_back(*pIt);
				}
		std::swap(plugins,newPlugins);
		
		/* Start all plug-in protocols: */
		for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
			(*pIt)->start();
		
		/* Done with message: */
		delete cont;
		return 0;
		}
	else
		{
		/* Not done with message: */
		return cont;
		}
	}

MessageContinuation* Client::connectRejectCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Disconnect the client: */
	Misc::formattedUserError("Client: Server at address %s rejected session password",serverAddress.c_str());
	
	/* Stop the event dispatcher: */
	dispatcher.stop();
	state=Disconnected;
	
	/* Done with message: */
	return 0;
	}

bool Client::udpMessageSignalCallback(Threads::EventDispatcher::ListenerKey signalKey,void* signalData)
	{
	/* Access the message: */
	MessageBuffer* message=static_cast<MessageBuffer*>(signalData);
	
	/* Send it to the server and release it: */
	queueUDPMessage(message);
	message->unref();
	
	/* Keep listening: */
	return false;
	}

void Client::udpConnectReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Bail out if the message has the wrong size: */
	if(message.getUnread()!=UDPConnectReplyMsg::size)
		{
		Misc::formattedLogError("Client::udpConnectReplyCallback: Wrong-size UDPConnectReply message");
		return;
		}
	
	/* Safeguard against duplicate replies: */
	if(!udpConnected)
		{
		/* Mark the UDP connection as valid: */
		udpConnected=true;
		
		/* Stop sending UDP connect requests: */
		dispatcher.removeTimerEventListener(udpConnectRequestTimerKey);
		
		// DEBUGGING
		#if 1
		/* Create a timer to send ping requests at regular intervals: */
		Threads::EventDispatcher::Time interval(1,0);
		Threads::EventDispatcher::Time first=Threads::EventDispatcher::Time::now();
		first+=interval;
		dispatcher.addTimerEventListener(first,interval,Threads::EventDispatcher::wrapMethod<Client,&Client::sendPingRequestCallback>,this);
		#endif
		}
	}

MessageContinuation* Client::pingReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Read the ping reply: */
	#if 0
	// DEBUGGING
	Misc::SInt16 replySequence=socket.read<Misc::SInt16>();
	Realtime::TimePointRealtime serverTime;
	serverTime.tv_sec=time_t(socket.read<Misc::SInt64>());
	serverTime.tv_nsec=long(socket.read<Misc::SInt64>());
	double roundTrip(lastPingTime.setAndDiff());
	
	/* Estimate the offset to the server's wall-clock time: */
	double timeOffset=double(lastPingTime-serverTime)-roundTrip*0.5;
	Misc::formattedLogNote("Client: Ping reply %d; round-trip time %f ms; server clock offset %f s",replySequence,roundTrip*1000.0,timeOffset);
	#else
	socket.read<Misc::SInt16>();
	socket.read<Misc::SInt64>();
	socket.read<Misc::SInt64>();
	#endif
	
	/* Done with message: */
	return 0;
	}

void Client::udpPingReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Bail out if the message has the wrong size: */
	if(message.getUnread()!=PingMsg::size)
		{
		Misc::formattedLogError("Client::udpPingReplyCallback: Wrong-size PingReply message");
		return;
		}
	
	/* Read the ping reply: */
	#if 0
	// DEBUGGING
	Misc::SInt16 replySequence=message.read<Misc::SInt16>();
	Realtime::TimePointRealtime serverTime;
	serverTime.tv_sec=time_t(message.read<Misc::SInt64>());
	serverTime.tv_nsec=long(message.read<Misc::SInt64>());
	double roundTrip(lastPingTime.setAndDiff());
	
	/* Estimate the offset to the server's wall-clock time: */
	double timeOffset=double(lastPingTime-serverTime)-roundTrip*0.5;
	Misc::formattedLogNote("Client: UDP Ping reply %d; round-trip time %f ms; server clock offset %f s",replySequence,roundTrip*1000.0,timeOffset);
	#endif
	}

MessageContinuation* Client::nameChangeReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Extract the success flag and the new client name: */
	bool granted=readBool(socket);
	std::string newClientName;
	charBufferToString(socket,NameChangeReplyMsg::nameLength,newClientName);
	
	/* Call the name change reply callbacks: */
	{
	NameChangeReplyCallbackData cbData(granted,clientName,newClientName);
	nameChangeReplyCallbacks.call(&cbData);
	}
	
	if(granted)
		{
		/* Change the client's name: */
		Misc::formattedLogNote("Client: Client changed name from %s to %s",clientName.c_str(),newClientName.c_str());
		clientName=newClientName;
		}
	else
		Misc::logWarning("Client: Client name change request was denied by server");
		
	/* Done with message: */
	return 0;
	}

MessageContinuation* Client::clientConnectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Continuation class for this message: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		RemoteClient* newClient; // Pointer to remote client whose connection message is being read
		unsigned int numUnreadProtocols; // Number of protocol entries that remain to be read
		};
	
	/* Check if this is a new client connect notification: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Create a continuation: */
		cont=new Cont;
		
		/* Extract the new client's ID and name: */
		unsigned int clientId=socket.read<ClientID>();
		std::string clientName;
		charBufferToString(socket,ClientConnectNotificationMsg::nameLength,clientName);
		
		/* Add a new remote client: */
		cont->newClient=new RemoteClient(clientId,clientName);
		remoteClients.push_back(cont->newClient);
		remoteClientMap.setEntry(RemoteClientMap::Entry(clientId,cont->newClient));
		
		/* Create the new remote client's protocol slots: */
		cont->newClient->plugins.reserve(plugins.size());
		for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
			cont->newClient->plugins.push_back(0);
		
		/* Start reading the new remote client's supported protocol list: */
		cont->numUnreadProtocols=socket.read<Misc::UInt16>();
		}
	
	/* Read a chunk of protocols: */
	unsigned int numProtocols=Misc::min((unsigned int)(socket.getUnread()/sizeof(Misc::UInt16)),cont->numUnreadProtocols);
	for(unsigned int i=0;i<numProtocols;++i)
		{
		/* Read the protocol index and find the protocol in the list: */
		unsigned int protocolIndex=socket.read<Misc::UInt16>();
		PluginList::iterator pIt;
		for(pIt=plugins.begin();pIt!=plugins.end()&&(*pIt)->getServerIndex()!=protocolIndex;++pIt)
			;
		if(pIt!=plugins.end())
			{
			/* Add the found protocol's client index to the remote client's protocol list: */
			cont->newClient->pluginIndices.push_back((*pIt)->getClientIndex());
			
			/* Notify the plug-in protocol that there is a new client: */
			(*pIt)->clientConnected(cont->newClient->id);
			}
		}
	cont->numUnreadProtocols-=numProtocols;
	
	/* Check whether all protocols have been read: */
	if(cont->numUnreadProtocols==0)
		{
		Misc::formattedLogNote("Client: New client %s with ID %u connected to the server",cont->newClient->name.c_str(),cont->newClient->id);
		
		/* Done with message: */
		delete cont;
		return 0;
		}
	else
		{
		/* Not done with message: */
		return cont;
		}
	}

MessageContinuation* Client::nameChangeNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Extract the client's ID and find the client structure: */
	unsigned int clientId=socket.read<ClientID>();
	RemoteClient* rc=getRemoteClient(clientId);
	
	/* Extract the new client name: */
	std::string newClientName;
	charBufferToString(socket,NameChangeNotificationMsg::nameLength,newClientName);
	Misc::formattedLogNote("Client: Remote client with ID %u changed name from %s to %s",clientId,rc->name.c_str(),newClientName.c_str());
	
	/* Call the name change notification callbacks: */
	{
	NameChangeNotificationCallbackData cbData(clientId,rc->name,newClientName);
	nameChangeNotificationCallbacks.call(&cbData);
	}
	
	/* Change the client's name: */
	rc->name=newClientName;
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* Client::clientDisconnectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Extract the client's ID: */
	unsigned int clientId=socket.read<ClientID>();
	Misc::formattedLogNote("Client: Client with ID %u disconnected from the server",clientId);
	
	/* Notify all protocols in which the remote client participated that it disconnected in reverse order: */
	RemoteClient* remoteClient=getRemoteClient(clientId);
	for(std::vector<unsigned int>::reverse_iterator piIt=remoteClient->pluginIndices.rbegin();piIt!=remoteClient->pluginIndices.rend();++piIt)
		plugins[*piIt]->clientDisconnected(clientId);
	
	/* Remove the client from the remote client map and the list of remote clients: */
	remoteClientMap.removeEntry(clientId);
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if((*rcIt)->id==clientId)
			{
			/* Remove the client: */
			RemoteClient* client=*rcIt;
			Misc::formattedLogNote("Client: Removing remote client state for client %s with ID %u",client->name.c_str(),client->id);
			*rcIt=remoteClients.back();
			remoteClients.pop_back();
			
			delete client;
			
			/* Stop looking: */
			break;
			}
	
	/* Done with message: */
	return 0;
	}

bool Client::sendPingRequestCallback(Threads::EventDispatcher::ListenerKey eventKey)
	{
	/* Get the current time: */
	lastPingTime.set();
	++lastPingSequence;
	
	/* Send a ping request to the server: */
	{
	MessageWriter pingRequest(PingMsg::createMessage(PingRequest));
	pingRequest.write(lastPingSequence);
	pingRequest.write(Misc::SInt64(lastPingTime.tv_sec));
	pingRequest.write(Misc::SInt64(lastPingTime.tv_nsec));
	if(haveUDP())
		queueUDPMessage(pingRequest.getBuffer());
	else
		queueMessage(pingRequest.getBuffer());
	}
	
	/* Keep repeating the timer event: */
	return false;
	}

MessageContinuation* Client::fixedSizeForwarderCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Retrieve the fixed message size from the message handler entry: */
	size_t fixedSize=tcpMessageHandlers[messageId].minUnread;
	
	/* Read the entire message into a new message buffer and forward it to the front end: */
	{
	MessageWriter message(MessageBuffer::create(messageId,fixedSize));
	socket.readRaw(message.getWritePtr(),message.getSpace());
	frontendPipe.write(message.getBuffer()->ref());
	}
	
	/* Done with message: */
	return 0;
	}

Client::Client(void)
	:configurationFile(COLLABORATION_CONFIGDIR "/" COLLABORATION_CONFIGFILENAME),
	 rootConfigSection(configurationFile.getSection("/Collaboration2Client")),
	 id(0),
	 pluginLoader(COLLABORATION_PLUGINDIR "/" COLLABORATION_PLUGINCLIENTDSONAMETEMPLATE),
	 swapOnRead(false),
	 udpSocket(0),numUDPConnectRequests(10),udpConnected(false),
	 frontend(false),frontendPipe(true), // Create front-end pipe in non-blocking mode
	 remoteClientMap(17),
	 state(ReadingPasswordRequest),continuation(0),
	 lastPingSequence(0)
	{
	/* Set the global client object if it does not exist yet: */
	if(theClient==0)
		theClient=this;
	
	/* Determine the default client name: */
	const char* dcn=getenv("HOSTNAME");
	if(dcn==0||dcn[0]=='\0')
		dcn=getenv("HOST");
	if(dcn==0||dcn[0]=='\0')
		dcn="Client";
	clientName=dcn;
	clientName=rootConfigSection.retrieveString("./clientName",clientName);
	
	/* Load the default protocol plug-ins: */
	std::vector<std::string> protocolNames;
	protocolNames=rootConfigSection.retrieveValue<std::vector<std::string> >("./protocolNames",protocolNames);
	for(std::vector<std::string>::iterator pnIt=protocolNames.begin();pnIt!=protocolNames.end();++pnIt)
		requestPluginProtocol(pnIt->c_str());
	}

Client::~Client(void)
	{
	if(socket.isConnected())
		{
		/* Shut down the socket connection: */
		socket.shutdown(true,true);
		
		/* Disconnect and delete all remote clients: */
		for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
			{
			/* Notify all protocols in which the remote client participated that it disconnected in reverse order: */
			for(std::vector<unsigned int>::reverse_iterator piIt=(*rcIt)->pluginIndices.rbegin();piIt!=(*rcIt)->pluginIndices.rend();++piIt)
				plugins[*piIt]->clientDisconnected((*rcIt)->id);
			
			/* Delete the remote client: */
			delete *rcIt;
			}
		
		/* Delete a remaining message continuation object: */
		delete continuation;
		
		/* Delete all pending messages in the front-end pipe: */
		MessageBuffer* message;
		size_t readSize;
		while((readSize=frontendPipe.read(&message,sizeof(MessageBuffer*)))==sizeof(MessageBuffer*))
			message->unref();
		
		/* Shut down all plug-in protocols in reverse order: */
		for(PluginList::reverse_iterator pIt=plugins.rbegin();pIt!=plugins.rend();++pIt)
			{
			if(pluginLoader.isManaged(*pIt))
				pluginLoader.destroyObject(*pIt);
			else
				delete *pIt;
			}
		}
	
	/* Reset the global client object if it was us: */
	if(theClient==this)
		theClient=0;
	}

bool Client::isURI(const char* string) // Returns true if the given string appears to be a server URI
	{
	return strncasecmp(string,"vci://",6)==0;
	}

bool Client::parseURI(const char* string,std::string& serverHostName,int serverPort,std::string& sessionPassword) // Parses an apparent server URI; returns false if format is wrong after all
	{
	/* Split the server URI into server host name, optional port, and optional session password: */
	const char* sPtr=string+6;
	const char* shnBegin=sPtr;
	while(*sPtr!='\0'&&*sPtr!='/'&&*sPtr!=':')
		++sPtr;
	const char* shnEnd=sPtr;
	int sp=-1;
	if(*sPtr==':')
		{
		++sPtr;
		sp=0;
		while(*sPtr!='\0'&&isdigit(*sPtr))
			{
			sp=sp*10+int(*sPtr-'0');
			++sPtr;
			}
		}
	if(*sPtr=='/')
		{
		++sPtr;
		sessionPassword=sPtr;
		while(*sPtr!='\0')
			++sPtr;
		}
	if(shnBegin!=shnEnd&&*sPtr=='\0')
		{
		serverHostName=std::string(shnBegin,shnEnd);
		if(sp>=0)
			serverPort=sp;
		return true;
		}
	else
		return false;
	}

void Client::requestNameChange(const char* newClientName)
	{
	if(socket.isConnected())
		{
		/* Create and send a name change request message: */
		MessageWriter nameChangeRequest(NameChangeRequestMsg::createMessage());
		stringToCharBuffer(newClientName,nameChangeRequest,NameChangeRequestMsg::nameLength);
		queueServerMessage(nameChangeRequest.getBuffer());
		}
	else
		{
		/* Change the client name locally: */
		clientName=newClientName;
		}
	}

PluginClient* Client::requestPluginProtocol(const char* protocolName,unsigned int protocolVersion)
	{
	/* Split the requested protocol version into major version (incompatible) and minor version (compatible): */
	unsigned int protocolMajor=protocolVersion>>16;
	
	/* Check if the requested plug-in protocol is already active, matching on major protocol version and name: */
	for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
		if(((*pIt)->getVersion()>>16)==protocolMajor&&strcmp((*pIt)->getName(),protocolName)==0)
			{
			/* Return the already-active plug-in protocol: */
			return *pIt;
			}
	
	/* Now try loading the requested protocol from a DSO: */
	PluginClient* newProtocol=pluginLoader.createObject(protocolName,protocolMajor,this);
	
	/* Add the new plug-in protocol to the list and return it: */
	addPluginProtocol(newProtocol);
	return newProtocol;
	}

PluginClient* Client::requestPluginProtocol(const char* protocolName)
	{
	/* Check if the requested plug-in protocol is already active, matching on name: */
	for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
		if(strcmp((*pIt)->getName(),protocolName)==0)
			{
			/* Return the already-active plug-in protocol: */
			return *pIt;
			}
	
	/* Now try loading the highest version of the requested protocol from a DSO: */
	PluginClient* newProtocol=pluginLoader.createObject(protocolName,this);
	
	/* Add the new plug-in protocol to the list and return it: */
	addPluginProtocol(newProtocol);
	return newProtocol;
	}

void Client::addPluginProtocol(PluginClient* protocol)
	{
	/* Add the plug-in protocol to the list: */
	plugins.push_back(protocol);
	}

PluginClient* Client::findPluginProtocol(const char* protocolName,unsigned int protocolVersion)
	{
	/* Search for the requested protocol in the list: */
	for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
		if((*pIt)->getVersion()==protocolVersion&&strcmp((*pIt)->getName(),protocolName)==0)
			return *pIt;
	
	return 0;
	}

void Client::setTCPMessageHandler(unsigned int messageId,Client::MessageContinuationHandlerFunction handler,void* handlerUserData,size_t minUnread)
	{
	/* Ensure that there are enough entries in the TCP message handler array: */
	while(tcpMessageHandlers.size()<messageId)
		tcpMessageHandlers.push_back(MessageContinuationHandler(0,0,0));
	
	/* Add or insert a new TCP message handler: */
	if(messageId<tcpMessageHandlers.size())
		tcpMessageHandlers[messageId]=MessageContinuationHandler(handler,handlerUserData,minUnread);
	else
		tcpMessageHandlers.push_back(MessageContinuationHandler(handler,handlerUserData,minUnread));
	}

void Client::setUDPMessageHandler(unsigned int messageId,Client::MessageReaderHandlerFunction handler,void* handlerUserData)
	{
	/* Ensure that there are enough entries in the UDP message handler array: */
	while(udpMessageHandlers.size()<messageId)
		udpMessageHandlers.push_back(MessageReaderHandler(0,0));
	
	/* Add or insert a new UDP message handler: */
	if(messageId<udpMessageHandlers.size())
		udpMessageHandlers[messageId]=MessageReaderHandler(handler,handlerUserData);
	else
		udpMessageHandlers.push_back(MessageReaderHandler(handler,handlerUserData));
	}

void Client::setFrontendMessageHandler(unsigned int messageId,Client::MessageReaderHandlerFunction handler,void* handlerUserData)
	{
	/* Ensure that there are enough entries in the front-end message handler array: */
	while(frontendMessageHandlers.size()<messageId)
		frontendMessageHandlers.push_back(MessageReaderHandler(0,0));
	
	/* Add or insert a new UDP message handler: */
	if(messageId<frontendMessageHandlers.size())
		frontendMessageHandlers[messageId]=MessageReaderHandler(handler,handlerUserData);
	else
		frontendMessageHandlers.push_back(MessageReaderHandler(handler,handlerUserData));
	}

void Client::setMessageForwarder(unsigned int messageId,MessageReaderHandlerFunction handler,void* handlerUserData,size_t fixedMessageSize)
	{
	/* Install the given message handler in the front-end: */
	setFrontendMessageHandler(messageId,handler,handlerUserData);
	
	/* Install an auto-forwarder in the back end: */
	setTCPMessageHandler(messageId,wrapMethod<Client,&Client::fixedSizeForwarderCallback>,this,fixedMessageSize);
	}

void Client::queueMessage(MessageBuffer* message)
	{
	/* Queue the message for sending and check if the socket was idle before: */
	if(socket.queueMessage(message)==0)
		{
		/* There is pending data; start dispatching write events on the socket: */
		dispatcher.setIOEventListenerEventTypeMaskFromCallback(socketKey,Threads::EventDispatcher::Read|Threads::EventDispatcher::Write);
		}
	}

void Client::queueUDPMessage(MessageBuffer* message)
	{
	/* Queue the message for sending and check if the socket was idle before: */
	if(udpSocket.queueMessage(udpServerAddress,message)==0)
		{
		/* There is pending data; start dispatching write events on the UDP socket: */
		dispatcher.setIOEventListenerEventTypeMaskFromCallback(udpSocketKey,Threads::EventDispatcher::Read|Threads::EventDispatcher::Write);
		}
	}

std::string Client::getDefaultServerHostName(void) const
	{
	return rootConfigSection.retrieveString("./serverHostName","localhost");
	}

int Client::getDefaultServerPort(void) const
	{
	return rootConfigSection.retrieveValue<int>("./serverPort",26000);
	}

int Client::enableFrontendForwarding(void)
	{
	/* Remember that front-end forwarding is enabled: */
	frontend=true;
	
	/* Return pipe read file descriptor: */
	return frontendPipe.getReadFd();
	}

void Client::setPassword(const std::string& newSessionPassword)
	{
	/* Remember the session password: */
	sessionPassword=newSessionPassword;
	}

void Client::start(const std::string& serverHostName,int serverPort)
	{
	/* Connect to the server: */
	socket.connect(serverHostName.c_str(),serverPort);
	
	/* Convert the server's socket address to a readable string: */
	serverAddress=socket.getPeerAddress().getAddress();
	char serverSocketPort[6];
	serverAddress.push_back(':');
	serverAddress.append(Misc::print(socket.getPeerAddress().getPort(),serverSocketPort+5));
	
	/* Dispatch read events on the server socket: */
	socketKey=dispatcher.addIOEventListener(socket.getFd(),Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<Client,&Client::socketEvent>,this);
	
	/* Use the server's TCP socket address also for its UDP socket: */
	if(!socket.getPeerAddress().isIPv4())
		Misc::throwStdErr("Client::start: Server %s's address is not an IPv4 address",serverHostName);
	udpServerAddress=UDPSocket::Address(socket.getPeerAddress().getIPv4Address());
	
	/* Dispatch read events on the UDP socket: */
	udpSocketKey=dispatcher.addIOEventListener(udpSocket.getFd(),Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<Client,&Client::udpSocketEvent>,this);
	
	/* Create signals to queue messages for the server from a different thread: */
	messageSignalKey=dispatcher.addSignalListener(Threads::EventDispatcher::wrapMethod<Client,&Client::messageSignalCallback>,this);
	udpMessageSignalKey=dispatcher.addSignalListener(Threads::EventDispatcher::wrapMethod<Client,&Client::udpMessageSignalCallback>,this);
	
	/* Register message handlers for core protocol messages: */
	setTCPMessageHandler(ConnectReply,wrapMethod<Client,&Client::connectReplyCallback>,this,ConnectReplyMsg::size);
	setTCPMessageHandler(ConnectReject,wrapMethod<Client,&Client::connectRejectCallback>,this,0);
	setUDPMessageHandler(UDPConnectReply,wrapMethod<Client,&Client::udpConnectReplyCallback>,this);
	setTCPMessageHandler(PingReply,wrapMethod<Client,&Client::pingReplyCallback>,this,PingMsg::size);
	setUDPMessageHandler(PingReply,wrapMethod<Client,&Client::udpPingReplyCallback>,this);
	setTCPMessageHandler(NameChangeReply,wrapMethod<Client,&Client::nameChangeReplyCallback>,this,NameChangeReplyMsg::size);
	setTCPMessageHandler(ClientConnectNotification,wrapMethod<Client,&Client::clientConnectNotificationCallback>,this,ClientConnectNotificationMsg::size);
	setTCPMessageHandler(NameChangeNotification,wrapMethod<Client,&Client::nameChangeNotificationCallback>,this,NameChangeNotificationMsg::size);
	setTCPMessageHandler(ClientDisconnectNotification,wrapMethod<Client,&Client::clientDisconnectNotificationCallback>,this,ClientDisconnectNotificationMsg::size);
	
	/* Start the client state machine: */
	state=ReadingPasswordRequest;
	}

void Client::run(void)
	{
	/* Dispatch events until shut down: */
	dispatcher.dispatchEvents();
	}

void Client::dispatchFrontendMessages(void)
	{
	try
		{
		/* Read message buffer pointers from the pipe until there are no more: */
		MessageBuffer* message;
		size_t readSize;
		while((readSize=frontendPipe.read(&message,sizeof(MessageBuffer*)))==sizeof(MessageBuffer*))
			{
			/* Create a reader for the message and read the message ID: */
			MessageReader reader(message,swapOnRead);
			unsigned int messageId=reader.read<MessageID>();
			
			/* Check if the message ID is valid: */
			const MessageReaderHandler& mh=frontendMessageHandlers[messageId];
			if(messageId>=frontendMessageHandlers.size()||mh.handler==0)
				throw std::runtime_error("Invalid message ID");
			
			/* Dispatch the message: */
			mh.handler(messageId,reader,mh.handlerUserData);
			}
		
		/* Check for a partial read (shouldn't happen and is a serious problem): */
		if(readSize!=0)
			throw std::runtime_error("Truncated read on front-end pipe");
		}
	catch(const std::runtime_error& err)
		{
		/* Delete all messages remaining on the pipe: */
		MessageBuffer* message;
		size_t readSize;
		while((readSize=frontendPipe.read(&message,sizeof(MessageBuffer*)))==sizeof(MessageBuffer*))
			message->unref();
		
		/* Drain the pipe of any message buffer fragments (should never happen): */
		char buffer[sizeof(MessageBuffer*)];
		frontendPipe.read(buffer,sizeof(MessageBuffer*));
		
		/* Show an error message and shut down the connection: */
		Misc::formattedUserError("Client::dispatchFrontendMessages: Caught exception %s",err.what());
		dispatcher.stop();
		state=Disconnected;
		}
	}

void Client::shutdown(void)
	{
	if(state<Disconnected)
		{
		/* Shut down the dispatcher: */
		dispatcher.stop();
		state=Shutdown;
		}
	}
