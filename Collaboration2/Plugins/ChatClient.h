/***********************************************************************
ChatClient - Client for text chat plug-in protocol.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_CHATCLIENT_INCLUDED
#define PLUGINS_CHATCLIENT_INCLUDED

#include <Threads/EventDispatcher.h>

#include <Collaboration2/PluginClient.h>
#include <Collaboration2/Plugins/ChatProtocol.h>

/* Forward declarations: */
class MessageContinuation;

class ChatClient:public PluginClient,public ChatProtocol
	{
	/* Elements: */
	private:
	Threads::EventDispatcher::ListenerKey inputKey; // Key for the input listener
	unsigned int destClientId; // The destination client for the current outgoing message, or 0 for broadcast messages
	std::string outMessage; // The outgoing message currently being read
	
	/* Private methods: */
	bool inputCallback(Threads::EventDispatcher::ListenerKey listenerKey,int eventType); // Callback when console input is available
	MessageContinuation* messageReplyCallback(unsigned int messageId,MessageContinuation* continuation); // Callback when a message arrives
	
	/* Constructors and destructors: */
	public:
	ChatClient(Client* sClient);
	virtual ~ChatClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	};

#endif
