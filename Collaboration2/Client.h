/***********************************************************************
Client - Class representing a collaboration client.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef CLIENT_INCLUDED
#define CLIENT_INCLUDED

#include <string>
#include <vector>
#include <Misc/HashTable.h>
#include <Misc/CallbackData.h>
#include <Misc/CallbackList.h>
#include <Misc/Pipe.h>
#include <Misc/ConfigurationFile.h>
#include <Realtime/Time.h>
#include <Threads/EventDispatcher.h>
#include <Plugins/ObjectLoader.h>

#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/UDPSocket.h>
#include <Collaboration2/CoreProtocol.h>
#include <Collaboration2/PluginClient.h>

/* Forward declarations: */
class MessageBuffer;
class MessageReader;
class MessageContinuation;

class Client:public CoreProtocol
	{
	/* Embedded classes: */
	public:
	class RemoteClient // Structure representing another client connected to the same server
		{
		friend class Client;
		
		/* Embedded classes: */
		private:
		typedef std::vector<PluginClient::RemoteClient*> PluginClientList; // Type for lists of remote client states of plug-in protocols
		
		/* Elements: */
		unsigned int id; // Other client's unique ID
		std::string name; // Other client's name
		std::vector<unsigned int> pluginIndices; // List of indices of plug-in protocols in which the remote client participates
		PluginClientList plugins; // Remote client states of plug-in protocols
		
		/* Constructors and destructors: */
		RemoteClient(unsigned int sId,const std::string& sName); // Creates a new remote client
		public:
		~RemoteClient(void); // Destroys the remote client
		
		/* Methods: */
		unsigned int getId(void) const // Returns the client's ID
			{
			return id;
			}
		const std::string& getName(void) const // Returns the client's name
			{
			return name;
			}
		void setPlugin(unsigned int pluginIndex,PluginClient::RemoteClient* newPlugin); // Sets the remote client state for the plug-in protocol of the given index
		PluginClient::RemoteClient* releasePlugin(unsigned int pluginIndex); // Releases the currently installed remote client state for the plug-in protocol of the given index and returns it to the caller
		template <class PluginClientRemoteClientParam>
		PluginClientRemoteClientParam* getPlugin(unsigned int pluginIndex) // Returns the remote client state for the plug-in protocol of the given index
			{
			return static_cast<PluginClientRemoteClientParam*>(plugins[pluginIndex]);
			}
		};
	
	class NameChangeReplyCallbackData:public Misc::CallbackData // Callback data for a reply to a name change request from this client
		{
		/* Elements: */
		public:
		bool granted; // Flag whether the name change request was granted
		const std::string& oldName; // The client's name before the change
		const std::string& newName; // Client's name after the change; same as old name if the request was denied
		
		/* Constructors and destructors: */
		NameChangeReplyCallbackData(bool sGranted,const std::string& sOldName,const std::string& sNewName)
			:granted(sGranted),oldName(sOldName),newName(sNewName)
			{
			}
		};
	
	class NameChangeNotificationCallbackData:public Misc::CallbackData // Callback data when a remote client changes its name
		{
		/* Elements: */
		public:
		unsigned int clientId; // ID of the client that changed its name
		const std::string& oldName; // Client name before the change
		const std::string& newName; // Client name after the change
		
		/* Constructors and destructors: */
		NameChangeNotificationCallbackData(unsigned int sClientId,const std::string& sOldName,const std::string& sNewName)
			:clientId(sClientId),oldName(sOldName),newName(sNewName)
			{
			}
		};
	
	/* Type for handler functions for messages arriving on a non-blocking TCP socket: */
	typedef MessageContinuation* (*MessageContinuationHandlerFunction)(unsigned int messageId,MessageContinuation* continuation,void* userData); // Returns a continuation object if the message handler is not done with the message
	
	/* Type for handler functions for messages arriving on a UDP socket or on a forwarding pipe from the back-end: */
	typedef void (*MessageReaderHandlerFunction)(unsigned int messageId,MessageReader& message,void* userData);
	
	/* Helper functions to wrap class methods as method handling functions: */
	template <class ClassParam,MessageContinuation* (ClassParam::*methodParam)(unsigned int,MessageContinuation*)>
	static MessageContinuation* wrapMethod(unsigned int messageId,MessageContinuation* continuation,void* userData) // Wrapper for MessageContinuation-based message handlers
		{
		return (static_cast<ClassParam*>(userData)->*methodParam)(messageId,continuation);
		}
	template <class ClassParam,void (ClassParam::*methodParam)(unsigned int,MessageReader&)>
	static void wrapMethod(unsigned int messageId,MessageReader& message,void* userData) // Wrapper for MessageReader-based message handlers
		{
		(static_cast<ClassParam*>(userData)->*methodParam)(messageId,message);
		}
	
	private:
	struct MessageContinuationHandler // Structure containing a handler function for a specific message arriving on a non-blocking TCP socket
		{
		/* Elements: */
		public:
		MessageContinuationHandlerFunction handler; // Message handler function
		void* handlerUserData; // User data passed along to the message handler function
		size_t minUnread; // Minimum amount of unread data in socket buffer before message handler function is called
		
		/* Constructors and destructors: */
		MessageContinuationHandler(MessageContinuationHandlerFunction sHandler,void* sHandlerUserData,size_t sMinUnread)
			:handler(sHandler),handlerUserData(sHandlerUserData),minUnread(sMinUnread)
			{
			}
		};
	
	struct MessageReaderHandler // Structure containing a handler function for a specific message arriving on a UDP socket or forwarding pipe
		{
		/* Elements: */
		public:
		MessageReaderHandlerFunction handler; // Message handler function
		void* handlerUserData; // User data passed along to the message handler  function
		
		/* Constructors and destructors: */
		MessageReaderHandler(MessageReaderHandlerFunction sHandler,void* sHandlerUserData)
			:handler(sHandler),handlerUserData(sHandlerUserData)
			{
			}
		};
	
	typedef std::vector<RemoteClient*> RemoteClientList; // Type for lists of other clients connected to the same server
	typedef Misc::HashTable<unsigned int,RemoteClient*> RemoteClientMap; // Type for hash tables mapping client IDs to remote client structures
	typedef Plugins::ObjectLoader<PluginClient> PluginLoader; // Type for loader than can load plug-in protocols from DSOs
	typedef std::vector<PluginClient*> PluginList; // Type for lists of plug-in protocols
	
	enum State // Enumerated type for states of the communication protocol
		{
		ReadingPasswordRequest,
		ReadingMessageID,
		ReadingMessageBody,
		HandlingMessage,
		Disconnecting,
		Disconnected,
		Shutdown
		};
	
	/* Elements: */
	static Client* theClient; // Pointer to a created collaboration client
	
	Misc::ConfigurationFile configurationFile; // The collaboration configuration file
	Misc::ConfigurationFileSection rootConfigSection; // The root client configuration section
	
	std::string serverAddress; // Socket address of server
	std::string serverName; // Name of server
	std::string sessionPassword; // The session password with which to authenticate with the server
	unsigned int id; // This client's unique ID
	
	std::string clientName; // Name under which this client registered with the server; can be changed on request
	Misc::CallbackList nameChangeReplyCallbacks; // List of callbacks to be called when the server replies to this client's requests to change its name
	Misc::CallbackList nameChangeNotificationCallbacks; // List of callbacks to be called when a remote client changes its name
	
	PluginLoader pluginLoader; // Object to load plug-in protocols from DSOs
	PluginList plugins; // List of requested or confirmed plug-in protocols
	
	Threads::EventDispatcher dispatcher; // Central dispatcher handling all communication channels
	
	NonBlockSocket socket; // TCP socket connected to the server
	bool swapOnRead; // Flag whether data read from the server must be endianness-swapped
	Threads::EventDispatcher::ListenerKey socketKey; // Key for server socket events
	std::vector<MessageContinuationHandler> tcpMessageHandlers; // List of message handlers for the TCP socket
	
	UDPSocket udpSocket; // UDP socket to send unreliable datagrams to the server
	Threads::EventDispatcher::ListenerKey udpSocketKey; // Key for server socket events
	Misc::UInt32 udpConnectionTicket; // Ticket with which the client can connect to the server's UDP socket
	UDPSocket::Address udpServerAddress; // Socket address of server's UDP socket
	Threads::EventDispatcher::ListenerKey udpConnectRequestTimerKey; // Key for the timer to send UDP connect requests
	unsigned int numUDPConnectRequests; // Maximum number of UDP connect requests to send before giving up
	bool udpConnected; // Flag if the UDP socket is connected to the server
	std::vector<MessageReaderHandler> udpMessageHandlers; // List of message handlers for the UDP socket
	
	bool frontend; // Flag if the client supports front-end message forwarding
	Misc::Pipe frontendPipe; // Pipe to send messages from the communications back-end to a front-end for synchronous processing
	std::vector<MessageReaderHandler> frontendMessageHandlers; // List of message handlers for the front-end forwarding pipe
	
	Threads::EventDispatcher::ListenerKey messageSignalKey; // Key for a signal to queue server messages from a different thread; signal data is MessageBuffer pointer
	Threads::EventDispatcher::ListenerKey udpMessageSignalKey; // Key for a signal to queue server messages over UDP from a different thread; signal data is MessageBuffer pointer
	
	RemoteClientList remoteClients; // List of other clients connected to the same server
	RemoteClientMap remoteClientMap; // Hash table mapping client IDs to remote client structures
	
	State state; // Current state in the communication protocol
	unsigned int messageId; // ID of the message currently being read
	MessageContinuation* continuation; // Message handler continuation state for the current partial message on the TCP socket
	
	Realtime::TimePointRealtime lastPingTime; // System time when the last ping request was sent
	Misc::SInt16 lastPingSequence; // Sequence number of last ping request
	
	/* Private methods: */
	bool socketEvent(Threads::EventDispatcher::ListenerKey,int eventTypeMask); // Callback called when an I/O event occurs on the TCP socket
	bool udpSocketEvent(Threads::EventDispatcher::ListenerKey,int eventTypeMask); // Callback called when an I/O event occurs on the UDP socket
	bool sendUDPConnectRequestCallback(Threads::EventDispatcher::ListenerKey eventKey); // Called at regular intervals to send a connect request to the server's UDP socket until a reply is received
	bool messageSignalCallback(Threads::EventDispatcher::ListenerKey signalKey,void* signalData); // Called when another thread wants to send a message to the server; signal data is MessageBuffer pointer
	bool udpMessageSignalCallback(Threads::EventDispatcher::ListenerKey signalKey,void* signalData); // Called when another thread wants to send a message to the server over UDP; signal data is MessageBuffer pointer
	MessageContinuation* connectReplyCallback(unsigned int messageId,MessageContinuation* continuation); // Handles the server's connect reply message
	MessageContinuation* connectRejectCallback(unsigned int messageId,MessageContinuation* continuation); // Handles the server's connect reject message
	void udpConnectReplyCallback(unsigned int messageId,MessageReader& message); // Handles the server's reply to a UDP connect request message
	MessageContinuation* pingReplyCallback(unsigned int messageId,MessageContinuation* continuation); // Handles the server's ping reply message
	void udpPingReplyCallback(unsigned int messageId,MessageReader& message); // Handles the server's ping reply message on the UDP socket
	MessageContinuation* nameChangeReplyCallback(unsigned int messageId,MessageContinuation* continuation); // Handles a name change reply for this client
	MessageContinuation* clientConnectNotificationCallback(unsigned int messageId,MessageContinuation* continuation); // Handles a notification that another client connected
	MessageContinuation* nameChangeNotificationCallback(unsigned int messageId,MessageContinuation* continuation); // Handles a name change notification for another client
	MessageContinuation* clientDisconnectNotificationCallback(unsigned int messageId,MessageContinuation* continuation); // Handles a notification that another client disconnected
	bool sendPingRequestCallback(Threads::EventDispatcher::ListenerKey eventKey); // Called at regular intervals to send a ping request to the server
	MessageContinuation* fixedSizeForwarderCallback(unsigned int messageId,MessageContinuation* continuation); // Callback called when a fixed-sized message arrives that needs to be forwarded to the front-end
	
	/* Constructors and destructors: */
	public:
	Client(void); // Creates a client in initial state
	~Client(void); // Disconnects from the server and destroys the client
	
	/* Methods: */
	static Client* getTheClient(void) // Returns an existing collaboration client
		{
		return theClient;
		}
	static bool isURI(const char* string); // Returns true if the given string appears to be a server URI
	static bool parseURI(const char* string,std::string& serverHostName,int serverPort,std::string& sessionPassword); // Parses an apparent server URI; returns false if format is wrong after all
	
	/* Configuration API: */
	const Misc::ConfigurationFileSection& getRootConfigSection(void) const // Returns the root client-side configuration section
		{
		return rootConfigSection;
		}
	
	/* Client ID and name API: */
	unsigned int getId(void) const // Returns the client's ID
		{
		return id;
		}
	const std::string& getClientName(void) const // Returns the name of this client
		{
		return clientName;
		}
	Misc::CallbackList& getNameChangeReplyCallbacks(void) // Returns the list of name change reply callbacks
		{
		return nameChangeReplyCallbacks;
		}
	Misc::CallbackList& getNameChangeNotificationCallbacks(void) // Returns the list of name change notification callbacks
		{
		return nameChangeNotificationCallbacks;
		}
	void requestNameChange(const char* newClientName); // Requests a change of this client's name to the given new name; can be called from a different thread
	
	/* Plug-in protocol API: */
	PluginClient* requestPluginProtocol(const char* protocolName,unsigned int protocolVersion); // Requests a plug-in protocol of the given name and version, which might load a new protocol from DSO; must be called befor start(); throws exception on failure
	PluginClient* requestPluginProtocol(const char* protocolName); // Ditto, but loads the available plug-in protocol with the highest version number matching the given name
	void addPluginProtocol(PluginClient* protocol); // Adds a plug-in protocol to the client; must be called befor start()
	PluginClient* findPluginProtocol(const char* protocolName,unsigned int protocolVersion); // Returns the loaded plug-in protocol of the given name and version; returns null if plug-in protocol is not loaded
	
	/* Socket communication API: */
	Threads::EventDispatcher& getDispatcher(void) // Returns the client's event dispatcher
		{
		return dispatcher;
		}
	NonBlockSocket& getSocket(void) // Returns the client's socket
		{
		return socket;
		}
	bool mustSwapOnRead(void) const // Returns true if messages received from the server over TCP or UDP must be byte-swapped
		{
		return swapOnRead;
		}
	bool haveUDP(void) const // Returns true if the client can send and receive messages over UDP
		{
		return udpConnected;
		}
	bool haveFrontend(void) const // Returns true if the client can forward messages and pass signals to a front-end
		{
		return frontend;
		}
	void setTCPMessageHandler(unsigned int messageId,MessageContinuationHandlerFunction handler,void* handlerUserData,size_t minUnread); // Sets the message handler for the given message ID on the TCP socket
	void setUDPMessageHandler(unsigned int messageId,MessageReaderHandlerFunction handler,void* handlerUserData); // Sets the message handler for the given message ID on the UDP socket
	void setFrontendMessageHandler(unsigned int messageId,MessageReaderHandlerFunction handler,void* handlerUserData); // Sets the message handler for the given message ID on the front-end pipe
	void setMessageForwarder(unsigned int messageId,MessageReaderHandlerFunction handler,void* handlerUserData,size_t fixedMessageSize); // Installs a front-end forwarder for fixed-size messages with the given message ID and size
	void queueMessage(MessageBuffer* message); // Queues the given message for sending on the socket; starts dispatching write events if necessary
	void queueUDPMessage(MessageBuffer* message); // Queues the given message for sending on the UDP socket; starts dispatching write events if necessary
	void queueServerMessage(MessageBuffer* message) // Queues the given message for sending on the socket from a different thread
		{
		/* Send a signal containing the message to the communication thread: */
		dispatcher.signal(messageSignalKey,message->ref());
		}
	void queueServerUDPMessage(MessageBuffer* message) // Queues the given message for sending on the UDP socket from a different thread
		{
		/* Send a signal containing the message to the communication thread: */
		dispatcher.signal(udpMessageSignalKey,message->ref());
		}
	void queueFrontendMessage(MessageBuffer* message) // Sends a message from the back end to the front end
		{
		/* Write a pointer to the message to the front-end pipe: */
		frontendPipe.write(message->ref());
		}
	RemoteClient* getRemoteClient(unsigned int clientId) // Returns the remote client structure associated with the given client ID
		{
		return remoteClientMap.getEntry(clientId).getDest();
		}
	
	/* Main methods: */
	std::string getDefaultServerHostName(void) const; // Returns the host name of the default collaboration server
	int getDefaultServerPort(void) const; // Returns the port number of the default collaboration server
	int enableFrontendForwarding(void); // Establishes a communication front end and returns a file descriptor to watch for front-end messages
	void setPassword(const std::string& newSessionPassword); // Sets a session password to connect to the server
	void start(const std::string& serverHostName,int serverPort); // Initiates communication with the server on the given host name and port
	void run(void); // Runs the client until shut down
	void dispatchFrontendMessages(void); // Dispatches messages that have been sent from the back-end to the front-end
	bool wasDisconnected(void) const // Returns true if the client was disconnected from the server due to a communication error or server shutdown
		{
		return state==Disconnected;
		}
	void shutdown(void); // Shuts down the client
	};

#endif
