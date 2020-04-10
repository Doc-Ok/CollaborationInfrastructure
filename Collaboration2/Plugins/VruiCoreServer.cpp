/***********************************************************************
VruiCoreServer - Server for the core Vrui collaboration protocol, which
represents remote users' physical VR environments, and maintains their
head positions/orientations and navigation transformations.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/VruiCoreServer.h>

#include <Misc/Marshaller.h>

#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/NonBlockSocket.h>

/***************************************
Methods of class VruiCoreServer::Client:
***************************************/

VruiCoreServer::Client::Client(VruiCoreServer::PhysicalEnvironment* sPhysicalEnvironment,NonBlockSocket& socket)
	:physicalEnvironment(sPhysicalEnvironment),
	 deviceIndexMap(5)
	{
	/* Extract the new client's initial state from the connect request message: */
	environment.read(socket);
	viewerConfig.read(socket);
	viewerState.read(socket);
	Misc::read(socket,navTransform);
	
	/* Check whether the client is sharing a physical environment: */
	if(physicalEnvironment!=0)
		{
		/* Check if this is the first client sharing this physical environment: */
		if(physicalEnvironment->clients.empty())
			{
			/* Initialize the shared environment's navigation transformation: */
			physicalEnvironment->navTransform=navTransform;
			}
		else
			{
			/* Adopt the physical environment's navigation transformation: */
			navTransform=physicalEnvironment->navTransform;
			
			/* Check if the physical environment is navigation-locked to another client: */
			if(physicalEnvironment->lockingClient.valid())
				{
				/* Lock the client to the same client as the rest of the physical environment: */
				navLockClient=physicalEnvironment->lockingClient->navLockClient;
				}
			}
		}
	}

/*******************************
Methods of class VruiCoreServer:
*******************************/

