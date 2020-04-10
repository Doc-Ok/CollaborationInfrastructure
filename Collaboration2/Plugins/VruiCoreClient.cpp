/***********************************************************************
VruiCoreClient - Client for the core Vrui collaboration protocol, which
represents remote users' physical VR environments, and maintains their
head and input device positions/orientations and navigation
transformations.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/VruiCoreClient.h>

#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Misc/CommandDispatcher.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/CompoundValueCoders.h>
#include <Misc/Marshaller.h>
#include <IO/OpenFile.h>
#include <Math/Math.h>
#include <Geometry/GeometryMarshallers.h>
#include <GL/gl.h>
#include <GL/GLColor.h>
#include <GL/GLGeometryWrappers.h>
#include <GL/GLTransformationWrappers.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/Margin.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Pager.h>
#include <GLMotif/Separator.h>
#include <GLMotif/Label.h>
#include <GLMotif/ScrolledListBox.h>
#include <AL/ALContextData.h>
#include <SceneGraph/ShapeNode.h>
#include <SceneGraph/AppearanceNode.h>
#include <SceneGraph/MaterialNode.h>
#include <SceneGraph/ImageTextureNode.h>
#include <SceneGraph/SphereNode.h>
#include <SceneGraph/ConeNode.h>
#include <SceneGraph/FancyTextNode.h>
#include <Vrui/GlyphRenderer.h>
#include <Vrui/GenericAbstractToolFactory.h>
#include <Vrui/ToolManager.h>
#include <Vrui/SceneGraphSupport.h>

#include <Collaboration2/Config.h>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/NonBlockSocket.h>

/*********************************************
Methods of class VruiCoreClient::RemoteClient:
*********************************************/

void VruiCoreClient::RemoteClient::updateEnvironmentFromVrui(void)
	{
	/* Read the environment definition from Vrui: */
	environment.inchFactor=Scalar(Vrui::getInchFactor());
	environment.displayCenter=Point(Vrui::getDisplayCenter());
	environment.displaySize=Scalar(Vrui::getDisplaySize());
	environment.forwardDirection=Vector(Vrui::getForwardDirection());
	environment.upDirection=Vector(Vrui::getUpDirection());
	environment.floorPlane=Plane(Vrui::getFloorPlane());
	
	/* Calculate the floor center point: */
	Scalar lambda=(environment.floorPlane.getOffset()-environment.displayCenter*environment.floorPlane.getNormal())/(environment.upDirection*environment.floorPlane.getNormal());
	floorCenter=environment.displayCenter+environment.upDirection*lambda;
	
	/* Calculate the base rotation: */
	Vector x=environment.forwardDirection^environment.upDirection;
	Vector y=environment.upDirection^x;
	baseRotation=Rotation::fromBaseVectors(x,y);
	}

void VruiCoreClient::RemoteClient::updateEnvironment(MessageReader& message)
	{
	/* Read the environment definition: */
	environment.read(message);
	
	/* Calculate the floor center point: */
	Scalar lambda=(environment.floorPlane.getOffset()-environment.displayCenter*environment.floorPlane.getNormal())/(environment.upDirection*environment.floorPlane.getNormal());
	floorCenter=environment.displayCenter+environment.upDirection*lambda;
	
	/* Calculate the base rotation: */
	Vector x=environment.forwardDirection^environment.upDirection;
	Vector y=environment.upDirection^x;
	baseRotation=Rotation::fromBaseVectors(x,y);
	
	/* Update the scene graph: */
	nameTagTransform->translation.setValue(SceneGraph::Vector(0,environment.inchFactor*Scalar(6),0));
	nameTagTransform->scale.setValue(SceneGraph::Size(environment.inchFactor));
	nameTagTransform->update();
	}

void VruiCoreClient::RemoteClient::updateViewerConfigFromVrui(void)
	{
	/* Read the main viewer configuration from Vrui: */
	Vrui::Viewer* mainViewer=Vrui::getMainViewer();
	viewerConfig.viewDirection=Vector(mainViewer->getDeviceViewDirection());
	viewerConfig.upDirection=Vector(mainViewer->getDeviceUpDirection());
	viewerConfig.eyePositions[0]=Point(mainViewer->getDeviceEyePosition(Vrui::Viewer::LEFT));
	viewerConfig.eyePositions[1]=Point(mainViewer->getDeviceEyePosition(Vrui::Viewer::RIGHT));
	}

void VruiCoreClient::RemoteClient::updateViewerConfig(MessageReader& message)
	{
	/* Read the viewer configuration: */
	viewerConfig.read(message);
	
	/* Update the scene graph: */
	Rotation eyeRot=Rotation::rotateFromTo(Vector(0,-1,0),viewerConfig.viewDirection);
	for(int eyeIndex=0;eyeIndex<2;++eyeIndex)
		{
		Vector translation=viewerConfig.eyePositions[eyeIndex]-Point::origin;
		translation+=eyeRot.transform(Vector(0,Math::div2(environment.inchFactor),0));
		eyes[eyeIndex]->translation.setValue(translation);
		eyes[eyeIndex]->rotation.setValue(eyeRot);
		eyes[eyeIndex]->scale.setValue(SceneGraph::Size(environment.inchFactor));
		eyes[eyeIndex]->update();
		}
	}

void VruiCoreClient::RemoteClient::updateViewerState(MessageReader& message)
	{
	/* Read the viewer state: */
	viewerState.read(message);
	
	/* Update the scene graph: */
	headRoot->transform.setValue(viewerState.headTransform);
	headRoot->update();
	}

VruiCoreClient::RemoteClient::RemoteClient(unsigned int sId)
	:id(sId),
	 sharedEnvironment(0),
	 navLockedClient(0),
	 deviceIndexMap(5),
	 hideMainViewerCount(0),hideDevicesCount(0)
	{
	}

VruiCoreClient::RemoteClient::RemoteClient(VruiCoreClient* client,MessageReader& message)
	:id(message.read<ClientID>()),
	 sharedEnvironment(0),
	 navLockedClient(0),
	 deviceIndexMap(5),
	 hideMainViewerCount(client->drawRemoteMainViewers?0:1),
	 hideNameTagCount(client->drawRemoteNameTags?0:1),
	 hideDevicesCount(client->drawRemoteDevices?0:1)
	{
	/* Read the client's name: */
	size_t nameLength=message.read<Misc::UInt16>();
	name.reserve(nameLength);
	charBufferToString(message,nameLength,name);
	
	/* Create the client's scene graph: */
	physicalRoot=new SceneGraph::DOGTransformNode;
	
	headRoot=new SceneGraph::ONTransformNode;
	
	for(int eyeIndex=0;eyeIndex<2;++eyeIndex)
		{
		eyes[eyeIndex]=new SceneGraph::TransformNode;
		eyes[eyeIndex]->children.appendValue(client->eye);
		eyes[eyeIndex]->update();
		
		if(hideMainViewerCount==0)
			headRoot->children.appendValue(eyes[eyeIndex]);
		}
	
	nameTag=new SceneGraph::BillboardNode;
	nameTag->axisOfRotation.setValue(SceneGraph::Vector::zero);
	
	nameTagTransform=new SceneGraph::TransformNode;
	
	SceneGraph::ShapeNode* nameTagShape=new SceneGraph::ShapeNode;
	
	SceneGraph::AppearanceNode* nameTagAppearance=new SceneGraph::AppearanceNode;
	
	nameTagMaterial=new SceneGraph::MaterialNode;
	nameTagMaterial->diffuseColor.setValue(SceneGraph::Color(0.25f,0.75f,0.25f));
	nameTagMaterial->update();
	
	nameTagAppearance->material.setValue(nameTagMaterial);
	nameTagAppearance->update();
	
	nameTagShape->appearance.setValue(nameTagAppearance);
	
	nameTagText=new SceneGraph::FancyTextNode;
	nameTagText->string.setValue(name);
	nameTagText->fontStyle.setValue(client->font);
	nameTagText->update();
	
	nameTagShape->geometry.setValue(nameTagText);
	nameTagShape->update();
	
	nameTagTransform->children.appendValue(nameTagShape);
	nameTagTransform->update();
	
	nameTag->children.appendValue(nameTagTransform);
	nameTag->update();
	
	if(hideNameTagCount==0)
		headRoot->children.appendValue(nameTag);
	
	headRoot->update();
	
	physicalRoot->children.appendValue(headRoot);
	physicalRoot->update();
	
	/* The client's scene graph will be fully initialized and added to the main scene graph once its Vrui Core connect notification message has arrived */
	}

void VruiCoreClient::RemoteClient::hideMainViewer(void)
	{
	if(hideMainViewerCount==0)
		{
		/* Remove the main viewer representation from the client's scene graph: */
		for(int eyeIndex=0;eyeIndex<2;++eyeIndex)
			headRoot->children.removeValue(eyes[eyeIndex]);
		headRoot->update();
		}
	++hideMainViewerCount;
	}

void VruiCoreClient::RemoteClient::showMainViewer(void)
	{
	--hideMainViewerCount;
	if(hideMainViewerCount==0)
		{
		/* Add the main viewer representation to the client's scene graph: */
		for(int eyeIndex=0;eyeIndex<2;++eyeIndex)
			headRoot->children.appendValue(eyes[eyeIndex]);
		headRoot->update();
		}
	}

void VruiCoreClient::RemoteClient::hideNameTag(void)
	{
	if(hideNameTagCount==0)
		{
		/* Remove the name tag from the client's scene graph: */
		headRoot->children.removeValue(nameTag);
		headRoot->update();
		}
	++hideNameTagCount;
	}

void VruiCoreClient::RemoteClient::showNameTag(void)
	{
	--hideNameTagCount;
	if(hideNameTagCount==0)
		{
		/* Add the name tag to the client's scene graph: */
		headRoot->children.appendValue(nameTag);
		headRoot->update();
		}
	}

void VruiCoreClient::RemoteClient::hideDevices(void)
	{
	if(hideDevicesCount==0)
		{
		/* Remove all enabled devices' representations from the client's scene graph: */
		for(DeviceList::iterator dIt=devices.begin();dIt!=devices.end();++dIt)
			if(dIt->enabled)
				physicalRoot->children.removeValue(dIt->root);
		physicalRoot->update();
		}
	++hideDevicesCount;
	}

void VruiCoreClient::RemoteClient::showDevices(void)
	{
	--hideDevicesCount;
	if(hideDevicesCount==0)
		{
		/* Add all enabled devices' representations to the client's scene graph: */
		for(DeviceList::iterator dIt=devices.begin();dIt!=devices.end();++dIt)
			if(dIt->enabled)
				physicalRoot->children.appendValue(dIt->root);
		physicalRoot->update();
		}
	}

#if 0 // Don't do that yet; need to figure shit out

void VruiCoreClient::RemoteClient::setNavTransform(const VruiCoreProtocol::NavTransform& newNavTransform)
	{
	/* Update the navigation transformation: */
	navTransform=newNavTransform;
	
	/* Update the client's physical-space scene graph root: */
	NavTransform physTransform=navTransform;
	const RemoteClient* lockClient=navLockedClient;
	while(lockClient!=0)
		{
		physTransform*=lockClient->navTransform;
		lockClient=lockClient->navLockedClient;
		}
	physTransform.doInvert();
	physTransform.renormalize();
	physicalRoot->transform.setValue(physTransform);
	}

#endif

/*******************************
Methods of class VruiCoreClient:
*******************************/

void VruiCoreClient::sendNavTransform(const VruiCoreProtocol::NavTransform& newNavTransform)
	{
	/* Send a navigation transformation update request to the server: */
	{
	MessageWriter navTransformUpdateRequest(NavTransformUpdateMsg::createMessage(clientMessageBase+NavTransformUpdateRequest));
	navTransformUpdateRequest.write(ClientID(client->getId()));
	Misc::write(newNavTransform,navTransformUpdateRequest);
	client->queueServerMessage(navTransformUpdateRequest.getBuffer());
	}
	}

void VruiCoreClient::nameChangeReplyCallback(Client::NameChangeReplyCallbackData* cbData)
	{
	/* Create a name change reply notification message and send it to the front end: */
	MessageWriter nameChangeReplyNotification(MessageBuffer::create(serverMessageBase+NameChangeReplyNotification,sizeof(Bool)+2*32*sizeof(Char)));
	writeBool(cbData->granted,nameChangeReplyNotification);
	stringToCharBuffer(cbData->oldName,nameChangeReplyNotification,32);
	stringToCharBuffer(cbData->newName,nameChangeReplyNotification,32);
	client->queueFrontendMessage(nameChangeReplyNotification.getBuffer());
	}

void VruiCoreClient::nameChangeNotificationCallback(Client::NameChangeNotificationCallbackData* cbData)
	{
	/* Create a client name change notification message and send it to the front end: */
	MessageWriter clientNameChangeNotification(MessageBuffer::create(serverMessageBase+ClientNameChangeNotification,sizeof(ClientID)+2*32*sizeof(Char)));
	clientNameChangeNotification.write(ClientID(cbData->clientId));
	stringToCharBuffer(cbData->oldName,clientNameChangeNotification,32);
	stringToCharBuffer(cbData->newName,clientNameChangeNotification,32);
	client->queueFrontendMessage(clientNameChangeNotification.getBuffer());
	}

