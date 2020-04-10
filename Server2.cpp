/***********************************************************************
Server2 - Server executable for the second-generation collaboration
infrastructure.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <string>
#include <stdexcept>
#include <Misc/MessageLogger.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/ConfigurationFile.h>

#include <Collaboration2/Config.h>
#include <Collaboration2/Server.h>

/*************
Main function:
*************/

int main(int argc,char* argv[])
	{
	/* Ignore SIGPIPE and leave handling of pipe errors to TCP sockets: */
	struct sigaction sigPipeAction;
	sigPipeAction.sa_handler=SIG_IGN;
	sigemptyset(&sigPipeAction.sa_mask);
	sigPipeAction.sa_flags=0x0;
	sigaction(SIGPIPE,&sigPipeAction,0);
	
	try
		{
		/* Open the collaboration configuration file: */
		Misc::ConfigurationFile configFile(COLLABORATION_CONFIGDIR "/" COLLABORATION_CONFIGFILENAME);
		Misc::ConfigurationFileSection serverConfig=configFile.getSection("Collaboration2Server");
		
		/* Parse the command line: */
		int portId=serverConfig.retrieveValue<int>("./listenPort",26000);
		std::string serverName=serverConfig.retrieveString("./serverName","Default Server Name");
		const char* sessionPassword=0;
		for(int argi=1;argi<argc;++argi)
			{
			if(argv[argi][0]=='-')
				{
				if(strcasecmp(argv[argi]+1,"port")==0)
					{
					if(argi+1<argc)
						portId=atoi(argv[argi+1]);
					else
						Misc::formattedUserWarning("Server: Ignoring dangling command line option %s",argv[argi]);
					
					++argi;
					}
				else if(strcasecmp(argv[argi]+1,"password")==0)
					{
					if(argi+1<argc)
						sessionPassword=argv[argi+1];
					else
						Misc::formattedUserWarning("Server: Ignoring dangling command line option %s",argv[argi]);
					
					++argi;
					}
				else if(strcasecmp(argv[argi]+1,"name")==0)
					{
					if(argi+1<argc)
						serverName=argv[argi+1];
					else
						Misc::formattedUserWarning("Server: Ignoring dangling command line option %s",argv[argi]);
					
					++argi;
					}
				else
					Misc::formattedUserWarning("Server: Ignoring unrecognized command line option %s",argv[argi]);
				}
			else
				Misc::formattedUserWarning("Server: Ignoring command line argument %s",argv[argi]);
			}
		
		/* Create the server: */
		Server server(serverConfig,portId,serverName.c_str());
		server.setPassword(sessionPassword);
		
		/* Run the server: */
		server.run();
		}
	catch(const std::runtime_error& err)
		{
		Misc::formattedUserError("Server: Shutting down server due to exception %s",err.what());
		}
	
	/* Clean up and return: */
	return 0;
	}
