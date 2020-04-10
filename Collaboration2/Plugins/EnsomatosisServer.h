/***********************************************************************
EnsomatosisServer - Server to share inverse-kinematics driven user
avatars between clients in a collaborative session.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#ifndef ENSOMATOSISSERVER_INCLUDED
#define ENSOMATOSISSERVER_INCLUDED

#include <Collaboration2/PluginServer.h>
#include <Collaboration2/Plugins/EnsomatosisProtocol.h>

/* Forward declarations: */
class MessageContinuation;

class EnsomatosisServer:public PluginServer,public EnsomatosisProtocol
	{
	/* Embedded classes: */
	private:
	class Client:public PluginServer::Client // Class representing a client participating in the Ensomatosis protocol
		{
		friend class EnsomatosisServer;
		
		/* Elements: */
		private:
		std::string avatarFileName; // Name of the VRML file containing the user's avatar representation (work-around until VRML streaming is implemented)
		Vrui::IKAvatar::Configuration avatarConfiguration; // Configuration of the user's avatar
		Vrui::Scalar avatarScale; // Scale factor for user's avatar
		Vrui::IKAvatar::State avatarState; // Current avatar state of the user
		bool avatarValid; // Flag if the current avatar state is valid, i.e., matches the avatar definition
		
		/* Constructors and destructors: */
		Client(void)
			:avatarScale(1),
			 avatarValid(false)
			{
			}
		};
	
	/* Private methods: */
	MessageContinuation* avatarUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* avatarStateUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	
	/* Constructors and destructors: */
	public:
	EnsomatosisServer(Server* sServer);
	virtual ~EnsomatosisServer(void);
	
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
