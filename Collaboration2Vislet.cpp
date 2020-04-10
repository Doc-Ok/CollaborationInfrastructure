/***********************************************************************
Collaboration2Vislet - Vislet class to embed a client for the second-
generation collaboration infrastructure into an otherwise unaware Vrui
application.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2Vislet.h>

#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <Misc/MessageLogger.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/CompoundValueCoders.h>
#include <Vrui/Vrui.h>
#include <Vrui/VisletManager.h>

#include <Collaboration2/Plugins/VruiCoreClient.h>

/**************************************
Methods of class Collaboration2Factory:
**************************************/

Collaboration2Factory::Collaboration2Factory(Vrui::VisletManager& visletManager)
	:Vrui::VisletFactory("Collaboration2",visletManager)
	{
	#if 0
	/* Insert class into class hierarchy: */
	Vrui::VisletFactory* visletFactory=visletManager.loadClass("Vislet");
	visletFactory->addChildClass(this);
	addParentClass(visletFactory);
	#endif
	
	/* Set vislet class's factory pointer: */
	Collaboration2::factory=this;
	}

Collaboration2Factory::~Collaboration2Factory(void)
	{
	/* Reset vislet class's factory pointer: */
	Collaboration2::factory=0;
	}

Vrui::Vislet* Collaboration2Factory::createVislet(int numArguments,const char* const arguments[]) const
	{
	return new Collaboration2(numArguments,arguments);
	}

void Collaboration2Factory::destroyVislet(Vrui::Vislet* vislet) const
	{
	delete vislet;
	}

extern "C" void resolveCollaboration2FactoryDependencies(Plugins::FactoryManager<Vrui::VisletFactory>& manager)
	{
	#if 0
	/* Load base classes: */
	manager.loadClass("Vislet");
	#endif
	}

extern "C" Vrui::VisletFactory* createCollaboration2Factory(Plugins::FactoryManager<Vrui::VisletFactory>& manager)
	{
	/* Get pointer to vislet manager: */
	Vrui::VisletManager* visletManager=static_cast<Vrui::VisletManager*>(&manager);
	
	/* Create factory object and insert it into class hierarchy: */
	Collaboration2Factory* factory=new Collaboration2Factory(*visletManager);
	
	/* Return factory object: */
	return factory;
	}

extern "C" void destroyCollaboration2Factory(Vrui::VisletFactory* factory)
	{
	delete factory;
	}

/***************************************
Static elements of class Collaboration2:
***************************************/

Collaboration2Factory* Collaboration2::factory=0;

/*******************************
Methods of class Collaboration2:
*******************************/

void Collaboration2::frontendPipeIOCallback(int fd,void* userData)
	{
	/* Dispatch front-end messages: */
	static_cast<Collaboration2*>(userData)->client.dispatchFrontendMessages();
	}

bool Collaboration2::shutdownClient(void* userData)
	{
	// DEBUGGING
	Misc::logNote("Collaboration2::Shutting down client");
	
	Collaboration2* thisPtr=static_cast<Collaboration2*>(userData);
	
	/* Join the back-end thread (it already terminated): */
	thisPtr->backendThread.join();
	
	/* Shut down the client: */
	thisPtr->client.shutdown();
	
	/* Shut down the Vrui Core protocol: */
	thisPtr->vruiCore->shutdown();
	
	/* Delete the client object: */
	thisPtr->vruiCore=0;
	
	/* Disable the vislet: */
	thisPtr->Vislet::disable(false);
	
	/* Remove the callback again: */
	return true;
	}

void* Collaboration2::backendThreadMethod(void)
	{
	try
		{
		/* Run the back end; will return when shut down by front end: */
		client.run();
		}
	catch(const std::runtime_error& err)
		{
		/* Notify the user: */
		Misc::formattedUserError("Collaboration2::backendThreadMethod: Client protocol terminated due to exception %s",err.what());
		
		/* Register a callback to shut down the collaboration client from the front end: */
		Vrui::addFrameCallback(&Collaboration2::shutdownClient,this);
		Vrui::requestUpdate();
		
		return 0;
		}
	
	/* Check if the connection was cut by the server: */
	if(client.wasDisconnected())
		{
		/* Notify the user: */
		Misc::userError("Collaboration2::backendThreadMethod: Server shut down the connection");
		
		/* Register a callback to shut down the collaboration client from the front end: */
		Vrui::addFrameCallback(&Collaboration2::shutdownClient,this);
		Vrui::requestUpdate();
		}
	
	return 0;
	}

