/***********************************************************************
Server - Class representing a collaboration server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Server.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <openssl/md5.h>
#include <stdexcept>
#include <iostream>
#include <Misc/SelfDestructPointer.h>
#include <Misc/ThrowStdErr.h>
#include <Misc/PrintInteger.h>
#include <Misc/MessageLogger.h>
#include <Misc/Endianness.h>
#include <Misc/StandardValueCoders.h>
#include <Realtime/Time.h>
#include <Misc/UTF8.h>
#include <IO/UTF8.h>
#include <Comm/IPSocketAddress.h>

#include <Collaboration2/Config.h>
#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>

/***************************************
Static elements of class Server::Client:
***************************************/

const std::runtime_error Server::Client::missingPluginError("Server::Client::getPlugin: Missing plug-in requested");

/*******************************
Methods of class Server::Client:
*******************************/

bool Server::Client::socketEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask)
	{
	/* Embedded classes: */
	class ConnectRequestContinuation:public MessageContinuation
		{
		/* Elements: */
		public:
		MessageWriter connectReply; // Connect reply message to be sent to connecting client
		
		/* Constructors and destructors: */
		ConnectRequestContinuation(unsigned int numProtocolRequests)
			:connectReply(ConnectReplyMsg::createMessage(numProtocolRequests))
			{
			}
		};
	
	try
		{
		/* Handle the client communication protocol: */
		if(eventTypeMask&Threads::EventDispatcher::Read)
			{
			/* Read data from the socket: */
			size_t unread=socket.readFromSocket();
			
			/* Process as much unread data as possible: */
			bool readAgain=false;
			do
				{
				readAgain=false;
				switch(clientState)
					{
					case Client::ReadingMessageID:
						
						/* Check if there is enough unread data to read a message ID: */
						if(unread>=sizeof(MessageID))
							{
							/* Retrieve the message ID: */
							messageId=socket.read<MessageID>();
							
							/* Check if the message ID is valid: */
							const MessageHandler& mh=server->messageHandlers[messageId];
							if(messageId>=server->messageHandlers.size()||mh.callback==0)
								throw std::runtime_error("Invalid message ID");
							
							/* Check if the message handler requires a minimum message body: */
							if(mh.minUnread>socket.getUnread())
								{
								/* Read the message body: */
								clientState=Client::ReadingMessageBody;
								}
							else
								{
								/* Handle the message: */
								continuation=mh.callback(messageId,id,0,mh.callbackUserData);
								if(continuation==0)
									{
									/* Handler is done processing the message; start reading the next one: */
									clientState=Client::ReadingMessageID;
									
									/* If there is unread data in the socket buffer at this point, read again: */
									readAgain=(unread=socket.getUnread())>0;
									}
								else
									{
									/* Handler is not done processing the message; continue calling the message handler: */
									clientState=Client::HandlingMessage;
									}
								}
							}
						
						break;
					
					case Client::ReadingMessageBody:
						{
						/* Check if there is enough unread data for the message handler: */
						const MessageHandler& mh=server->messageHandlers[messageId];
						if(unread>=mh.minUnread)
							{
							/* Handle the message: */
							continuation=mh.callback(messageId,id,0,mh.callbackUserData);
							if(continuation==0)
								{
								/* Handler is done processing the message; start reading the next one: */
								clientState=Client::ReadingMessageID;
								
								/* If there is unread data in the socket buffer at this point, read again: */
								readAgain=(unread=socket.getUnread())>0;
								}
							else
								{
								/* Handler is not done processing the message; continue calling the message handler: */
								clientState=Client::HandlingMessage;
								}
							}
						
						break;
						}
					
					case Client::HandlingMessage:
						{
						/* Handle the message: */
						const MessageHandler& mh=server->messageHandlers[messageId];
						continuation=mh.callback(messageId,id,continuation,mh.callbackUserData);
						if(continuation==0)
							{
							/* Handler is done processing the message; start reading the next one: */
							clientState=Client::ReadingMessageID;
							
							/* If there is unread data in the socket buffer at this point, read again: */
							readAgain=(unread=socket.getUnread())>0;
							}
						
						break;
						}
					
					case Client::ReadingClientConnectRequest:
						
						/* Check if there is enough unread data to process a connection request message: */
						if(unread>=ConnectRequestMsg::size)
							{
							/* Extract the endianness marker: */
							Misc::UInt32 endiannessMarker=socket.read<Misc::UInt32>();
							if(endiannessMarker==0x78563412U)
								{
								socket.setSwapOnRead(true);
								swapOnRead=true;
								}
							else if(endiannessMarker!=0x12345678U)
								throw std::runtime_error("Invalid endianness marker in connect request");
							
							/* Extract the protocol version: */
							Misc::UInt32 clientProtocolVersion=socket.read<Misc::UInt32>();
							if(clientProtocolVersion!=protocolVersion)
								Misc::throwStdErr("Invalid protocol version %u",clientProtocolVersion);
							
							/* Authenticate the password hash sent by the client: */
							MD5_CTX md5Context;
							MD5_Init(&md5Context);
							
							/* Hash the nonce sent to the client: */
							MD5_Update(&md5Context,nonce,PasswordRequestMsg::nonceLength);
							
							/* Hash the session password: */
							if(!server->sessionPassword.empty())
								MD5_Update(&md5Context,server->sessionPassword.data(),server->sessionPassword.size());
							
							/* Retrieve the hash value: */
							Byte hash[ConnectRequestMsg::hashLength];
							MD5_Final(hash,&md5Context);
							
							/* Compare the hash value to the hash sent by the client: */
							Byte clientHash[ConnectRequestMsg::hashLength];
							socket.read(clientHash,ConnectRequestMsg::hashLength);
							if(memcmp(hash,clientHash,ConnectRequestMsg::hashLength)!=0)
								{
								/* Queue a connect reject message to the client and disconnect the client: */
								{
								MessageWriter connectReject(MessageBuffer::create(ConnectReject,0));
								socket.queueMessage(connectReject.getBuffer());
								}
								
								/* Ignore further messages and disconnect the client when the connect reject message has been sent: */
								server->dispatcher.setIOEventListenerEventTypeMaskFromCallback(socketKey,Threads::EventDispatcher::Write);
								clientState=Drain;
								throw std::runtime_error("Wrong session password");
								}
							
							/* Extract the client name: */
							name.clear();
							charBufferToString(socket,ConnectRequestMsg::nameLength,name);
							
							/* Check if the client name is a valid non-empty UTF-8 encoded string: */
							if(name.empty()||!Misc::UTF8::isValid(name.begin(),name.end()))
								{
								/* Assign the client a default name: */
								name="Client";
								}
							
							/* Check if the requested client name is unique: */
							bool nameUnique=true;
							for(ClientList::iterator cIt=server->clients.begin();cIt!=server->clients.end()&&nameUnique;++cIt)
								nameUnique=*cIt==this||(*cIt)->name!=name;
							if(!nameUnique)
								{
								/* Shorten the client name until there's room for a uniquifying suffix (underscore and 4 digits): */
								while(name.length()>27)
									{
									/* Check for a UTF-8 continuation character: */
									if(name.back()&0x80)
										{
										/* Remove the entire code sequence (safe because the name is valid UTF-8): */
										while(name.back()&0x80)
											name.pop_back();
										}
									else
										name.pop_back();
									}
								
								/* Collect all numerical suffixes of names that match this name: */
								Misc::HashTable<unsigned int,void> suffixes(17);
								name.push_back('_');
								for(ClientList::iterator cIt=server->clients.begin();cIt!=server->clients.end()&&nameUnique;++cIt)
									if(*cIt!=this)
										{
										/* Check if the name prefix matches: */
										std::string::iterator nIt=name.begin();
										std::string::iterator onIt=(*cIt)->name.begin();
										for(;nIt!=name.end()&&onIt!=(*cIt)->name.end()&&*nIt==*onIt;++nIt,++onIt)
											;
										if(nIt==name.end())
											{
											/* Check if the other name ends in a 4-digit number: */
											unsigned int otherSuffix=0;
											unsigned int numDigits=0;
											for(;onIt!=(*cIt)->name.end()&&isdigit(*onIt);++onIt,++numDigits)
												otherSuffix=otherSuffix*10+(*onIt-'0');
											if(onIt==(*cIt)->name.end()&&numDigits==4)
												suffixes.setEntry(Misc::HashTable<unsigned int,void>::Entry(otherSuffix));
											}
										}
								
								/* Find the smallest unused suffix: */
								unsigned int suffix;
								for(suffix=1;suffixes.isEntry(suffix);++suffix)
									;
								
								/* Append the suffix to the name: */
								char suffixString[4];
								for(char* suffixPtr=suffixString+3;suffixPtr>=suffixString;--suffixPtr,suffix/=10)
									*suffixPtr=suffix%10+'0';
								name.append(suffixString,4);
								}
							
							/* Read the number of protocol requests: */
							unsigned int numProtocolRequests=socket.read<Misc::UInt16>();
							
							/* Create a continuation object to read the rest of the message: */
							ConnectRequestContinuation* cont=new ConnectRequestContinuation(numProtocolRequests);
							continuation=cont;
							
							/* Start the connect reply message to be sent to the new client: */
							stringToCharBuffer(server->name,cont->connectReply,ConnectReplyMsg::nameLength);
							cont->connectReply.write(ClientID(id));
							stringToCharBuffer(name,cont->connectReply,ConnectReplyMsg::nameLength);
							cont->connectReply.write(udpConnectionTicket);
							cont->connectReply.write(Misc::UInt16(numProtocolRequests));
							
							/* Read the protocol requests: */
							clientState=Client::ReadingProtocolRequests;
							readAgain=(unread=socket.getUnread())>0||numProtocolRequests==0;
							}
						
						break;
					
					case Client::ReadingProtocolRequests:
						{
						/* Read all complete protocol requests that can be read: */
						ConnectRequestContinuation* cont=static_cast<ConnectRequestContinuation*>(continuation);
						while(unread>=ConnectRequestMsg::ProtocolRequest::size&&!cont->connectReply.eof())
							{
							/* Read the requested protocol name and version: */
							std::string protocolName;
							charBufferToString(socket,ConnectRequestMsg::ProtocolRequest::nameLength,protocolName);
							unsigned int protocolVersion=socket.read<Misc::UInt32>();
							
							/* Request the plug-in protocol: */
							PluginServer* requestedPlugin=server->requestPluginProtocol(protocolName.c_str(),protocolVersion);
							if(requestedPlugin!=0)
								{
								/* Grant the request: */
								cont->connectReply.write(Misc::UInt8(ConnectReplyMsg::ProtocolReply::Success));
								cont->connectReply.write(Misc::UInt32(requestedPlugin->getVersion()));
								cont->connectReply.write(Misc::UInt16(requestedPlugin->getIndex()));
								cont->connectReply.write(MessageID(requestedPlugin->getClientMessageBase()));
								cont->connectReply.write(MessageID(requestedPlugin->getServerMessageBase()));
								
								/* Mark the client as participating in the protocol: */
								pluginIndices.push_back(requestedPlugin->getIndex());
								}
							else
								{
								/* Deny the request with an unknown protocol error: */
								cont->connectReply.write(Misc::UInt8(ConnectReplyMsg::ProtocolReply::UnknownProtocol));
								cont->connectReply.write(Misc::UInt32(0));
								cont->connectReply.write(Misc::UInt16(0));
								cont->connectReply.write(MessageID(0));
								cont->connectReply.write(MessageID(0));
								}
							
							unread-=ConnectRequestMsg::ProtocolRequest::size;
							}
						
						/* Check if all protocol request sub-messages have been read: */
						if(cont->connectReply.eof())
							{
							/* Send the connect reply message: */
							queueMessage(cont->connectReply.getBuffer());
							delete cont;
							
							/* Send a client connect notification to all other clients: */
							{
							MessageWriter clientConnectNotification(ClientConnectNotificationMsg::createMessage(pluginIndices.size()));
							clientConnectNotification.write(ClientID(id));
							stringToCharBuffer(name,clientConnectNotification,ClientConnectNotificationMsg::nameLength);
							clientConnectNotification.write(Misc::UInt16(pluginIndices.size()));
							for(std::vector<unsigned int>::iterator piIt=pluginIndices.begin();piIt!=pluginIndices.end();++piIt)
								clientConnectNotification.write(Misc::UInt16(*piIt));
							for(ClientList::iterator cIt=server->clients.begin();cIt!=server->clients.end();++cIt)
								if((*cIt)->clientState>=Client::ReadingMessageID)
									{
									/* Tell the other client about the new client: */
									(*cIt)->queueMessage(clientConnectNotification.getBuffer());
									
									/* Tell the new client about the other client: */
									{
									MessageWriter clientConnectNotification2(ClientConnectNotificationMsg::createMessage((*cIt)->pluginIndices.size()));
									clientConnectNotification2.write(ClientID((*cIt)->id));
									stringToCharBuffer((*cIt)->name,clientConnectNotification2,ClientConnectNotificationMsg::nameLength);
									clientConnectNotification2.write(Misc::UInt16((*cIt)->pluginIndices.size()));
									for(std::vector<unsigned int>::iterator piIt=(*cIt)->pluginIndices.begin();piIt!=(*cIt)->pluginIndices.end();++piIt)
										clientConnectNotification2.write(Misc::UInt16(*piIt));
									queueMessage(clientConnectNotification2.getBuffer());
									}
									}
							}
							
							/* Notify all plug-in protocols in which the new client is participating: */
							for(std::vector<unsigned int>::iterator piIt=pluginIndices.begin();piIt!=pluginIndices.end();++piIt)
								server->plugins[*piIt]->clientConnected(id);
							
							/* Go to connected state: */
							Misc::formattedLogNote("Server: Serving client %s from %s",name.c_str(),clientAddress.c_str());
							continuation=0;
							connected=true;
							clientState=Client::ReadingMessageID;
							
							/* If there is unread data in the socket buffer at this point, read again: */
							readAgain=unread>0;
							}
						
						break;
						}
					
					default:
						; // Never reached; just to make compiler happy
					}
				}
			while(readAgain);
			
			/* Check if the client closed the connection: */
			if(clientState<Drain&&socket.eof())
				{
				/* Client hung up; shut down the connection: */
				Misc::formattedLogNote("Server: Client %s from %s closed connection",name.c_str(),clientAddress.c_str());
				clientState=Disconnect;
				}
			}
		
		if((eventTypeMask&Threads::EventDispatcher::Write)&&clientState!=Disconnect)
			{
			/* Write any pending data to the socket: */
			if(socket.writeToSocket()==0)
				{
				if(clientState!=Drain)
					{
					/* There is no more pending data; stop dispatching write events on the socket: */
					server->dispatcher.setIOEventListenerEventTypeMaskFromCallback(socketKey,Threads::EventDispatcher::Read);
					}
				else
					{
					/* Disconnect the client now: */
					clientState=Disconnect;
					}
				}
			}
		}
	catch(const std::runtime_error& err)
		{
		/* There was a fatal error; shut down the connection: */
		Misc::formattedLogWarning("Server: Disconnecting client %s from %s due to exception %s",name.c_str(),clientAddress.c_str(),err.what());
		if(clientState<Drain)
			clientState=Disconnect;
		}
	
	if(clientState==Disconnect)
		{
		/* Disconnect this client: */
		server->disconnect(this);
		
		/* Stop listening on the client's (now closed) socket: */
		return true;
		}
	else
		{
		/* Continue listening: */
		return false;
		}
	}