void VruiCoreClient::startNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Put this client's self-representation into the remote client map, as a sentinel: */
	remoteClientMap.setEntry(RemoteClientMap::Entry(message.read<ClientID>(),&self));
	
	/* Retrieve the main viewer's current head-tracking device: */
	mainViewerHeadDevice=const_cast<Vrui::InputDevice*>(Vrui::getMainViewer()->getHeadDevice()); // This is a bit of API flaw in Vrui
	
	/* Initialize the self-representation: */
	self.updateEnvironmentFromVrui();
	self.updateViewerConfigFromVrui();
	self.viewerState.headTransform=ONTransform(Vrui::getMainViewer()->getHeadTransformation());
	self.navTransform=NavTransform(Vrui::getNavigationTransformation());
	
	/* Check if this client is part of a shared physical environment: */
	std::string sharedEnvironmentName=vruiCoreConfig.retrieveString("./sharedEnvironmentName",std::string());
	
	/* Send a Vrui Core connect request message to the server: */
	{
	MessageWriter connectRequest(ConnectRequestMsg::createMessage(clientMessageBase));
	stringToCharBuffer(sharedEnvironmentName,connectRequest,ConnectRequestMsg::nameLength);
	self.environment.write(connectRequest);
	self.viewerConfig.write(connectRequest);
	self.viewerState.write(connectRequest);
	Misc::write(self.navTransform,connectRequest);
	client->queueServerMessage(connectRequest.getBuffer());
	}
	
	/* Register a callback to get notified when the definition of Vrui's physical environment changes: */
	Vrui::getEnvironmentDefinitionChangedCallbacks().add(this,&VruiCoreClient::environmentDefinitionChangedCallback);
	
	if(mainViewerHeadDevice!=0)
		{
		/* Register a callback with the head-tracking device: */
		mainViewerHeadDevice->getTrackingCallbacks().add(this,&VruiCoreClient::mainViewerTrackingCallback);
		}
	
	/* Register a callback to get notified when Vrui's main viewer changes configuration: */
	Vrui::getMainViewer()->getConfigChangedCallbacks().add(this,&VruiCoreClient::mainViewerConfigurationChangedCallback);
	
	/* Register a callback to get notified when Vrui's navigation transformation changes: */
	Vrui::getNavigationTransformationChangedCallbacks().add(this,&VruiCoreClient::navigationTransformationChangedCallback);
	
	/* Retrieve a list of physical input devices which are not to be shared with the server: */
	std::vector<std::string> localDeviceNames=vruiCoreConfig.retrieveValue<std::vector<std::string> >("./localDevices",std::vector<std::string>());
	std::vector<Vrui::InputDevice*> localDevices;
	for(std::vector<std::string>::iterator ldnIt=localDeviceNames.begin();ldnIt!=localDeviceNames.end();++ldnIt)
		{
		Vrui::InputDevice* device=Vrui::findInputDevice(ldnIt->c_str());
		if(device!=0)
			localDevices.push_back(device);
		}
	
	/* Add all physical input devices, which will also notify the server: */
	Vrui::InputDeviceManager* idm=Vrui::getInputDeviceManager();
	int numDevices=idm->getNumInputDevices();
	for(int deviceIndex=0;deviceIndex<numDevices;++deviceIndex)
		{
		/* Check if the input device is on the no-share list: */
		Vrui::InputDevice* device=idm->getInputDevice(deviceIndex);
		std::vector<Vrui::InputDevice*>::iterator ldIt;
		for(ldIt=localDevices.begin();ldIt!=localDevices.end()&&*ldIt!=device;++ldIt)
			;
		if(ldIt==localDevices.end())
			addInputDevice(device,false);
		}
	
	/* Register callbacks to get notified when input devices are created or destroyed: */
	idm->getInputDeviceCreationCallbacks().add(this,&VruiCoreClient::inputDeviceCreationCallback);
	idm->getInputDeviceDestructionCallbacks().add(this,&VruiCoreClient::inputDeviceDestructionCallback);
	
	/* Register a callback to get notified when an input device changes state: */
	Vrui::getInputGraphManager()->getInputDeviceStateChangeCallbacks().add(this,&VruiCoreClient::inputDeviceStateChangeCallback);
	
	/* Register command pipe callbacks: */
	Vrui::getCommandDispatcher().addCommandCallback("VruiCore::changeName",&VruiCoreClient::changeNameCommandCallback,this,"<new client name>","Asks the server to change the client's display name");
	
	/* Create the collaboration dialog: */
	createCollaborationDialog(sharedEnvironmentName);
	
	/* Add a button to pop up the collaboration dialog to Vrui's system menu: */
	showCollaborationDialogButton=Vrui::addShowSettingsDialogButton("Show Collaboration Settings");
	showCollaborationDialogButton->getSelectCallbacks().add(this,&VruiCoreClient::showCollaborationDialogCallback);
	firstShown=true;
	
	/* Create the navigational scene graph root and initialize it to the current navigation transformation: */
	navigationalRoot=new SceneGraph::DOGTransformNode;
	navigationalRoot->transform.setValue(self.navTransform);
	navigationalRoot->update();
	
	physicalRoot->children.appendValue(navigationalRoot);
	physicalRoot->update();
	
	/* Create a scene graph to render the eyes of remote clients: */
	SceneGraph::ShapeNode* eyeShape=new SceneGraph::ShapeNode;
	
	SceneGraph::AppearanceNode* eyeAppearance=new SceneGraph::AppearanceNode;
	
	SceneGraph::MaterialNode* eyeMaterial=new SceneGraph::MaterialNode;
	eyeMaterial->diffuseColor.setValue(SceneGraph::Color(1.0f,1.0f,1.0f));
	eyeMaterial->specularColor.setValue(SceneGraph::Color(1.0f,1.0f,1.0f));
	eyeMaterial->shininess.setValue(0.95f);
	eyeMaterial->update();
	
	eyeAppearance->material.setValue(eyeMaterial);
	
	SceneGraph::ImageTextureNode* eyeTexture=new SceneGraph::ImageTextureNode;
	eyeTexture->setUrl("EyeBrown.png",*IO::openDirectory(COLLABORATION_RESOURCEDIR));
	eyeTexture->repeatS.setValue(true);
	eyeTexture->repeatT.setValue(false);
	eyeTexture->filter.setValue(false);
	eyeTexture->update();
	
	eyeAppearance->texture.setValue(eyeTexture);
	eyeAppearance->update();
	
	eyeShape->appearance.setValue(eyeAppearance);
	
	SceneGraph::SphereNode* eyeSphere=new SceneGraph::SphereNode;
	eyeSphere->radius.setValue(SceneGraph::Scalar(0.5)); // Human eyeball has close to 1" diameter
	eyeSphere->numSegments.setValue(16);
	eyeSphere->latLong.setValue(true);
	eyeSphere->texCoords.setValue(true);
	eyeSphere->update();
	
	eyeShape->geometry.setValue(eyeSphere);
	eyeShape->update();
	
	eye=eyeShape;
	
	/* Create a font style to render name tags for remote clients: */
	font=new SceneGraph::FancyFontStyleNode;
	if(vruiCoreConfig.hasTag("./nameTagFontURL"))
		{
		/* Use a specific font file: */
		font->url.setValue(vruiCoreConfig.retrieveString("./nameTagFontURL"));
		}
	else
		{
		/* Use a standard font and style: */
		font->family.setValue(vruiCoreConfig.retrieveString("./nameTagFontFamily","SANS"));
		font->style.setValue(vruiCoreConfig.retrieveString("./nameTagFontStyle","PLAIN"));
		}
	font->size.setValue(vruiCoreConfig.retrieveValue<SceneGraph::Scalar>("./nameTagFontSize",SceneGraph::Scalar(2.5)));
	font->justify.appendValue("MIDDLE");
	font->justify.appendValue("END");
	font->precision.setValue(SceneGraph::Scalar(2));
	font->update();
	
	/* Create a default scene graph to render a remote device: */
	SceneGraph::ShapeNode* deviceShape=new SceneGraph::ShapeNode;
	
	SceneGraph::AppearanceNode* deviceAppearance=new SceneGraph::AppearanceNode;
	
	SceneGraph::MaterialNode* deviceMaterial=new SceneGraph::MaterialNode;
	deviceMaterial->diffuseColor.setValue(SceneGraph::Color(0.5f,0.5f,0.5f));
	deviceMaterial->specularColor.setValue(SceneGraph::Color(1.0f,1.0f,1.0f));
	deviceMaterial->shininess.setValue(25.0f/128.0f);
	deviceMaterial->update();
	
	deviceAppearance->material.setValue(deviceMaterial);
	deviceAppearance->update();
	
	deviceShape->appearance.setValue(deviceAppearance);
	
	SceneGraph::ConeNode* deviceCone=new SceneGraph::ConeNode;
	SceneGraph::Scalar coneHeight=SceneGraph::Scalar(Vrui::getGlyphRenderer()->getGlyphSize()/Vrui::getInchFactor());
	SceneGraph::Scalar coneRadius=coneHeight*SceneGraph::Scalar(0.25);
	deviceCone->height.setValue(coneHeight);
	deviceCone->bottomRadius.setValue(coneRadius);
	deviceCone->update();
	
	deviceShape->geometry.setValue(deviceCone);
	deviceShape->update();
	
	device=deviceShape;
	deviceOffset=Vector(0,-Math::div2(coneHeight),0);
	}

void VruiCoreClient::registerProtocolNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Retrieve the new protocol client object: */
	VruiPluginClient* newProtocol=message.read<VruiPluginClient*>();
	
	/* Add the protocol to the list: */
	dependentProtocols.push_back(newProtocol);
	
	/* Call the new protocol's start method immediately: */
	newProtocol->frontendStart();
	}

void VruiCoreClient::nameChangeReplyNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the name change reply message: */
	bool granted=readBool(message);
	std::string oldName,newName;
	charBufferToString(message,32,oldName);
	charBufferToString(message,32,newName);
	
	if(!granted)
		{
		/* Reset the client name text field: */
		Misc::userError("VruiCoreClient: Name change was denied by server");
		clientNameTextField->setString(oldName.c_str());
		}
	}

void VruiCoreClient::clientConnectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Create a new remote client structure for the new client's ID: */
	RemoteClient* newClient=new RemoteClient(this,message);
	
	/* Add the new client to the remote client map: */
	remoteClientMap.setEntry(RemoteClientMap::Entry(newClient->id,newClient));
	
	/* Notify all dependent protocols that a new remote client structure has been created: */
	for(std::vector<VruiPluginClient*>::iterator dpIt=dependentProtocols.begin();dpIt!=dependentProtocols.end();++dpIt)
		(*dpIt)->frontendClientConnected(newClient->id);
	}

