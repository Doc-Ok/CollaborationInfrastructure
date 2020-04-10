/***********************************************************************
PluginClient - Base class for clients of plug-in protocols.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINCLIENT_INCLUDED
#define PLUGINCLIENT_INCLUDED

#include <vector>

/* Forward declarations: */
namespace Plugins {
template <class ManagedClassParam>
class ObjectLoader;
}
class Client;

class PluginClient
	{
	/* Embedded classes: */
	public:
	class RemoteClient // Class representing a remote client of the plug-in protocol
		{
		/* Constructors and destructors: */
		public:
		virtual ~RemoteClient(void); // Destroys the remote client representation
		};
	
	typedef std::vector<unsigned int> ClientIDList; // Type for lists of client IDs
	
	/* Elements: */
	protected:
	Client* client; // Pointer to the client object that owns this plug-in protocol
	unsigned int clientProtocolIndex; // Protocol's index in the client's plug-in protocol list
	unsigned int serverProtocolIndex; // Protocol's index in the server's plug-in protocol list
	unsigned int clientMessageBase; // Base message ID for messages sent from a client to a server
	unsigned int serverMessageBase; // Base message ID for messages sent from a server to a client
	ClientIDList remoteClients; // List of indices of remote clients that participate in this plug-in protocol
	
	/* Protected methods: */
	void addClientToList(unsigned int clientId); // Adds a newly-connected remote client to the remote client list
	void removeClientFromList(unsigned int clientId); // Removes a newly-disconnected remote client from the remote client list
	
	/* Constructors and destructors: */
	public:
	PluginClient(Client* sClient); // Creates a plug-in protocol for the given client
	virtual ~PluginClient(void); // Destroys the protocol client plug-in
	
	/* Methods: */
	virtual const char* getName(void) const =0; // Returns the protocol's name
	virtual unsigned int getVersion(void) const =0; // Returns the protocol's version
	void setClientIndex(unsigned int newClientProtocolIndex); // Sets the protocol's client-side index
	unsigned int getClientIndex(void) const // Returns the protocol's client-side index
		{
		return clientProtocolIndex;
		}
	void setServerIndex(unsigned int newServerProtocolIndex); // Sets the protocol's server-side index
	unsigned int getServerIndex(void) const // Returns the protocol's server-side index
		{
		return serverProtocolIndex;
		}
	virtual unsigned int getNumClientMessages(void) const; // Returns the number of messages sent from clients to servers
	virtual unsigned int getNumServerMessages(void) const; // Returns the number of messages sent from servers to clients
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase); // Sets the base ID for client and server messages
	virtual void start(void); // Starts the protocol
	virtual void clientConnected(unsigned int clientId); // Notifies the protocol that a new remote client participating in it has connected
	virtual void clientDisconnected(unsigned int clientId); // Notifies the protocol that a remote client participating in the protocol has disconnected
	};

typedef Plugins::ObjectLoader<PluginClient> PluginClientLoader;

#endif