MessageContinuation* VruiCoreServer::connectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object and its TCP socket: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the client's physical environment name: */
	std::string environmentName;
	charBufferToString(socket,ConnectRequestMsg::nameLength,environmentName);
	PhysicalEnvironment* environment=0;
	if(!environmentName.empty())
		{
		/* Check if a physical environment of that name already exists: */
		PhysicalEnvironmentMap::Iterator peIt=physicalEnvironmentMap.findEntry(environmentName);
		if(peIt.isFinished())
			{
			/* Start a new shared physical environment: */
			do
				{
				++lastPhysicalEnvironmentId;
				}
			while(lastPhysicalEnvironmentId==0);
			environment=new PhysicalEnvironment(lastPhysicalEnvironmentId,environmentName);
			physicalEnvironmentMap.setEntry(PhysicalEnvironmentMap::Entry(environmentName,environment));
			}
		else
			{
			/* Join the existing shared physical environment: */
			environment=peIt->getDest();
			}
		}
	
	/* Create a new Vrui Core client structure by reading the message: */
	Client* newClient=new Client(environment,socket);
	
	if(environment!=0)
		{
		/* Add the client to its shared physical environment: */
		environment->clients.push_back(ClientIdentifier(clientId,newClient));
		
		/* Send a connect reply message containing the client's shared environment's ID and fully-resolved navigation transformation: */
		{
		MessageWriter connectReply(ConnectReplyMsg::createMessage(serverMessageBase));
		connectReply.write(Misc::UInt16(environment->id));
		Misc::write(newClient->getNavTransform(),connectReply);
		client->queueMessage(connectReply.getBuffer());
		}
		}
	
	/* Notify all other Vrui Core clients about the new client, and send the other clients' full states to the new client: */
	{
	MessageWriter connectNotification(ConnectNotificationMsg::createMessage(serverMessageBase));
	connectNotification.write(ClientID(clientId));
	connectNotification.write(Misc::UInt16(environment!=0?environment->id:0));
	newClient->environment.write(connectNotification);
	newClient->viewerConfig.write(connectNotification);
	newClient->viewerState.write(connectNotification);
	Misc::write(newClient->navTransform,connectNotification);
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		/* Tell the other client about the new client: */
		Server::Client* otherClient=server->getClient(*cIt);
		otherClient->queueMessage(connectNotification.getBuffer());
		
		/* Tell the new client about the other client, including its fully-qualified navigation transformation: */
		Client* otherVcClient=otherClient->getPlugin<Client>(pluginIndex);
		{
		MessageWriter connectNotification2(ConnectNotificationMsg::createMessage(serverMessageBase));
		connectNotification2.write(ClientID(*cIt));
		connectNotification2.write(Misc::UInt16(otherVcClient->physicalEnvironment!=0?otherVcClient->physicalEnvironment->id:0));
		otherVcClient->environment.write(connectNotification2);
		otherVcClient->viewerConfig.write(connectNotification2);
		otherVcClient->viewerState.write(connectNotification2);
		Misc::write(otherVcClient->getNavTransform(),connectNotification2);
		client->queueMessage(connectNotification2.getBuffer());
		}
		
		/* Tell the new client about the other client's input devices: */
		for(Client::DeviceList::iterator dIt=otherVcClient->devices.begin();dIt!=otherVcClient->devices.end();++dIt)
			{
			/* Send a device creation message: */
			{
			MessageWriter createInputDeviceNotification(CreateInputDeviceMsg::createMessage(serverMessageBase+CreateInputDeviceNotification));
			createInputDeviceNotification.write(ClientID(*cIt));
			createInputDeviceNotification.write(InputDeviceID(dIt->id));
			Misc::write(dIt->rayDirection,createInputDeviceNotification);
			createInputDeviceNotification.write(dIt->rayStart);
			client->queueMessage(createInputDeviceNotification.getBuffer());
			}
			
			/* Check if the device is enabled: */
			if(dIt->enabled)
				{
				/* Send a device enablation (haha) message: */
				{
				MessageWriter enableInputDeviceNotification(EnableInputDeviceMsg::createMessage(serverMessageBase+EnableInputDeviceNotification));
				enableInputDeviceNotification.write(ClientID(*cIt));
				enableInputDeviceNotification.write(InputDeviceID(dIt->id));
				Misc::write(dIt->transform,enableInputDeviceNotification);
				client->queueMessage(enableInputDeviceNotification.getBuffer());
				}
				}
			}
		}
	}
	
	/* Notify the new client of all existing navigation locks: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		Client* otherVcClient=getClient(*cIt);
		if(otherVcClient->navLockClient.valid())
			{
			/* Check if this client is alone, or the lock holder for its shared physical environment: */
			PhysicalEnvironment* pe=otherVcClient->physicalEnvironment;
			if(pe==0||*cIt==pe->lockingClient.id)
				{
				/* Send a lock notification message to the new client: */
				MessageWriter lockNavTransformNotification(LockNavTransformMsg::createMessage(serverMessageBase+LockNavTransformNotification));
				lockNavTransformNotification.write(ClientID(*cIt));
				lockNavTransformNotification.write(ClientID(otherVcClient->navLockClient.id));
				Misc::write(otherVcClient->navTransform,lockNavTransformNotification);
				client->queueMessage(lockNavTransformNotification.getBuffer());
				}
			}
		}
	
	/* Check if the new client is part of a shared physical environment that is currently in a navigation sequence: */
	if(environment!=0&&environment->navigatingClient.valid())
		{
		/* Send a navigation sequence start message to the new client: */
		MessageWriter startNavSequenceNotification(NavSequenceNotificationMsg::createMessage(serverMessageBase+StartNavSequenceNotification));
		startNavSequenceNotification.write(ClientID(environment->navigatingClient.id));
		client->queueMessage(startNavSequenceNotification.getBuffer());
		}
	
	/* Add the new client to the list: */
	addClientToList(clientId);
	
	/* Set the new client's state structure: */
	client->setPlugin(pluginIndex,newClient);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::environmentUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the Vrui Core client state object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the message's placeholder client ID: */
	socket.read<ClientID>();
	
	/* Update the client's environment definition: */
	vcClient->environment.read(socket);
	
	/* Forward the client's environment definition to all other Vrui Core clients: */
	{
	MessageWriter environmentUpdateNotification(EnvironmentUpdateMsg::createMessage(serverMessageBase+EnvironmentUpdateNotification));
	environmentUpdateNotification.write(ClientID(clientId));
	vcClient->environment.write(environmentUpdateNotification);
	broadcastMessage(clientId,environmentUpdateNotification.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::viewerConfigUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the Vrui Core client state object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the message's placeholder client ID: */
	socket.read<ClientID>();
	
	/* Update the client's viewer configuration: */
	vcClient->viewerConfig.read(socket);
	
	/* Forward the client's viewer configuration to all other Vrui Core clients: */
	{
	MessageWriter viewerConfigUpdateNotification(ViewerConfigUpdateMsg::createMessage(serverMessageBase+ViewerConfigUpdateNotification));
	viewerConfigUpdateNotification.write(ClientID(clientId));
	vcClient->viewerConfig.write(viewerConfigUpdateNotification);
	broadcastMessage(clientId,viewerConfigUpdateNotification.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::viewerUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the Vrui Core client state object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the message's placeholder client ID: */
	socket.read<ClientID>();
	
	/* Update the client's viewer state: */
	vcClient->viewerState.read(socket);
	
	/* Forward the client's viewer state to all other Vrui Core clients: */
	{
	MessageWriter viewerUpdateNotification(ViewerUpdateMsg::createMessage(serverMessageBase+ViewerUpdateNotification));
	viewerUpdateNotification.write(ClientID(clientId));
	vcClient->viewerState.write(viewerUpdateNotification);
	broadcastMessage(clientId,viewerUpdateNotification.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::startNavSequenceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, the Vrui Core client state object, and its shared physical environment: */
	Server::Client* client=server->getClient(clientId);
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	PhysicalEnvironment* pe=vcClient->physicalEnvironment;
	if(pe==0)
		throw std::runtime_error("VruiCoreServer::startNavSequenceRequestCallback: Client is not part of a shared environment");
	
	/* It is a protocol error for a client who initiated a navigation lock to then start a navigation sequence: */
	if(pe->lockingClient.id==clientId)
		throw std::runtime_error("VruiCoreServer::startNavSequenceRequestCallback: Lock-holding client started a navigation sequence");
	
	/* Check that there is no other client already holding a navigation lock, or in a navigation sequence, on the same physical environment: */
	bool grantRequest=!(pe->navigatingClient.valid()||pe->lockingClient.valid());
	if(grantRequest)
		{
		/* Set the requesting client as its shared environment's navigation holder: */
		pe->navigatingClient=ClientIdentifier(clientId,vcClient);
		
		/* Send a start navigation sequence notification to all other clients sharing the same physical space: */
		{
		MessageWriter startNavSequenceNotification(NavSequenceNotificationMsg::createMessage(serverMessageBase+StartNavSequenceNotification));
		startNavSequenceNotification.write(ClientID(clientId));
		for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
			if(cIt->id!=clientId)
				server->queueMessage(cIt->id,startNavSequenceNotification.getBuffer());
		}
		}
	
	/* Send a positive or negative start nav sequence reply to the requesting client: */
	{
	MessageWriter startNavSequenceReply(StartNavSequenceReplyMsg::createMessage(serverMessageBase));
	writeBool(grantRequest,startNavSequenceReply);
	client->queueMessage(startNavSequenceReply.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::navTransformUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the Vrui Core client state object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the message's placeholder client ID: */
	socket.read<ClientID>();
	
	/* Read the client's new navigation transformation: */
	NavTransform navTransform=Misc::Marshaller<NavTransform>::read(socket);
	
	bool grantRequest=true;
	
	/* Check if the requesting client is part of a shared physical environment: */
	PhysicalEnvironment* pe=vcClient->physicalEnvironment;
	if(pe!=0)
		{
		/* Check that no other client is currently in a navigation sequence or holding a navigation lock: */
		grantRequest=(!pe->navigatingClient.valid()||pe->navigatingClient.id==clientId)&&(!pe->lockingClient.valid()||pe->lockingClient.id==clientId);
		if(grantRequest)
			{
			/* Update the shared environment's navigation transformation: */
			pe->navTransform=navTransform;
			
			/* Update the navigation transformation of all clients in the shared environment: */
			for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
				(*cIt)->navTransform=navTransform;
			}
		
		/* Check whether this client is its shared environment's navigating client: */
		if(pe->navigatingClient.id!=clientId)
			{
			/* A navigation update outside of a navigation sequence is an implicit sequence start; send a positive or negative start reply: */
			MessageWriter startNavSequenceReply(StartNavSequenceReplyMsg::createMessage(serverMessageBase));
			writeBool(grantRequest,startNavSequenceReply);
			client->queueMessage(startNavSequenceReply.getBuffer());
			}
		}
	else
		{
		/* Update the client's navigation transformation: */
		vcClient->navTransform=navTransform;
		}
	
	if(grantRequest)
		{
		/* Forward the client's navigation transformation to all other Vrui Core clients: */
		{
		MessageWriter navTransformUpdateNotification(NavTransformUpdateMsg::createMessage(serverMessageBase+NavTransformUpdateNotification));
		navTransformUpdateNotification.write(ClientID(clientId));
		Misc::write(vcClient->navTransform,navTransformUpdateNotification);
		broadcastMessage(clientId,navTransformUpdateNotification.getBuffer());
		}
		}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::stopNavSequenceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, the Vrui Core client state object, and its shared physical environment: */
	Server::Client* client=server->getClient(clientId);
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	PhysicalEnvironment* pe=vcClient->physicalEnvironment;
	if(pe==0)
		throw std::runtime_error("VruiCoreServer::stopNavSequenceRequestCallback: Client is not part of a shared environment");
	
	/* Check whether this client is the navigating client in its shared environment: */
	if(!pe->navigatingClient.valid()||pe->navigatingClient.id!=clientId)
		throw std::runtime_error("VruiCoreServer::stopNavSequenceRequestCallback: Client is not its shared environment's navigating client");
	
	/* Reset the shared environment's navigating client: */
	pe->navigatingClient=ClientIdentifier(0,0);
	
	/* Send a stop navigation sequence notification to all other clients sharing the same physical space: */
	{
	MessageWriter stopNavSequenceNotification(NavSequenceNotificationMsg::createMessage(serverMessageBase+StopNavSequenceNotification));
	stopNavSequenceNotification.write(ClientID(clientId));
	for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
		if(cIt->id!=clientId)
			server->queueMessage(cIt->id,stopNavSequenceNotification.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::lockNavTransformRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, the Vrui Core client state object, and its shared physical environment: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	PhysicalEnvironment* pe=vcClient->physicalEnvironment;
	
	/* Read the full message: */
	socket.read<ClientID>(); // Skip source client ID placeholder
	unsigned int lockClientId=socket.read<ClientID>();
	NavTransform lockTransform=Misc::Marshaller<NavTransform>::read(socket);
	
	/* Check if the requesting client wants to lock to an invalid client: */
	if(lockClientId==0)
		throw std::runtime_error("VruiCoreServer::lockNavTransformRequestCallback: Client is trying to lock to invalid client");
	
	/* Check if the requesting client tries to lock to itself: */
	if(lockClientId==clientId)
		throw std::runtime_error("VruiCoreServer::lockNavTransformRequestCallback: Client is trying to lock to itself");
	
	/* Check if the requesting client is in a navigation sequence: */
	if(pe!=0&&pe->navigatingClient.id==clientId)
		throw std::runtime_error("VruiCoreServer::lockNavTransformRequestCallback: Client is in a navigation sequence");
	
	/* Check if the requesting client is already holding a navigation lock: */
	if(vcClient->navLockClient.valid()&&(pe==0||pe->lockingClient.id==clientId))
		throw std::runtime_error("VruiCoreServer::lockNavTransformRequestCallback: Client is already holding a navigation lock");
	
	/* Bail out if the lock client recently disconnected; the requesting client will get a disconnect notification soon enough and realize its mistake: */
	Client* lockVcClient=server->testAndGetPlugin<Client>(lockClientId,pluginIndex);
	if(lockVcClient==0)
		return 0; // Done with the message
	
	/* Request can tentatively be granted if the requesting client is not part of a shared environment, or if the shared environment is neither inside a navigation sequence nor nav-locked: */
	bool grantRequest=pe==0||!(pe->navigatingClient.valid()||pe->lockingClient.valid());
	if(grantRequest)
		{
		/* Check if granting the request would introduce a cycle into the locking graph: */
		Client* lockRoot=lockVcClient;
		while(lockRoot->navLockClient.valid())
			lockRoot=lockRoot->navLockClient.client;
		grantRequest=lockRoot!=vcClient;
		}
	
	if(grantRequest)
		{
		/* Check if the requesting client is part of a shared environment: */
		if(pe!=0)
			{
			/* Set the environment's locking client: */
			pe->lockingClient=ClientIdentifier(clientId,vcClient);
			pe->navTransform=lockTransform;
			
			/* Lock all clients in the shared environment to the lock client: */
			for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
				{
				(*cIt)->navLockClient=ClientIdentifier(lockClientId,lockVcClient);
				(*cIt)->navTransform=lockTransform;
				}
			}
		else
			{
			/* Lock the requesting client to the lock client: */
			vcClient->navLockClient=ClientIdentifier(lockClientId,lockVcClient);
			vcClient->navTransform=lockTransform;
			}
		
		/* Send a lock notification message to all other clients: */
		{
		MessageWriter lockNavTransformNotification(LockNavTransformMsg::createMessage(serverMessageBase+LockNavTransformNotification));
		lockNavTransformNotification.write(ClientID(clientId));
		lockNavTransformNotification.write(ClientID(lockClientId));
		Misc::write(lockTransform,lockNavTransformNotification);
		broadcastMessage(clientId,lockNavTransformNotification.getBuffer());
		}
		}
	
	/* Send a lock reply message to the requesting client: */
	{
	MessageWriter lockNavTransformReply(LockNavTransformReplyMsg::createMessage(serverMessageBase));
	lockNavTransformReply.write(ClientID(lockClientId));
	writeBool(grantRequest,lockNavTransformReply);
	Misc::write(vcClient->navTransform,lockNavTransformReply);
	client->queueMessage(lockNavTransformReply.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::unlockNavTransformRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, the Vrui Core client state object, and its shared physical environment: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	PhysicalEnvironment* pe=vcClient->physicalEnvironment;
	
	/* Read the full message: */
	unsigned int lockClientId=socket.read<ClientID>();
	NavTransform navTransform=Misc::Marshaller<NavTransform>::read(socket);
	
	/* Check if the requesting client wants to unlock from an invalid client: */
	if(lockClientId==0)
		throw std::runtime_error("VruiCoreServer::unlockNavTransformRequestCallback: Client is trying to unlock from invalid client");
	
	/* Check if the client is holding a lock to a different client than the one from which it wants to release: */
	if(vcClient->navLockClient.valid()&&vcClient->navLockClient.id!=lockClientId&&(pe==0||pe->lockingClient.id==clientId))
		throw std::runtime_error("VruiCoreServer::unlockNavTransformRequestCallback: Client is trying to unlock from wrong client");
	
	/* Check if the client is actually holding the navigation lock it requests to release: */
	if(vcClient->navLockClient.id==lockClientId&&(pe==0||pe->lockingClient.id==clientId))
		{
		/* Check if the client is part of a shared environment: */
		if(pe!=0)
			{
			/* Reset the environment's locking client: */
			pe->lockingClient=ClientIdentifier(0,0);
			pe->navTransform=navTransform;
			
			/* Unlock all clients in the shared environment: */
			for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
				{
				(*cIt)->navLockClient=ClientIdentifier(0,0);
				(*cIt)->navTransform=navTransform;
				}
			}
		else
			{
			/* Release the client's navigation lock: */
			vcClient->navLockClient=ClientIdentifier(0,0);
			vcClient->navTransform=navTransform;
			}
		
		/* Send an unlock notification to all other clients: */
		{
		MessageWriter unlockNavTransformNotification(UnlockNavTransformMsg::createMessage(serverMessageBase+UnlockNavTransformNotification));
		unlockNavTransformNotification.write(ClientID(clientId));
		Misc::write(navTransform,unlockNavTransformNotification);
		broadcastMessage(clientId,unlockNavTransformNotification.getBuffer());
		}
		}
	else if(!vcClient->navLockClient.valid()&&(pe==0||!pe->navigatingClient.valid()))
		{
		/* Treat the unlock request as a navigation transformation update: */
		
		/* Check if the client is part of a shared environment: */
		if(pe!=0)
			{
			/* Update the environment's navigation transformation: */
			pe->navTransform=navTransform;
			
			/* Update the navigation transformation of all clients in the shared environment: */
			for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
				(*cIt)->navTransform=navTransform;
			}
		else
			{
			/* Update the client's navigation transformation: */
			vcClient->navTransform=navTransform;
			}
		
		/* Send a navigation transformation update notification to all other clients: */
		MessageWriter navTransformUpdateNotification(NavTransformUpdateMsg::createMessage(serverMessageBase+NavTransformUpdateNotification));
		navTransformUpdateNotification.write(ClientID(clientId));
		Misc::write(navTransform,navTransformUpdateNotification);
		broadcastMessage(clientId,navTransformUpdateNotification.getBuffer());
		}
	else
		{
		/* Send the client's current navigation transformation as an update notification: */
		MessageWriter navTransformUpdateNotification(NavTransformUpdateMsg::createMessage(serverMessageBase+NavTransformUpdateNotification));
		navTransformUpdateNotification.write(ClientID(clientId));
		Misc::write(vcClient->navTransform,navTransformUpdateNotification);
		client->queueMessage(navTransformUpdateNotification.getBuffer());
		}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::createInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the placeholder client ID: */
	socket.read<ClientID>();
	
	/* Create a new device structure in disabled state and read the new device's ID and pointing ray: */
	ClientInputDeviceState newDevice;
	newDevice.id=socket.read<InputDeviceID>();
	Misc::read(socket,newDevice.rayDirection);
	newDevice.rayStart=socket.read<Scalar>();
	newDevice.enabled=false;
	
	/* Check if the device ID already exists: */
	if(vcClient->deviceIndexMap.isEntry(newDevice.id))
		throw std::runtime_error("VruiCoreServer::createInputDeviceRequestCallback: Duplicate device ID");
	
	/* Add the new device structure to the list and map: */
	vcClient->deviceIndexMap.setEntry(Client::DeviceIndexMap::Entry(newDevice.id,vcClient->devices.size()));
	vcClient->devices.push_back(newDevice);
	
	/* Forward the request to all other clients: */
	{
	MessageWriter createInputDeviceNotification(CreateInputDeviceMsg::createMessage(serverMessageBase+CreateInputDeviceNotification));
	createInputDeviceNotification.write(ClientID(clientId));
	createInputDeviceNotification.write(InputDeviceID(newDevice.id));
	Misc::write(newDevice.rayDirection,createInputDeviceNotification);
	createInputDeviceNotification.write(newDevice.rayStart);
	broadcastMessage(clientId,createInputDeviceNotification.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::updateInputDeviceRayRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the placeholder client ID: */
	socket.read<ClientID>();
	
	/* Retrieve the input device structure: */
	ClientInputDeviceState& device=vcClient->devices[vcClient->deviceIndexMap.getEntry(socket.read<InputDeviceID>()).getDest()];
	
	/* Update the device's pointing ray: */
	Misc::read(socket,device.rayDirection);
	device.rayStart=socket.read<Scalar>();
	
	/* Check whether the device is enabled: */
	if(device.enabled)
		{
		/* Forward the request to all other clients: */
		{
		MessageWriter updateInputDeviceRayNotification(UpdateInputDeviceRayMsg::createMessage(serverMessageBase+UpdateInputDeviceRayNotification));
		updateInputDeviceRayNotification.write(ClientID(clientId));
		updateInputDeviceRayNotification.write(InputDeviceID(device.id));
		Misc::write(device.rayDirection,updateInputDeviceRayNotification);
		updateInputDeviceRayNotification.write(device.rayStart);
		broadcastMessage(clientId,updateInputDeviceRayNotification.getBuffer());
		}
		}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::updateInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the placeholder client ID: */
	socket.read<ClientID>();
	
	/* Retrieve the input device structure: */
	ClientInputDeviceState& device=vcClient->devices[vcClient->deviceIndexMap.getEntry(socket.read<InputDeviceID>()).getDest()];
	
	/* Update the device's transformation: */
	Misc::read(socket,device.transform);
	
	/* Check whether the device is enabled: */
	if(device.enabled)
		{
		/* Forward the request to all other clients: */
		{
		MessageWriter updateInputDeviceNotification(UpdateInputDeviceMsg::createMessage(serverMessageBase+UpdateInputDeviceNotification));
		updateInputDeviceNotification.write(ClientID(clientId));
		updateInputDeviceNotification.write(InputDeviceID(device.id));
		Misc::write(device.transform,updateInputDeviceNotification);
		broadcastMessage(clientId,updateInputDeviceNotification.getBuffer());
		}
		}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::disableInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the placeholder client ID: */
	socket.read<ClientID>();
	
	/* Retrieve the input device structure: */
	ClientInputDeviceState& device=vcClient->devices[vcClient->deviceIndexMap.getEntry(socket.read<InputDeviceID>()).getDest()];
	
	/* Check whether the device is enabled: */
	if(device.enabled)
		{
		/* Disable the device: */
		device.enabled=false;
		
		/* Forward the request to all other clients: */
		{
		MessageWriter disableInputDeviceNotification(DisableInputDeviceMsg::createMessage(serverMessageBase+DisableInputDeviceNotification));
		disableInputDeviceNotification.write(ClientID(clientId));
		disableInputDeviceNotification.write(InputDeviceID(device.id));
		broadcastMessage(clientId,disableInputDeviceNotification.getBuffer());
		}
		}
	else
		throw std::runtime_error("VruiCoreServer::disableInputDeviceRequestCallback: Device already disabled");
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::enableInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the placeholder client ID: */
	socket.read<ClientID>();
	
	/* Retrieve the input device structure: */
	ClientInputDeviceState& device=vcClient->devices[vcClient->deviceIndexMap.getEntry(socket.read<InputDeviceID>()).getDest()];
	
	/* Check whether the device is disabled: */
	if(!device.enabled)
		{
		/* Enable the device: */
		device.enabled=true;
		
		/* Update the device's transformation: */
		Misc::read(socket,device.transform);
		
		/* Forward the request to all other clients: */
		{
		MessageWriter enableInputDeviceNotification(EnableInputDeviceMsg::createMessage(serverMessageBase+EnableInputDeviceNotification));
		enableInputDeviceNotification.write(ClientID(clientId));
		enableInputDeviceNotification.write(InputDeviceID(device.id));
		Misc::write(device.transform,enableInputDeviceNotification);
		broadcastMessage(clientId,enableInputDeviceNotification.getBuffer());
		}
		}
	else
		throw std::runtime_error("VruiCoreServer::disableInputDeviceRequestCallback: Device already disabled");
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* VruiCoreServer::destroyInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* vcClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the placeholder client ID: */
	socket.read<ClientID>();
	
	/* Retrieve the input device structure: */
	unsigned int deviceId=socket.read<InputDeviceID>();
	Client::DeviceIndexMap::Iterator diIt=vcClient->deviceIndexMap.findEntry(deviceId);
	if(!diIt.isFinished())
		{
		/* Check if the destroyed device is the last in the device list: */
		size_t deviceIndex=diIt->getDest();
		if(deviceIndex<vcClient->devices.size()-1)
			{
			/* Move the last device in the list to the front: */
			vcClient->devices[deviceIndex]=vcClient->devices.back();
			vcClient->deviceIndexMap.setEntry(Client::DeviceIndexMap::Entry(vcClient->devices[deviceIndex].id,deviceIndex));
			}
		
		/* Remove the destroyed device or the copied last device from the list: */
		vcClient->devices.pop_back();
		
		/* Remove the destroyed device from the device index map: */
		vcClient->deviceIndexMap.removeEntry(deviceId);
		
		/* Forward the request to all other clients: */
		{
		MessageWriter destroyInputDeviceNotification(DestroyInputDeviceMsg::createMessage(serverMessageBase+DestroyInputDeviceNotification));
		destroyInputDeviceNotification.write(ClientID(clientId));
		destroyInputDeviceNotification.write(InputDeviceID(deviceId));
		broadcastMessage(clientId,destroyInputDeviceNotification.getBuffer());
		}
		}
	else
		throw std::runtime_error("VruiCoreServer::destroyInputDeviceRequestCallback: Device does not exist");
	
	/* Done with the message: */
	return 0;
	}

VruiCoreServer::VruiCoreServer(Server* sServer)
	:PluginServer(sServer),
	 lastPhysicalEnvironmentId(0),physicalEnvironmentMap(5)
	{
	}

VruiCoreServer::~VruiCoreServer(void)
	{
	}

const char* VruiCoreServer::getName(void) const
	{
	return protocolName;
	}

unsigned int VruiCoreServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int VruiCoreServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int VruiCoreServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void VruiCoreServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+ConnectRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::connectRequestCallback>,this,ConnectRequestMsg::size);
	server->setMessageHandler(clientMessageBase+EnvironmentUpdateRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::environmentUpdateRequestCallback>,this,EnvironmentUpdateMsg::size);
	server->setMessageHandler(clientMessageBase+ViewerUpdateRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::viewerUpdateRequestCallback>,this,ViewerUpdateMsg::size);
	server->setMessageHandler(clientMessageBase+ViewerConfigUpdateRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::viewerConfigUpdateRequestCallback>,this,ViewerConfigUpdateMsg::size);
	server->setMessageHandler(clientMessageBase+StartNavSequenceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::startNavSequenceRequestCallback>,this,NavSequenceRequestMsg::size);
	server->setMessageHandler(clientMessageBase+NavTransformUpdateRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::navTransformUpdateRequestCallback>,this,NavTransformUpdateMsg::size);
	server->setMessageHandler(clientMessageBase+StopNavSequenceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::stopNavSequenceRequestCallback>,this,NavSequenceRequestMsg::size);
	server->setMessageHandler(clientMessageBase+LockNavTransformRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::lockNavTransformRequestCallback>,this,LockNavTransformMsg::size);
	server->setMessageHandler(clientMessageBase+UnlockNavTransformRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::unlockNavTransformRequestCallback>,this,UnlockNavTransformMsg::size);
	server->setMessageHandler(clientMessageBase+CreateInputDeviceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::createInputDeviceRequestCallback>,this,CreateInputDeviceMsg::size);
	server->setMessageHandler(clientMessageBase+UpdateInputDeviceRayRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::updateInputDeviceRayRequestCallback>,this,UpdateInputDeviceRayMsg::size);
	server->setMessageHandler(clientMessageBase+UpdateInputDeviceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::updateInputDeviceRequestCallback>,this,UpdateInputDeviceMsg::size);
	server->setMessageHandler(clientMessageBase+DisableInputDeviceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::disableInputDeviceRequestCallback>,this,DisableInputDeviceMsg::size);
	server->setMessageHandler(clientMessageBase+EnableInputDeviceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::enableInputDeviceRequestCallback>,this,EnableInputDeviceMsg::size);
	server->setMessageHandler(clientMessageBase+DestroyInputDeviceRequest,Server::wrapMethod<VruiCoreServer,&VruiCoreServer::destroyInputDeviceRequestCallback>,this,DestroyInputDeviceMsg::size);
	}

void VruiCoreServer::start(void)
	{
	}

void VruiCoreServer::clientConnected(unsigned int clientId)
	{
	/* Don't do anything; connection happens when the new client sends its initial full state update */
	}

void VruiCoreServer::clientDisconnected(unsigned int clientId)
	{
	/* Remove the disconnected client from the list: */
	removeClientFromList(clientId);
	
	/* Check if the client is fully connected: */
	Client* vcClient=server->testAndGetPlugin<Client>(clientId,pluginIndex);
	if(vcClient!=0)
		{
		/* Calculate the disconnected client's full navigation transformation: */
		NavTransform disconnectedNavTransform=vcClient->getNavTransform();
		
		/* Check if the client is part of a shared physical environment: */
		PhysicalEnvironment* pe=vcClient->physicalEnvironment;
		if(pe!=0)
			{
			/* Remove the client from its shared physical environment: */
			for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
				if(cIt->client==vcClient)
					{
					/* Move the last client to the disconnecting client's slot and remove the duplicated last client: */
					*cIt=pe->clients.back();
					pe->clients.pop_back();
					
					/* Stop looking: */
					break;
					}
			
			/* Check if the disconnected client was the shared environment's navigating client: */
			if(pe->navigatingClient.id==clientId)
				{
				/* Reset the shared environment's navigating client: */
				pe->navigatingClient=ClientIdentifier(0,0);
				}
			
			/* Check if the disconnected client was the shared environment's locking client: */
			if(pe->lockingClient.id==clientId)
				{
				/* Remove the shared environment's navigation lock: */
				pe->lockingClient=ClientIdentifier(0,0);
				pe->navTransform=disconnectedNavTransform;
				
				/* Remove the navigation locks of all other clients sharing the same environment: */
				for(std::vector<ClientIdentifier>::iterator cIt=pe->clients.begin();cIt!=pe->clients.end();++cIt)
					{
					(*cIt)->navLockClient=ClientIdentifier(0,0);
					(*cIt)->navTransform=disconnectedNavTransform;
					}
				}
			
			/* Check if this was the last client sharing this physical environment: */
			if(pe->clients.empty())
				{
				/* Destroy the shared physical environment: */
				physicalEnvironmentMap.removeEntry(pe->name);
				delete pe;
				}
			}
		
		/*********************************************************************
		A sudden disconnect might leave dangling navigation lock relationships
		that need to be broken before the client is actually disconnected.
		*********************************************************************/
		
		/* Check if any remaining clients hold navigation locks on the disconnected client: */
		for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
			{
			/* Access the client structure: */
			Client* otherVCClient=getClient(*cIt);
			
			/* Check for a navigation lock to the disconnected client: */
			if(otherVCClient->navLockClient.id==clientId)
				{
				/* Break the navigation lock: */
				otherVCClient->navLockClient=ClientIdentifier(0,0);
				otherVCClient->navTransform*=disconnectedNavTransform;
				otherVCClient->navTransform.renormalize();
				}
			}
		}
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new VruiCoreServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}