void VruiCoreClient::clientNameChangeNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client structure: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Read the old and new names: */
	std::string oldName,newName;
	charBufferToString(message,32,oldName);
	charBufferToString(message,32,newName);
	
	/* Find the client's current index in the client list: */
	int oldClientIndex=0;
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end()&&*rcIt!=rc;++rcIt,++oldClientIndex)
		;
	
	/* Check if the client is currently selected: */
	bool clientSelected=remoteClientList->isItemSelected(oldClientIndex);
	
	/* Temporarily remove the client from the client lists: */
	remoteClients.erase(remoteClients.begin()+oldClientIndex);
	remoteClientList->removeItem(oldClientIndex);
	
	/* Update the client's name: */
	rc->name=newName;
	
	/* Find the client's new index in the client list based on alphabetic ordering: */
	int newClientIndex;
	for(newClientIndex=0;newClientIndex<remoteClientList->getNumItems()&&strcmp(rc->name.c_str(),remoteClientList->getItem(newClientIndex))>=0;++newClientIndex)
		;
	
	/* Re-insert the client into the client lists: */
	remoteClients.insert(remoteClients.begin()+newClientIndex,rc);
	remoteClientList->insertItem(newClientIndex,rc->name.c_str(),false);
	
	/* Re-select the client if it was previously selected: */
	if(clientSelected)
		remoteClientList->selectItem(newClientIndex,true);
	
	/* Update the client's scene graph: */
	rc->nameTagText->string.setValue(rc->name);
	rc->nameTagText->update();
	
	/* Update the Vrui Core settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::clientDisconnectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Access the disconnected client's Vrui Core state: */
	RemoteClient* disconnected=getRemoteClient(message);
	
	/* Notify all dependent protocols that the remote client structure is about to be destroyed: */
	for(std::vector<VruiPluginClient*>::iterator dpIt=dependentProtocols.begin();dpIt!=dependentProtocols.end();++dpIt)
		(*dpIt)->frontendClientDisconnected(disconnected->id);
	
	/* Remove the disconnected client's scene graph from the main scene graph (this is a no-op if the scene graph was never added): */
	navigationalRoot->children.removeValue(disconnected->physicalRoot);
	navigationalRoot->update();
	
	/* Remove the disconnected client from the front-end client list and remote client list box: */
	int index=0;
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt,++index)
		if(*rcIt==disconnected)
			{
			/* Remove the client's entry: */
			remoteClients.erase(rcIt);
			
			/* Remove the client's name from the list box: */
			remoteClientList->removeItem(index);
			
			/* Stop looking: */
			break;
			}
	
	/* Remove the disconnected client from the client map: */
	remoteClientMap.removeEntry(disconnected->id);
	
	/* Calculate the disconnected client's full navigation transformation in case any navigation locks need to be resolved: */
	NavTransform disconnectedNavTransform=disconnected->getNavTransform();
	
	/* Make a note if this client is ending a navigation sequence or releasing a navigation lock as a result of the client's disconnection: */
	bool selfEndNavSequence=false;
	bool selfReleaseNavLock=false;
	
	/* Check if the disconnected client is part of a shared physical environment: */
	SharedEnvironment* se=disconnected->sharedEnvironment;
	if(se!=0)
		{
		/* Remove the disconnected client from its shared environment: */
		for(std::vector<RemoteClient*>::iterator cIt=se->clients.begin();cIt!=se->clients.end();++cIt)
			if(*cIt==disconnected)
				{
				/* Move the last client in the list to the disconnected client's place: */
				*cIt=se->clients.back();
				se->clients.pop_back();
				
				/* Stop looking: */
				break;
				}
		
		/* Check if the disconnected client was in a navigation sequence: */
		if(se->navigatingClient==disconnected)
			{
			/* End the shared environment's navigation sequence: */
			se->navigatingClient=0;
			
			/* End this client's navigation sequence if it is part of the same shared environment: */
			selfEndNavSequence=self.sharedEnvironment==se;
			}
		
		/* Check if the disconnected client was holding a navigation lock on behalf of its shared environment: */
		if(se->lockingClient==disconnected)
			{
			/* Release the navigation locks of all other clients sharing the same environment: */
			for(std::vector<RemoteClient*>::iterator cIt=se->clients.begin();cIt!=se->clients.end();++cIt)
				{
				(*cIt)->navLockedClient=0;
				(*cIt)->setNavTransform(disconnectedNavTransform);
				}
			
			/* Release this client's navigation lock if it is part of the same shared environment: */
			selfReleaseNavLock=self.sharedEnvironment==se;
			
			/* Release the shared environment's lock: */
			se->lockingClient=0;
			}
		
		/* Check if this was the last client taking part in that shared environment: */
		if(se->clients.empty())
			{
			/* Remove the no-longer-shared environment from the map: */
			sharedEnvironmentMap.removeEntry(se->id);
			}
		}
	
	/* Check if this client is locked to the disconnected client: */
	if(self.navLockedClient==disconnected)
		{
		selfReleaseNavLock=true;
		
		/* Release the lock: */
		self.navLockedClient=0;
		self.navTransform*=disconnectedNavTransform;
		self.navTransform.renormalize();
		}
	
	/* Check if any remaining remote clients hold navigation locks on the disconnected client: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt,++index)
		{
		/* Check for a navigation lock to the disconnected client: */
		if((*rcIt)->navLockedClient==disconnected)
			{
			/* Release the lock: */
			(*rcIt)->navLockedClient=0;
			(*rcIt)->navTransform*=disconnectedNavTransform;
			(*rcIt)->navTransform.renormalize();
			}
		}
	
	/* Destroy the disconnected client's Vrui Core state: */
	delete disconnected;
	
	/* Check if this client ended a navigation sequence or released a navigation lock: */
	if(selfEndNavSequence||selfReleaseNavLock)
		{
		/* Unlock Vrui's navigation transformation: */
		Vrui::deactivateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
		navigationTransformationLocked=false;
		
		/* Resume sending navigation updates: */
		--noNavigationUpdateCallback;
		
		if(selfReleaseNavLock)
			{
			/* Send a navigation update message to the server to get all participants synched again: */
			sendNavTransform(self.navTransform);
			}
		}
	
	/* Update the Vrui Core settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::connectReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the ID of the shared physical environment: */
	unsigned int sharedEnvironmentId=message.read<Misc::UInt16>();
	
	/* Create a new shared environment with that ID: */
	sharedEnvironmentMap.setEntry(SharedEnvironmentMap::Entry(sharedEnvironmentId,SharedEnvironment(sharedEnvironmentId)));
	
	/* Add this client's self-representation to the new shared environment: */
	self.sharedEnvironment=&sharedEnvironmentMap.getEntry(sharedEnvironmentId).getDest();
	self.sharedEnvironment->clients.push_back(&self);
	
	/* Read the shared environment's current navigation transformation: */
	Misc::read(message,self.navTransform);
	
	/* Update Vrui's navigation transformation: */
	setNavTransform(self.navTransform);
	
	/* Install a callback to get notified when Vrui's navigation transformation is locked or unlocked: */
	Vrui::getNavigationToolActivationCallbacks().add(this,&VruiCoreClient::navigationToolActivationCallback);
	}

void VruiCoreClient::connectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the new client's complete configuration and state from the connect notification message: */
	RemoteClient* newClient=getRemoteClient(message);
	
	/* Find the shared environment of which the new client is part: */
	unsigned int sharedEnvironmentId=message.read<Misc::UInt16>();
	if(sharedEnvironmentId!=0)
		{
		SharedEnvironmentMap::Iterator seIt=sharedEnvironmentMap.findEntry(sharedEnvironmentId);
		if(seIt.isFinished())
			{
			/* Create a new shared environment with that ID: */
			sharedEnvironmentMap.setEntry(SharedEnvironmentMap::Entry(sharedEnvironmentId,SharedEnvironment(sharedEnvironmentId)));
			
			/* Add the new client to the new shared environment: */
			newClient->sharedEnvironment=&sharedEnvironmentMap.getEntry(sharedEnvironmentId).getDest();
			newClient->sharedEnvironment->clients.push_back(newClient);
			}
		else
			{
			/* Add the new client to the existing shared environment: */
			newClient->sharedEnvironment=&seIt->getDest();
			newClient->sharedEnvironment->clients.push_back(newClient);
			}
		}
	newClient->updateEnvironment(message);
	newClient->updateViewerConfig(message);
	newClient->updateViewerState(message);
	Misc::read(message,newClient->navTransform);
	
	/* Insert the new client into the front-end client list and the remote client list box in alphabetical order by name: */
	int insertIndex;
	for(insertIndex=0;insertIndex<remoteClientList->getNumItems()&&strcmp(newClient->name.c_str(),remoteClientList->getItem(insertIndex))>=0;++insertIndex)
		;
	remoteClients.insert(remoteClients.begin()+insertIndex,newClient);
	remoteClientList->insertItem(insertIndex,newClient->name.c_str(),true);
	
	/* Initialize the new client's scene graph and add it to the main scene graph: */
	NavTransform inverseNav=newClient->getNavTransform();
	inverseNav.doInvert();
	newClient->physicalRoot->transform.setValue(inverseNav);
	newClient->physicalRoot->update();
	navigationalRoot->children.appendValue(newClient->physicalRoot);
	navigationalRoot->update();
	
	/* Color the client's name tag depending on whether the client is in the same shared environment: */
	if(newClient->sharedEnvironment!=0&&newClient->sharedEnvironment==self.sharedEnvironment)
		newClient->nameTagMaterial->diffuseColor.setValue(SceneGraph::Color(1.0,0.25,0.25));
	
	/* Update the Vrui Core settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::environmentUpdateNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Update the remote client's environment: */
	getRemoteClient(message)->updateEnvironment(message);
	}

void VruiCoreClient::viewerConfigUpdateNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Update the remote client's viewer configuration: */
	getRemoteClient(message)->updateViewerConfig(message);
	}

void VruiCoreClient::viewerUpdateNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Update the remote client's viewer state: */
	getRemoteClient(message)->updateViewerState(message);
	}

void VruiCoreClient::startNavSequenceReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Check whether the request was granted: */
	if(readBool(message))
		{
		/* Act depending on the navigation sequence state: */
		if(navSequenceState>=PendingToolActive&&navSequenceState<=PendingSingleton)
			{
			/* Update all clients sharing the environment to the most recently requested navigation transformation: */
			for(std::vector<RemoteClient*>::iterator cIt=self.sharedEnvironment->clients.begin();cIt!=self.sharedEnvironment->clients.end();++cIt)
				(*cIt)->setNavTransform(requestedNavTransform);
			
			if(navSequenceState==PendingToolActive)
				{
				/* Send the most recently requested navigation transformation to the server to keep the navigation sequence going: */
				sendNavTransform(self.navTransform);
				
				/* Go to granted state: */
				navSequenceState=Granted;
				}
			else if(navSequenceState==PendingToolInactive)
				{
				/* Send the most recently requested navigation transformation to the server to notify it of the sequence's end state: */
				sendNavTransform(self.navTransform);
				
				/* Stop the navigation sequence: */
				navSequenceState=Idle;
				
				/* Send a navigation sequence stop request to the server: */
				{
				MessageWriter stopNavSequenceRequest(NavSequenceRequestMsg::createMessage(clientMessageBase+StopNavSequenceRequest));
				client->queueServerMessage(stopNavSequenceRequest.getBuffer());
				}
				}
			else
				{
				/* Stop the navigation sequence: */
				navSequenceState=Idle;
				}
			}
		else if(navSequenceState>=PendingSingletonUpdated&&navSequenceState<=PendingSingletonUpdatedToolActive)
			{
			/* Update all clients sharing the environment to the originally requested navigation transformation: */
			for(std::vector<RemoteClient*>::iterator cIt=self.sharedEnvironment->clients.begin();cIt!=self.sharedEnvironment->clients.end();++cIt)
				(*cIt)->setNavTransform(singletonNavTransform);
			
			if(navSequenceState==PendingSingletonUpdated)
				{
				/* Start another singleton update for the most recently requested navigation transformation: */
				navSequenceState=PendingSingleton;
				savedNavTransform=singletonNavTransform;
				singletonNavTransform=requestedNavTransform;
				
				/* Send the most recently requested navigation transformation to the server: */
				sendNavTransform(requestedNavTransform);
				}
			else
				{
				/* Immediately request another navigation sequence: */
				navSequenceState=PendingToolActive;
				savedNavTransform=singletonNavTransform;
				
				/* Send a navigation sequence start request to the server: */
				{
				MessageWriter startNavSequenceRequest(NavSequenceRequestMsg::createMessage(clientMessageBase+StartNavSequenceRequest));
				client->queueServerMessage(startNavSequenceRequest.getBuffer());
				}
				}
			}
		}
	else // Navigation sequence request was denied
		{
		/* Act depending on the navigation sequence state: */
		if(navSequenceState==PendingToolActive)
			{
			/* Go to denied state: */
			navSequenceState=Denied;
			}
		else if(navSequenceState==PendingToolInactive||navSequenceState==PendingSingleton)
			{
			/* Reset all clients sharing the environment to the saved navigation transformation: */
			for(std::vector<RemoteClient*>::iterator cIt=self.sharedEnvironment->clients.begin();cIt!=self.sharedEnvironment->clients.end();++cIt)
				(*cIt)->setNavTransform(savedNavTransform);
			
			/* Reset Vrui's navigation transformation to its pre-sequence state: */
			setNavTransform(self.navTransform);
			
			/* Stop the navigation sequence: */
			navSequenceState=Idle;
			}
		else if(navSequenceState==PendingSingletonUpdated)
			{
			/* Start another singleton update for the most recently requested navigation transformation: */
			navSequenceState=PendingSingleton;
			singletonNavTransform=requestedNavTransform;
			
			/* Send the most recently requested navigation transformation to the server: */
			sendNavTransform(requestedNavTransform);
			}
		else if(navSequenceState==PendingSingletonToolActive||navSequenceState==PendingSingletonUpdatedToolActive)
			{
			/* Request another navigation sequence: */
			navSequenceState=PendingToolActive;
			
			/* Send a navigation sequence start request to the server: */
			{
			MessageWriter startNavSequenceRequest(NavSequenceRequestMsg::createMessage(clientMessageBase+StartNavSequenceRequest));
			client->queueServerMessage(startNavSequenceRequest.getBuffer());
			}
			}
		}
	}

void VruiCoreClient::startNavSequenceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Set the shared physical environment's navigating client: */
	self.sharedEnvironment->navigatingClient=getRemoteClient(message);
	
	/* Lock Vrui's navigation transformation: */
	navigationTransformationLocked=true;
	if(!Vrui::activateNavigationTool(reinterpret_cast<Vrui::Tool*>(this)))
		{
		/* This is a serious problem and must be dealt with! */
		navigationTransformationLocked=false;
		}
	
	/* Stop sending navigation update callbacks: */
	++noNavigationUpdateCallback;
	
	/* Update Vrui Core's settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::navTransformUpdateNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Check if the remote client is part of a shared physical environment: */
	RemoteClient* rc=getRemoteClient(message);
	if(rc->sharedEnvironment!=0)
		{
		/* Set the navigation transformations of all clients (potentially including this client) that are part of the remote client's shared physical environment: */
		NavTransform navTransform=Misc::Marshaller<NavTransform>::read(message);
		for(std::vector<RemoteClient*>::iterator cIt=rc->sharedEnvironment->clients.begin();cIt!=rc->sharedEnvironment->clients.end();++cIt)
			(*cIt)->setNavTransform(navTransform);
		
		/* Check if this client is part of the same shared physical environment as the remote client: */
		if(self.sharedEnvironment==rc->sharedEnvironment)
			{
			/* Set Vrui's navigation transformation: */
			setNavTransform(self.getNavTransform());
			}
		else
			{
			/* Check if this client is locked to any client sharing the remote client's physical environment: */
			RemoteClient* lockClient=self.navLockedClient;
			while(lockClient!=0&&lockClient->sharedEnvironment!=rc->sharedEnvironment)
				lockClient=lockClient->navLockedClient;
			if(lockClient!=0)
				{
				/* Update Vrui's navigation transformation: */
				setNavTransform(self.getNavTransform());
				}
			}
		}
	else
		{
		/* Update the remote client's navigation transformation: */
		Misc::read(message,rc->navTransform);
		
		/* Check if this client is locked to the remote client: */
		RemoteClient* lockClient=self.navLockedClient;
		while(lockClient!=0&&lockClient!=rc)
			lockClient=lockClient->navLockedClient;
		if(lockClient!=0)
			{
			/* Update Vrui's navigation transformation: */
			setNavTransform(self.getNavTransform());
			}
		}
	}

