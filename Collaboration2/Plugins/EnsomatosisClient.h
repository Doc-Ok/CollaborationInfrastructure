/***********************************************************************
EnsomatosisClient - Client to share inverse-kinematics driven user
avatars between clients in a collaborative session.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#ifndef ENSOMATOSISCLIENT_INCLUDED
#define ENSOMATOSISCLIENT_INCLUDED

#include <vector>
#include <Misc/StandardHashFunction.h>
#include <Misc/HashTable.h>
#include <Misc/ConfigurationFile.h>
#include <GLMotif/ToggleButton.h>
#include <Vrui/IKAvatarDriver.h>
#include <Collaboration2/VruiPluginClient.h>
#include <Collaboration2/Plugins/VruiCoreClient.h>
#include <Collaboration2/Plugins/EnsomatosisProtocol.h>

/* Forward declarations: */
namespace Vrui {
class InputDevice;
}
class MessageReader;
class MessageContinuation;

class EnsomatosisClient:public VruiPluginClient,public EnsomatosisProtocol
	{
	/* Embedded classes: */
	private:
	class RemoteClient:public PluginClient::RemoteClient
		{
		friend class EnsomatosisClient;
		
		/* Elements: */
		const VruiCoreClient::RemoteClient* vcClient; // Pointer to the Vrui Core remote client state representing this client
		Vrui::IKAvatar avatar; // Avatar representing the remote client
		
		/* Constructors and destructors: */
		RemoteClient(const VruiCoreClient::RemoteClient* sVcClient);
		virtual ~RemoteClient(void);
		};
	
	typedef std::vector<RemoteClient*> RemoteClientList; // Type for lists of remote client state structures
	typedef Misc::HashTable<unsigned int,RemoteClient*> RemoteClientMap; // Type for hash tables mapping remote client IDs to remote client structures
	
	/* Elements: */
	VruiCoreClient* vruiCore; // Pointer to the Vrui Core protocol client
	Misc::ConfigurationFileSection ensomatosisConfig; // Configuration file section for Ensomatosis protocol
	
	/* Local avatar representation and IK state: */
	bool haveAvatar; // Flag if this client has its own avatar
	Vrui::IKAvatar avatar; // Client's own avatar
	Vrui::IKAvatarDriver driver; // Inverse kinematics driver for client's own avatar
	bool showAvatar; // Flag to enable rendering of this client's avatar
	
	/* Remote client representation state: */
	RemoteClientList remoteClients; // List of remote client structures
	RemoteClientMap remoteClientMap; // Map from remote client IDs to remote client structures
	
	/* Private methods: */
	void setShowAvatar(bool newShowAvatar); // Shows or hides the local client's avatar
	
	/* Methods receiving forwarded messages from the back end: */
	void avatarUpdateNotificationFrontendCallback(unsigned int messageId,MessageReader& message);
	void avatarStateUpdateNotificationFrontendCallback(unsigned int messageId,MessageReader& message);
	
	/* Methods receiving messages from the server: */
	MessageContinuation* avatarUpdateNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	
	/* User interface methods: */
	static void tposeCommandCallback(const char* argumentBegin,const char* argumentEnd,void* userData);
	void showLocalAvatarValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void lockFeetToNavSpaceValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void createSettingsPage(bool haveAvatar); // Creates an Ensomatosis protocol settings page in the Vrui Core client's collaboration dialog
	
	/* Constructors and destructors: */
	public:
	EnsomatosisClient(Client* sClient);
	virtual ~EnsomatosisClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	
	/* Methods from class VruiPluginClient: */
	virtual void frontendStart(void);
	virtual void frontendClientConnected(unsigned int clientId);
	virtual void frontendClientDisconnected(unsigned int clientId);
	virtual void frame(void);
	virtual void shutdown(void);
	};

#endif
