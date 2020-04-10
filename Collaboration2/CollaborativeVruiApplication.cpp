/***********************************************************************
CollaborativeVruiApplication - Class for Vrui applications using the
second-generation collaboration infrastructure.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/CollaborativeVruiApplication.h>

#include <stdlib.h>
#include <string.h>
#include <string>
#include <Misc/MessageLogger.h>
#include <Vrui/Vrui.h>

#include <Collaboration2/Plugins/VruiCoreClient.h>

/*********************************************
Methods of class CollaborativeVruiApplication:
*********************************************/

void CollaborativeVruiApplication::frontendPipeIOCallback(int fd,void* userData)
	{
	/* Dispatch front-end messages: */
	static_cast<CollaborativeVruiApplication*>(userData)->client.dispatchFrontendMessages();
	}

bool CollaborativeVruiApplication::shutdownClientWrapper(void* userData)
	{
	/* Call the virtual method: */
	static_cast<CollaborativeVruiApplication*>(userData)->shutdownClient();
	
	/* Remove the callback: */
	return true;
	}

void CollaborativeVruiApplication::startClient(void)
	{
	/* Start the client communication protocol in the background: */
	backendThread.start(this,&CollaborativeVruiApplication::backendThreadMethod);
	}

void CollaborativeVruiApplication::shutdownClient(void)
	{
	/* Join the back-end thread (it already terminated): */
	backendThread.join();
	
	/* Shut down the client: */
	client.shutdown();
	
	/* Shut down the Vrui Core protocol: */
	if(vruiCoreClient!=0)
		vruiCoreClient->shutdown();
	
	/* Delete the client object: */
	vruiCoreClient=0;
	}

void* CollaborativeVruiApplication::backendThreadMethod(void)
	{
	try
		{
		/* Run the back end; will return when shut down by front end: */
		client.run();
		}
	catch(const std::runtime_error& err)
		{
		/* Notify the user: */
		Misc::formattedUserError("CollaborativeVruiApplication::backendThreadMethod: Client protocol terminated due to exception %s",err.what());
		
		/* Register a callback to shut down the collaboration client from the front end: */
		Vrui::addFrameCallback(&CollaborativeVruiApplication::shutdownClientWrapper,this);
		Vrui::requestUpdate();
		
		return 0;
		}
	
	/* Check if the connection was cut by the server: */
	if(client.wasDisconnected())
		{
		/* Notify the user: */
		Misc::userError("CollaborativeVruiApplication::backendThreadMethod: Server shut down the connection");
		
		/* Register a callback to shut down the collaboration client from the front end: */
		Vrui::addFrameCallback(&CollaborativeVruiApplication::shutdownClientWrapper,this);
		Vrui::requestUpdate();
		}
		
	return 0;
	}

namespace {

/***************************************
Helper functions to parse command lines:
***************************************/

void parseArg(char**& argPtr,char**& argEnd,int& value)
	{
	/* Check if there is another argument: */
	char** newArgPtr=argPtr;
	++newArgPtr;
	if(newArgPtr!=argEnd)
		{
		/* Parse the value: */
		value=atoi(*newArgPtr);
		++newArgPtr;
		}
	else
		Misc::formattedUserWarning("CollaborativeVruiApplication: Ignoring dangling command line option %s",*argPtr);
	
	/* Remove the parsed arguments: */
	char** dPtr=argPtr;
	char** sPtr=newArgPtr;
	while(sPtr!=argEnd)
		*(dPtr++)=*(sPtr++);
	argEnd=dPtr;
	
	/* Continue parsing the next argument: */
	--argPtr;
	}

void parseArg(char**& argPtr,char**& argEnd,std::string& value)
	{
	/* Check if there is another argument: */
	char** newArgPtr=argPtr;
	++newArgPtr;
	if(newArgPtr!=argEnd)
		{
		/* Parse the value: */
		value=*newArgPtr;
		++newArgPtr;
		}
	else
		Misc::formattedUserWarning("CollaborativeVruiApplication: Ignoring dangling command line option %s",*argPtr);
	
	/* Remove the parsed arguments: */
	char** dPtr=argPtr;
	char** sPtr=newArgPtr;
	while(sPtr!=argEnd)
		*(dPtr++)=*(sPtr++);
	argEnd=dPtr;
	
	/* Continue parsing the next argument: */
	--argPtr;
	}

}