void VruiCoreClient::stopNavSequenceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Skip the ID of the remote client (what are we going to do about it?): */
	message.read<ClientID>();
	
	/* Reset the shared physical environment's navigating client: */
	self.sharedEnvironment->navigatingClient=0;
	
	/* Unlock Vrui's navigation transformation: */
	if(navigationTransformationLocked)
		Vrui::deactivateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
	navigationTransformationLocked=false;
	
	/* Resume sending navigation updates: */
	--noNavigationUpdateCallback;
	
	/* Update Vrui Core's settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::lockNavTransformReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the lock client: */
	RemoteClient* lockClient=getRemoteClient(message);
	
	/* Read the granted flag: */
	bool requestGranted=readBool(message);
	
	/* Read the lock transform: */
	NavTransform lockTransform=Misc::Marshaller<NavTransform>::read(message);
	
	/* Check if the lock request was granted: */
	if(requestGranted)
		{
		/* Check if this client is part of a shared environment: */
		SharedEnvironment* se=self.sharedEnvironment;
		if(se!=0)
			{
			/* Set the shared environment's locking client: */
			se->lockingClient=&self;
			
			/* Activate navigation locks for all clients sharing the environment: */
			for(std::vector<RemoteClient*>::iterator cIt=se->clients.begin();cIt!=se->clients.end();++cIt)
				{
				(*cIt)->navLockedClient=lockClient;
				(*cIt)->setNavTransform(lockTransform);
				}
			}
		else
			{
			/* Activate the navigation lock: */
			self.navLockedClient=lockClient;
			self.navTransform=lockTransform;
			}
		
		/* Stop sending navigation update callbacks: */
		++noNavigationUpdateCallback;
		
		/* Update Vrui's navigation transformation: */
		setNavTransform(self.getNavTransform());
		}
	else
		{
		/* Unlock Vrui's navigation transformation again: */
		Vrui::deactivateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
		navigationTransformationLocked=false;
		
		/* Update Vrui Core's settings page: */
		updateVruiCoreSettingsPage();
		}
	}

void VruiCoreClient::lockNavTransformNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Check if the remote client is part of a shared physical environment: */
	SharedEnvironment* se=rc->sharedEnvironment;
	if(se!=0)
		{
		/* Set the remote client as the shared environment's locking client: */
		se->lockingClient=rc;
		
		/* Lock all clients sharing the same environment to the lock client: */
		RemoteClient* lockRc=getRemoteClient(message);
		NavTransform lockTransform=Misc::Marshaller<NavTransform>::read(message);
		for(std::vector<RemoteClient*>::iterator cIt=se->clients.begin();cIt!=se->clients.end();++cIt)
			{
			(*cIt)->navLockedClient=lockRc;
			(*cIt)->setNavTransform(lockTransform);
			}
		
		/* Check if this client is sharing the same environment as the remote client: */
		if(self.sharedEnvironment==se)
			{
			/* Lock Vrui's navigation transformation: */
			navigationTransformationLocked=true;
			if(!Vrui::activateNavigationTool(reinterpret_cast<Vrui::Tool*>(this)))
				{
				/* This is a serious problem and must be dealt with! */
				navigationTransformationLocked=false;
				}
			
			/* Stop sending navigation update callbacks: */
			++noNavigationUpdateCallback;
			
			/* Update Vrui's navigation transformation: */
			setNavTransform(self.getNavTransform());
			}
		}
	else
		{
		/* Lock the remote client to the lock client: */
		rc->navLockedClient=getRemoteClient(message);
		Misc::read(message,rc->navTransform);
		}
	
	/* Check if this client is locked to the remote client: */
	RemoteClient* lockClient=self.navLockedClient;
	while(lockClient!=0&&lockClient!=rc)
		lockClient=lockClient->navLockedClient;
	if(lockClient==rc)
		{
		/* Update Vrui's navigation transformation: */
		setNavTransform(self.getNavTransform());
		}
	
	/* Update Vrui Core's settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::unlockNavTransformNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Check if the remote client is part of a shared physical environment: */
	SharedEnvironment* se=rc->sharedEnvironment;
	if(se!=0)
		{
		/* Reset the shared environment's locking client: */
		se->lockingClient=0;
		
		/* Unlock all clients sharing the same environment: */
		NavTransform navTransform=Misc::Marshaller<NavTransform>::read(message);
		for(std::vector<RemoteClient*>::iterator cIt=se->clients.begin();cIt!=se->clients.end();++cIt)
			{
			(*cIt)->navLockedClient=0;
			(*cIt)->setNavTransform(navTransform);
			}
		
		/* Check if this client is sharing the same environment as the remote client: */
		if(self.sharedEnvironment==se)
			{
			/* Unlock Vrui's navigation transformation: */
			if(navigationTransformationLocked)
				Vrui::deactivateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
			navigationTransformationLocked=false;
			
			/* Resume sending navigation updates: */
			--noNavigationUpdateCallback;
			
			/* Update Vrui's navigation transformation: */
			setNavTransform(self.navTransform);
			}
		}
	else
		{
		/* Unlock the remote client: */
		rc->navLockedClient=0;
		Misc::read(message,rc->navTransform);
		}
	
	/* Check if this client is locked to the remote client: */
	RemoteClient* lockClient=self.navLockedClient;
	while(lockClient!=0&&lockClient!=rc)
		lockClient=lockClient->navLockedClient;
	if(lockClient==rc)
		{
		/* Update Vrui's navigation transformation: */
		setNavTransform(self.getNavTransform());
		}
	
	/* Update Vrui Core's settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::createInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Create a new input device state: */
	RemoteClient::DeviceState newDevice;
	
	/* Read the new device's ID and pointing direction: */
	newDevice.id=message.read<InputDeviceID>();
	Misc::read(message,newDevice.rayDirection);
	newDevice.rayStart=message.read<Scalar>();
	
	/* Create the device in disabled state: */
	newDevice.enabled=false;
	
	/* Create a scene graph to represent the new device: */
	newDevice.root=new SceneGraph::TransformNode;
	newDevice.root->scale.setValue(SceneGraph::Size(rc->environment.inchFactor));
	newDevice.root->children.appendValue(device);
	newDevice.root->update();
	
	/* Initialize the new device's glyph transformation: */
	Rotation glyphRot=Rotation::rotateFromTo(Vector(0,1,0),newDevice.rayDirection);
	newDevice.glyphTransform=ONTransform(glyphRot.transform(deviceOffset),glyphRot);
	
	/* Add the new device to the device list and device index map: */
	rc->deviceIndexMap.setEntry(RemoteClient::DeviceIndexMap::Entry(newDevice.id,rc->devices.size()));
	rc->devices.push_back(newDevice);
	}

void VruiCoreClient::updateInputDeviceRayNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Find the device state structure: */
	RemoteClient::DeviceState& device=rc->devices[rc->deviceIndexMap.getEntry(message.read<InputDeviceID>()).getDest()];
	
	/* Update the device's pointing ray: */
	Misc::read(message,device.rayDirection);
	device.rayStart=message.read<Scalar>();
	
	/* Update the new device's glyph transformation: */
	Rotation glyphRot=Rotation::rotateFromTo(Vector(0,1,0),device.rayDirection);
	device.glyphTransform=ONTransform(glyphRot.transform(deviceOffset),glyphRot);
	
	/* Check if the device is enabled and the remote client is showing devices: */
	if(device.enabled&&rc->hideDevicesCount==0)
		{
		/* Update the device's scene graph: */
		ONTransform t=device.transform;
		t*=device.glyphTransform;
		device.root->translation.setValue(t.getTranslation());
		device.root->rotation.setValue(t.getRotation());
		device.root->update();
		}
	}

void VruiCoreClient::updateInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Find the device state structure: */
	RemoteClient::DeviceState& device=rc->devices[rc->deviceIndexMap.getEntry(message.read<InputDeviceID>()).getDest()];
	
	/* Update the device's transformation: */
	Misc::read(message,device.transform);
	
	/* Check if the device is enabled and the remote client is showing devices: */
	if(device.enabled&&rc->hideDevicesCount==0)
		{
		/* Update the device's scene graph: */
		ONTransform t=device.transform;
		t*=device.glyphTransform;
		device.root->translation.setValue(t.getTranslation());
		device.root->rotation.setValue(t.getRotation());
		device.root->update();
		}
	}

void VruiCoreClient::disableInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Find the device state structure: */
	RemoteClient::DeviceState& device=rc->devices[rc->deviceIndexMap.getEntry(message.read<InputDeviceID>()).getDest()];
	
	/* Check if the device is currently shown: */
	if(device.enabled&&rc->hideDevicesCount==0)
		{
		/* Remove the device's scene graph from the remote client's scene graph: */
		rc->physicalRoot->children.removeValue(device.root);
		rc->physicalRoot->update();
		}
	
	/* Disable the device: */
	device.enabled=false;
	}

void VruiCoreClient::enableInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Find the device state structure: */
	RemoteClient::DeviceState& device=rc->devices[rc->deviceIndexMap.getEntry(message.read<InputDeviceID>()).getDest()];
	
	/* Re-initialize the device's transformation: */
	Misc::read(message,device.transform);
	
	/* Check if the device needs to be shown: */
	if(!device.enabled&&rc->hideDevicesCount==0)
		{
		/* Add the device's scene graph to the remote client's scene graph: */
		rc->physicalRoot->children.appendValue(device.root);
		rc->physicalRoot->update();
		}
	
	/* Enable the device: */
	device.enabled=true;
	}

void VruiCoreClient::destroyInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Find the remote client: */
	RemoteClient* rc=getRemoteClient(message);
	
	/* Find the device's index: */
	RemoteClient::DeviceIndexMap::Iterator diIt=rc->deviceIndexMap.findEntry(message.read<InputDeviceID>());
	if(!diIt.isFinished())
		{
		/* Check if the device is currently shown: */
		size_t deviceIndex=diIt->getDest();
		RemoteClient::DeviceState& device=rc->devices[deviceIndex];
		if(device.enabled&&rc->hideDevicesCount==0)
			{
			/* Remove the device's scene graph from the remote client's scene graph: */
			rc->physicalRoot->children.removeValue(device.root);
			rc->physicalRoot->update();
			}
		
		/* Check if the destroyed device is the last in the device list: */
		if(deviceIndex<rc->devices.size()-1)
			{
			/* Move the last device in the list to the front: */
			rc->devices[deviceIndex]=rc->devices.back();
			rc->deviceIndexMap.setEntry(RemoteClient::DeviceIndexMap::Entry(rc->devices[deviceIndex].id,deviceIndex));
			}
		
		/* Remove the destroyed device or the copied last device from the list: */
		rc->devices.pop_back();
		
		/* Remove the destroyed device from the device index map: */
		rc->deviceIndexMap.removeEntry(diIt);
		}
	}

void VruiCoreClient::environmentDefinitionChangedCallback(Vrui::EnvironmentDefinitionChangedCallbackData* cbData)
	{
	/* Update the client's own environment definition: */
	self.updateEnvironmentFromVrui();
	
	/* Send the new definition of the physical environment to the server: */
	{
	MessageWriter environmentUpdateRequest(EnvironmentUpdateMsg::createMessage(clientMessageBase+EnvironmentUpdateRequest));
	environmentUpdateRequest.write(ClientID(client->getId()));
	self.environment.write(environmentUpdateRequest);
	client->queueServerMessage(environmentUpdateRequest.getBuffer());
	}
	}