Server::Client::Client(Server* sServer,Comm::ListeningTCPSocket& listenSocket)
	:server(sServer),
	 socket(listenSocket),swapOnRead(false),
	 connected(false),udpConnected(false),
	 name("<unknown>"),
	 continuation(0)
	{
	/* Remember the client's socket address: */
	clientAddress=socket.getPeerAddress().getAddress();
	char clientSocketPort[6];
	clientAddress.push_back(':');
	clientAddress.append(Misc::print(socket.getPeerAddress().getPort(),clientSocketPort+5));
	
	/* Dispatch read and write events on the listening socket: */
	socketKey=server->dispatcher.addIOEventListener(socket.getFd(),Threads::EventDispatcher::ReadWrite,Threads::EventDispatcher::wrapMethod<Client,&Client::socketEvent>,this);
	
	/* Collect a sequence of random bytes for the authentication nonce and the UDP connection ticket: */
	Byte entropy[PasswordRequestMsg::nonceLength+sizeof(Misc::UInt32)];
	#if COLLABORATION_HAVE_GETENTROPY
	if(getentropy(entropy,(PasswordRequestMsg::nonceLength+sizeof(Misc::UInt32)))!=0)
		throw std::runtime_error("Server::Client::Client: Error retrieving entropy");
	#else
	for(size_t i=0;i<PasswordRequestMsg::nonceLength+sizeof(Misc::UInt32);++i)
		entropy[i]=Byte((unsigned int)(rand())&0xffU); // This is pretty terrible, but oh well
	#endif
	
	/* Extract the random authentication nonce and UDP connection ticket: */
	memcpy(nonce,entropy,PasswordRequestMsg::nonceLength);
	memcpy(&udpConnectionTicket,entropy+PasswordRequestMsg::nonceLength,sizeof(Misc::UInt32));
	
	/* Send a password request message to the client: */
	{
	MessageWriter passwordRequest(PasswordRequestMsg::createMessage());
	passwordRequest.write(Misc::UInt32(0x12345678));
	passwordRequest.write(Misc::UInt32(protocolVersion));
	passwordRequest.write(nonce,PasswordRequestMsg::nonceLength);
	socket.queueMessage(passwordRequest.getBuffer());
	}
	
	/* Create the client's plug-in protocol array: */
	plugins.reserve(server->plugins.size());
	for(size_t i=0;i<server->plugins.size();++i)
		plugins.push_back(0);
	
	/* Start the client communication protocol: */
	clientState=ReadingClientConnectRequest;
	}

