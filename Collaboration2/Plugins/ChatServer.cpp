/***********************************************************************
ChatServer - Server for text chat plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/ChatServer.h>

#include <Misc/Utility.h>

#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/Server.h>

/***************************
Methods of class ChatServer:
***************************/

MessageContinuation* ChatServer::messageRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		unsigned int destClientId;
		MessageWriter messageReply;
		
		/* Constructors and destructors: */
		Cont(unsigned int sDestClientId,unsigned int serverMessageBase,size_t messageLen)
			:destClientId(sDestClientId),
			 messageReply(MessageReplyMsg::createMessage(serverMessageBase,messageLen))
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
		size_t messageLen=socket.read<Misc::UInt16>();
		
		/* Create a message continuation with a reply message: */
		cont=new Cont(destClientId,serverMessageBase,messageLen*sizeof(Char));
		
		/* Fill in the reply message header: */
		cont->messageReply.write(ClientID(clientId));
		writeBool(destClientId!=0,cont->messageReply);
		cont->messageReply.write(Misc::UInt16(messageLen));
		}
	
	/* Read a chunk of the message: */
	size_t readSize=Misc::min(socket.getUnread(),cont->messageReply.getSpace());
	socket.readRaw(cont->messageReply.getWritePtr(),readSize);
	cont->messageReply.advanceWritePtr(readSize);
	
	/* Check if the message was read completely: */
	if(cont->messageReply.eof())
		{
		/* Check if the message is a broadcast message: */
		if(cont->destClientId==0)
			{
			/* Send the message to all clients except the source: */
			broadcastMessage(clientId,cont->messageReply.getBuffer());
			}
		else
			{
			/* Send the message to the destination client: */
			sendMessage(cont->destClientId,cont->messageReply.getBuffer());
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

ChatServer::ChatServer(Server* sServer)
	:PluginServer(sServer)
	{
	}

ChatServer::~ChatServer(void)
	{
	}

const char* ChatServer::getName(void) const
	{
	return protocolName;
	}

unsigned int ChatServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int ChatServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int ChatServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void ChatServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+MessageRequest,Server::wrapMethod<ChatServer,&ChatServer::messageRequestCallback>,this,MessageRequestMsg::size);
	}

void ChatServer::start(void)
	{
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new ChatServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}