void VruiCoreClient::mainViewerConfigurationChangedCallback(Vrui::Viewer::ConfigChangedCallbackData* cbData)
	{
	/* Update the client's own viewer configuration: */
	self.updateViewerConfigFromVrui();
	
	/* Retrieve the main viewer's new head-tracking device: */
	Vrui::InputDevice* newHeadDevice=const_cast<Vrui::InputDevice*>(Vrui::getMainViewer()->getHeadDevice());
	if(mainViewerHeadDevice!=newHeadDevice)
		{
		if(mainViewerHeadDevice!=0)
			{
			/* Release the tracking callback with the previous head-tracking device: */
			mainViewerHeadDevice->getTrackingCallbacks().remove(this,&VruiCoreClient::mainViewerTrackingCallback);
			}
		
		/* Set the new head-tracking device: */
		mainViewerHeadDevice=newHeadDevice;
		if(mainViewerHeadDevice!=0)
			{
			/* Install a tracking callback with the new head-tracking device: */
			mainViewerHeadDevice->getTrackingCallbacks().add(this,&VruiCoreClient::mainViewerTrackingCallback);
			}
		}
	
	/* Send the main viewer's new configuration to the server: */
	{
	MessageWriter viewerConfigUpdateRequest(ViewerConfigUpdateMsg::createMessage(clientMessageBase+ViewerConfigUpdateRequest));
	viewerConfigUpdateRequest.write(ClientID(client->getId()));
	self.viewerConfig.write(viewerConfigUpdateRequest);
	client->queueServerMessage(viewerConfigUpdateRequest.getBuffer());
	}
	
	/* Update the client's own viewer state: */
	self.viewerState.headTransform=ONTransform(Vrui::getMainViewer()->getHeadTransformation());
	
	/* Send the new viewer state to the server: */
	{
	MessageWriter viewerUpdateRequest(ViewerUpdateMsg::createMessage(clientMessageBase+ViewerUpdateRequest));
	viewerUpdateRequest.write(ClientID(client->getId()));
	self.viewerState.write(viewerUpdateRequest);
	client->queueServerMessage(viewerUpdateRequest.getBuffer());
	}
	}

void VruiCoreClient::mainViewerTrackingCallback(Vrui::InputDevice::CallbackData* cbData)
	{
	/* Update the client's own viewer state: */
	self.viewerState.headTransform=ONTransform(Vrui::getMainViewer()->getHeadTransformation());
	
	/* Send the new viewer state to the server: */
	{
	MessageWriter viewerUpdateRequest(ViewerUpdateMsg::createMessage(clientMessageBase+ViewerUpdateRequest));
	viewerUpdateRequest.write(ClientID(client->getId()));
	self.viewerState.write(viewerUpdateRequest);
	client->queueServerMessage(viewerUpdateRequest.getBuffer());
	}
	}

void VruiCoreClient::navigationTransformationChangedCallback(Vrui::NavigationTransformationChangedCallbackData* cbData)
	{
	/* Update the navigational scene graph root: */
	navigationalRoot->transform.setValue(cbData->newTransform);
	
	/* Bail out if this update was requested by the Vrui Core client itself: */
	if(noNavigationUpdateCallback>0)
		return;
	
	/* Check whether this client is part of a shared physical environment: */
	if(self.sharedEnvironment!=0)
		{
		/* Retrieve the new navigation transformation: */
		NavTransform newNavTransform(cbData->newTransform);
		
		/* Act depending on navigation sequence state: */
		if(navSequenceState==Idle)
			{
			/* Start a one-off navigation sequence: */
			navSequenceState=PendingSingleton;
			savedNavTransform=self.navTransform;
			requestedNavTransform=newNavTransform;
			
			/* Send the requested navigation transformation to the server: */
			sendNavTransform(requestedNavTransform);
			}
		else if(navSequenceState<Granted)
			{
			/* Update the requested navigation transformation: */
			requestedNavTransform=newNavTransform;
			
			/* If waiting on a one-off update reply, remember that another request was made: */
			if(navSequenceState==PendingSingleton)
				navSequenceState=PendingSingletonUpdated;
			else if(navSequenceState==PendingSingletonToolActive)
				navSequenceState=PendingSingletonUpdatedToolActive;
			}
		else if(navSequenceState==Granted)
			{
			/* Send the new navigation transformation to the server to continue the navigation sequence: */
			sendNavTransform(newNavTransform);
			}
		
		/*******************************************************************
		Since we're not yet blocking updates to Vrui's navigation
		transformation while a request is pending, we need to update all
		clients sharing this environment to the new transformation, or there
		will be mismatches.
		*******************************************************************/
		
		/* Update all clients sharing the environment to the new navigation transformation: */
		for(std::vector<RemoteClient*>::iterator cIt=self.sharedEnvironment->clients.begin();cIt!=self.sharedEnvironment->clients.end();++cIt)
			(*cIt)->setNavTransform(newNavTransform);
		}
	else
		{
		/* Update the client's own navigation transformation: */
		self.navTransform=NavTransform(cbData->newTransform);
		
		/* Send the new navigation transformation to the server: */
		sendNavTransform(self.navTransform);
		}
	}

void VruiCoreClient::navigationToolActivationCallback(Vrui::NavigationToolActivationCallbackData* cbData)
	{
	/* Bail out if this activation/deactivation was requested by the Vrui Core client itself: */
	if(navigationTransformationLocked)
		return;
	
	/* Check whether this is a lock or unlock notification: */
	if(cbData->navigationToolActive)
		{
		/* Act depending on navigation sequence state: */
		if(navSequenceState==Idle)
			{
			/* Start a navigation sequence: */
			navSequenceState=PendingToolActive;
			savedNavTransform=self.navTransform;
			requestedNavTransform=self.navTransform;
			
			/* Send a navigation sequence start request to the server: */
			{
			MessageWriter startNavSequenceRequest(NavSequenceRequestMsg::createMessage(clientMessageBase+StartNavSequenceRequest));
			client->queueServerMessage(startNavSequenceRequest.getBuffer());
			}
			}
		else if(navSequenceState==PendingToolInactive)
			{
			/* Carry the pending request over to the new active tool: */
			navSequenceState=PendingToolActive;
			}
		else if(navSequenceState==PendingSingleton)
			{
			/* Remember that a tool wants to be activated: */
			navSequenceState=PendingSingletonToolActive;
			}
		else if(navSequenceState==PendingSingletonUpdated)
			{
			/* Remember that a tool wants to be activated and that there has been an update: */
			navSequenceState=PendingSingletonUpdatedToolActive;
			}
		}
	else // It's an unlock notification
		{
		/* Act depending on navigation sequence state: */
		if(navSequenceState==PendingToolActive)
			{
			/* Mark the tool as inactive: */
			navSequenceState=PendingToolInactive;
			}
		else if(navSequenceState==PendingSingletonToolActive)
			{
			/* Tool has become inactive again: */
			navSequenceState=PendingSingleton;
			}
		else if(navSequenceState==PendingSingletonUpdatedToolActive)
			{
			/* Tool has become inactive again: */
			navSequenceState=PendingSingletonUpdated;
			}
		else if(navSequenceState==Granted)
			{
			/* Stop the navigation sequence: */
			navSequenceState=Idle;
			
			/* Send a navigation sequence stop request to the server: */
			{
			MessageWriter stopNavSequenceRequest(NavSequenceRequestMsg::createMessage(clientMessageBase+StopNavSequenceRequest));
			client->queueServerMessage(stopNavSequenceRequest.getBuffer());
			}
			}
		else if(navSequenceState==Denied)
			{
			/* Reset all clients sharing the environment to the saved navigation transformation: */
			for(std::vector<RemoteClient*>::iterator cIt=self.sharedEnvironment->clients.begin();cIt!=self.sharedEnvironment->clients.end();++cIt)
				(*cIt)->setNavTransform(savedNavTransform);
			
			/* Reset Vrui's navigation transformation to its pre-sequence state: */
			setNavTransform(self.navTransform);
			
			/* Stop the navigation sequence: */
			navSequenceState=Idle;
			}
		}
	}

unsigned int VruiCoreClient::addInputDevice(Vrui::InputDevice* inputDevice,bool force)
	{
	unsigned int result=0;
	
	/* Check if the input device is a real device, and not the main viewer's head tracking device: */
	Vrui::InputGraphManager* igm=Vrui::getInputGraphManager();
	if(inputDevice!=mainViewerHeadDevice&&(force||igm->isReal(inputDevice)))
		{
		/* Generate a new unique device ID: */
		do
			{
			++lastDeviceId;
			}
		while(lastDeviceId==0||self.deviceIndexMap.isEntry(lastDeviceId));
		
		/* Create a new device structure: */
		RemoteClient::DeviceState newDevice;
		newDevice.id=lastDeviceId;
		newDevice.rayDirection=Vector(inputDevice->getDeviceRayDirection());
		newDevice.rayStart=Scalar(inputDevice->getDeviceRayStart());
		
		/* Check if the device is enabled: */
		newDevice.enabled=igm->isEnabled(inputDevice);
		if(newDevice.enabled)
			{
			/* Retrieve the device's transformation: */
			newDevice.transform=ONTransform(inputDevice->getTransformation());
			}
		
		/* Add the device to the device lists and device index maps: */
		size_t newIndex=self.devices.size();
		self.devices.push_back(newDevice);
		self.deviceIndexMap.setEntry(RemoteClient::DeviceIndexMap::Entry(newDevice.id,newIndex));
		inputDevices.push_back(inputDevice);
		inputDeviceIndexMap.setEntry(InputDeviceIndexMap::Entry(inputDevice,newIndex));
		
		/* Install device ray change and tracking callbacks with the device: */
		inputDevice->getDeviceRayCallbacks().add(this,&VruiCoreClient::inputDeviceRayDirectionCallback);
		inputDevice->getTrackingCallbacks().add(this,&VruiCoreClient::inputDeviceTrackingCallback);
		
		/* Send a device creation message: */
		{
		MessageWriter createInputDeviceRequest(CreateInputDeviceMsg::createMessage(clientMessageBase+CreateInputDeviceRequest));
		createInputDeviceRequest.write(ClientID(client->getId()));
		createInputDeviceRequest.write(InputDeviceID(newDevice.id));
		Misc::write(newDevice.rayDirection,createInputDeviceRequest);
		createInputDeviceRequest.write(newDevice.rayStart);
		client->queueServerMessage(createInputDeviceRequest.getBuffer());
		}
		
		/* Check if the device is enabled: */
		if(newDevice.enabled)
			{
			/* Send a device enablation (haha again) message: */
			{
			MessageWriter enableInputDeviceRequest(EnableInputDeviceMsg::createMessage(clientMessageBase+EnableInputDeviceRequest));
			enableInputDeviceRequest.write(ClientID(client->getId()));
			enableInputDeviceRequest.write(InputDeviceID(newDevice.id));
			Misc::write(newDevice.transform,enableInputDeviceRequest);
			client->queueServerMessage(enableInputDeviceRequest.getBuffer());
			}
			}
		
		result=lastDeviceId;
		}
	
	return result;
	}

void VruiCoreClient::removeInputDevice(Vrui::InputDevice* inputDevice)
	{
	/* Check if the input device is represented: */
	InputDeviceIndexMap::Iterator diIt=inputDeviceIndexMap.findEntry(inputDevice);
	if(!diIt.isFinished())
		{
		/* Get the device's ID: */
		size_t deviceIndex=diIt->getDest();
		unsigned int deviceId=self.devices[deviceIndex].id;
		
		/* Check if the device is the last in the device list: */
		if(deviceIndex<self.devices.size()-1)
			{
			/* Move the last device in the list to the front: */
			self.devices[deviceIndex]=self.devices.back();
			self.deviceIndexMap.setEntry(RemoteClient::DeviceIndexMap::Entry(self.devices[deviceIndex].id,deviceIndex));
			inputDevices[deviceIndex]=inputDevices.back();
			inputDeviceIndexMap.setEntry(InputDeviceIndexMap::Entry(inputDevices[deviceIndex],deviceIndex));
			}
		
		/* Remove the destroyed device or the copied last device from the lists: */
		self.devices.pop_back();
		inputDevices.pop_back();
		
		/* Remove the destroyed device from the device index maps: */
		self.deviceIndexMap.removeEntry(deviceId);
		inputDeviceIndexMap.removeEntry(inputDevice);
		
		/* Send a device destruction message: */
		{
		MessageWriter destroyInputDeviceRequest(DestroyInputDeviceMsg::createMessage(clientMessageBase+DestroyInputDeviceRequest));
		destroyInputDeviceRequest.write(ClientID(client->getId()));
		destroyInputDeviceRequest.write(InputDeviceID(deviceId));
		client->queueServerMessage(destroyInputDeviceRequest.getBuffer());
		}
		}
	}

void VruiCoreClient::inputDeviceCreationCallback(Vrui::InputDeviceManager::InputDeviceCreationCallbackData* cbData)
	{
	/* Add the new device to Vrui Core's representation: */
	addInputDevice(cbData->inputDevice,false);
	}

void VruiCoreClient::inputDeviceDestructionCallback(Vrui::InputDeviceManager::InputDeviceDestructionCallbackData* cbData)
	{
	/* Remove the device from Vrui Core's representation: */
	removeInputDevice(cbData->inputDevice);
	}