Server::Client::~Client(void)
	{
	/* Delete all plug-in protocol clients in reverse order: */
	for(PluginClientList::reverse_iterator pIt=plugins.rbegin();pIt!=plugins.rend();++pIt)
		delete *pIt;
	
	/* Be nice and send a TCP FIN to the peer: */
	socket.shutdown(true,true);
	
	/* Delete a remaining message continuation object: */
	delete continuation;
	}

void Server::Client::setPlugin(unsigned int pluginIndex,PluginServer::Client* newPlugin)
	{
	/* Throw an exception if the plug-in protocol state structure is already set: */
	if(plugins[pluginIndex]!=0)
		{
		/* Delete the new structure: */
		delete newPlugin;
		throw std::runtime_error("Server::Client::setPlugin: Client plug-in state structure already set");
		}
	
	/* Set the plug-in protocol state structure: */
	plugins[pluginIndex]=newPlugin;
	}

void Server::Client::queueMessage(MessageBuffer* message)
	{
	/* Queue the message for sending and check if the socket was idle before: */
	if(socket.queueMessage(message)==0)
		{
		/* There is pending data; start dispatching write events on the socket: */
		server->dispatcher.setIOEventListenerEventTypeMaskFromCallback(socketKey,Threads::EventDispatcher::ReadWrite);
		}
	}

