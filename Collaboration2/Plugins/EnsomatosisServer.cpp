/***********************************************************************
EnsomatosisServer - Server to share inverse-kinematics driven user
avatars between clients in a collaborative session.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/EnsomatosisServer.h>

#include <Misc/Marshaller.h>
#include <Misc/StandardMarshallers.h>
#include <Geometry/GeometryMarshallers.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/StringContinuation.h>
#include <Collaboration2/Server.h>

/**********************************
Methods of class EnsomatosisServer:
**********************************/

MessageContinuation* EnsomatosisServer::avatarUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the Ensomatosis client state object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* eClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the message: */
	StringContinuation* stringCont=dynamic_cast<StringContinuation*>(continuation);
	if(stringCont==0)
		{
		/* Skip the redundant client ID: */
		socket.read<ClientID>();
		
		/* Read the avatar configuration and scale factor: */
		readAvatarConfiguration(socket,eClient->avatarConfiguration);
		eClient->avatarScale=socket.read<Scalar>();
		
		/* Create a string continuation object to read the avatar file name: */
		stringCont=new StringContinuation(eClient->avatarFileName);
		}
	
	/* Continue reading the message: */
	if(stringCont->read(socket))
		{
		/* Invalidate the client's avatar state: */
		eClient->avatarValid=false;
		
		/* Broadcast an avatar update notification to all connected clients: */
		{
		MessageWriter avatarUpdateNotification(AvatarUpdateMsg::createMessage(serverMessageBase+AvatarUpdateNotification,eClient->avatarFileName));
		avatarUpdateNotification.write(ClientID(clientId));
		writeAvatarConfiguration(eClient->avatarConfiguration,avatarUpdateNotification);
		avatarUpdateNotification.write(Scalar(eClient->avatarScale));
		Misc::write(eClient->avatarFileName,avatarUpdateNotification);
		broadcastMessage(clientId,avatarUpdateNotification.getBuffer());
		}
		
		/* Delete the string reader: */
		delete stringCont;
		stringCont=0;
		}
	
	return stringCont;
	}

MessageContinuation* EnsomatosisServer::avatarStateUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the Ensomatosis client state object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* eClient=client->getPlugin<Client>(pluginIndex);
	
	/* Skip the redundant client ID: */
	socket.read<ClientID>();
	
	/* Read the new avatar state: */
	readAvatarState(socket,eClient->avatarState);
	
	/* Validate the client's avatar state: */
	eClient->avatarValid=true;
	
	/* Broadcast an avatar state update notification to all connected clients: */
	{
	MessageWriter avatarStateUpdateNotification(AvatarStateUpdateMsg::createMessage(serverMessageBase+AvatarStateUpdateNotification));
	avatarStateUpdateNotification.write(ClientID(clientId));
	writeAvatarState(eClient->avatarState,avatarStateUpdateNotification);
	broadcastMessage(clientId,avatarStateUpdateNotification.getBuffer());
	}
	
	/* Done with message: */
	return 0;
	}

EnsomatosisServer::EnsomatosisServer(Server* sServer)
	:PluginServer(sServer)
	{
	}

EnsomatosisServer::~EnsomatosisServer(void)
	{
	}

const char* EnsomatosisServer::getName(void) const
	{
	return protocolName;
	}

unsigned int EnsomatosisServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int EnsomatosisServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int EnsomatosisServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void EnsomatosisServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+AvatarUpdateRequest,Server::wrapMethod<EnsomatosisServer,&EnsomatosisServer::avatarUpdateRequestCallback>,this,AvatarUpdateMsg::size);
	server->setMessageHandler(clientMessageBase+AvatarStateUpdateRequest,Server::wrapMethod<EnsomatosisServer,&EnsomatosisServer::avatarStateUpdateRequestCallback>,this,AvatarStateUpdateMsg::size);
	}

void EnsomatosisServer::start(void)
	{
	}

void EnsomatosisServer::clientConnected(unsigned int clientId)
	{
	/* Get the new client's base state: */
	Server::Client* newClient=server->getClient(clientId);
	
	/* Send the new client avatar configurations and states of all already-connected clients: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		{
		/* Access the client structures: */
		Server::Client* client=server->getClient(*cIt);
		Client* eClient=client->getPlugin<Client>(pluginIndex);
		
		/* Check if the client has a valid avatar: */
		if(!eClient->avatarFileName.empty())
			{
			/* Send an avatar update notification to the new client: */
			{
			MessageWriter avatarUpdateNotification(AvatarUpdateMsg::createMessage(serverMessageBase+AvatarUpdateNotification,eClient->avatarFileName));
			avatarUpdateNotification.write(ClientID(*cIt));
			writeAvatarConfiguration(eClient->avatarConfiguration,avatarUpdateNotification);
			avatarUpdateNotification.write(Scalar(eClient->avatarScale));
			Misc::write(eClient->avatarFileName,avatarUpdateNotification);
			newClient->queueMessage(avatarUpdateNotification.getBuffer());
			}
			
			/* Check if the client has a valid avatar state: */
			if(eClient->avatarValid)
				{
				/* Send an avatar state update notification to all connected clients: */
				MessageWriter avatarStateUpdateNotification(AvatarStateUpdateMsg::createMessage(serverMessageBase+AvatarStateUpdateNotification));
				avatarStateUpdateNotification.write(ClientID(*cIt));
				writeAvatarState(eClient->avatarState,avatarStateUpdateNotification);
				newClient->queueMessage(avatarStateUpdateNotification.getBuffer());
				}
			}
		}
	
	/* Add the new client to the list: */
	addClientToList(clientId);
	
	/* Set the new client's state structure: */
	newClient->setPlugin(pluginIndex,new Client());
	}

void EnsomatosisServer::clientDisconnected(unsigned int clientId)
	{
	/* Remove the disconnected client from the list: */
	removeClientFromList(clientId);
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new EnsomatosisServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}