void VruiCoreClient::inputDeviceStateChangeCallback(Vrui::InputGraphManager::InputDeviceStateChangeCallbackData* cbData)
	{
	/* Check if the changed input device is represented in the client: */
	InputDeviceIndexMap::Iterator dIt=inputDeviceIndexMap.findEntry(cbData->inputDevice);
	if(!dIt.isFinished())
		{
		/* Get the input device's state structure: */
		ClientInputDeviceState& device=self.devices[dIt->getDest()];
		
		/* Check if the device's state really changed: */
		if(device.enabled!=cbData->newEnabled)
			{
			/* Set the device's state: */
			device.enabled=cbData->newEnabled;
			
			/* Send the new device state to the server: */
			if(device.enabled)
				{
				/* Update the device's transformation (devices get enabled after their transformation is updated): */
				device.transform=ONTransform(cbData->inputDevice->getTransformation());
				
				/* Send an enable device request: */
				{
				MessageWriter enableInputDeviceRequest(EnableInputDeviceMsg::createMessage(clientMessageBase+EnableInputDeviceRequest));
				enableInputDeviceRequest.write(ClientID(client->getId()));
				enableInputDeviceRequest.write(InputDeviceID(device.id));
				Misc::write(device.transform,enableInputDeviceRequest);
				client->queueServerMessage(enableInputDeviceRequest.getBuffer());
				}
				}
			else
				{
				/* Send a disable device request: */
				{
				MessageWriter disableInputDeviceRequest(DisableInputDeviceMsg::createMessage(clientMessageBase+DisableInputDeviceRequest));
				disableInputDeviceRequest.write(ClientID(client->getId()));
				disableInputDeviceRequest.write(InputDeviceID(device.id));
				client->queueServerMessage(disableInputDeviceRequest.getBuffer());
				}
				}
			}
		}
	}

void VruiCoreClient::inputDeviceRayDirectionCallback(Vrui::InputDevice::CallbackData* cbData)
	{
	/* Find the input device in the device list: */
	ClientInputDeviceState& device=self.devices[inputDeviceIndexMap.getEntry(cbData->inputDevice).getDest()];
	
	/* Update the device's pointing ray: */
	device.rayDirection=Vector(cbData->inputDevice->getDeviceRayDirection());
	device.rayStart=Scalar(cbData->inputDevice->getDeviceRayStart());
	
	/* Check whether the device is enabled: */
	if(device.enabled)
		{
		/* Send the new device pointing ray to the server: */
		{
		MessageWriter updateInputDeviceRayRequest(UpdateInputDeviceRayMsg::createMessage(clientMessageBase+UpdateInputDeviceRayRequest));
		updateInputDeviceRayRequest.write(ClientID(client->getId()));
		updateInputDeviceRayRequest.write(InputDeviceID(device.id));
		Misc::write(device.rayDirection,updateInputDeviceRayRequest);
		updateInputDeviceRayRequest.write(device.rayStart);
		client->queueServerMessage(updateInputDeviceRayRequest.getBuffer());
		}
		}
	}

void VruiCoreClient::inputDeviceTrackingCallback(Vrui::InputDevice::CallbackData* cbData)
	{
	/* Find the input device in the device list: */
	ClientInputDeviceState& device=self.devices[inputDeviceIndexMap.getEntry(cbData->inputDevice).getDest()];
	
	/* Update the device's transformation: */
	device.transform=ONTransform(cbData->inputDevice->getTransformation());
	
	/* Check whether the back end is ready and the device is enabled: */
	if(device.enabled)
		{
		/* Send the new device transformation to the server: */
		{
		MessageWriter updateInputDeviceRequest(UpdateInputDeviceMsg::createMessage(clientMessageBase+UpdateInputDeviceRequest));
		updateInputDeviceRequest.write(ClientID(client->getId()));
		updateInputDeviceRequest.write(InputDeviceID(device.id));
		Misc::write(device.transform,updateInputDeviceRequest);
		client->queueServerMessage(updateInputDeviceRequest.getBuffer());
		}
		}
	}

void VruiCoreClient::changeNameCommandCallback(const char* argumentBegin,const char* argumentEnd,void* userData)
	{
	VruiCoreClient* thisPtr=static_cast<VruiCoreClient*>(userData);
	
	/* Retrieve the requested name: */
	std::string newName(argumentBegin,argumentEnd);
	if(thisPtr->client->getClientName()!=newName)
		{
		/* Update the client name text field: */
		thisPtr->clientNameTextField->setString(newName.c_str());
		
		/* Request a name change from the client: */
		thisPtr->client->requestNameChange(newName.c_str());
		}
	}

void VruiCoreClient::showCollaborationDialogCallback(Misc::CallbackData* cbData)
	{
	/* Reset the collaboration dialog to show Vrui Core's settings page if this is the first time it's shown: */
	if(firstShown)
		settingsPager->setCurrentChildIndex(0);
	firstShown=false;

	/* Pop up the collaboration dialog: */
	Vrui::popupPrimaryWidget(collaborationDialog);
	}

void VruiCoreClient::unlockNavTransform(bool sendMessage)
	{
	/* Query the locked client's ID and the current navigation transformation: */
	unsigned int lockClientId=self.navLockedClient->id;
	NavTransform newNavTransform=self.getNavTransform();
	
	/* Check if this client is part of a shared environment: */
	SharedEnvironment* se=self.sharedEnvironment;
	if(se!=0)
		{
		/* Reset the shared environment's locking client: */
		se->lockingClient=0;
		
		/* Unlock the navigation transformations of all clients sharing the environment: */
		for(std::vector<RemoteClient*>::iterator cIt=se->clients.begin();cIt!=se->clients.end();++cIt)
			{
			(*cIt)->navLockedClient=0;
			(*cIt)->setNavTransform(newNavTransform);
			}
		}
	else
		{
		/* Unlock the navigation transformation: */
		self.navLockedClient=0;
		self.navTransform=newNavTransform;
		}
	
	/* Unlock Vrui's navigation transformation: */
	Vrui::deactivateNavigationTool(reinterpret_cast<Vrui::Tool*>(this));
	navigationTransformationLocked=false;
	
	/* Resume sending navigation updates: */
	--noNavigationUpdateCallback;
	
	if(sendMessage)
		{
		/* Send an unlock request to the server: */
		{
		MessageWriter unlockNavTransformRequest(UnlockNavTransformMsg::createMessage(clientMessageBase+UnlockNavTransformRequest));
		unlockNavTransformRequest.write(ClientID(lockClientId));
		Misc::write(self.navTransform,unlockNavTransformRequest);
		client->queueServerMessage(unlockNavTransformRequest.getBuffer());
		}
		}
	}

void VruiCoreClient::requestNavTransformLock(VruiCoreClient::RemoteClient* lockClient,const VruiCoreProtocol::NavTransform& lockTransform)
	{
	/* Send a navigation lock request message to the server: */
	{
	MessageWriter lockNavTransformRequest(LockNavTransformMsg::createMessage(clientMessageBase+LockNavTransformRequest));
	lockNavTransformRequest.write(ClientID(client->getId()));
	lockNavTransformRequest.write(ClientID(lockClient->id));
	Misc::write(lockTransform,lockNavTransformRequest);
	client->queueServerMessage(lockNavTransformRequest.getBuffer());
	}
	}

void VruiCoreClient::updateVruiCoreSettingsPage(void)
	{
	/* Check if this client is part of a shared environment: */
	SharedEnvironment* se=self.sharedEnvironment;
	if(se!=0)
		{
		/* Update shared environment status: */
		if(se->navigatingClient!=0)
			{
			std::string status=se->navigatingClient->name;
			status.append(" is navigating");
			sharedEnvironmentStatusLabel->setString(status.c_str());
			}
		else if(se->lockingClient!=0)
			{
			std::string status=se->lockingClient->name;
			status.append(" is following ");
			status.append(se->lockingClient->navLockedClient->name);
			sharedEnvironmentStatusLabel->setString(status.c_str());
			}
		else
			sharedEnvironmentStatusLabel->setString("Idle");
		}
	
	/* Check if there is a selected remote client: */
	if(remoteClientList->getSelectedItem()>=0)
		{
		/* Retrieve the currently selected client's state: */
		RemoteClient* selectedClient=remoteClients[remoteClientList->getSelectedItem()];
		
		/* Determine whether the selected client follows this client: */
		bool clientFollowsUs=false;
		if(se!=0)
			{
			/* Check if the selected client is following any client in this client's shared environment: */
			RemoteClient* lockClient=selectedClient->navLockedClient;
			while(lockClient!=0&&lockClient->sharedEnvironment!=se)
				lockClient=lockClient->navLockedClient;
			clientFollowsUs=lockClient!=0;
			}
		else
			{
			/* Check if the selected client is following this client: */
			RemoteClient* lockClient=selectedClient->navLockedClient;
			while(lockClient!=0&&lockClient!=&self)
				lockClient=lockClient->navLockedClient;
			clientFollowsUs=lockClient!=0;
			}
		
		/* Set the following indicator: */
		clientFollowsYouToggle->setToggle(clientFollowsUs);
		
		/* Determine whether this client can follow the selected client: */
		bool weCanFollowClient=!clientFollowsUs;
		if(se!=0)
			{
			/* Clients can't follow clients in the same shared environment: */
			weCanFollowClient=weCanFollowClient&&se!=selectedClient->sharedEnvironment;
			
			/* There's no following while a navigation sequence is active: */
			weCanFollowClient=weCanFollowClient&&se->navigatingClient==0;
			
			/* There is no following while another client is holding a follow lock: */
			weCanFollowClient=weCanFollowClient&&(se->lockingClient==0||se->lockingClient==&self);
			}
		
		if(weCanFollowClient)
			{
			/* Enable all follow settings: */
			followClientToggle->setEnabled(true);
			alignWithClientToggle->setEnabled(true);
			faceClientToggle->setEnabled(true);
			
			/* Check if this client directly follows the selected client: */
			if(self.navLockedClient==selectedClient)
				{
				/* Set the appropriate toggle: */
				followClientToggle->setToggle(followMode==0);
				alignWithClientToggle->setToggle(followMode==1);
				faceClientToggle->setToggle(followMode==2);
				}
			else
				{
				/* Reset all follow toggles: */
				followClientToggle->setToggle(false);
				alignWithClientToggle->setToggle(false);
				faceClientToggle->setToggle(false);
				}
			}
		else
			{
			/* Unset and disable all follow settings: */
			followClientToggle->setToggle(false);
			followClientToggle->setEnabled(false);
			alignWithClientToggle->setToggle(false);
			alignWithClientToggle->setEnabled(false);
			faceClientToggle->setToggle(false);
			faceClientToggle->setEnabled(false);
			}
		}
	else
		{
		/* Unset and disable all per-client settings: */
		clientFollowsYouToggle->setToggle(false);
		followClientToggle->setToggle(false);
		followClientToggle->setEnabled(false);
		alignWithClientToggle->setToggle(false);
		alignWithClientToggle->setEnabled(false);
		faceClientToggle->setToggle(false);
		faceClientToggle->setEnabled(false);
		}
	}

void VruiCoreClient::remoteClientListValueChangedCallback(GLMotif::ListBox::ValueChangedCallbackData* cbData)
	{
	/* Update the Vrui Core settings page: */
	updateVruiCoreSettingsPage();
	}

void VruiCoreClient::clientNameValueChangedCallback(GLMotif::TextField::ValueChangedCallbackData* cbData)
	{
	if(client->getClientName()!=cbData->value)
		{
		/* Request a name change from the client: */
		client->requestNameChange(cbData->value);
		}
	}

void VruiCoreClient::drawRemoteMainViewersValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	/* Bail out if nothing changed: */
	if(drawRemoteMainViewers==cbData->set)
		return;
	
	/* Update all connected clients: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		{
		if(cbData->set)
			(*rcIt)->showMainViewer();
		else
			(*rcIt)->hideMainViewer();
		}
	
	/* Update the drawing flag: */
	drawRemoteMainViewers=cbData->set;
	}

void VruiCoreClient::drawRemoteNameTagsValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	/* Bail out if nothing changed: */
	if(drawRemoteNameTags==cbData->set)
		return;
	
	/* Update all connected clients: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		{
		if(cbData->set)
			(*rcIt)->showNameTag();
		else
			(*rcIt)->hideNameTag();
		}
	
	/* Update the drawing flag: */
	drawRemoteNameTags=cbData->set;
	}

void VruiCoreClient::drawRemoteDevicesValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	/* Bail out if nothing changed: */
	if(drawRemoteDevices==cbData->set)
		return;
	
	/* Update all connected clients: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		{
		if(cbData->set)
			(*rcIt)->showDevices();
		else
			(*rcIt)->hideDevices();
		}
	
	/* Update the drawing flag: */
	drawRemoteDevices=cbData->set;
	}

void VruiCoreClient::followClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	if(cbData->set)
		{
		/* Check if this client is already following another client, and release that lock if so: */
		if(self.navLockedClient!=0)
			{
			unlockNavTransform();
			
			/* Reset the other control page buttons, just in case: */
			alignWithClientToggle->setToggle(false);
			faceClientToggle->setToggle(false);
			}
		
		/* Try locking Vrui's navigation transformation: */
		navigationTransformationLocked=true;
		if(Vrui::activateNavigationTool(reinterpret_cast<Vrui::Tool*>(this)))
			{
			/* Retrieve the currently selected client's state: */
			RemoteClient* selectedClient=remoteClients[remoteClientList->getSelectedItem()];
			
			/* Calculate the relative navigation transformation to the selected client: */
			NavTransform lockTransform=self.navTransform;
			lockTransform*=Geometry::invert(selectedClient->getNavTransform());
			
			/* Request a navigation lock: */
			followMode=0;
			requestNavTransformLock(selectedClient,lockTransform);
			}
		else
			{
			navigationTransformationLocked=false;
			
			/* Reset the toggle button: */
			cbData->toggle->setToggle(false);
			}
		}
	else
		{
		/* Unlock the navigation transformation: */
		unlockNavTransform();
		}
	}