CollaborativeVruiApplication::CollaborativeVruiApplication(int& argc,char**& argv)
	:Vrui::Application(argc,argv),
	 frontendPipeFd(-1),vruiCoreClient(0)
	{
	/* Enable front-end forwarding in the client and hook it into Vrui's main loop: */
	frontendPipeFd=client.enableFrontendForwarding();
	Vrui::addSynchronousIOCallback(frontendPipeFd,&CollaborativeVruiApplication::frontendPipeIOCallback,this);
	
	/* Request the Vrui Core plug-in protocol: */
	vruiCoreClient=VruiCoreClient::requestClient(&client);
	
	/* Parse the command line: */
	std::string serverHostName=client.getDefaultServerHostName();
	int serverPort=client.getDefaultServerPort();
	std::string sessionPassword;
	char** argEnd=argv+argc;
	for(char** argPtr=argv;argPtr!=argEnd;++argPtr)
		{
		if((*argPtr)[0]=='-')
			{
			if(strcasecmp(*argPtr+1,"server")==0)
				parseArg(argPtr,argEnd,serverHostName);
			else if(strcasecmp(*argPtr+1,"port")==0)
				parseArg(argPtr,argEnd,serverPort);
			else if(strcasecmp(*argPtr+1,"password")==0)
				parseArg(argPtr,argEnd,sessionPassword);
			else if(strcasecmp(*argPtr+1,"name")==0)
				{
				/* Retrieve the client name and request a name change: */
				std::string newClientName;
				parseArg(argPtr,argEnd,newClientName);
				client.requestNameChange(newClientName.c_str());
				}
			}
		else if(Client::isURI(*argPtr))
			{
			/* Parse the server URI: */
			if(!Client::parseURI(*argPtr,serverHostName,serverPort,sessionPassword))
				Misc::formattedUserWarning("CollaborativeVruiApplication: Ignoring malformed server URI %s",argPtr);
			
			/* Remove the server URI: */
			char** dPtr=argPtr;
			char** sPtr=argPtr+1;
			while(sPtr!=argEnd)
				*(dPtr++)=*(sPtr++);
			argEnd=dPtr;
			
			/* Continue parsing the next argument: */
			--argPtr;
			}
		}
	
	/* Start the collaboration protocol: */
	client.setPassword(sessionPassword);
	client.start(serverHostName,serverPort);
	}

CollaborativeVruiApplication::~CollaborativeVruiApplication(void)
	{
	}

void CollaborativeVruiApplication::frame(void)
	{
	if(vruiCoreClient!=0)
		{
		/* Let the Vrui Core plug-in update its state: */
		vruiCoreClient->frame();
		}
	}

void CollaborativeVruiApplication::display(GLContextData& contextData) const
	{
	if(vruiCoreClient!=0)
		{
		/* Render the Vrui Core plug-in's state: */
		vruiCoreClient->display(contextData);
		}
	}

void CollaborativeVruiApplication::sound(ALContextData& contextData) const
	{
	if(vruiCoreClient!=0)
		{
		/* Render the Vrui Core plug-in's audio state: */
		vruiCoreClient->sound(contextData);
		}
	}

void CollaborativeVruiApplication::finishMainLoop(void)
	{
	/* Shut down the collaboration back-end: */
	client.shutdown();
	
	/* Wait for the back-end thread to terminate: */
	if(!backendThread.isJoined())
		backendThread.join();
	
	if(vruiCoreClient!=0)
		{
		/* Shut down the Vrui Core protocol: */
		vruiCoreClient->shutdown();
		}
	}
