/***********************************************************************
KoinoniaClient - Client for data sharing plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_KOINONIACLIENT_INCLUDED
#define PLUGINS_KOINONIACLIENT_INCLUDED

#include <string>
#include <vector>
#include <Misc/StringHashFunctions.h>
#include <Misc/HashTable.h>
#include <Threads/Mutex.h>

#include <Collaboration2/DataType.h>
#include <Collaboration2/Client.h>
#include <Collaboration2/PluginClient.h>
#include <Collaboration2/Plugins/KoinoniaProtocol.h>

/* Forward declarations: */
class MessageReader;
class MessageContinuation;

class KoinoniaClient:public PluginClient,public KoinoniaProtocol
	{
	/* Embedded classes: */
	public:
	typedef void (*SharedObjectUpdatedCallback)(KoinoniaClient* client,ObjectID id,void* object,void* userData); // Type of callback called when a shared object is updated
	
	typedef void* (*CreateNsObjectFunction)(KoinoniaClient* client,NamespaceID namespaceId,ObjectID objectId,DataType::TypeID type,void* userData); // Function called to create a memory representation for a new namespace object; returns pointer to new object's memory representation
	typedef void (*NsObjectCreatedCallback)(KoinoniaClient* client,NamespaceID namespaceId,ObjectID objectId,void* object,void* userData); // Callback called when a new namespace object has been created
	typedef void (*NsObjectReplacedCallback)(KoinoniaClient* client,NamespaceID namespaceId,ObjectID objectId,VersionNumber newVersion,void* object,void* userData); // Callback called when a namespace object's value has been replaced
	typedef void (*NsObjectDestroyedCallback)(KoinoniaClient* client,NamespaceID namespaceId,ObjectID objectId,void* object,void* userData); // Callback called when a namespace object has been destroyed
	
	private:
	typedef Misc::HashTable<std::string,void> NameSet; // Hash table to represent sets of names for collision checks
	
	struct SharedObject // Structure representing a shared object on the client side
		{
		/* Elements: */
		public:
		ObjectID clientId; // Unique client-side ID of this shared object
		ObjectID serverId; // Unique server-side ID of this shared object
		std::string name; // Unique name of this shared object
		DataType dataType; // Data type dictionary defining the shared object's type
		DataType::TypeID type; // The type of the shared object as defined by the data type dictionary
		VersionNumber version; // Version number of the shared object
		void* object; // Memory representation of the shared object
		SharedObjectUpdatedCallback sharedObjectUpdatedCallback; // Callback called when the shared object is updated by the server
		void* sharedObjectUpdatedCallbackData; // Additional data passed to shared object updated callback
		
		/* Constructors and destructors: */
		SharedObject(ObjectID sClientId,const std::string& sName,const DataType& sDataType,DataType::TypeID sType,void* sObject);
		~SharedObject(void);
		};
	
	typedef Misc::HashTable<ObjectID,SharedObject*> SharedObjectMap; // Hash table mapping client- or server-side shared object IDs to shared objects
	
	struct Namespace // Structure representing a shared namespace on the client side
		{
		/* Embedded classes: */
		public:
		struct SharedObject // Structure representing a shared object on the client side
			{
			/* Elements: */
			public:
			ObjectID clientId; // Client-side ID of this object
			ObjectID serverId; // Server-side ID of this object
			DataType::TypeID type; // The type of this shared object as defined by the namespace's data type dictionary
			VersionNumber version; // Server-side version number of the shared object
			void* object; // Memory representation of the shared object
			
			/* Constructors and destructors: */
			SharedObject(ObjectID sClientId,ObjectID sServerId,DataType::TypeID sType)
				:clientId(sClientId),serverId(sServerId),
				 type(sType),
				 version(0),object(0)
				{
				}
			};
		
		typedef Misc::HashTable<ObjectID,SharedObject*> SharedObjectMap; // Hash table mapping client- or server-side shared object IDs to shared objects
		
		/* Elements: */
		NamespaceID clientId; // Client-side ID of this namespace
		NamespaceID serverId; // Server-side ID of this namespace
		std::string name; // Namespace's name
		DataType dataType; // Data type dictionary defining the types of objects shared in this namespace
		
		Threads::Mutex objectMapMutex; // Mutex serializing access to the shared object maps
		ObjectID lastObjectId; // Client-side ID that was assigned to the most recently created shared object
		SharedObjectMap clientSharedObjects; // Map from client-side shared object IDs to shared objects
		SharedObjectMap serverSharedObjects; // Map from server-side shared object IDs to shared objects
		
		CreateNsObjectFunction createNsObjectFunction; // Function called to create a memory representation for a new shared object
		void* createNsObjectFunctionData; // Opaque pointer passed to the createNsObject function
		NsObjectCreatedCallback nsObjectCreatedCallback; // Callback called when a new shared object has been created
		void* nsObjectCreatedCallbackData; // Opaque pointer passed to the nsObjectCreated callback
		NsObjectReplacedCallback nsObjectReplacedCallback; // Callback called when a shared object's value has been replaced
		void* nsObjectReplacedCallbackData; // Opaque pointer passed to the nsObjectReplaced callback
		NsObjectDestroyedCallback nsObjectDestroyedCallback; // Callback called when a shared object has been destroyed
		void* nsObjectDestroyedCallbackData; // Opaque pointer passed to the nsObjectDestroyed callback
		
		Threads::Mutex startupMutex; // Mutex serializing access to the namespace's start-up state
		std::vector<MessageBuffer*> startupMessages; // List of messages queued up before the namespace received its server-side ID
		
		/* Constructors and destructors: */
		Namespace(NamespaceID sClientId,const std::string& name,const DataType& sDataType,CreateNsObjectFunction sCreateNsObjectFunction,void* sCreateNsObjectFunctionData);
		~Namespace(void);
		
		/* Methods: */
		ObjectID getObjectId(void) // Returns an unused client-side object ID; assumes object map is locked
			{
			do
				{
				++lastObjectId;
				}
			while(lastObjectId==ObjectID(0)||clientSharedObjects.isEntry(lastObjectId));
			return lastObjectId;
			}
		SharedObject* getClientSharedObject(ObjectID clientObjectId) // Returns a shared object by its client-side ID
			{
			Threads::Mutex::Lock objectMapLock(objectMapMutex);
			return clientSharedObjects.getEntry(clientObjectId).getDest();
			}
		SharedObject* getServerSharedObject(ObjectID serverObjectId) // Returns a shared object by its server-side ID
			{
			Threads::Mutex::Lock objectMapLock(objectMapMutex);
			return serverSharedObjects.getEntry(serverObjectId).getDest();
			}
		};
	
	typedef Misc::HashTable<NamespaceID,Namespace*> NamespaceMap; // Hash table mapping client- or server-side namespace IDs to namespaces
	
	/* Elements: */
	Threads::Mutex objectMapMutex; // Mutex serializing access to the shared object maps
	ObjectID lastObjectId; // Client-side ID that was assigned to the most recently created shared object
	SharedObjectMap clientSharedObjects; // Map from client-side shared object IDs to shared objects
	SharedObjectMap serverSharedObjects; // Map from server-side shared object IDs to shared objects
	NameSet sharedObjectNames; // Set of used shared object names
	
	Threads::Mutex namespaceMapMutex; // Mutex serializing access to the namespace maps
	NamespaceID lastNamespaceId; // Client-side ID that was assigned to the most recently created namespace
	NamespaceMap clientNamespaces; // Map from client-side namespace IDs to namespaces
	NamespaceMap serverNamespaces; // Map from server-side namespace IDs to namespaces
	NameSet namespaceNames; // Set of used namespace names
	
	Threads::Mutex startupMutex; // Mutex serializing access to the protocol's start-up state
	bool started; // Flag if the Koinonia protocol has been started and can exchange messages with the server
	std::vector<MessageBuffer*> startupMessages; // List of messages queued up before the Koinonia protocol was started
	
	/* Private methods: */
	ObjectID getObjectId(void) // Returns an unused client-side object ID; assumes object map is locked
		{
		do
			{
			++lastObjectId;
			}
		while(lastObjectId==ObjectID(0)||clientSharedObjects.isEntry(lastObjectId));
		return lastObjectId;
		}
	SharedObject* getClientSharedObject(ObjectID clientObjectId) // Returns a shared object by its client-side ID
		{
		Threads::Mutex::Lock objectMapLock(objectMapMutex);
		return clientSharedObjects.getEntry(clientObjectId).getDest();
		}
	SharedObject* getServerSharedObject(ObjectID serverObjectId) // Returns a shared object by its server-side ID
		{
		Threads::Mutex::Lock objectMapLock(objectMapMutex);
		return serverSharedObjects.getEntry(serverObjectId).getDest();
		}
	NamespaceID getNamespaceId(void) // Returns an unused client-side namespace ID; assumes namespace map is locked
		{
		do
			{
			++lastNamespaceId;
			}
		while(lastNamespaceId==NamespaceID(0)||clientNamespaces.isEntry(lastNamespaceId));
		return lastNamespaceId;
		}
	Namespace* getClientNamespace(NamespaceID clientNamespaceId) // Returns a namespace by its client-side ID
		{
		Threads::Mutex::Lock namespaceMapLock(namespaceMapMutex);
		return clientNamespaces.getEntry(clientNamespaceId).getDest();
		}
	Namespace* getServerNamespace(NamespaceID serverNamespaceId) // Returns a namespace by its server-side ID
		{
		Threads::Mutex::Lock namespaceMapLock(namespaceMapMutex);
		return serverNamespaces.getEntry(serverNamespaceId).getDest();
		}
	
	/* Methods receiving messages from the back end: */
	void frontendReplaceObjectNotificationCallback(unsigned int messageId,MessageReader& message);
	
	/* Methods receiving messages from the server: */
	MessageContinuation* createObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* replaceObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* replaceObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	
	void frontendCreateNsObjectNotificationCallback(unsigned int messageId,MessageReader& message);
	void frontendReplaceNsObjectNotificationCallback(unsigned int messageId,MessageReader& message);
	void frontendDestroyNsObjectNotificationCallback(unsigned int messageId,MessageReader& message);
	
	MessageContinuation* createNamespaceReplyCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* createNsObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* createNsObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* replaceNsObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* replaceNsObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* destroyNsObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	
	/* Constructors and destructors: */
	public:
	KoinoniaClient(Client* sClient);
	virtual ~KoinoniaClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	
	/* New methods: */
	static KoinoniaClient* requestClient(Client* client) // Returns a Koinonia protocol client
		{
		/* Request the Koinonia protocol client and cast it to the correct type: */
		return static_cast<KoinoniaClient*>(client->requestPluginProtocol(KOINONIA_PROTOCOLNAME,KOINONIA_PROTOCOLVERSION));
		}
	static KoinoniaClient* getClient(Client* client) // Returns a Koinonia protocol client that has previously been registered with the given collaboration client
		{
		/* Find the Koinonia protocol client and cast it to the correct type: */
		return static_cast<KoinoniaClient*>(client->findPluginProtocol(KOINONIA_PROTOCOLNAME,KOINONIA_PROTOCOLVERSION));
		}
	
	virtual ObjectID shareObject(const std::string& name,const DataType& dataType,DataType::TypeID type,void* object,KoinoniaClient::SharedObjectUpdatedCallback newCallback,void* newCallbackData); // Requests sharing of the given object of the given type with the server; returns client-side object ID
	virtual void replaceSharedObject(ObjectID objectId); // Notifies the server that the shared object of the given client-side ID has been replaced with a new version
	
	virtual NamespaceID shareNamespace(const std::string& name,const DataType& dataType,
	                                   CreateNsObjectFunction createNsObjectFunction,void* createNsObjectFunctionData,
	                                   NsObjectCreatedCallback nsObjectCreatedCallback,void* nsObjectCreatedCallbackData,
	                                   NsObjectReplacedCallback nsObjectReplacedCallback,void* nsObjectReplacedCallbackData,
	                                   NsObjectDestroyedCallback nsObjectDestroyedCallback,void* nsObjectDestroyedCallbackData); // Shares a namespace of the given name and data type dictionary with the server
	virtual ObjectID createNsObject(NamespaceID namespaceId,DataType::TypeID type,void* object); // Creates a new shared object of the given type and memory representation in the namespace of the given client-side ID
	virtual void replaceNsObject(NamespaceID namespaceId,ObjectID objectId); // Notifies the server that the shared object of the given client-side ID in the namespace of the given client-side ID has been replaced with a new value
	virtual void destroyNsObject(NamespaceID namespaceId,ObjectID objectId); // Notifies the server that the shared object of the given client-side ID in the namespace of the given client-side ID has been destroyed
	};

#endif
