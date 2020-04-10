/***********************************************************************
EnsomatosisClient - Client to share inverse-kinematics driven user
avatars between clients in a collaborative session.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/EnsomatosisClient.h>

#include <stdexcept>
#include <Misc/MessageLogger.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/Marshaller.h>
#include <Misc/StandardMarshallers.h>
#include <Misc/CommandDispatcher.h>
#include <Geometry/GeometryValueCoders.h>
#include <GLMotif/Margin.h>
#include <GLMotif/RowColumn.h>
#include <SceneGraph/GLRenderState.h>
#include <Vrui/Vrui.h>
#include <Vrui/Viewer.h>
#include <Vrui/SceneGraphSupport.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/StringContinuation.h>
#include <Collaboration2/NonBlockSocket.h>

/************************************************
Methods of class EnsomatosisClient::RemoteClient:
************************************************/

EnsomatosisClient::RemoteClient::RemoteClient(const VruiCoreClient::RemoteClient* sVcClient)
	:vcClient(sVcClient)
	{
	}

EnsomatosisClient::RemoteClient::~RemoteClient(void)
	{
	}

/**********************************
Methods of class EnsomatosisClient:
**********************************/

void EnsomatosisClient::setShowAvatar(bool newShowAvatar)
	{
	/* Bail out if nothing changed: */
	if(showAvatar==newShowAvatar)
		return;
	
	/* Check if the avatar will be shown or hidden: */
	if(newShowAvatar)
		{
		/* Add the avatar's scene graph to Vrui Core's main scene graph: */
		vruiCore->getPhysicalRoot().children.appendValue(avatar.getSceneGraph());
		}
	else
		{
		/* Remove the avatar's scene graph from Vrui Core's main scene graph: */
		vruiCore->getPhysicalRoot().children.removeValue(avatar.getSceneGraph());
		}
	showAvatar=newShowAvatar;
	}

void EnsomatosisClient::avatarUpdateNotificationFrontendCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the client's ID: */
	unsigned int clientId=message.read<ClientID>();
	
	/* Access the remote client structure: */
	RemoteClient* rc=remoteClientMap.getEntry(clientId).getDest();
	
	/* Remember if the remote client's avatar is currently valid: */
	bool avatarValid=rc->avatar.isValid();
	
	/* Read the remote client's new avatar configuration and scale: */
	Vrui::IKAvatar::Configuration avatarConfiguration;
	readAvatarConfiguration(message,avatarConfiguration);
	Vrui::Scalar avatarScale(message.read<Scalar>());
	
	/* Read the remote client's new avatar file name: */
	std::string avatarModelFileName=Misc::Marshaller<std::string>::read(message);
	
	/* Update the remote client's avatar: */
	rc->avatar.loadAvatar(avatarModelFileName.c_str());
	rc->avatar.scaleAvatar(avatarScale);
	rc->avatar.configureAvatar(avatarConfiguration);
	rc->avatar.setRootTransform(Vrui::ONTransform::identity);
	
	/* If the client's avatar was not valid before, add it to the Vrui Core client's scene graph: */
	if(!avatarValid)
		rc->vcClient->getHeadRoot().children.appendValue(rc->avatar.getSceneGraph());
	}

void EnsomatosisClient::avatarStateUpdateNotificationFrontendCallback(unsigned int messageId,MessageReader& message)
	{
	/* Access the remote client structure: */
	RemoteClient* rc=remoteClientMap.getEntry(message.read<ClientID>()).getDest();
	
	/* Read the remote client's new avatar state: */
	Vrui::IKAvatar::State avatarState;
	readAvatarState(message,avatarState);
	
	/* Update the remote client's avatar state: */
	rc->avatar.updateState(avatarState);
	}

