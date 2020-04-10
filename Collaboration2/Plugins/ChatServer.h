/***********************************************************************
ChatServer - Server for text chat plug-in protocol.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_CHATSERVER_INCLUDED
#define PLUGINS_CHATSERVER_INCLUDED

#include <Collaboration2/PluginServer.h>
#include <Collaboration2/Plugins/ChatProtocol.h>

/* Forward declarations: */
class MessageContinuation;

class ChatServer:public PluginServer,public ChatProtocol
	{
	/* Private methods: */
	private:
	MessageContinuation* messageRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	
	/* Constructors and destructors: */
	public:
	ChatServer(Server* sServer);
	virtual ~ChatServer(void);
	
	/* Methods from class PluginServer: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	};

#endif