/***********************
Methods of class Server:
***********************/

void Server::disconnect(Server::Client* client)
	{
	if(client->connected)
		{
		/* Notify all plug-in protocols in which the client was participating in reverse order: */
		for(std::vector<unsigned int>::reverse_iterator piIt=client->pluginIndices.rbegin();piIt!=client->pluginIndices.rend();++piIt)
			plugins[*piIt]->clientDisconnected(client->id);
		}
	
	/* Remove the client from the client map: */
	clientMap.removeEntry(client->id);
	if(client->udpConnected)
		clientAddressMap.removeEntry(client->udpAddress);
	
	if(client->connected)
		{
		/* Remove the disconnected client from the list, and send a disconnect notification to all other clients: */
		{
		MessageWriter clientDisconnectNotification(ClientDisconnectNotificationMsg::createMessage());
		clientDisconnectNotification.write(ClientID(client->id));
		for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
			{
			if(*cIt!=client)
				{
				if((*cIt)->clientState>=Client::ReadingMessageID)
					{
					/* Send the disconnect notification: */
					(*cIt)->queueMessage(clientDisconnectNotification.getBuffer());
					}
				}
			else
				{
				/* Remove the client from the list: */
				*cIt=clients.back();
				clients.pop_back();
				--cIt;
				}
			}
		}
		}
	else
		{
		/* Remove the disconnected client from the list: */
		for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
			if(*cIt==client)
				{
				/* Remove the client from the list: */
				*cIt=clients.back();
				clients.pop_back();
				
				/* Stop looking: */
				break;
				}
		}
	
	
	/* Destroy the client object: */
	delete client;
	}

void Server::setPasswordCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* Retrieve the new password: */
	std::string newPassword(argumentBegin,argumentEnd);
	
	/* Change the session password: */
	if(newPassword.empty())
		std::cout<<"Server: Disabling session password"<<std::endl;
	else
		std::cout<<"Server: Changing session password to \""<<newPassword<<"\""<<std::endl;
	sessionPassword=newPassword;
	}

void Server::netstatCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* Print the UDP socket send queue size: */
	std::cout<<"Server::netstat: UDP socket send queue size: "<<udpSocket.getUnsent()<<std::endl;
	
	/* Print each connected client's TCP socket send and receive queue sizes: */
	for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		std::cout<<"Server::netstat: Client "<<(*cIt)->id<<" TCP socket send/receive queue sizes: "<<(*cIt)->socket.getUnsent()<<'/'<<(*cIt)->socket.getUnread()<<std::endl;
	}

void Server::listClientsCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* List all connected clients: */
	std::cout<<"Server::listClients: "<<clients.size()<<" clients connected"<<std::endl;
	for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		std::cout<<"\tClient "<<(*cIt)->id;
		
		if((*cIt)->connected)
			{
			std::cout<<": \""<<(*cIt)->name<<"\" from "<<(*cIt)->clientAddress<<", UDP";
			if(!(*cIt)->udpConnected)
				std::cout<<" not";
			std::cout<<" connected, participates in "<<(*cIt)->pluginIndices.size()<<" plug-in protocols"<<std::endl;
			for(std::vector<unsigned int>::iterator piIt=(*cIt)->pluginIndices.begin();piIt!=(*cIt)->pluginIndices.end();++piIt)
				{
				PluginServer* plugin=plugins[*piIt];
				unsigned int protocolMajor=plugin->getVersion()>>16;
				unsigned int protocolMinor=plugin->getVersion()&0xffffU;
				std::cout<<"\t\tPlug-in \""<<plugin->getName()<<"\", version "<<protocolMajor<<'.'<<protocolMinor<<std::endl;
				}
			}
		else
			std::cout<<" from "<<(*cIt)->clientAddress<<" is connecting"<<std::endl;
		}
	}

void Server::disconnectClientCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* Retrieve client to be disconnected: */
	unsigned int clientId=Misc::ValueCoder<unsigned int>::decode(argumentBegin,argumentEnd);
	Client* client=clientMap.getEntry(clientId).getDest();
	
	/* Stop listening on the client's socket, and disconnect the client: */
	dispatcher.removeIOEventListener(client->socketKey);
	disconnect(client);
	}

