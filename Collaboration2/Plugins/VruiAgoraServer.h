/***********************************************************************
VruiAgoraServer - Server for real-time audio chat plug-in protocol for
clients running inside a Vrui environment.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_VRUIAGORASERVER_INCLUDED
#define PLUGINS_VRUIAGORASERVER_INCLUDED

#include <Collaboration2/PluginServer.h>
#include <Collaboration2/Plugins/VruiAgoraProtocol.h>

/* Forward declarations: */
class MessageReader;
class MessageContinuation;
class VruiCoreServer;

class VruiAgoraServer:public PluginServer,public VruiAgoraProtocol
	{
	/* Embedded classes: */
	private:
	class Client:public PluginServer::Client // Class representing a client participating in the Agora protocol
		{
		friend class VruiAgoraServer;
		
		/* Elements: */
		private:
		unsigned int sampleRate; // The client's capture sample rate
		unsigned int numPacketFrames; // The client's audio packet size in frames
		Point mouthPos; // The client's "mouth" position in its main viewer's head coordinate system
		ClientIDList receivers; // List of IDs of clients that receive audio data from this client, i.e., that haven't muted this client
		};
	
	/* Elements: */
	VruiCoreServer* vruiCore; // Pointer to the Vrui Core server plug-in
	
	/* Private methods: */
	MessageContinuation* connectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* audioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	void udpAudioPacketRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message);
	MessageContinuation* muteClientRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	
	/* Constructors and destructors: */
	public:
	VruiAgoraServer(Server* sServer);
	virtual ~VruiAgoraServer(void);
	
	/* Methods from class PluginServer: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	};

#endif