MessageContinuation* EnsomatosisClient::avatarUpdateNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		unsigned int clientId; // ID of the remote client
		Vrui::IKAvatar::Configuration avatarConfiguration; // Configuration of the remote client's avatar
		Vrui::Scalar avatarScale; // Scale factor for the remote client's avatar
		std::string avatarFileName; // File name of VRML file defining the remote client's avatar
		StringContinuation stringCont; // Continuation object to read the avatar file name
		
		/* Constructors and destructors: */
		Cont(void)
			:stringCont(avatarFileName)
			{
			}
		};
	
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the message: */
	Cont* cont=dynamic_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Create a continuation object to read the message: */
		cont=new Cont();
		
		/* Read the client ID: */
		cont->clientId=socket.read<ClientID>();
		
		/* Read the client's avatar configuration and scale: */
		readAvatarConfiguration(socket,cont->avatarConfiguration);
		cont->avatarScale=Vrui::Scalar(socket.read<Scalar>());
		}
	
	/* Continue reading the message: */
	if(cont->stringCont.read(socket))
		{
		/* Forward the avatar update notification message to the front end: */
		{
		MessageWriter avatarUpdateNotification(AvatarUpdateMsg::createMessage(serverMessageBase+AvatarUpdateNotification,cont->avatarFileName));
		avatarUpdateNotification.write(ClientID(cont->clientId));
		writeAvatarConfiguration(cont->avatarConfiguration,avatarUpdateNotification);
		avatarUpdateNotification.write(Scalar(cont->avatarScale));
		Misc::write(cont->avatarFileName,avatarUpdateNotification);
		client->queueFrontendMessage(avatarUpdateNotification.getBuffer());
		}
		
		/* Delete the continuation object: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void EnsomatosisClient::tposeCommandCallback(const char* argumentBegin,const char* argumentEnd,void* userData)
	{
	EnsomatosisClient* thisPtr=static_cast<EnsomatosisClient*>(userData);
	if(thisPtr->haveAvatar)
		{
		/* Request a T-pose calibration from the IK avatar driver: */
		thisPtr->driver.configureFromTPose();
		
		/* Apply the driver's configuration to the avatar: */
		thisPtr->avatar.configureAvatar(thisPtr->driver.getConfiguration());
		
		/* Send an avatar update request to the server: */
		{
		MessageWriter avatarUpdateRequest(AvatarUpdateMsg::createMessage(thisPtr->clientMessageBase+AvatarUpdateRequest,thisPtr->driver.getAvatarModelFileName()));
		avatarUpdateRequest.write(ClientID(0));
		writeAvatarConfiguration(thisPtr->driver.getConfiguration(),avatarUpdateRequest);
		avatarUpdateRequest.write(Scalar(Vrui::getMeterFactor()));
		Misc::write(thisPtr->driver.getAvatarModelFileName(),avatarUpdateRequest);
		thisPtr->client->queueServerMessage(avatarUpdateRequest.getBuffer());
		}
		}
	}

void EnsomatosisClient::showLocalAvatarValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	/* Set the avatar's visibility: */
	setShowAvatar(cbData->set);
	}

void EnsomatosisClient::lockFeetToNavSpaceValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	/* Update the IK avatar driver: */
	driver.setLockFeetToNavSpace(cbData->set);
	}

void EnsomatosisClient::createSettingsPage(bool haveAvatar)
	{
	vruiCore->getSettingsPager()->setNextPageName("Ensomatosis");
	
	GLMotif::Margin* ensomatosisSettingsMargin=new GLMotif::Margin("EnsomatosisSettingsMargin",vruiCore->getSettingsPager(),false);
	ensomatosisSettingsMargin->setAlignment(GLMotif::Alignment(GLMotif::Alignment::HFILL,GLMotif::Alignment::TOP));
	
	GLMotif::RowColumn* ensomatosisSettingsPanel=new GLMotif::RowColumn("EnsomatosisSettingsPanel",ensomatosisSettingsMargin,false);
	ensomatosisSettingsPanel->setOrientation(GLMotif::RowColumn::VERTICAL);
	ensomatosisSettingsPanel->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	ensomatosisSettingsPanel->setNumMinorWidgets(1);
	
	/* Create widgets for plug-in-wide settings: */
	GLMotif::ToggleButton* showLocalAvatarToggle=new GLMotif::ToggleButton("ShowLocalAvatarToggle",ensomatosisSettingsPanel,"Show Local Avatar");
	showLocalAvatarToggle->setBorderWidth(0.0f);
	showLocalAvatarToggle->setBorderType(GLMotif::Widget::PLAIN);
	showLocalAvatarToggle->setHAlignment(GLFont::Left);
	showLocalAvatarToggle->setToggle(showAvatar);
	showLocalAvatarToggle->getValueChangedCallbacks().add(this,&EnsomatosisClient::showLocalAvatarValueChangedCallback);
	if(!haveAvatar)
		showLocalAvatarToggle->setEnabled(false);
	
	GLMotif::ToggleButton* lockFeetToNavSpaceToggle=new GLMotif::ToggleButton("LockFeetToNavSpaceToggle",ensomatosisSettingsPanel,"Lock Feet to Nav Space");
	lockFeetToNavSpaceToggle->setBorderWidth(0.0f);
	lockFeetToNavSpaceToggle->setBorderType(GLMotif::Widget::PLAIN);
	lockFeetToNavSpaceToggle->setHAlignment(GLFont::Left);
	lockFeetToNavSpaceToggle->setToggle(driver.getLockFeetToNavSpace());
	if(!haveAvatar)
		lockFeetToNavSpaceToggle->setEnabled(false);
	lockFeetToNavSpaceToggle->getValueChangedCallbacks().add(this,&EnsomatosisClient::lockFeetToNavSpaceValueChangedCallback);
	
	ensomatosisSettingsPanel->manageChild();
	
	ensomatosisSettingsMargin->manageChild();
	}