void Server::listPluginsCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* List all active plug-in protocols: */
	std::cout<<"Server::listPlugins: "<<plugins.size()<<" active plug-in protocols"<<std::endl;
	unsigned int pluginIndex=0;
	for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt,++pluginIndex)
		{
		/* Count the number of clients participating in this plug-in protocol: */
		unsigned int numParticipants=0;
		for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
			{
			for(std::vector<unsigned int>::iterator piIt=(*cIt)->pluginIndices.begin();piIt!=(*cIt)->pluginIndices.end();++piIt)
				if(*piIt==pluginIndex)
					++numParticipants;
			}
		
		unsigned int protocolMajor=(*pIt)->getVersion()>>16;
		unsigned int protocolMinor=(*pIt)->getVersion()&0xffffU;
		std::cout<<"\tPlug-in \""<<(*pIt)->getName()<<"\", version "<<protocolMajor<<'.'<<protocolMinor<<", "<<numParticipants<<" participating clients"<<std::endl;
		}
	}

void Server::loadPluginCommand(const char* argumentBegin,const char* argumentEnd)
	{
	const char* argPtr=argumentBegin;
	
	/* Get the plug-in protocol name and version number: */
	std::string protocolName=Misc::ValueCoder<std::string>::decode(argPtr,argumentEnd,&argPtr);
	while(argPtr!=argumentEnd&&isspace(*argPtr))
		++argPtr;
	unsigned int protocolVersion=Misc::ValueCoder<unsigned int>::decode(argPtr,argumentEnd,&argPtr);
	
	/* Load the protocol plug-in: */
	PluginServer* plugin=requestPluginProtocol(protocolName.c_str(),protocolVersion<<16);
	if(plugin!=0)
		{
		unsigned int protocolMajor=plugin->getVersion()>>16;
		unsigned int protocolMinor=plugin->getVersion()&0xffffU;
		std::cout<<"Loaded plug-in protocol \""<<plugin->getName()<<"\", version "<<protocolMajor<<'.'<<protocolMinor<<std::endl;
		}
	else
		throw std::runtime_error("Plug-in protocol not found");
	}

void Server::unloadPluginCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* Find the plug-in protocol of the given name: */
	std::string pluginName(argumentBegin,argumentEnd);
	unsigned int pluginIndex=0;
	PluginList::iterator pIt;
	for(pIt=plugins.begin();pIt!=plugins.end()&&(*pIt)->getName()!=pluginName;++pIt,++pluginIndex)
		;
	if(pIt==plugins.end())
		Misc::throwStdErr("Plug-in %s not found",pluginName.c_str());
	
	/* Count the number of clients participating in this plug-in protocol: */
	unsigned int numParticipants=0;
	for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		for(std::vector<unsigned int>::iterator piIt=(*cIt)->pluginIndices.begin();piIt!=(*cIt)->pluginIndices.end();++piIt)
			if(*piIt==pluginIndex)
				++numParticipants;
		}
	
	if(numParticipants==0)
		{
		/* Remove the plug-in protocol: */
		pluginLoader.destroyObject(*pIt);
		plugins.erase(pIt);
		}
	else
		Misc::throwStdErr("Plug-in %s still used by %u client(s)",pluginName.c_str(),numParticipants);
	}

void Server::quitCommand(const char* argumentBegin,const char* argumentEnd)
	{
	/* Shut down the event dispatcher to terminate the server: */
	dispatcher.stop();
	}

bool Server::stdinEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask)
	{
	bool result=false;
	
	if(eventTypeMask&Threads::EventDispatcher::Read)
		{
		/* Dispatch commands from stdin and stop listening if there is an error: */
		result=commandDispatcher.dispatchCommands(0);
		}
	
	return result;
	}

bool Server::commandPipeEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask)
	{
	bool result=false;
	
	if(eventTypeMask&Threads::EventDispatcher::Read)
		{
		/* Dispatch commands from the command pipe and stop listening if there is an error: */
		result=commandDispatcher.dispatchCommands(commandPipe);
		}
	
	return result;
	}

bool Server::listenSocketEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask)
	{
	try
		{
		/* Connect a new client: */
		Misc::SelfDestructPointer<Client> newClient(new Client(this,listenSocket));
		Misc::formattedLogNote("Server: Accepting incoming connection from %s",newClient->clientAddress.c_str());
		
		/* Assign a unique ID to the client: */
		do
			{
			++nextClientId;
			}
		while(nextClientId==0||clientMap.isEntry(nextClientId));
		newClient->id=nextClientId;
		
		/* Add the client to the client map: */
		clientMap.setEntry(ClientMap::Entry(newClient->id,newClient.getTarget()));
		
		clients.push_back(newClient.releaseTarget());
		}
	catch(const std::runtime_error& err)
		{
		Misc::formattedLogWarning("Server: Rejecting incoming connection due to exception %s",err.what());
		}
	
	/* Keep listening: */
	return false;
	}