void VruiCoreClient::alignWithClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	if(cbData->set)
		{
		/* Check if this client is already following another client, and release that lock if so: */
		if(self.navLockedClient!=0)
			{
			unlockNavTransform();
			
			/* Reset the other control page buttons, just in case: */
			followClientToggle->setToggle(false);
			faceClientToggle->setToggle(false);
			}
		
		/* Try locking Vrui's navigation transformation: */
		navigationTransformationLocked=true;
		if(Vrui::activateNavigationTool(reinterpret_cast<Vrui::Tool*>(this)))
			{
			/* Retrieve the currently selected client's state: */
			RemoteClient* selectedClient=remoteClients[remoteClientList->getSelectedItem()];
			
			/* Calculate the relative navigation transformation to the selected client: */
			NavTransform lockTransform=NavTransform(self.environment.displayCenter-Point::origin,
			                                        self.baseRotation,
			                                        self.environment.displaySize);
			lockTransform*=Geometry::invert(NavTransform(selectedClient->environment.displayCenter-Point::origin,
			                                selectedClient->baseRotation,
			                                selectedClient->environment.displaySize));
			lockTransform.renormalize();
			
			/* Request a navigation lock: */
			followMode=1;
			requestNavTransformLock(selectedClient,lockTransform);
			}
		else
			{
			navigationTransformationLocked=false;
			
			/* Reset the toggle button: */
			cbData->toggle->setToggle(false);
			}
		}
	else
		{
		/* Unlock the navigation transformation: */
		unlockNavTransform();
		}
	}

void VruiCoreClient::faceClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	if(cbData->set)
		{
		/* Check if this client is already following another client, and release that lock if so: */
		if(self.navLockedClient!=0)
			{
			unlockNavTransform();
			
			/* Reset the other control page buttons, just in case: */
			followClientToggle->setToggle(false);
			alignWithClientToggle->setToggle(false);
			}
		
		/* Try locking Vrui's navigation transformation: */
		navigationTransformationLocked=true;
		if(Vrui::activateNavigationTool(reinterpret_cast<Vrui::Tool*>(this)))
			{
			/* Retrieve the currently selected client's state: */
			RemoteClient* selectedClient=remoteClients[remoteClientList->getSelectedItem()];
			
			/* Calculate the relative navigation transformation to the selected client: */
			NavTransform lockTransform=NavTransform(self.floorCenter-Point::origin,
			                                        self.baseRotation,
			                                        self.environment.inchFactor);
			lockTransform*=NavTransform::rotate(Rotation::rotateZ(Math::rad(Scalar(180))));
			lockTransform*=Geometry::invert(NavTransform(selectedClient->floorCenter-Point::origin,
			                                selectedClient->baseRotation,
			                                selectedClient->environment.inchFactor));
			lockTransform.renormalize();
			
			/* Request a navigation lock: */
			followMode=2;
			requestNavTransformLock(selectedClient,lockTransform);
			}
		else
			{
			navigationTransformationLocked=false;
			
			/* Reset the toggle button: */
			cbData->toggle->setToggle(false);
			}
		}
	else
		{
		/* Unlock the navigation transformation: */
		unlockNavTransform();
		}
	}

void VruiCoreClient::createCollaborationDialog(const std::string& sharedEnvironmentName)
	{
	/* Retrieve the GLMotif style sheet: */
	const GLMotif::StyleSheet& ss=*Vrui::getUiStyleSheet();
	
	/* Create the collaboration dialog window pop-up: */
	collaborationDialog=new GLMotif::PopupWindow("CollaborationDialog",Vrui::getWidgetManager(),"Collaboration Settings");
	collaborationDialog->setCloseButton(true);
	collaborationDialog->setResizableFlags(true,true);
	
	/* Create the main dialog panel with a scrolled list box on the left and a pager for per-protocol settings on the right: */
	GLMotif::RowColumn* rootPanel=new GLMotif::RowColumn("RootPanel",collaborationDialog,false);
	rootPanel->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	rootPanel->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	rootPanel->setNumMinorWidgets(1);
	
	/* Create the remote client list: */
	GLMotif::ScrolledListBox* remoteClientScrolledList=new GLMotif::ScrolledListBox("RemoteClientList",rootPanel,GLMotif::ListBox::ALWAYS_ONE,20,10);
	remoteClientList=remoteClientScrolledList->getListBox();
	remoteClientList->getValueChangedCallbacks().add(this,&VruiCoreClient::remoteClientListValueChangedCallback);
	
	/* Create the settings pager: */
	settingsPager=new GLMotif::Pager("SettingsPager",rootPanel,false);
	settingsPager->setMarginWidth(ss.size*0.5f);
	
	/* Create the Vrui Core settings page: */
	settingsPager->setNextPageName("Vrui Core");
	
	GLMotif::Margin* vruiCoreSettingsMargin=new GLMotif::Margin("VruiCoreSettingsMargin",settingsPager,false);
	vruiCoreSettingsMargin->setAlignment(GLMotif::Alignment(GLMotif::Alignment::HFILL,GLMotif::Alignment::TOP));
	
	GLMotif::RowColumn* vruiCoreSettingsPanel=new GLMotif::RowColumn("VruiCoreSettingsPanel",vruiCoreSettingsMargin,false);
	vruiCoreSettingsPanel->setOrientation(GLMotif::RowColumn::VERTICAL);
	vruiCoreSettingsPanel->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	vruiCoreSettingsPanel->setNumMinorWidgets(1);
	
	/* Create widgets for plug-in-wide settings: */
	GLMotif::RowColumn* clientNameBox=new GLMotif::RowColumn("ClientNameBox",vruiCoreSettingsPanel,false);
	clientNameBox->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	clientNameBox->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	clientNameBox->setNumMinorWidgets(1);
	
	new GLMotif::Label("ClientNameLabel",clientNameBox,"Name");
	
	clientNameTextField=new GLMotif::TextField("ClientNameTextField",clientNameBox,20);
	clientNameTextField->setHAlignment(GLFont::Left);
	clientNameTextField->setFieldWidth(32);
	clientNameTextField->setString(client->getClientName().c_str());
	clientNameTextField->setEditable(true);
	clientNameTextField->getValueChangedCallbacks().add(this,&VruiCoreClient::clientNameValueChangedCallback);
	
	clientNameBox->setColumnWeight(1,1.0f);
	clientNameBox->manageChild();
	
	GLMotif::ToggleButton* drawRemoteMainViewersToggle=new GLMotif::ToggleButton("DrawRemoteMainViewersToggle",vruiCoreSettingsPanel,"Draw Remote Viewers");
	drawRemoteMainViewersToggle->setBorderWidth(0.0f);
	drawRemoteMainViewersToggle->setBorderType(GLMotif::Widget::PLAIN);
	drawRemoteMainViewersToggle->setHAlignment(GLFont::Left);
	drawRemoteMainViewersToggle->setToggle(drawRemoteMainViewers);
	drawRemoteMainViewersToggle->getValueChangedCallbacks().add(this,&VruiCoreClient::drawRemoteMainViewersValueChangedCallback);
	
	GLMotif::ToggleButton* drawRemoteNameTagsToggle=new GLMotif::ToggleButton("DrawRemoteNameTagsToggle",vruiCoreSettingsPanel,"Draw Remote Name Tags");
	drawRemoteNameTagsToggle->setBorderWidth(0.0f);
	drawRemoteNameTagsToggle->setBorderType(GLMotif::Widget::PLAIN);
	drawRemoteNameTagsToggle->setHAlignment(GLFont::Left);
	drawRemoteNameTagsToggle->setToggle(drawRemoteNameTags);
	drawRemoteNameTagsToggle->getValueChangedCallbacks().add(this,&VruiCoreClient::drawRemoteNameTagsValueChangedCallback);
	
	GLMotif::ToggleButton* drawRemoteDevicesToggle=new GLMotif::ToggleButton("DrawRemoteDevicesToggle",vruiCoreSettingsPanel,"Draw Remote Devices");
	drawRemoteDevicesToggle->setBorderWidth(0.0f);
	drawRemoteDevicesToggle->setBorderType(GLMotif::Widget::PLAIN);
	drawRemoteDevicesToggle->setHAlignment(GLFont::Left);
	drawRemoteDevicesToggle->setToggle(drawRemoteDevices);
	drawRemoteDevicesToggle->getValueChangedCallbacks().add(this,&VruiCoreClient::drawRemoteDevicesValueChangedCallback);
	
	if(!sharedEnvironmentName.empty())
		{
		/* Insert a separator for shared environment status: */
		GLMotif::RowColumn* sharedEnvironmentSeparator=new GLMotif::RowColumn("SharedEnvironmentSeparator",vruiCoreSettingsPanel,false);
		sharedEnvironmentSeparator->setOrientation(GLMotif::RowColumn::HORIZONTAL);
		sharedEnvironmentSeparator->setPacking(GLMotif::RowColumn::PACK_TIGHT);
		sharedEnvironmentSeparator->setNumMinorWidgets(1);
		
		new GLMotif::Separator("Sep1",sharedEnvironmentSeparator,GLMotif::Separator::HORIZONTAL,ss.fontHeight*2.0f,GLMotif::Separator::LOWERED);
		std::string separatorLabel=sharedEnvironmentName;
		separatorLabel.append(" Status");
		new GLMotif::Label("Label",sharedEnvironmentSeparator,separatorLabel.c_str());
		new GLMotif::Separator("Sep2",sharedEnvironmentSeparator,GLMotif::Separator::HORIZONTAL,ss.fontHeight*2.0f,GLMotif::Separator::LOWERED);
		
		sharedEnvironmentSeparator->setColumnWeight(0,1.0f);
		sharedEnvironmentSeparator->setColumnWeight(2,1.0f);
		sharedEnvironmentSeparator->manageChild();
		
		/* Create widgets for shared environment status: */
		sharedEnvironmentStatusLabel=new GLMotif::Label("SharedEnvironmentStatusLabel",vruiCoreSettingsPanel,"Idle");
		}
	
	/* Insert a separator for per-client settings: */
	GLMotif::RowColumn* perClientSeparator=new GLMotif::RowColumn("PerClientSeparator",vruiCoreSettingsPanel,false);
	perClientSeparator->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	perClientSeparator->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	perClientSeparator->setNumMinorWidgets(1);
	
	new GLMotif::Separator("Sep1",perClientSeparator,GLMotif::Separator::HORIZONTAL,ss.fontHeight*2.0f,GLMotif::Separator::LOWERED);
	new GLMotif::Label("Label",perClientSeparator,"Per-Client Settings");
	new GLMotif::Separator("Sep2",perClientSeparator,GLMotif::Separator::HORIZONTAL,ss.fontHeight*2.0f,GLMotif::Separator::LOWERED);
	
	perClientSeparator->setColumnWeight(0,1.0f);
	perClientSeparator->setColumnWeight(2,1.0f);
	perClientSeparator->manageChild();
	
	/* Create widgets for per-client settings: */
	clientFollowsYouToggle=new GLMotif::ToggleButton("ClientFollowsYouToggle",vruiCoreSettingsPanel,"Client follows you");
	clientFollowsYouToggle->setBorderWidth(0.0f);
	clientFollowsYouToggle->setBorderType(GLMotif::Widget::PLAIN);
	clientFollowsYouToggle->setHAlignment(GLFont::Left);
	clientFollowsYouToggle->setEnabled(false);
	
	followClientToggle=new GLMotif::ToggleButton("FollowClientToggle",vruiCoreSettingsPanel,"Follow client");
	followClientToggle->setBorderWidth(0.0f);
	followClientToggle->setBorderType(GLMotif::Widget::PLAIN);
	followClientToggle->setHAlignment(GLFont::Left);
	followClientToggle->getValueChangedCallbacks().add(this,&VruiCoreClient::followClientValueChangedCallback);
	followClientToggle->setEnabled(false);
	
	alignWithClientToggle=new GLMotif::ToggleButton("AlignWithClientToggle",vruiCoreSettingsPanel,"Align with client");
	alignWithClientToggle->setBorderWidth(0.0f);
	alignWithClientToggle->setBorderType(GLMotif::Widget::PLAIN);
	alignWithClientToggle->setHAlignment(GLFont::Left);
	alignWithClientToggle->getValueChangedCallbacks().add(this,&VruiCoreClient::alignWithClientValueChangedCallback);
	alignWithClientToggle->setEnabled(false);
	
	faceClientToggle=new GLMotif::ToggleButton("FaceClientToggle",vruiCoreSettingsPanel,"Face client at 1:1 scale");
	faceClientToggle->setBorderWidth(0.0f);
	faceClientToggle->setBorderType(GLMotif::Widget::PLAIN);
	faceClientToggle->setHAlignment(GLFont::Left);
	faceClientToggle->getValueChangedCallbacks().add(this,&VruiCoreClient::faceClientValueChangedCallback);
	faceClientToggle->setEnabled(false);
	
	vruiCoreSettingsPanel->manageChild();
	
	vruiCoreSettingsMargin->manageChild();
	
	settingsPager->manageChild();
	
	rootPanel->setColumnWeight(0,0.5f);
	rootPanel->setColumnWeight(1,0.5f);
	rootPanel->manageChild();
	}

