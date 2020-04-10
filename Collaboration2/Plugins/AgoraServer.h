/***********************************************************************
AgoraServer - Server for real-time audio chat plug-in protocol.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_AGORASERVER_INCLUDED
#define PLUGINS_AGORASERVER_INCLUDED

#include <Collaboration2/PluginServer.h>
#include <Collaboration2/Plugins/AgoraProtocol.h>

/* Forward declarations: */
class MessageReader;
class MessageContinuation;

class AgoraServer:public PluginServer,public AgoraProtocol
	{
	/* Embedded classes: */
	private:
	class Client:public PluginServer::Client // Class representing a client participating in the Agora protocol
		{
		friend class AgoraServer;
		
		/* Elements: */
		private:
		unsigned int sampleRate; // The client's capture sample rate
		unsigned int numPacketFrames; // The client's audio packet size in frames
		};
	
	/* Private methods: */
	MessageContinuation* connectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* audioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	void udpAudioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message);
	
	/* Constructors and destructors: */
	public:
	AgoraServer(Server* sServer);
	virtual ~AgoraServer(void);
	
	/* Methods from class PluginServer: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	};

#endif