bool Server::udpSocketEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask)
	{
	try
		{
		if(eventTypeMask&Threads::EventDispatcher::Read)
			{
			/* Get the next pending message: */
			UDPSocket::Address senderAddress;
			MessageReader message(udpSocket.readFromSocket(senderAddress));
			
			/* Check if the message contains at least a message ID: */
			if(message.getUnread()>=sizeof(MessageID))
				{
				/* Find the client who sent the message: */
				ClientAddressMap::Iterator cIt=clientAddressMap.findEntry(senderAddress);
				if(!cIt.isFinished())
					{
					/* Read the message ID and check if the message ID is valid: */
					message.setSwapOnRead(cIt->getDest()->swapOnRead);
					unsigned int messageId=message.read<MessageID>();
					const UDPMessageHandler& mh=udpMessageHandlers[messageId];
					if(messageId>=udpMessageHandlers.size()||mh.callback==0)
						{
						/* A bad message ID from a known and connected client is an error: */
						Misc::formattedLogWarning("Server::udpSocketEvent: Invalid message ID from client %u",cIt->getDest()->id);
						}
					
					/* Dispatch the message: */
					mh.callback(messageId,cIt->getDest()->id,message,mh.callbackUserData);
					}
				else
					{
					/* The message is not from a known and connected client; check if it's a UDP connect request: */
					MessageID swappedUDPConnectRequest(UDPConnectRequest);
					Misc::swapEndianness(swappedUDPConnectRequest); // We don't know the sender's endianness at this point, unfortunately
					unsigned int messageId=message.read<MessageID>();
					if((messageId==UDPConnectRequest||messageId==swappedUDPConnectRequest)&&message.getUnread()==UDPConnectRequestMsg::size)
						{
						/* Set the message's endianness: */
						message.setSwapOnRead(messageId==swappedUDPConnectRequest);
						
						/* Read the client ID and UDP connection ticket: */
						unsigned int clientId=message.read<ClientID>();
						Misc::UInt32 udpConnectionTicket=message.read<Misc::UInt32>();
						
						/* Check if the credentials are valid: */
						ClientMap::Iterator cIt=clientMap.findEntry(clientId);
						if(!cIt.isFinished()&&cIt->getDest()->udpConnectionTicket==udpConnectionTicket)
							{
							Client* client=cIt->getDest();
							
							#if 0
							std::string clientUDPAddress=senderAddress.getAddress().getHostname();
							clientUDPAddress.push_back(':');
							char portBuffer[6];
							clientUDPAddress.append(Misc::print(senderAddress.getPort(),portBuffer+5));
							Misc::formattedLogNote("Server::udpSocketEvent: UDP connect request from client %u from address %s",clientId,clientUDPAddress.c_str());
							#endif
							
							/* Message checks out; remember the client's UDP socket address and send a UDP connect reply: */
							client->udpAddress=senderAddress;
							client->udpConnected=true;
							clientAddressMap.setEntry(ClientAddressMap::Entry(senderAddress,client));
							
							{
							MessageWriter udpConnectReply(UDPConnectReplyMsg::createMessage());
							udpConnectReply.write(udpConnectionTicket);
							queueUDPMessage(senderAddress,udpConnectReply.getBuffer());
							}
							}
						}
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
		Misc::formattedLogError("Server::udpSocketEvent: Caught exception %s",err.what());
		}
	
	/* Keep listening: */
	return false;
	}

void Server::udpConnectRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message)
	{
	/* This is a duplicate message; check for correctness, and then reply with another UDP connect reply: */
	Client* client=clientMap.getEntry(clientId).getDest();
	if(message.getUnread()==UDPConnectRequestMsg::size&&message.read<ClientID>()==clientId&&message.read<Misc::UInt32>()==client->udpConnectionTicket)
		{
		/* Send the UDP connect reply: */
		MessageWriter udpConnectReply(UDPConnectReplyMsg::createMessage());
		udpConnectReply.write(client->udpConnectionTicket);
		queueUDPMessage(client->udpAddress,udpConnectReply.getBuffer());
		}
	}

MessageContinuation* Server::disconnectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Client* client=getClient(clientId);
	
	/* Disconnect the client: */
	Misc::formattedLogNote("Server: Client %s from %s requested disconnect",client->name.c_str(),client->clientAddress.c_str());
	client->clientState=Client::Disconnect;
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* Server::pingRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Client* client=getClient(clientId);
	
	/* Read the ping request message body (not doing clock synchronization yet): */
	Misc::SInt16 sequence=client->socket.read<Misc::SInt16>();
	client->socket.read<Misc::SInt64>();
	client->socket.read<Misc::SInt64>();
	
	/* Reply to the ping request: */
	{
	MessageWriter pingReply(PingMsg::createMessage(PingReply));
	Realtime::TimePointRealtime now;
	pingReply.write(sequence);
	pingReply.write(Misc::SInt64(now.tv_sec));
	pingReply.write(Misc::SInt64(now.tv_nsec));
	client->queueMessage(pingReply.getBuffer());
	}
	
	/* Done with message: */
	return 0;
	}

void Server::udpPingRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message)
	{
	/* Bail out if the message is the wrong size: */
	if(message.getUnread()!=PingMsg::size)
		{
		Misc::formattedLogError("Server::udpPingRequestCallback: Wrong-size PingRequest message");
		return;
		}
	
	Client* client=getClient(clientId);
	
	/* Read the ping request message body (not doing clock synchronization yet): */
	Misc::SInt16 sequence=message.read<Misc::SInt16>();
	message.read<Misc::SInt64>();
	message.read<Misc::SInt64>();
	
	#if 0
	// DEBUGGING
	std::string clientUDPAddress=client->udpAddress.getAddress().getHostname();
	clientUDPAddress.push_back(':');
	char portBuffer[6];
	clientUDPAddress.append(Misc::print(client->udpAddress.getPort(),portBuffer+5));
	Misc::formattedLogNote("Server::udpPingRequestCallback: Ping request from client %u from address %s",clientId,clientUDPAddress.c_str());
	#endif
	
	/* Reply to the ping request: */
	{
	MessageWriter pingReply(PingMsg::createMessage(PingReply));
	pingReply.write(sequence);
	Realtime::TimePointRealtime now;
	pingReply.write(Misc::SInt64(now.tv_sec));
	pingReply.write(Misc::SInt64(now.tv_nsec));
	queueUDPMessage(client->udpAddress,pingReply.getBuffer());
	}
	}

