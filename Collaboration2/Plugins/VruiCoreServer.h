/***********************************************************************
VruiCoreServer - Server for the core Vrui collaboration protocol, which
represents remote users' physical VR environments, and maintains their
head positions/orientations and navigation transformations.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_VRUICORESERVER_INCLUDED
#define PLUGINS_VRUICORESERVER_INCLUDED

#include <string>
#include <vector>
#include <Misc/StringHashFunctions.h>
#include <Misc/HashTable.h>

#include <Collaboration2/PluginServer.h>
#include <Collaboration2/Server.h>
#include <Collaboration2/Plugins/VruiCoreProtocol.h>

/* Forward declarations: */
class MessageReader;
class NonBlockSocket;
class MessageContinuation;

class VruiCoreServer:public PluginServer,public VruiCoreProtocol
	{
	/* Embedded classes: */
	public:
	class Client; // Forward declaration
	
	private:
	struct ClientIdentifier // Class to identify a client by ID and state object pointer
		{
		/* Elements: */
		public:
		unsigned int id; // Client's ID
		Client* client; // Client's state object
		
		/* Constructors and destructors: */
		ClientIdentifier(void) // Creates invalid identifier
			:id(0),client(0)
			{
			}
		ClientIdentifier(unsigned int sId,Client* sClient)
			:id(sId),client(sClient)
			{
			}
		
		/* Methods: */
		bool valid(void) const // Returns true if the structure identifies a client
			{
			return id!=0;
			}
		const Client* operator->(void) const // Indirects to identified client
			{
			return client;
			}
		Client* operator->(void) // Ditto
			{
			return client;
			}
		};
	
	class PhysicalEnvironment // Class representing a physical environment that may be shared by more than one client
		{
		friend class VruiCoreServer;
		friend class Client;
		
		/* Elements: */
		private:
		unsigned int id; // Unique ID number of this shared physical environment
		std::string name; // Unique name of the shared physical environment
		std::vector<ClientIdentifier> clients; // List of clients sharing this physical environment
		NavTransform navTransform; // Navigation transformation shared by all clients sharing this physical environment
		ClientIdentifier navigatingClient; // Client currently executing a navigation sequence on this physical environment, or (0, 0)
		ClientIdentifier lockingClient; // Client who initiated the current navigation lock for this physical environment, or (0, 0)
		
		/* Constructors and destructors: */
		PhysicalEnvironment(unsigned int sId,const std::string& sName)
			:id(sId),name(sName)
			{
			}
		};
	
	typedef Misc::HashTable<std::string,PhysicalEnvironment*> PhysicalEnvironmentMap; // Type for hash tables mapping names to shared physical environments
	
	public:
	class Client:public PluginServer::Client // Class representing a client participating in the Vrui Core protocol
		{
		friend class VruiCoreServer;
		
		/* Embedded classes: */
		private:
		typedef std::vector<ClientInputDeviceState> DeviceList; // Type for lists of input devices
		typedef Misc::HashTable<unsigned int,size_t> DeviceIndexMap; // Type for hash tables mapping input device IDs to indices in the device list
		
		/* Elements: */
		PhysicalEnvironment* physicalEnvironment; // Pointer to a shared physical environment in which this client is located, or 0
		ClientEnvironment environment; // Client's physical environment
		ClientViewerConfig viewerConfig; // Client's viewer configuration
		ClientViewerState viewerState; // Client's viewer state
		ClientIdentifier navLockClient; // Client to whom this client has locked its navigation transformation
		NavTransform navTransform; // Client's navigation transformation relative to physical space if unlocked, or relative to navLockClient's navigation transformation
		DeviceList devices; // List of client's input devices
		DeviceIndexMap deviceIndexMap; // Map from input device IDs to indices in the device list
		
		/* Constructors and destructors: */
		Client(PhysicalEnvironment* sPhysicalEnvironment,NonBlockSocket& socket); // Creates a client from a connect request message pending on the given socket
		
		/* Methods: */
		public:
		bool samePhysicalEnvironment(const Client& other) const // Returns true if the two clients share the same physical environment
			{
			return physicalEnvironment!=0&&physicalEnvironment==other.physicalEnvironment;
			}
		const ONTransform& getHeadTransform(void) const // Returns the main viewer's position and orientation
			{
			return viewerState.headTransform;
			}
		NavTransform getNavTransform(void) const // Returns the client's navigation transformation
			{
			NavTransform result=navTransform;
			
			if(navLockClient.valid())
				{
				/* Follow the client's lock chain to calculate its full navigation transformation: */
				const Client* lockClient=navLockClient.client;
				do
					{
					result*=lockClient->navTransform;
					lockClient=lockClient->navLockClient.client;
					}
				while(lockClient!=0);
				
				result.renormalize();
				}
			
			return result;
			}
		const ClientInputDeviceState& getDevice(unsigned int deviceId) const // Returns the state of the input device of the given ID
			{
			return devices[deviceIndexMap.getEntry(deviceId).getDest()];
			}
		};
	
	/* Elements: */
	private:
	Misc::UInt16 lastPhysicalEnvironmentId; // Last ID assigned to a new shared physical environment
	PhysicalEnvironmentMap physicalEnvironmentMap; // Map of shared physical environments currently used on the server
	
	/* Private methods: */
	MessageContinuation* connectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* environmentUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* viewerConfigUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* viewerUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* startNavSequenceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* navTransformUpdateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* stopNavSequenceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* lockNavTransformRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* unlockNavTransformRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* createInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* updateInputDeviceRayRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* updateInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* disableInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* enableInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* destroyInputDeviceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	
	/* Constructors and destructors: */
	public:
	VruiCoreServer(Server* sServer);
	virtual ~VruiCoreServer(void);
	
	/* Methods from class PluginServer: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	
	/* New methods: */
	static VruiCoreServer* requestServer(Server* server) // Returns a Vrui Core protocol server
		{
		/* Request the Vrui Core protocol server and cast it to the correct type: */
		return static_cast<VruiCoreServer*>(server->requestPluginProtocol(VRUICORE_PROTOCOLNAME,VRUICORE_PROTOCOLVERSION));
		}
	static VruiCoreServer* getServer(Server* server) // Returns a Vrui Core protocol server that has previously been registered with the given collaboration server
		{
		/* Find the Vrui Core protocol server and cast it to the correct type: */
		return static_cast<VruiCoreServer*>(server->findPluginProtocol(VRUICORE_PROTOCOLNAME,VRUICORE_PROTOCOLVERSION));
		}
	Client* getClient(unsigned int clientId) // Returns the Vrui Core client structure with the given client ID; throws exception if Vrui Core client does not exist
		{
		return server->getClient(clientId)->getPlugin<Client>(pluginIndex);
		}
	};

#endif