void VruiCoreClient::initialize(void)
	{
	/* Bail out if the plug-in was already initialized: */
	if(initialized)
		return;
	
	/* Mark the plug-in as initialized: */
	initialized=true;
	
	/* Load additional plug-in protocols requested in the Vrui Core configuration file section: */
	typedef std::vector<std::string> StringList;
	StringList protocolNames=vruiCoreConfig.retrieveValue<StringList>("./protocolNames",StringList());
	for(StringList::iterator pnIt=protocolNames.begin();pnIt!=protocolNames.end();++pnIt)
		client->requestPluginProtocol(pnIt->c_str());
	}

VruiCoreClient::VruiCoreClient(Client* sClient)
	:VruiPluginClient(sClient),
	 initialized(false),
	 vruiCoreConfig(client->getRootConfigSection().getSection(Vrui::getRootSectionName())),
	 remoteClientMap(17),
	 self(client->getId()),
	 sharedEnvironmentMap(5),
	 navigationTransformationLocked(false),noNavigationUpdateCallback(0),navSequenceState(Idle),
	 lastDeviceId(0),inputDeviceIndexMap(5),
	 mainViewerHeadDevice(0),
	 drawRemoteMainViewers(vruiCoreConfig.retrieveValue<bool>("./drawRemoteMainViewers",true)),
	 drawRemoteNameTags(vruiCoreConfig.retrieveValue<bool>("./drawRemoteNameTags",true)),
	 drawRemoteDevices(vruiCoreConfig.retrieveValue<bool>("./drawRemoteDevices",true)),
	 showCollaborationDialogButton(0),collaborationDialog(0),
	 collaborationToolBase(0),
	 physicalRoot(new SceneGraph::GroupNode)
	{
	}

VruiCoreClient::~VruiCoreClient(void)
	{
	/* Unregister callbacks with the base client: */
	client->getNameChangeReplyCallbacks().remove(this,&VruiCoreClient::nameChangeReplyCallback);
	client->getNameChangeNotificationCallbacks().remove(this,&VruiCoreClient::nameChangeNotificationCallback);
	
	/* Remove the collaboration tool base class if it has been created: */
	if(collaborationToolBase!=0)
		Vrui::getToolManager()->releaseClass(collaborationToolBase);
	}

const char* VruiCoreClient::getName(void) const
	{
	return protocolName;
	}

unsigned int VruiCoreClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int VruiCoreClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int VruiCoreClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void VruiCoreClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register front-end message handlers: */
	client->setMessageForwarder(serverMessageBase+ConnectReply,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::connectReplyCallback>,this,ConnectReplyMsg::size);
	client->setMessageForwarder(serverMessageBase+ConnectNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::connectNotificationCallback>,this,ConnectNotificationMsg::size);
	client->setMessageForwarder(serverMessageBase+EnvironmentUpdateNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::environmentUpdateNotificationCallback>,this,EnvironmentUpdateMsg::size);
	client->setMessageForwarder(serverMessageBase+ViewerConfigUpdateNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::viewerConfigUpdateNotificationCallback>,this,ViewerConfigUpdateMsg::size);
	client->setMessageForwarder(serverMessageBase+ViewerUpdateNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::viewerUpdateNotificationCallback>,this,ViewerUpdateMsg::size);
	client->setMessageForwarder(serverMessageBase+StartNavSequenceReply,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::startNavSequenceReplyCallback>,this,StartNavSequenceReplyMsg::size);
	client->setMessageForwarder(serverMessageBase+StartNavSequenceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::startNavSequenceNotificationCallback>,this,NavSequenceNotificationMsg::size);
	client->setMessageForwarder(serverMessageBase+NavTransformUpdateNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::navTransformUpdateNotificationCallback>,this,NavTransformUpdateMsg::size);
	client->setMessageForwarder(serverMessageBase+StopNavSequenceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::stopNavSequenceNotificationCallback>,this,NavSequenceNotificationMsg::size);
	client->setMessageForwarder(serverMessageBase+LockNavTransformReply,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::lockNavTransformReplyCallback>,this,LockNavTransformReplyMsg::size);
	client->setMessageForwarder(serverMessageBase+LockNavTransformNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::lockNavTransformNotificationCallback>,this,LockNavTransformMsg::size);
	client->setMessageForwarder(serverMessageBase+UnlockNavTransformNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::unlockNavTransformNotificationCallback>,this,UnlockNavTransformMsg::size);
	client->setMessageForwarder(serverMessageBase+CreateInputDeviceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::createInputDeviceNotificationCallback>,this,CreateInputDeviceMsg::size);
	client->setMessageForwarder(serverMessageBase+UpdateInputDeviceRayNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::updateInputDeviceRayNotificationCallback>,this,UpdateInputDeviceRayMsg::size);
	client->setMessageForwarder(serverMessageBase+UpdateInputDeviceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::updateInputDeviceNotificationCallback>,this,UpdateInputDeviceMsg::size);
	client->setMessageForwarder(serverMessageBase+DisableInputDeviceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::disableInputDeviceNotificationCallback>,this,DisableInputDeviceMsg::size);
	client->setMessageForwarder(serverMessageBase+EnableInputDeviceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::enableInputDeviceNotificationCallback>,this,EnableInputDeviceMsg::size);
	client->setMessageForwarder(serverMessageBase+DestroyInputDeviceNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::destroyInputDeviceNotificationCallback>,this,DestroyInputDeviceMsg::size);
	
	/* Register callbacks with the base client: */
	client->getNameChangeReplyCallbacks().add(this,&VruiCoreClient::nameChangeReplyCallback);
	client->getNameChangeNotificationCallbacks().add(this,&VruiCoreClient::nameChangeNotificationCallback);
	
	/* Add front-end message handlers to handle signals from the back end: */
	client->setFrontendMessageHandler(serverMessageBase+StartNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::startNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+RegisterProtocolNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::registerProtocolNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+NameChangeReplyNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::nameChangeReplyNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+ClientConnectNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::clientConnectNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+ClientNameChangeNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::clientNameChangeNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+ClientDisconnectNotification,Client::wrapMethod<VruiCoreClient,&VruiCoreClient::clientDisconnectNotificationCallback>,this);
	}

void VruiCoreClient::start(void)
	{
	/* Send a start notification to the front end: */
	MessageWriter startNotification(MessageBuffer::create(serverMessageBase+StartNotification,sizeof(ClientID)));
	startNotification.write(ClientID(client->getId()));
	client->queueFrontendMessage(startNotification.getBuffer());
	}

void VruiCoreClient::clientConnected(unsigned int clientId)
	{
	/* Send a client connect notification to the front end: */
	const std::string& name=client->getRemoteClient(clientId)->getName();
	size_t nameLength=name.size();
	MessageWriter clientConnectNotification(MessageBuffer::create(serverMessageBase+ClientConnectNotification,sizeof(ClientID)+sizeof(Misc::UInt16)+nameLength*sizeof(Char)));
	clientConnectNotification.write(ClientID(clientId));
	clientConnectNotification.write(Misc::UInt16(nameLength));
	stringToCharBuffer(name,clientConnectNotification,nameLength);
	client->queueFrontendMessage(clientConnectNotification.getBuffer());
	}

void VruiCoreClient::clientDisconnected(unsigned int clientId)
	{
	/* Send a client disconnect notification to the front end: */
	MessageWriter clientDisconnectNotification(MessageBuffer::create(serverMessageBase+ClientDisconnectNotification,sizeof(ClientID)));
	clientDisconnectNotification.write(ClientID(clientId));
	client->queueFrontendMessage(clientDisconnectNotification.getBuffer());
	}

void VruiCoreClient::frame(void)
	{
	/* Update the scene graphs of all connected clients (this is inefficient and should not be done: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		{
		NavTransform inverseNav=(*rcIt)->getNavTransform();
		inverseNav.doInvert();
		(*rcIt)->physicalRoot->transform.setValue(inverseNav);
		(*rcIt)->physicalRoot->update();
		}
	
	/* Call all dependent protocols: */
	for(std::vector<VruiPluginClient*>::iterator dpIt=dependentProtocols.begin();dpIt!=dependentProtocols.end();++dpIt)
		(*dpIt)->frame();
	}

void VruiCoreClient::display(GLContextData& contextData) const
	{
	/* Go to physical space: */
	Vrui::goToPhysicalSpace(contextData);
	
	/* Render the Vrui Core scene graph: */
	glPushAttrib(GL_ENABLE_BIT|GL_LIGHTING_BIT|GL_TEXTURE_BIT);
	Vrui::renderSceneGraph(physicalRoot.getPointer(),false,contextData);
	glPopAttrib();
	
	/* Call all dependent protocols: */
	for(std::vector<VruiPluginClient*>::const_iterator dpIt=dependentProtocols.begin();dpIt!=dependentProtocols.end();++dpIt)
		(*dpIt)->display(contextData);
	
	/* Return to original space: */
	glPopMatrix();
	}

void VruiCoreClient::sound(ALContextData& contextData) const
	{
	/* Go to physical space: */
	contextData.pushMatrix();
	contextData.loadIdentity();
	
	/* Call all dependent protocols: */
	for(std::vector<VruiPluginClient*>::const_iterator dpIt=dependentProtocols.begin();dpIt!=dependentProtocols.end();++dpIt)
		(*dpIt)->sound(contextData);
	
	/* Return to original space: */
	contextData.popMatrix();
	}

void VruiCoreClient::shutdown(void)
	{
	/* Call all dependent protocols in reverse order: */
	for(std::vector<VruiPluginClient*>::reverse_iterator dpIt=dependentProtocols.rbegin();dpIt!=dependentProtocols.rend();++dpIt)
		(*dpIt)->shutdown();
	
	/* Unregister all callbacks: */
	Vrui::getEnvironmentDefinitionChangedCallbacks().remove(this,&VruiCoreClient::environmentDefinitionChangedCallback);
	if(mainViewerHeadDevice!=0)
		mainViewerHeadDevice->getTrackingCallbacks().remove(this,&VruiCoreClient::mainViewerTrackingCallback);
	Vrui::getMainViewer()->getConfigChangedCallbacks().remove(this,&VruiCoreClient::mainViewerConfigurationChangedCallback);
	Vrui::getNavigationTransformationChangedCallbacks().remove(this,&VruiCoreClient::navigationTransformationChangedCallback);
	Vrui::getNavigationToolActivationCallbacks().remove(this,&VruiCoreClient::navigationToolActivationCallback);
	for(std::vector<Vrui::InputDevice*>::iterator dIt=inputDevices.begin();dIt!=inputDevices.end();++dIt)
		{
		(*dIt)->getDeviceRayCallbacks().remove(this,&VruiCoreClient::inputDeviceRayDirectionCallback);
		(*dIt)->getTrackingCallbacks().remove(this,&VruiCoreClient::inputDeviceTrackingCallback);
		}
	Vrui::getInputDeviceManager()->getInputDeviceCreationCallbacks().remove(this,&VruiCoreClient::inputDeviceCreationCallback);
	Vrui::getInputDeviceManager()->getInputDeviceDestructionCallbacks().remove(this,&VruiCoreClient::inputDeviceDestructionCallback);
	Vrui::getInputGraphManager()->getInputDeviceStateChangeCallbacks().remove(this,&VruiCoreClient::inputDeviceStateChangeCallback);
	
	/* Unregister command callbacks: */
	Vrui::getCommandDispatcher().removeCommandCallback("VruiCore::changeName");
	
	/* Remove the button to show the collaboration dialog from the Vrui system menu: */
	Vrui::removeShowSettingsDialogButton(showCollaborationDialogButton);
	showCollaborationDialogButton=0;
	
	/* Destroy the collaboration dialog: */
	delete collaborationDialog;
	collaborationDialog=0;
	}

void VruiCoreClient::registerDependentProtocol(VruiPluginClient* newProtocol)
	{
	/* Send a notification to the front end: */
	MessageWriter registerProtocolNotification(MessageBuffer::create(serverMessageBase+RegisterProtocolNotification,sizeof(VruiPluginClient*)));
	registerProtocolNotification.write(newProtocol);
	client->queueFrontendMessage(registerProtocolNotification.getBuffer());
	}

unsigned int VruiCoreClient::getInputDeviceId(Vrui::InputDevice* device)
	{
	/* Find the input device in the index map, which may fail: */
	InputDeviceIndexMap::Iterator diIt=inputDeviceIndexMap.findEntry(device);
	if(!diIt.isFinished())
		{
		/* Return the existing device index: */
		return self.devices[diIt->getDest()].id;
		}
	else
		{
		/* Since this device appears to be important, share it with the server: */
		return addInputDevice(device,true);
		}
	}

Vrui::ToolFactory* VruiCoreClient::getCollaborationToolBase(void)
	{
	/* Check if the tool base factory needs to be created: */
	if(collaborationToolBase==0)
		{
		/* Create the abstract tool factory and register it with the tool manager: */
		collaborationToolBase=new Vrui::GenericAbstractToolFactory<Vrui::Tool>("CollaborationTool","Collaboration",0,*Vrui::getToolManager());
		Vrui::getToolManager()->addAbstractClass(collaborationToolBase,Vrui::ToolManager::defaultToolFactoryDestructor);
		}
	
	return collaborationToolBase;
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginClient* createObject(PluginClientLoader& objectLoader,Client* client)
	{
	return new VruiCoreClient(client);
	}

void destroyObject(PluginClient* object)
	{
	delete object;
	}

}
