/***********************************************************************
VruiAgoraServer - Server for real-time audio chat plug-in protocol for
clients running inside a Vrui environment.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/VruiAgoraServer.h>

#include <stdexcept>
#include <Misc/Utility.h>
#include <Misc/MessageLogger.h>
#include <Misc/Marshaller.h>

#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/Server.h>
#include <Collaboration2/Plugins/VruiCoreServer.h>

/********************************
Methods of class VruiAgoraServer:
********************************/

MessageContinuation* VruiAgoraServer::connectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the encoder parameters message: */
	Client* agoraClient=new Client;
	agoraClient->sampleRate=socket.read<Misc::UInt32>();
	agoraClient->numPacketFrames=socket.read<Misc::UInt32>();
	Misc::read(socket,agoraClient->mouthPos);
	agoraClient->receivers.reserve(clients.size());
	
	/* Notify all other Agora clients of the new client's encoder parameters: */
	{
	MessageWriter connectNotification(ConnectNotificationMsg::createMessage(serverMessageBase));
	connectNotification.write(ClientID(clientId));
	connectNotification.write(Misc::UInt32(agoraClient->sampleRate));
	connectNotification.write(Misc::UInt32(agoraClient->numPacketFrames));
	Misc::write(agoraClient->mouthPos,connectNotification);
	VruiCoreServer::Client* vcClient=vruiCore->getClient(clientId);
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		/* Retrieve the other client's state structure: */
		Server::Client* otherClient=server->getClient(*cIt);
		
		/* Tell the other client about the new client: */
		otherClient->queueMessage(connectNotification.getBuffer());
		
		/* Tell the new client about the other client: */
		Client* otherAgoraClient=otherClient->getPlugin<Client>(pluginIndex);
		{
		MessageWriter connectNotification2(ConnectNotificationMsg::createMessage(serverMessageBase));
		connectNotification2.write(ClientID(*cIt));
		connectNotification2.write(Misc::UInt32(otherAgoraClient->sampleRate));
		connectNotification2.write(Misc::UInt32(otherAgoraClient->numPacketFrames));
		Misc::write(otherAgoraClient->mouthPos,connectNotification2);
		client->queueMessage(connectNotification2.getBuffer());
		}
		
		/* Let the new client and the other client receive audio from each other if they are not in the same physical environment: */
		if(!vcClient->samePhysicalEnvironment(*vruiCore->getClient(*cIt)))
			{
			// DEBUGGING
			Misc::formattedLogNote("VruiAgora: Routing audio between clients %u and %u",clientId,*cIt);
			
			agoraClient->receivers.push_back(*cIt);
			otherAgoraClient->receivers.push_back(clientId);
			}
		}
	}
	
	/* Add the client to the list: */
	addClientToList(clientId);
	
	/* Set the client's state structure: */
	client->setPlugin(pluginIndex,agoraClient);
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiAgoraServer::audioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		unsigned int destClientId;
		MessageWriter audioPacketReply;
		
		/* Constructors and destructors: */
		Cont(unsigned int sDestClientId,unsigned int serverMessageBase,size_t audioPacketLen)
			:destClientId(sDestClientId),
			 audioPacketReply(AudioPacketMsg::createMessage(serverMessageBase+AudioPacketReply,audioPacketLen))
			{
			}
		};
	
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the message header: */
		unsigned int destClientId=socket.read<ClientID>();
		Sequence sequenceNumber=socket.read<Sequence>();
		size_t audioPacketLen=socket.read<Misc::UInt16>();
		
		/* Create a message continuation with a reply message: */
		cont=new Cont(destClientId,serverMessageBase,audioPacketLen);
		
		/* Fill in the reply message header: */
		cont->audioPacketReply.write(ClientID(clientId));
		cont->audioPacketReply.write(sequenceNumber);
		cont->audioPacketReply.write(Misc::UInt16(audioPacketLen));
		}
	
	/* Read a chunk of the audio packet: */
	size_t readSize=Misc::min(socket.getUnread(),cont->audioPacketReply.getSpace());
	socket.read(cont->audioPacketReply.getWritePtr(),readSize);
	cont->audioPacketReply.advanceWritePtr(readSize);
	
	/* Check if the message was read completely: */
	if(cont->audioPacketReply.eof())
		{
		/* Check if the source client is currently connected (even TCP audio packets may arrive early): */
		Client* vaClient=server->testAndGetPlugin<Client>(clientId,pluginIndex);
		if(vaClient!=0)
			{
			/* Check if the message is a broadcast message: */
			if(cont->destClientId==0)
				{
				/* Send the message to all clients receiving audio data from the source client: */
				for(ClientIDList::iterator cIt=vaClient->receivers.begin();cIt!=vaClient->receivers.end();++cIt)
					server->queueUDPMessageFallback(*cIt,cont->audioPacketReply.getBuffer());
				}
			else
				{
				/* Send the message to the destination client: */
				for(ClientIDList::iterator cIt=vaClient->receivers.begin();cIt!=vaClient->receivers.end();++cIt)
					if(*cIt==cont->destClientId)
						{
						server->queueUDPMessageFallback(*cIt,cont->audioPacketReply.getBuffer());
						break;
						}
				}
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void VruiAgoraServer::udpAudioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message)
	{
	/* Give the message a basic smell test: */
	if(message.getUnread()<AudioPacketMsg::size)
		throw std::runtime_error("VruiAgoraServer: Truncated audio packet message");
	
	/* Read the message header: */
	unsigned int destClientId=message.read<ClientID>();
	Sequence sequenceNumber=message.read<Sequence>();
	size_t audioPacketLen=message.read<Misc::UInt16>();
	
	/* Check if the message is complete: */
	if(message.getUnread()!=audioPacketLen)
		throw std::runtime_error("VruiAgoraServer: Wrong-size audio packet message");
	
	/* Re-write the message header to set the proper message ID, exchange destination for source client, and fix potential endianness difference: */
	message.getBuffer()->setMessageId(serverMessageBase+AudioPacketReply);
	{
	MessageWriter writer(message.getBuffer()->ref()); // Add another reference for the writer
	writer.write(ClientID(clientId));
	writer.write(sequenceNumber);
	writer.write(Misc::UInt16(audioPacketLen));
	}
	
	/* Check if the source client is currently connected (audio packets may arrive early or late): */
	Client* vaClient=server->testAndGetPlugin<Client>(clientId,pluginIndex);
	if(vaClient!=0)
		{
		/* Check if the message is a broadcast message: */
		if(destClientId==0)
			{
			/* Send the message to all clients receiving audio data from the source client: */
			for(ClientIDList::iterator cIt=vaClient->receivers.begin();cIt!=vaClient->receivers.end();++cIt)
				server->queueUDPMessageFallback(*cIt,message.getBuffer());
			}
		else
			{
			/* Send the message to the destination client: */
			for(ClientIDList::iterator cIt=vaClient->receivers.begin();cIt!=vaClient->receivers.end();++cIt)
				if(*cIt==destClientId)
					{
					server->queueUDPMessageFallback(*cIt,message.getBuffer());
					break;
					}
			}
		}
	}

MessageContinuation* VruiAgoraServer::muteClientRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the message: */
	unsigned int muteClientId=socket.read<ClientID>();
	Client* muteClient=server->testAndGetPlugin<Client>(muteClientId,pluginIndex);
	bool mute=socket.read<Bool>()!=Bool(0);
	bool notifyClient=socket.read<Bool>()!=Bool(0);
	
	/* Mute or unmute the other client for the requesting client: */
	if(mute)
		{
		/* Remove the requesting client from the muted client's distribution list: */
		for(ClientIDList::iterator rIt=muteClient->receivers.begin();rIt!=muteClient->receivers.end();++rIt)
			if(*rIt==clientId)
				{
				/* Remove the requesting client from the receivers list: */
				*rIt=muteClient->receivers.back();
				muteClient->receivers.pop_back();
				
				/* Stop looking: */
				break;
				}
		}
	else
		{
		/* Check if the requesting client is already in the muted client's distribution list: */
		bool found=false;
		for(ClientIDList::iterator rIt=muteClient->receivers.begin();rIt!=muteClient->receivers.end()&&!found;++rIt)
			found=*rIt==clientId;
		if(!found)
			{
			/* Add the requesting client to the muted client's distribution list: */
			muteClient->receivers.push_back(clientId);
			}
		}
	
	if(notifyClient)
		{
		// DEBUGGING
		Misc::formattedLogNote("VruiAgora: Client %u requested muting client %u and is being a bitch about it",clientId,muteClientId);
		
		/* Send a notification message to the muted client: */
		MessageWriter muteClientNotification(MuteClientNotificationMsg::createMessage(serverMessageBase));
		muteClientNotification.write(ClientID(clientId));
		muteClientNotification.write(Bool(mute?1:0));
		server->queueMessage(muteClientId,muteClientNotification.getBuffer());
		}
	else
		{
		// DEBUGGING
		Misc::formattedLogNote("VruiAgora: Client %u requested muting client %u",clientId,muteClientId);
		}
	
	/* Done with message: */
	return 0;
	}

