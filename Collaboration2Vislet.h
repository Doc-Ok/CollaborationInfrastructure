/***********************************************************************
Collaboration2Vislet - Vislet class to embed a client for the second-
generation collaboration infrastructure into an otherwise unaware Vrui
application.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef COLLABORATION2VISLET_INCLUDED
#define COLLABORATION2VISLET_INCLUDED

#include <Threads/Thread.h>
#include <Vrui/Vislet.h>

#include <Collaboration2/Client.h>

namespace Vrui {
class VisletManager;
}
class VruiCoreClient;

class Collaboration2Factory:public Vrui::VisletFactory
	{
	friend class Collaboration2;
	
	/* Constructors and destructors: */
	public:
	Collaboration2Factory(Vrui::VisletManager& visletManager);
	virtual ~Collaboration2Factory(void);
	
	/* Methods: */
	virtual Vrui::Vislet* createVislet(int numVisletArguments,const char* const visletArguments[]) const;
	virtual void destroyVislet(Vrui::Vislet* vislet) const;
	};

class Collaboration2:public Vrui::Vislet
	{
	friend class Collaboration2Factory;
	
	/* Elements: */
	private:
	static Collaboration2Factory* factory; // Pointer to the factory object for this class
	Client client; // The collaboration client
	int frontendPipeFd; // The file descriptor receiving forwarded messages in the front end
	VruiCoreClient* vruiCore; // Vrui Core plug-in protocol
	Threads::Thread backendThread; // Thread running the collaboration back end
	
	/* Private methods: */
	static void frontendPipeIOCallback(int fd,void* userData); // Method called when there are pending messages on the client's front-end pipe
	static bool shutdownClient(void* userData); // Method called in the front-end when the back-end thread terminates due to some exception
	void* backendThreadMethod(void); // Method running the collaboration back-end thread
	
	/* Constructors and destructors: */
	public:
	Collaboration2(int numArguments,const char* const arguments[]); // Creates a collaboration client vislet from the given argument list
	virtual ~Collaboration2(void); // Disconnects from the collaboration server and destroys the vislet
	
	/* Methods: */
	virtual Vrui::VisletFactory* getFactory(void) const;
	virtual void enable(bool startup);
	virtual void disable(bool shutdown);
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void sound(ALContextData& contextData) const;
	};

#endif