Collaboration2::Collaboration2(int numArguments,const char* const arguments[])
	:frontendPipeFd(-1),vruiCore(0)
	{
	/* Enable front-end forwarding in the client and hook it into Vrui's main loop: */
	frontendPipeFd=client.enableFrontendForwarding();
	Vrui::addSynchronousIOCallback(frontendPipeFd,&Collaboration2::frontendPipeIOCallback,this);
	
	/* Request the Vrui Core plug-in protocol: */
	vruiCore=VruiCoreClient::requestClient(&client);
	
	/* Parse the command line: */
	std::string serverHostName=client.getDefaultServerHostName();
	int serverPort=client.getDefaultServerPort();
	std::string sessionPassword;
	for(int argi=0;argi<numArguments;++argi)
		{
		if(arguments[argi][0]=='-')
			{
			if(strcasecmp(arguments[argi]+1,"server")==0)
				{
				if(argi+1<numArguments)
					{
					/* Split server name into host name and port: */
					const char* colonPtr=0;
					for(const char* aPtr=arguments[argi+1];*aPtr!='\0';++aPtr)
						if(*aPtr==':')
							colonPtr=aPtr;
					if(colonPtr!=0)
						{
						serverHostName=std::string(arguments[argi+1],colonPtr);
						serverPort=atoi(colonPtr+1);
						}
					else
						serverHostName=arguments[argi+1];
					}
				else
					Misc::formattedUserWarning("Collaboration2: Ignoring dangling command line option %s",arguments[argi]);
				
				++argi;
				}
			else if(strcasecmp(arguments[argi]+1,"serverHostName")==0)
				{
				if(argi+1<numArguments)
					serverHostName=arguments[argi+1];
				else
					Misc::formattedUserWarning("Collaboration2: Ignoring dangling command line option %s",arguments[argi]);
				
				++argi;
				}
			else if(strcasecmp(arguments[argi]+1,"serverPort")==0)
				{
				if(argi+1<numArguments)
					serverPort=atoi(arguments[argi+1]);
				else
					Misc::formattedUserWarning("Collaboration2: Ignoring dangling command line option %s",arguments[argi]);
				
				++argi;
				}
			else if(strcasecmp(arguments[argi]+1,"password")==0)
				{
				if(argi+1<numArguments)
					sessionPassword=arguments[argi+1];
				else
					Misc::formattedUserWarning("Collaboration2: Ignoring dangling command line option %s",arguments[argi]);
				
				++argi;
				}
			else if(strcasecmp(arguments[argi]+1,"name")==0)
				{
				if(argi+1<numArguments)
					{
					/* Change the client's name: */
					client.requestNameChange(arguments[argi+1]);
					}
				else
					Misc::formattedUserWarning("Collaboration2: Ignoring dangling command line option %s",arguments[argi]);
				
				++argi;
				}
			else if(strcasecmp(arguments[argi]+1,"protocol")==0)
				{
				if(argi+1<numArguments)
					{
					/* Request the given protocol: */
					client.requestPluginProtocol(arguments[argi+1]);
					}
				else
					Misc::formattedUserWarning("Collaboration2: Ignoring dangling command line option %s",arguments[argi]);
				
				++argi;
				}
			else
				Misc::formattedUserWarning("Collaboration2: Ignoring unrecognized command line option %s",arguments[argi]);
			}
		else if(Client::isURI(arguments[argi]))
			{
			if(!Client::parseURI(arguments[argi],serverHostName,serverPort,sessionPassword))
				Misc::formattedUserWarning("Collaboration2: Ignoring malformed server URI %s",arguments[argi]);
			}
		else
			Misc::formattedUserWarning("Collaboration2: Ignoring command line argument %s",arguments[argi]);
		}
	
	/* Start the collaboration protocol: */
	client.setPassword(sessionPassword);
	client.start(serverHostName,serverPort);
	}

Collaboration2::~Collaboration2(void)
	{
	/* Remove the front-end pipe I/O callback from Vrui's main loop: */
	Vrui::removeSynchronousIOCallback(frontendPipeFd);
	}

Vrui::VisletFactory* Collaboration2::getFactory(void) const
	{
	return factory;
	}

void Collaboration2::enable(bool startup)
	{
	if(startup&&vruiCore!=0)
		{
		/* Start the client communication protocol in the background: */
		backendThread.start(this,&Collaboration2::backendThreadMethod);
		}
	
	/* Call the base class method: */
	Vislet::enable(startup);
	}

void Collaboration2::disable(bool shutdown)
	{
	if(shutdown)
		{
		/* Shut down the collaboration back-end: */
		client.shutdown();
		
		/* Wait for the back-end thread to terminate: */
		if(!backendThread.isJoined())
			backendThread.join();
		
		if(vruiCore!=0)
			{
			/* Shut down the Vrui Core protocol: */
			vruiCore->shutdown();
			}
		}
	
	/* Call the base class method: */
	Vislet::disable(shutdown);
	}

void Collaboration2::frame(void)
	{
	if(vruiCore!=0)
		{
		/* Let the Vrui Core plug-in update its state: */
		vruiCore->frame();
		}
	}

void Collaboration2::display(GLContextData& contextData) const
	{
	if(vruiCore!=0)
		{
		/* Render the Vrui Core plug-in's state: */
		vruiCore->display(contextData);
		}
	}

void Collaboration2::sound(ALContextData& contextData) const
	{
	if(vruiCore!=0)
		{
		/* Render the Vrui Core plug-in's audio state: */
		vruiCore->sound(contextData);
		}
	}