EnsomatosisClient::EnsomatosisClient(Client* sClient)
	:VruiPluginClient(sClient),
	 vruiCore(VruiCoreClient::requestClient(client)),
	 ensomatosisConfig(vruiCore->getProtocolConfig(protocolName)),
	 haveAvatar(false),showAvatar(true),
	 remoteClientMap(17)
	{
	/* Access the Ensomatosis configuration file section and configure the client: */
	ensomatosisConfig=vruiCore->getProtocolConfig(protocolName);
	showAvatar=ensomatosisConfig.retrieveValue<bool>("./showAvatar",showAvatar);
	}

EnsomatosisClient::~EnsomatosisClient(void)
	{
	/* Delete all remote client representations: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		delete *rcIt;
	}

const char* EnsomatosisClient::getName(void) const
	{
	return protocolName;
	}

unsigned int EnsomatosisClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int EnsomatosisClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int EnsomatosisClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void EnsomatosisClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Add front-end message handlers to handle signals from the back end: */
	client->setFrontendMessageHandler(serverMessageBase+AvatarUpdateNotification,Client::wrapMethod<EnsomatosisClient,&EnsomatosisClient::avatarUpdateNotificationFrontendCallback>,this);
	client->setMessageForwarder(serverMessageBase+AvatarStateUpdateNotification,Client::wrapMethod<EnsomatosisClient,&EnsomatosisClient::avatarStateUpdateNotificationFrontendCallback>,this,AvatarStateUpdateMsg::size);
	
	/* Register back-end message handlers: */
	client->setTCPMessageHandler(serverMessageBase+AvatarUpdateNotification,Client::wrapMethod<EnsomatosisClient,&EnsomatosisClient::avatarUpdateNotificationCallback>,this,AvatarUpdateMsg::size);
	}

void EnsomatosisClient::start(void)
	{
	/* Register this protocol with Vrui Core: */
	vruiCore->registerDependentProtocol(this);
	}

void EnsomatosisClient::clientConnected(unsigned int clientId)
	{
	/* Do nothing; new client will be added in the front-end method */
	}

void EnsomatosisClient::clientDisconnected(unsigned int clientId)
	{
	/* Do nothing; client will be removed in the front-end method */
	}

