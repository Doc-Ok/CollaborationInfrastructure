/***********************************************************************
VruiPluginClient - Abstract base class for plug-in protocol clients that
interface with Vrui applications.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/VruiPluginClient.h>

/*********************************
Methods of class VruiPluginClient:
*********************************/

VruiPluginClient::VruiPluginClient(Client* sClient)
	:PluginClient(sClient)
	{
	}

void VruiPluginClient::frontendStart(void)
	{
	}

void VruiPluginClient::frontendClientConnected(unsigned int clientId)
	{
	}

void VruiPluginClient::frontendClientDisconnected(unsigned int clientId)
	{
	}

void VruiPluginClient::frame(void)
	{
	}

void VruiPluginClient::display(GLContextData& contextData) const
	{
	}

void VruiPluginClient::sound(ALContextData& contextData) const
	{
	}

void VruiPluginClient::shutdown(void)
	{
	}