MessageContinuation* Server::nameChangeRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Client* client=getClient(clientId);
	
	/* Read the name change request message body: */
	std::string requestedName;
	charBufferToString(client->socket,NameChangeRequestMsg::nameLength,requestedName);
	
	bool ok=true;
	
	/* Check if the requested name is a non-empty valid UTF-8 encoded string: */
	if(ok)
		ok=!requestedName.empty()&&Misc::UTF8::isValid(requestedName.begin(),requestedName.end());
	
	if(ok)
		{
		/* Check if the requested client name is unique: */
		for(ClientList::iterator cIt=clients.begin();cIt!=clients.end()&&ok;++cIt)
			ok=(*cIt)->name!=requestedName;
		}
	
	if(ok)
		{
		/* Change the client's name: */
		Misc::formattedLogNote("Server::nameChangeRequestCallback: Client %u changed name from %s to %s",client->id,client->name.c_str(),requestedName.c_str());
		client->name=requestedName;
		
		/* Send name change notifications to all other clients: */
		{
		MessageWriter nameChangeNotification(NameChangeNotificationMsg::createMessage());
		nameChangeNotification.write(ClientID(client->id));
		stringToCharBuffer(client->name,nameChangeNotification,NameChangeNotificationMsg::nameLength);
		for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
			if(*cIt!=client&&(*cIt)->clientState>=Client::ReadingMessageID)
				{
				/* Send the name change notification: */
				(*cIt)->queueMessage(nameChangeNotification.getBuffer());
				}
		}
		}
	else
		{
		/* Deny the request: */
		Misc::formattedLogNote("Server::nameChangeRequestCallback: Client %u's request to change name from %s to %s was denied",client->id,client->name.c_str(),requestedName.c_str());
		}
	
	/* Send a name change reply to the client: */
	{
	MessageWriter nameChangeReply(NameChangeReplyMsg::createMessage());
	writeBool(ok,nameChangeReply);
	stringToCharBuffer(client->name,nameChangeReply,NameChangeReplyMsg::nameLength);
	client->queueMessage(nameChangeReply.getBuffer());
	}
	
	/* Done with message: */
	return 0;
	}

Server::Server(const Misc::ConfigurationFileSection& sServerConfig,int portId,const char* sName)
	:serverConfig(sServerConfig),
	 commandPipe(-1),commandPipeHolder(-1),
	 listenSocket(portId,5),
	 udpSocket(portId),
	 name(sName),
	 nextClientId(0),clientMap(17),clientAddressMap(17),
	 pluginLoader(COLLABORATION_PLUGINDIR "/" COLLABORATION_PLUGINSERVERDSONAMETEMPLATE)
	{
	/* Dispatch read events on stdin: */
	stdinKey=dispatcher.addIOEventListener(0,Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<Server,&Server::stdinEvent>,this);
	
	/* Check if there should be a command pipe: */
	if(serverConfig.hasTag("./commandPipeName"))
		{
		/* Open the command pipe: */
		std::string commandPipeName=serverConfig.retrieveString("./commandPipeName");
		commandPipe=open(commandPipeName.c_str(),O_RDONLY|O_NONBLOCK);
		if(commandPipe>=0)
			{
			/* Open an extra (non-writing) writer to hold the command pipe open between external writers: */
			commandPipeHolder=open(commandPipeName.c_str(),O_WRONLY|O_NONBLOCK);
			}
		if(commandPipeHolder>=0)
			{
			/* Dispatch read events on stdin: */
			commandPipeKey=dispatcher.addIOEventListener(commandPipe,Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<Server,&Server::commandPipeEvent>,this);
			}
		else
			{
			int error=errno;
			Misc::formattedUserError("Server: Unable to listen for commands on pipe %s due to error %d (%s)",commandPipeName.c_str(),error,strerror(error));
			if(commandPipe>=0)
				close(commandPipe);
			commandPipe=-1;
			}
		}
	
	/* Register console command handlers: */
	commandDispatcher.addCommandCallback("setPassword",Misc::CommandDispatcher::wrapMethod<Server,&Server::setPasswordCommand>,this,"[<new session password>]","Changes the server's session password; empty password disables password check");
	commandDispatcher.addCommandCallback("netstat",Misc::CommandDispatcher::wrapMethod<Server,&Server::netstatCommand>,this,0,"Displays TCP and UDP socket statistics");
	commandDispatcher.addCommandCallback("listClients",Misc::CommandDispatcher::wrapMethod<Server,&Server::listClientsCommand>,this,0,"Lists currently connected clients");
	commandDispatcher.addCommandCallback("disconnectClient",Misc::CommandDispatcher::wrapMethod<Server,&Server::disconnectClientCommand>,this,"<client ID>","Disconnects the client of the given ID");
	commandDispatcher.addCommandCallback("listPlugins",Misc::CommandDispatcher::wrapMethod<Server,&Server::listPluginsCommand>,this,0,"Lists loaded plug-in protocols");
	commandDispatcher.addCommandCallback("loadPlugin",Misc::CommandDispatcher::wrapMethod<Server,&Server::loadPluginCommand>,this,"<protocol name> <protocol version>","Loads the plug-in protocol of the given name and major version number");
	commandDispatcher.addCommandCallback("unloadPlugin",Misc::CommandDispatcher::wrapMethod<Server,&Server::unloadPluginCommand>,this,"<protocol name>","Unloads the plug-in protocol of the given name");
	commandDispatcher.addCommandCallback("quit",Misc::CommandDispatcher::wrapMethod<Server,&Server::quitCommand>,this,0,"Shuts down the server");
	
	/* Make the listening socket non-blocking: */
	listenSocket.setBlocking(false);
	
	/* Dispatch read events on the listening socket: */
	listenSocketKey=dispatcher.addIOEventListener(listenSocket.getFd(),Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<Server,&Server::listenSocketEvent>,this);
	
	/* Dispatch read events on the UDP socket: */
	udpSocketKey=dispatcher.addIOEventListener(udpSocket.getFd(),Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<Server,&Server::udpSocketEvent>,this);
	
	/* Register message handlers for core protocol messages: */
	setUDPMessageHandler(UDPConnectRequest,wrapMethod<Server,&Server::udpConnectRequestCallback>,this);
	setMessageHandler(DisconnectRequest,wrapMethod<Server,&Server::disconnectRequestCallback>,this,0);
	setMessageHandler(PingRequest,wrapMethod<Server,&Server::pingRequestCallback>,this,PingMsg::size);
	setUDPMessageHandler(PingRequest,wrapMethod<Server,&Server::udpPingRequestCallback>,this);
	setMessageHandler(NameChangeRequest,wrapMethod<Server,&Server::nameChangeRequestCallback>,this,NameChangeRequestMsg::size);
	
	/* Initialize the plug-in protocol list: */
	clientMessageBase=NumClientMessages;
	serverMessageBase=NumServerMessages;
	}