VruiAgoraServer::VruiAgoraServer(Server* sServer)
	:PluginServer(sServer),
	 vruiCore(VruiCoreServer::requestServer(server))
	{
	}

VruiAgoraServer::~VruiAgoraServer(void)
	{
	}

const char* VruiAgoraServer::getName(void) const
	{
	return protocolName;
	}

unsigned int VruiAgoraServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int VruiAgoraServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int VruiAgoraServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void VruiAgoraServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+ConnectRequest,Server::wrapMethod<VruiAgoraServer,&VruiAgoraServer::connectRequestCallback>,this,ConnectRequestMsg::size);
	server->setMessageHandler(clientMessageBase+AudioPacketRequest,Server::wrapMethod<VruiAgoraServer,&VruiAgoraServer::audioPacketRequestCallback>,this,AudioPacketMsg::size);
	server->setUDPMessageHandler(clientMessageBase+AudioPacketRequest,Server::wrapMethod<VruiAgoraServer,&VruiAgoraServer::udpAudioPacketRequestCallback>,this);
	server->setMessageHandler(clientMessageBase+MuteClientRequest,Server::wrapMethod<VruiAgoraServer,&VruiAgoraServer::muteClientRequestCallback>,this,MuteClientRequestMsg::size);
	}

void VruiAgoraServer::start(void)
	{
	}

void VruiAgoraServer::clientConnected(unsigned int clientId)
	{
	/* Don't do anything; connection happens when the new client sends its encoder parameters */
	}

void VruiAgoraServer::clientDisconnected(unsigned int clientId)
	{
	/* Remove the disconnected client from the client list, and from the receiver lists of all other clients: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		if(*cIt!=clientId)
			{
			/* Get the other client's VruiAgora state object: */
			Client* vaClient=server->getClient(*cIt)->getPlugin<Client>(pluginIndex);
			
			/* Remove the disconnected client from the other client's receiver list: */
			for(ClientIDList::iterator rIt=vaClient->receivers.begin();rIt!=vaClient->receivers.end();++rIt)
				if(*rIt==clientId)
					{
					/* Remove the disconnected client from the receivers list: */
					*rIt=vaClient->receivers.back();
					vaClient->receivers.pop_back();
					
					/* Stop looking: */
					break;
					}
			}
		else
			{
			/* Remove the disconnected client from the list: */
			*cIt=clients.back();
			clients.pop_back();
			--cIt;
			}
		}
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new VruiAgoraServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}
