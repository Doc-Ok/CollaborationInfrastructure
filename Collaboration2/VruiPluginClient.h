/***********************************************************************
VruiPluginClient - Abstract base class for plug-in protocol clients that
interface with Vrui applications.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef VRUIPLUGINCLIENT_INCLUDED
#define VRUIPLUGINCLIENT_INCLUDED

#include <Collaboration2/PluginClient.h>

/* Forward declarations: */
class GLContextData;
class ALContextData;

class VruiPluginClient:public PluginClient
	{
	/* Constructors and destructors: */
	public:
	VruiPluginClient(Client* sClient);
	
	/* New methods: */
	virtual void frontendStart(void); // Called from front end after a protocol registers itself with Vrui Core
	virtual void frontendClientConnected(unsigned int clientId); // Called from front end right after Vrui Core creates a new remote client structure, but before that client is fully initialized or added to the remote client list
	virtual void frontendClientDisconnected(unsigned int clientId); // Called from front end right before Vrui Core destroys one of its remote client structures
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void sound(ALContextData& contextData) const;
	virtual void shutdown(void); // Called from front end immediately after Vrui's main loop shuts down
	};

#endif