void EnsomatosisClient::frontendStart(void)
	{
	/* Check if this client has a local avatar representation: */
	haveAvatar=ensomatosisConfig.hasTag("./avatarConfigName");
	if(haveAvatar)
		{
		try
			{
			/* Configure the avatar driver and scale it from meters to physical coordinate units: */
			driver.configure(ensomatosisConfig.retrieveString("./avatarConfigName").c_str());
			driver.scaleAvatar(Vrui::getMeterFactor());
			
			/* Load the avatar, scale it from meters, and apply the avatar driver's configuration: */
			avatar.loadAvatar(driver.getAvatarModelFileName().c_str());
			avatar.scaleAvatar(Vrui::getMeterFactor());
			avatar.configureAvatar(driver.getConfiguration());
			
			/* Prepare the avatar to be shown (or not): */
			bool newShowAvatar=showAvatar;
			showAvatar=false;
			setShowAvatar(newShowAvatar);
			
			/* Send an avatar update request to the server: */
			{
			MessageWriter avatarUpdateRequest(AvatarUpdateMsg::createMessage(clientMessageBase+AvatarUpdateRequest,driver.getAvatarModelFileName()));
			avatarUpdateRequest.write(ClientID(0));
			writeAvatarConfiguration(driver.getConfiguration(),avatarUpdateRequest);
			avatarUpdateRequest.write(Scalar(Vrui::getMeterFactor()));
			Misc::write(driver.getAvatarModelFileName(),avatarUpdateRequest);
			client->queueServerMessage(avatarUpdateRequest.getBuffer());
			}
			}
		catch(const std::runtime_error& err)
			{
			/* Show an error message: */
			Misc::formattedUserError("EnsomatosisClient: Caught exception %s while configuring local IK avatar",err.what());
			
			/* Don't have an avatar afer all... */
			haveAvatar=false;
			showAvatar=false;
			}
		}
	else
		showAvatar=false;
	
	if(haveAvatar)
		{
		/* Register a command callback to calibrate the local avatar via a T-pose: */
		Vrui::getCommandDispatcher().addCommandCallback("Ensomatosis::TPose",&EnsomatosisClient::tposeCommandCallback,this,0,"Calibrates the local IK avatar from a standard T-pose");
		}
	
	/* Create a settings page in the Vrui Core protocol client's collaboration dialog: */
	createSettingsPage(haveAvatar);
	}

void EnsomatosisClient::frontendClientConnected(unsigned int clientId)
	{
	/* Add a new remote client structure to the remote client list and map: */
	RemoteClient* newClient=new RemoteClient(vruiCore->getRemoteClient(clientId));
	remoteClients.push_back(newClient);
	remoteClientMap.setEntry(RemoteClientMap::Entry(clientId,newClient));
	}

void EnsomatosisClient::frontendClientDisconnected(unsigned int clientId)
	{
	/* Retrieve the client structure: */
	RemoteClientMap::Iterator rcIt=remoteClientMap.findEntry(clientId);
	RemoteClient* client=rcIt->getDest();
	
	/* Remove the client's avatar from the Vrui Core client's scene graph if it was valid: */
	if(client->avatar.isValid())
		client->vcClient->getHeadRoot().children.removeValue(client->avatar.getSceneGraph());
	
	/* Remove the disconnected client from the remote client map: */
	remoteClientMap.removeEntry(rcIt);
	
	/* Remove the disconnected client from the remote client list: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if((*rcIt)==client)
			{
			/* Remove the client: */
			*rcIt=remoteClients.back();
			remoteClients.pop_back();
			
			/* Stop looking: */
			break;
			}
	
	/* Delete the client structure: */
	delete client;
	}

void EnsomatosisClient::frame(void)
	{
	if(haveAvatar&&driver.needsUpdate())
		{
		/* Update the avatar: */
		Vrui::IKAvatar::State newState;
		if(driver.calculateState(newState))
			Vrui::scheduleUpdate(Vrui::getNextAnimationTime());
		avatar.updateState(newState);
		
		/* Send an avatar state update message to the server: */
		{
		MessageWriter avatarStateUpdateRequest(AvatarStateUpdateMsg::createMessage(clientMessageBase+AvatarStateUpdateRequest));
		avatarStateUpdateRequest.write(ClientID(0));
		writeAvatarState(newState,avatarStateUpdateRequest);
		client->queueServerMessage(avatarStateUpdateRequest.getBuffer());
		}
		
		if(showAvatar)
			{
			/* Attach the avatar to the viewer: */
			avatar.setRootTransform(Vrui::getMainViewer()->getHeadTransformation());
			}
		}
	}

void EnsomatosisClient::shutdown(void)
	{
	if(haveAvatar)
		{
		/* Remove the local avatar from the Vrui Core main scene graph: */
		setShowAvatar(false);
		
		/* Unregister the command callback to calibrate the local avatar via a T-pose: */
		Vrui::getCommandDispatcher().removeCommandCallback("Ensomatosis::TPose");
		}
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginClient* createObject(PluginClientLoader& objectLoader,Client* client)
	{
	return new EnsomatosisClient(client);
	}

void destroyObject(PluginClient* object)
	{
	delete object;
	}

}