Server::~Server(void)
	{
	/* Forcefully disconnect all remaining clients: */
	for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		delete *cIt;
	
	/* Shut down all plug-in protocols in reverse order: */
	for(PluginList::reverse_iterator pIt=plugins.rbegin();pIt!=plugins.rend();++pIt)
		pluginLoader.destroyObject(*pIt);
	
	/* Close the optional command pipe: */
	if(commandPipeHolder>=0)
		{
		close(commandPipeHolder);
		close(commandPipe);
		}
	}

void Server::setPassword(const char* newSessionPassword)
	{
	/* Replace the current session password: */
	if(newSessionPassword!=0)
		sessionPassword=newSessionPassword;
	else
		sessionPassword.clear();
	}

void Server::queueUDPMessage(const UDPSocket::Address& receiverAddress,MessageBuffer* message)
	{
	/* Queue the message for sending and check if the socket was idle before: */
	if(udpSocket.queueMessage(receiverAddress,message)==0)
		{
		/* There is pending data; start dispatching write events on the UDP socket: */
		dispatcher.setIOEventListenerEventTypeMaskFromCallback(udpSocketKey,Threads::EventDispatcher::Read|Threads::EventDispatcher::Write);
		}
	}

void Server::setMessageHandler(unsigned int messageId,Server::MessageHandlerCallback callback,void* callbackUserData,size_t minUnread)
	{
	/* Ensure that there are enough entries in the message handler array: */
	while(messageHandlers.size()<messageId)
		messageHandlers.push_back(MessageHandler(0,0,0));
	
	/* Add or insert a new message handler: */
	if(messageId<messageHandlers.size())
		messageHandlers[messageId]=MessageHandler(callback,callbackUserData,minUnread);
	else
		messageHandlers.push_back(MessageHandler(callback,callbackUserData,minUnread));
	}

void Server::setUDPMessageHandler(unsigned int messageId,Server::UDPMessageHandlerCallback callback,void* callbackUserData)
	{
	/* Ensure that there are enough entries in the message handler array: */
	while(udpMessageHandlers.size()<messageId)
		udpMessageHandlers.push_back(UDPMessageHandler(0,0));
	
	/* Add or insert a new message handler: */
	if(messageId<udpMessageHandlers.size())
		udpMessageHandlers[messageId]=UDPMessageHandler(callback,callbackUserData);
	else
		udpMessageHandlers.push_back(UDPMessageHandler(callback,callbackUserData));
	}

PluginServer* Server::requestPluginProtocol(const char* protocolName,unsigned int protocolVersion)
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
	PluginServer* result=0;
	try
		{
		/* Load the plug-in protocol object: */
		PluginServer* newProtocol=pluginLoader.createObject(protocolName,protocolMajor,this);
		
		/* Add the plug-in protocol to the list: */
		addPluginProtocol(newProtocol);
		
		/* Return the new protocol: */
		result=newProtocol;
		}
	catch(std::runtime_error& err)
		{
		/* Print an error message and return 0: */
		Misc::formattedUserError("Server::requestPluginProtocol: Unable to load plug-in %s, version %u.x, due to exception %s",protocolName,protocolMajor,err.what());
		}
	
	return result;
	}

void Server::addPluginProtocol(PluginServer* protocol)
	{
	/* Add the new protocol to the plug-in list: */
	protocol->setIndex(plugins.size());
	plugins.push_back(protocol);
	
	/* Set the new protocol's base message IDs: */
	protocol->setMessageBases(clientMessageBase,serverMessageBase);
	clientMessageBase+=protocol->getNumClientMessages();
	serverMessageBase+=protocol->getNumServerMessages();
	
	/* Add room for the plug-in protocol's client representation to all connected clients: */
	for(ClientList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		(*cIt)->plugins.push_back(0);
	
	/* Start the plug-in protocol: */
	protocol->start();
	}

PluginServer* Server::findPluginProtocol(const char* protocolName,unsigned int protocolVersion)
	{
	/* Search the list of active plug-in protocols: */
	for(PluginList::iterator pIt=plugins.begin();pIt!=plugins.end();++pIt)
		if((*pIt)->getVersion()==protocolVersion&&strcmp((*pIt)->getName(),protocolName)==0)
			return *pIt;
	
	return 0;
	}

Misc::ConfigurationFileSection Server::getPluginConfig(PluginServer* plugin)
	{
	/* Create a configuration section name from the plug-in's name and major version number: */
	std::string sectionName=plugin->getName();
	sectionName.push_back('-');
	char buffer[11];
	sectionName.append(Misc::print(plugin->getVersion()>>16,buffer+10));
	
	/* Return the configuration section: */
	return serverConfig.getSection(sectionName.c_str());
	}

void Server::run(void)
	{
	Misc::formattedLogNote("Server::run: Listening for incoming connections on port %d",listenSocket.getPortId());
	
	/* Stop the server on SIGINT or SIGTERM: */
	dispatcher.stopOnSignals();
	
	/* Dispatch events until shut down: */
	dispatcher.dispatchEvents();
	}

void Server::shutdown(void)
	{
	Misc::logNote("Server:shutdown: Shutting down server");
	
	/* Shut down the dispatcher: */
	dispatcher.stop();
	}
