/***********************************************************************
PluginServer - Base class for servers of plug-in protocols.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/PluginServer.h>

#include <Collaboration2/Server.h>

/*************************************
Methods of class PluginServer::Client:
*************************************/

PluginServer::Client::~Client(void)
	{
	}

/*****************************
Methods of class PluginServer:
*****************************/

void PluginServer::removeClientFromList(PluginServer::ClientIDList& list,unsigned int clientId)
	{
	/* Remove the client from the list: */
	for(ClientIDList::iterator cIt=list.begin();cIt!=list.end();++cIt)
		if(*cIt==clientId)
			{
			/* Remove the client: */
			*cIt=list.back();
			list.pop_back();
			
			/* Stop looking: */
			break;
			}
	}

void PluginServer::addClientToList(unsigned int clientId)
	{
	/* Add the client to the list: */
	clients.push_back(clientId);
	}

void PluginServer::sendMessage(unsigned int destClientId,MessageBuffer* message)
	{
	/* Look for the destination client in the list of connected clients: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		if(*cIt==destClientId)
			{
			/* Send the message: */
			server->getClient(*cIt)->queueMessage(message);
			
			/* Stop looking: */
			break;
			}
	}

void PluginServer::broadcastMessage(unsigned int sourceClientId,MessageBuffer* message)
	{
	/* Send the message to all connected clients except the source: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		if(*cIt!=sourceClientId)
			{
			/* Send the message: */
			server->getClient(*cIt)->queueMessage(message);
			}
	}

void PluginServer::sendUDPMessage(unsigned int destClientId,MessageBuffer* message)
	{
	/* Look for the destination client in the list of connected clients: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		if(*cIt==destClientId)
			{
			/* Send the message: */
			server->queueUDPMessage(*cIt,message);
			
			/* Stop looking: */
			break;
			}
	}

void PluginServer::broadcastUDPMessage(unsigned int sourceClientId,MessageBuffer* message)
	{
	/* Send the message to all connected clients except the source: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		if(*cIt!=sourceClientId)
			{
			/* Send the message: */
			server->queueUDPMessage(*cIt,message);
			}
	}

void PluginServer::sendUDPMessageFallback(unsigned int destClientId,MessageBuffer* message)
	{
	/* Look for the destination client in the list of connected clients: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		if(*cIt==destClientId)
			{
			/* Send the message: */
			server->queueUDPMessageFallback(*cIt,message);
			
			/* Stop looking: */
			break;
			}
	}

void PluginServer::broadcastUDPMessageFallback(unsigned int sourceClientId,MessageBuffer* message)
	{
	/* Send the message to all connected clients except the source: */
	for(ClientIDList::iterator cIt=clients.begin();cIt!=clients.end();++cIt)
		if(*cIt!=sourceClientId)
			{
			/* Send the message: */
			server->queueUDPMessageFallback(*cIt,message);
			}
	}

PluginServer::PluginServer(Server* sServer)
	:server(sServer),
	 pluginIndex(0),
	 clientMessageBase(0),serverMessageBase(0)
	{
	}

PluginServer::~PluginServer(void)
	{
	}

void PluginServer::setIndex(unsigned int newIndex)
	{
	pluginIndex=newIndex;
	}

unsigned int PluginServer::getNumClientMessages(void) const
	{
	return 0;
	}

unsigned int PluginServer::getNumServerMessages(void) const
	{
	return 0;
	}

void PluginServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	clientMessageBase=newClientMessageBase;
	serverMessageBase=newServerMessageBase;
	}

void PluginServer::start(void)
	{
	}

void PluginServer::clientConnected(unsigned int clientId)
	{
	/* Add the new client to the list of clients: */
	addClientToList(clientId);
	}

void PluginServer::clientDisconnected(unsigned int clientId)
	{
	/* Remove the client from the list of clients: */
	removeClientFromList(clientId);
	}
