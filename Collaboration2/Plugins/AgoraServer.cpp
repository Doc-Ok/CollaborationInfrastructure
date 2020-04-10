/***********************************************************************
AgoraServer - Server for real-time audio chat plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/AgoraServer.h>

#include <Misc/Utility.h>
#include <Misc/MessageLogger.h>

#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/Server.h>

/****************************
Methods of class AgoraServer:
****************************/

MessageContinuation* AgoraServer::connectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the encoder parameters message: */
	Client* agoraClient=new Client;
	agoraClient->sampleRate=socket.read<Misc::UInt32>();
	agoraClient->numPacketFrames=socket.read<Misc::UInt32>();
	
	/* Notify all other Agora clients of the new client's encoder parameters: */
	{
	MessageWriter connectNotification(ConnectNotificationMsg::createMessage(serverMessageBase));
	connectNotification.write(ClientID(clientId));
	connectNotification.write(Misc::UInt32(agoraClient->sampleRate));
	connectNotification.write(Misc::UInt32(agoraClient->numPacketFrames));
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
		client->queueMessage(connectNotification2.getBuffer());
		}
		}
	}
	
	/* Add the client to the list: */
	addClientToList(clientId);
	
	/* Set the client's state structure: */
	client->setPlugin(pluginIndex,agoraClient);
	
	// DEBUGGING
	Misc::formattedLogNote("AgoraServer: New client with sample rate %u, packet size %u",agoraClient->sampleRate,agoraClient->numPacketFrames);
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* AgoraServer::audioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
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
		/* Check if the message is a broadcast message: */
		if(cont->destClientId==0)
			{
			/* Send the message to all clients except the source: */
			broadcastUDPMessageFallback(clientId,cont->audioPacketReply.getBuffer());
			}
		else
			{
			/* Send the message to the destination client: */
			sendUDPMessageFallback(cont->destClientId,cont->audioPacketReply.getBuffer());
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void AgoraServer::udpAudioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message)
	{
	/* Give the message a basic smell test: */
	if(message.getUnread()<AudioPacketMsg::size)
		throw std::runtime_error("AgoraServer: Truncated audio packet message");
	
	/* Read the message header: */
	unsigned int destClientId=message.read<ClientID>();
	Sequence sequenceNumber=message.read<Sequence>();
	size_t audioPacketLen=message.read<Misc::UInt16>();
	
	/* Check if the message is complete: */
	if(message.getUnread()!=audioPacketLen)
		throw std::runtime_error("AgoraServer: Wrong-size audio packet message");
	
	/* Re-write the message header to set the proper message ID, exchange destination for source client, and fix potential endianness difference: */
	message.getBuffer()->setMessageId(serverMessageBase+AudioPacketReply);
	{
	MessageWriter writer(message.getBuffer()->ref()); // Add another reference for the writer
	writer.write(ClientID(clientId));
	writer.write(sequenceNumber);
	writer.write(Misc::UInt16(audioPacketLen));
	}
	
	/* Check if the message is a broadcast message: */
	if(destClientId==0)
		{
		/* Send the message to all clients except the source: */
		broadcastUDPMessageFallback(clientId,message.getBuffer());
		}
	else
		{
		/* Send the message to the destination client: */
		sendUDPMessageFallback(destClientId,message.getBuffer());
		}
	}

AgoraServer::AgoraServer(Server* sServer)
	:PluginServer(sServer)
	{
	}

AgoraServer::~AgoraServer(void)
	{
	}

const char* AgoraServer::getName(void) const
	{
	return protocolName;
	}

unsigned int AgoraServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int AgoraServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int AgoraServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void AgoraServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+ConnectRequest,Server::wrapMethod<AgoraServer,&AgoraServer::connectRequestCallback>,this,ConnectRequestMsg::size);
	server->setMessageHandler(clientMessageBase+AudioPacketRequest,Server::wrapMethod<AgoraServer,&AgoraServer::audioPacketRequestCallback>,this,AudioPacketMsg::size);
	server->setUDPMessageHandler(clientMessageBase+AudioPacketRequest,Server::wrapMethod<AgoraServer,&AgoraServer::udpAudioPacketRequestCallback>,this);
	}

void AgoraServer::start(void)
	{
	}

void AgoraServer::clientConnected(unsigned int clientId)
	{
	/* Don't do anything; connection happens when the new client sends its encoder parameters */
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new AgoraServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}
