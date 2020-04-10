/***********************************************************************
ChatClient - Client for text chat plug-in protocol.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/ChatClient.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <Misc/Utility.h>
#include <Misc/MessageLogger.h>
#include <Threads/EventDispatcher.h>

#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/Client.h>

/***************************
Methods of class ChatClient:
***************************/

bool ChatClient::inputCallback(Threads::EventDispatcher::ListenerKey listenerKey,int eventType)
	{
	bool result=false;
	
	/* Read input from stdin: */
	char buffer[1024];
	ssize_t readSize=read(0,buffer,sizeof(buffer));
	if(readSize>0)
		{
		/* Process all read characters: */
		char* bPtr=buffer;
		char* bufferEnd=buffer+readSize;
		while(bPtr!=bufferEnd)
			{
			/* Append characters to the outgoing message until the next newline: */
			for(;bPtr!=bufferEnd&&*bPtr!='\n';++bPtr)
				outMessage.push_back(*bPtr);
			
			/* Check if a line was completed: */
			if(bPtr!=bufferEnd)
				{
				{
				/* Send the chat message to the server: */
				MessageWriter messageRequest(MessageRequestMsg::createMessage(clientMessageBase,outMessage.length()));
				messageRequest.write(ClientID(0));
				messageRequest.write(Misc::UInt16(outMessage.length()));
				stringToCharBuffer(outMessage,messageRequest,outMessage.length());
				client->queueMessage(messageRequest.getBuffer());
				}
				
				/* Start another chat message: */
				outMessage.clear();
				
				/* Skip the newline: */
				++bPtr;
				}
			}
		}
	else if(readSize==0)
		{
		/* Console user hung up: */
		Misc::logWarning("ChatClient: Console was closed");
		result=true;
		}
	else
		{
		Misc::formattedLogWarning("ChatClient: Error %d (%s) while reading from console",errno,strerror(errno));
		result=true;
		}
	
	return result;
	}

MessageContinuation* ChatClient::messageReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		unsigned int sourceClientId;
		bool privateMessage;
		std::string message;
		size_t unread;
		};
	
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the message header: */
		cont=new Cont;
		cont->sourceClientId=socket.read<ClientID>();
		cont->privateMessage=readBool(socket);
		cont->unread=socket.read<Misc::UInt16>();
		cont->message.reserve(cont->unread);
		}
	
	/* Read a chunk of the message body: */
	size_t readSize=Misc::min(socket.getUnread()/sizeof(Char),cont->unread);
	charBufferToString(socket,readSize,cont->message);
	cont->unread-=readSize;
	
	/* Check if the message has been completely read: */
	if(cont->unread==0)
		{
		/* Print the message: */
		Client::RemoteClient* sourceClient=client->getRemoteClient(cont->sourceClientId);
		if(cont->privateMessage)
			Misc::formattedUserNote("ChatClient: Private message from %s: %s",sourceClient->getName().c_str(),cont->message.c_str());
		else
			Misc::formattedUserNote("ChatClient: %s: %s",sourceClient->getName().c_str(),cont->message.c_str());
		
		/* Done with the message: */
		delete cont;
		return 0;
		}
	else
		{
		/* Keep reading the message: */
		return cont;
		}
	}

ChatClient::ChatClient(Client* sClient)
	:PluginClient(sClient)
	{
	/* Register a callback to read from stdin: */
	inputKey=client->getDispatcher().addIOEventListener(0,Threads::EventDispatcher::Read,Threads::EventDispatcher::wrapMethod<ChatClient,&ChatClient::inputCallback>,this);
	}

ChatClient::~ChatClient(void)
	{
	}

const char* ChatClient::getName(void) const
	{
	return protocolName;
	}

unsigned int ChatClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int ChatClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int ChatClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void ChatClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	client->setTCPMessageHandler(serverMessageBase+MessageReply,Client::wrapMethod<ChatClient,&ChatClient::messageReplyCallback>,this,MessageReplyMsg::size);
	}

void ChatClient::start(void)
	{
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginClient* createObject(PluginClientLoader& objectLoader,Client* client)
	{
	return new ChatClient(client);
	}

void destroyObject(PluginClient* object)
	{
	delete object;
	}

}
