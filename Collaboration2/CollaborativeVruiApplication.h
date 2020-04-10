/***********************************************************************
CollaborativeVruiApplication - Class for Vrui applications using the
second-generation collaboration infrastructure.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#ifndef COLLABORATIVEVRUIAPPLICATION_INCLUDED
#define COLLABORATIVEVRUIAPPLICATION_INCLUDED

#include <Threads/Thread.h>
#include <Vrui/Application.h>

#include <Collaboration2/Client.h>

/* Forward declarations: */
class VruiCoreClient;

class CollaborativeVruiApplication:public Vrui::Application
	{
	/* Elements: */
	protected:
	Client client; // The collaboration client
	int frontendPipeFd; // The file descriptor receiving forwarded messages in the front end
	VruiCoreClient* vruiCoreClient; // Vrui Core client plug-in protocol
	private:
	Threads::Thread backendThread; // Thread running the collaboration back end
	
	/* Protected methods: */
	protected:
	void startClient(void); // Starts the collaboration back end
	virtual void shutdownClient(void); // Shuts down collaboration when the server disconnects
	
	/* Private methods: */
	private:
	static void frontendPipeIOCallback(int fd,void* userData); // Method called when there are pending messages on the client's front-end pipe
	static bool shutdownClientWrapper(void* userData); // Shuts down the collaboration client when the server disconnects
	void* backendThreadMethod(void); // Thread method running the collaboration back end
	
	/* Constructors and destructors: */
	public:
	CollaborativeVruiApplication(int& argc,char**& argv);
	virtual ~CollaborativeVruiApplication(void);
	
	/* Methods from class Vrui::Application: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void sound(ALContextData& contextData) const;
	virtual void finishMainLoop(void);
	};

#endif
