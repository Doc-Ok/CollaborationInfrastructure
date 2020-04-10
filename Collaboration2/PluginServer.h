/***********************************************************************
PluginServer - Base class for servers of plug-in protocols.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINSERVER_INCLUDED
#define PLUGINSERVER_INCLUDED

#include <vector>

/* Forward declarations: */
namespace Plugins {
template <class ManagedClassParam>
class ObjectLoader;
}
class MessageBuffer;
class Server;

class PluginServer
	{
	/* Embedded classes: */
	public:
	class Client// Class representing a client of the plug-in protocol
		{
		/* Constructors and destructors: */
		public:
		virtual ~Client(void); // Destroys the client representation
		};
	
	typedef std::vector<unsigned int> ClientIDList; // Type for lists of client IDs
	
	/* Elements: */
	protected:
	Server* server; // Pointer to the server object that owns this plug-in protocol
	unsigned int pluginIndex; // Protocol's index in the server's protocol list; used to access per-client state
	unsigned int clientMessageBase; // Base message ID for messages sent from a client to a server
	unsigned int serverMessageBase; // Base message ID for messages sent from a server to a client
	ClientIDList clients; // List of IDs of connected clients that participate in this plug-in protocol
	
	/* Protected methods: */
	static void removeClientFromList(ClientIDList& list,unsigned int clientId); // Removes the given client from the given client list
	void addClientToList(unsigned int clientId); // Adds a newly-connected client to the client list
	void removeClientFromList(unsigned int clientId) // Removes a newly-disconnected client from the client list
		{
		/* Forward to the general method: */
		removeClientFromList(clients,clientId);
		}
	void sendMessage(unsigned int destClientId,MessageBuffer* message); // Sends the given message to the client with the given ID, if that client participates in this plug-in protocol
	void broadcastMessage(unsigned int sourceClientId,MessageBuffer* message); // Sends the given message to all clients participating in this plug-in protocol, except the one with the given ID
	void sendUDPMessage(unsigned int destClientId,MessageBuffer* message); // Sends the given message over UDP to the client with the given ID, if that client participates in this plug-in protocol
	void broadcastUDPMessage(unsigned int sourceClientId,MessageBuffer* message); // Sends the given message over UDP to all clients participating in this plug-in protocol, except the one with the given ID
	void sendUDPMessageFallback(unsigned int destClientId,MessageBuffer* message); // Sends the given message over UDP to the client with the given ID, if that client participates in this plug-in protocol; falls back to TCP if the client is not UDP-connected
	void broadcastUDPMessageFallback(unsigned int sourceClientId,MessageBuffer* message); // Sends the given message over UDP to all clients participating in this plug-in protocol, except the one with the given ID; falls back to TCP for each client that is not UDP-connected
	
	/* Constructors and destructors: */
	public:
	PluginServer(Server* sServer); // Creates a plug-in protocol for the given server
	virtual ~PluginServer(void); // Destroys the plug-in protocol server
	
	/* Methods: */
	virtual const char* getName(void) const =0; // Returns the protocol's name
	virtual unsigned int getVersion(void) const =0; // Returns the protocol's version
	void setIndex(unsigned int newPluginIndex); // Sets the protocol's index in the server's plug-in protocol list
	unsigned int getIndex(void) const // Returns the protocol's index in the server's plug-in protocol list
		{
		return pluginIndex;
		}
	virtual unsigned int getNumClientMessages(void) const; // Returns the number of messages sent from clients to servers
	virtual unsigned int getNumServerMessages(void) const; // Returns the number of messages sent from servers to clients
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase); // Sets the base ID for client and server messages
	unsigned int getClientMessageBase(void) const // Returns the base message ID for messages sent from a client to a server
		{
		return clientMessageBase;
		}
	unsigned int getServerMessageBase(void) const // Returns the base message ID for messages sent from a server to a client
		{
		return serverMessageBase;
		}
	virtual void start(void); // Starts the protocol
	virtual void clientConnected(unsigned int clientId); // Notifies the protocol that a new client requesting it has connected; protocol may send messages to the new client
	virtual void clientDisconnected(unsigned int clientId); // Notifies the protocol that a client using the protocol has disconnected
	};

typedef Plugins::ObjectLoader<PluginServer> PluginServerLoader;

#endif
