/***********************************************************************
PluginClient - Base class for clients of plug-in protocols.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/PluginClient.h>

/*******************************************
Methods of class PluginClient::RemoteClient:
*******************************************/

PluginClient::RemoteClient::~RemoteClient(void)
	{
	}

/*****************************
Methods of class PluginClient:
*****************************/

void PluginClient::addClientToList(unsigned int clientId)
	{
	/* Add the client to the list: */
	remoteClients.push_back(clientId);
	}

void PluginClient::removeClientFromList(unsigned int clientId)
	{
	/* Remove the client from the list: */
	for(ClientIDList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if(*rcIt==clientId)
			{
			/* Remove the client: */
			*rcIt=remoteClients.back();
			remoteClients.pop_back();
			
			/* Stop looking: */
			break;
			}
	}

PluginClient::PluginClient(Client* sClient)
	:client(sClient),
	 clientProtocolIndex(0),serverProtocolIndex(0),
	 clientMessageBase(0),serverMessageBase(0)
	{
	}

PluginClient::~PluginClient(void)
	{
	}

void PluginClient::setClientIndex(unsigned int newClientProtocolIndex)
	{
	clientProtocolIndex=newClientProtocolIndex;
	}

void PluginClient::setServerIndex(unsigned int newServerProtocolIndex)
	{
	serverProtocolIndex=newServerProtocolIndex;
	}

unsigned int PluginClient::getNumClientMessages(void) const
	{
	return 0;
	}

unsigned int PluginClient::getNumServerMessages(void) const
	{
	return 0;
	}

void PluginClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	clientMessageBase=newClientMessageBase;
	serverMessageBase=newServerMessageBase;
	}

void PluginClient::start(void)
	{
	}

void PluginClient::clientConnected(unsigned int clientId)
	{
	/* Add the new client to the list of clients: */
	addClientToList(clientId);
	}

void PluginClient::clientDisconnected(unsigned int clientId)
	{
	/* Remove the client from the list of clients: */
	removeClientFromList(clientId);
	}
