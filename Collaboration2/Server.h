/***********************************************************************
Server - Class representing a collaboration server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef SERVER_INCLUDED
#define SERVER_INCLUDED

#include <string>
#include <vector>
#include <Misc/HashTable.h>
#include <Misc/CommandDispatcher.h>
#include <Misc/ConfigurationFile.h>
#include <Threads/EventDispatcher.h>
#include <Plugins/ObjectLoader.h>
#include <Comm/ListeningTCPSocket.h>

#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/UDPSocket.h>
#include <Collaboration2/CoreProtocol.h>
#include <Collaboration2/PluginServer.h>

/* Forward declarations: */
class MessageContinuation;
class MessageBuffer;
class MessageReader;

class Server:public CoreProtocol
	{
	/* Embedded classes: */
	public:
	class Client // Class representing a connected client
		{
		friend class Server;
		
		/* Embedded classes: */
		private:
		enum ClientState // Enumerated type for states of the client communication protocol
			{
			ReadingClientConnectRequest,
			ReadingProtocolRequests,
			ReadingMessageID,
			ReadingMessageBody,
			HandlingMessage,
			Drain, // Disconnect the client gently when its write queue is empty
			Disconnect // Disconnect the client immediately
			};
		
		typedef std::vector<PluginServer::Client*> PluginClientList; // Type for list of plug-in protocol client states
		
		/* Elements: */
		static const std::runtime_error missingPluginError; // Error to be thrown when a caller requests a non-existing plug-in client
		Server* server; // Pointer back to the server for simplified event handling
		unsigned int id; // Unique ID for this client
		NonBlockSocket socket; // TCP socket connected to the client
		Byte nonce[PasswordRequestMsg::nonceLength]; // The nonce sent to the client during authentication
		bool swapOnRead; // Flag whether data read from the client must be endianness-swapped
		std::string clientAddress; // Socket address from which the client connected
		Threads::EventDispatcher::ListenerKey socketKey; // Key for events on the client's socket
		bool connected; // Flag if the client successfully connected
		Misc::UInt32 udpConnectionTicket; // Ticket with which the client can open a connection to the server's UDP socket
		UDPSocket::Address udpAddress; // Address of the UDP socket from which the client connected to the server's UDP socket
		bool udpConnected; // Flag if the UDP connection to the client has been established
		ClientState clientState; // Current state of client communication protocol
		std::string name; // Client's chosen name
		unsigned int messageId; // ID of the message currently being read
		MessageContinuation* continuation; // Message handler continuation state for the current partial message on the client's socket
		std::vector<unsigned int> pluginIndices; // List of indices of plug-in protocols in which the client participates
		PluginClientList plugins; // Client states of plug-in protocols
		
		/* Private methods: */
		bool socketEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask); // Callback called when an I/O event occurs on the client's TCP socket
		
		/* Constructors and destructors: */
		Client(Server* sServer,Comm::ListeningTCPSocket& listenSocket); // Connects to a client by accepting the listening socket's first pending connection request
		public:
		~Client(void); // Destroys a client
		
		/* Methods: */
		NonBlockSocket& getSocket(void) // Returns the client's socket
			{
			return socket;
			}
		bool haveUDP(void) const // Returns true if the client can send and receive messages over UDP
			{
			return udpConnected;
			}
		void setPlugin(unsigned int pluginIndex,PluginServer::Client* newPlugin); // Sets a client's state for the plug-in protocol of the given index
		template <class PluginServerClientParam>
		PluginServerClientParam* getPlugin(unsigned int pluginIndex) // Returns the client's state for the plug-in protocol of the given index
			{
			if(plugins[pluginIndex]==0)
				throw missingPluginError;
			return static_cast<PluginServerClientParam*>(plugins[pluginIndex]);
			}
		void queueMessage(MessageBuffer* message); // Queues the given message for sending on the socket; starts dispatching write events if necessary
		};
	
	friend class Client;
	
	typedef std::vector<Client*> ClientList; // Type for lists of clients
	typedef Misc::HashTable<unsigned int,Client*> ClientMap; // Type for hash tables mapping client IDs to client structures
	typedef Misc::HashTable<UDPSocket::Address,Client*> ClientAddressMap; // Type for hash tables mapping client's UDP socket addresses to client structures
	typedef Plugins::ObjectLoader<PluginServer> PluginLoader; // Type for loader than can load plug-in protocols from DSOs
	typedef std::vector<PluginServer*> PluginList; // Type for lists of plug-in protocols
	
	/* Types for handlers for messages arriving on a client's TCP socket: */
	typedef MessageContinuation* (*MessageHandlerCallback)(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation,void* userData); // Type for callback functions handling incoming client messages; returns true if handler is done processing the message
	template <class ClassParam,MessageContinuation* (ClassParam::*methodParam)(unsigned int,unsigned int,MessageContinuation*)>
	static MessageContinuation* wrapMethod(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation,void* userData) // Helper function to register class methods as callback functions
		{
		return (static_cast<ClassParam*>(userData)->*methodParam)(messageId,clientId,continuation);
		}
	
	/* Types for handlers for messages arriving on the shared UDP socket: */
	typedef void (*UDPMessageHandlerCallback)(unsigned int messageId,unsigned int clientId,MessageReader& message,void* userData); // Type for callback functions handling incoming client messages on the shared UDP socket
	template <class ClassParam,void (ClassParam::*methodParam)(unsigned int,unsigned int,MessageReader&)>
	static void wrapMethod(unsigned int messageId,unsigned int clientId,MessageReader& message,void* userData) // Helper function to register class methods as callback functions
		{
		(static_cast<ClassParam*>(userData)->*methodParam)(messageId,clientId,message);
		}
	
	private:
	struct MessageHandler // Structure containing a callback function for a specific message
		{
		/* Elements: */
		public:
		MessageHandlerCallback callback; // Callback function to be called when a message has been completely read
		void* callbackUserData; // User data passed along to the callback function
		size_t minUnread; // Minimum amount of unread data in socket buffer before message handler callback is called
		
		/* Constructors and destructors: */
		MessageHandler(MessageHandlerCallback sCallback,void* sCallbackUserData,size_t sMinUnread)
			:callback(sCallback),callbackUserData(sCallbackUserData),minUnread(sMinUnread)
			{
			}
		};
	
	struct UDPMessageHandler // Structure containing a callback function for a specific message arriving on the shared UDP socket
		{
		/* Elements: */
		public:
		UDPMessageHandlerCallback callback; // Callback function to be called when a message has been completely read
		void* callbackUserData; // User data passed along to the callback function
		
		/* Constructors and destructors: */
		UDPMessageHandler(UDPMessageHandlerCallback sCallback,void* sCallbackUserData)
			:callback(sCallback),callbackUserData(sCallbackUserData)
			{
			}
		};
	
	/* Elements: */
	Misc::ConfigurationFileSection serverConfig; // The server's configuration file section
	Threads::EventDispatcher dispatcher; // Central dispatcher handling all communication channels
	Threads::EventDispatcher::ListenerKey stdinKey; // Key for events on stdin
	int commandPipe; // File descriptor of a named pipe from which to read commands
	int commandPipeHolder; // Additional file descriptor to hold open the named command pipe
	Threads::EventDispatcher::ListenerKey commandPipeKey; // Key for events on the command pipe
	Comm::ListeningTCPSocket listenSocket; // Socket on which the server listens for incoming connections
	Threads::EventDispatcher::ListenerKey listenSocketKey; // Key for listening socket events
	UDPSocket udpSocket; // Shared UDP socket for transport of unreliable datagrams
	Threads::EventDispatcher::ListenerKey udpSocketKey; // Key for UDP socket events
	std::string name; // The server's name
	std::string sessionPassword; // The server's session password
	ClientID nextClientId; // ID number to be assigned to the next successfully connecting client
	ClientList clients; // List of currently connected clients
	ClientMap clientMap; // Map from client IDs to client structures
	ClientAddressMap clientAddressMap; // Map from client's UDP socket addresses to client structures
	std::vector<MessageHandler> messageHandlers; // List of message handlers
	std::vector<UDPMessageHandler> udpMessageHandlers; // List of message handlers for the shared UDP socket
	PluginLoader pluginLoader; // Object to load plug-in protocols from DSOs
	PluginList plugins; // List of plug-in protocols
	unsigned int clientMessageBase; // Base ID for client messages for the next plug-in protocol
	unsigned int serverMessageBase; // Base ID for server messages for the next plug-in protocol
	Misc::CommandDispatcher commandDispatcher; // A dispatcher for commands read from the console
	
	/* Private methods: */
	void disconnect(Client* client); // Disconnects the given client
	void setPasswordCommand(const char* argumentBegin,const char* argumentEnd);
	void netstatCommand(const char* argumentBegin,const char* argumentEnd);
	void listClientsCommand(const char* argumentBegin,const char* argumentEnd);
	void disconnectClientCommand(const char* argumentBegin,const char* argumentEnd);
	void listPluginsCommand(const char* argumentBegin,const char* argumentEnd);
	void loadPluginCommand(const char* argumentBegin,const char* argumentEnd);
	void unloadPluginCommand(const char* argumentBegin,const char* argumentEnd);
	void quitCommand(const char* argumentBegin,const char* argumentEnd);
	bool stdinEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask); // Callback called when text arrives on stdin
	bool commandPipeEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask); // Callback called when text arrives on the optional command pipe
	bool listenSocketEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask); // Callback called when a connection request appears on the listening socket
	bool udpSocketEvent(Threads::EventDispatcher::ListenerKey eventKey,int eventTypeMask); // Callback called when a datagram appears on the UDP socket
	void udpConnectRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message); // Handles a redundant UDP connection request from an already-connected client
	MessageContinuation* disconnectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation); // Handles a client's disconnect request message
	MessageContinuation* pingRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation); // Handles a client's ping request message
	void udpPingRequestCallback(unsigned int messageId,unsigned int clientId,MessageReader& message); // Handles a client's ping request message coming in over the shared UDP socket
	MessageContinuation* nameChangeRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation); // Handles a client's name change request
	
	/* Constructors and destructors: */
	public:
	Server(const Misc::ConfigurationFileSection& sServerConfig,int portId,const char* sName); // Creates a server of the given name listening on the given TCP port using the given configuration section
	~Server(void); // Shuts down and destroys the server
	
	/* Methods: */
	Threads::EventDispatcher& getDispatcher(void) // Returns the client's event dispatcher
		{
		return dispatcher;
		}
	void setPassword(const char* newSessionPassword); // Sets a session password that all clients have to know
	Client* getClient(unsigned int clientId) // Returns the client structure associated with the given client ID; throws exception if client does not exist
		{
		return clientMap.getEntry(clientId).getDest();
		}
	Client* testAndGetClient(unsigned int clientId) // Returns the client structure associated with the given client ID if that client ID exists; returns null otherwise
		{
		/* Find the client ID in the client map: */
		ClientMap::Iterator cIt=clientMap.findEntry(clientId);
		
		/* Return the client structure if the client ID was found, null otherwise: */
		if(!cIt.isFinished())
			return cIt->getDest();
		else
			return 0;
		}
	void queueMessage(unsigned int clientId,MessageBuffer* message) // Queues the given message for sending on the given client's TCP socket; starts dispatching write events if necessary
		{
		/* Forward to the given client's method: */
		clientMap.getEntry(clientId).getDest()->queueMessage(message);
		}
	void queueUDPMessage(const UDPSocket::Address& receiverAddress,MessageBuffer* message); // Queues the given message to the given receiver for sending on the UDP socket; starts dispatching write events if necessary
	void queueUDPMessage(unsigned int clientId,MessageBuffer* message) // Queues the given message to the given client for sending on the UDP socket; starts dispatching write events if necessary
		{
		/* Retrieve client's UDP socket address and forward to the other method: */
		queueUDPMessage(clientMap.getEntry(clientId).getDest()->udpAddress,message);
		}
	void queueUDPMessageFallback(unsigned int clientId,MessageBuffer* message) // Queues the given message to the given client for sending on the UDP socket, or on the client's TCP socket if the client does not have UDP connectivity
		{
		Client* client=clientMap.getEntry(clientId).getDest();
		if(client->haveUDP())
			queueUDPMessage(client->udpAddress,message);
		else
			client->queueMessage(message);
		}
	void setMessageHandler(unsigned int messageId,MessageHandlerCallback callback,void* callbackUserData,size_t minUnread); // Sets the message handler for the given message ID
	void setUDPMessageHandler(unsigned int messageId,UDPMessageHandlerCallback callback,void* callbackUserData); // Sets the message handler for the given message ID coming in over the shared UDP socket
	
	/* Handling of plug-in protocols: */
	PluginServer* requestPluginProtocol(const char* protocolName,unsigned int protocolVersion); // Requests a plug-in protocol of the given name and version, which might load a new protocol from DSO; returns null on failure
	void addPluginProtocol(PluginServer* protocol); // Adds a plug-in protocol to the server
	PluginServer* findPluginProtocol(const char* protocolName,unsigned int protocolVersion); // Returns the loaded plug-in protocol of the given name; returns null if plug-in protocol is not loaded
	template <class PluginServerClientParam>
	PluginServerClientParam* getPlugin(unsigned int clientId,unsigned int pluginIndex) // Returns the client plug-in of the given plug-in index for the client of the given ID; throws exception if client or plug-in do not exist
		{
		PluginServerClientParam* result=static_cast<PluginServerClientParam*>(clientMap.getEntry(clientId).getDest()->plugins[pluginIndex]);
		if(result==0)
			throw Client::missingPluginError;
		return result;
		}
	template <class PluginServerClientParam>
	PluginServerClientParam* testAndGetPlugin(unsigned int clientId,unsigned int pluginIndex) // Returns the client plug-in of the given plug-in index for the client of the given ID; returns 0 if client or plug-in do not exist
		{
		PluginServerClientParam* result=0;
		
		/* Find the client ID in the client map: */
		ClientMap::Iterator cIt=clientMap.findEntry(clientId);
		if(!cIt.isFinished())
			{
			/* Return the client's plug-in client: */
			result=static_cast<PluginServerClientParam*>(cIt->getDest()->plugins[pluginIndex]);
			}
		
		return result;
		}
	Misc::ConfigurationFileSection getPluginConfig(PluginServer* plugin); // Returns a configuration file section for the given plug-in protocol
	
	/* Handling of console commands: */
	Misc::CommandDispatcher& getCommandDispatcher(void) // Returns the dispatcher for console commands
		{
		return commandDispatcher;
		}
	
	/* Main methods: */
	void run(void); // Runs the server until shut down
	void shutdown(void); // Shuts down the server
	};

#endif
