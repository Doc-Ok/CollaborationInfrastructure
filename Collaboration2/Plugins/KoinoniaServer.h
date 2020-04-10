/***********************************************************************
KoinoniaServer - Server for data sharing plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_KOINONIASERVER_INCLUDED
#define PLUGINS_KOINONIASERVER_INCLUDED

#include <string>
#include <Misc/SizedTypes.h>
#include <Misc/StringHashFunctions.h>
#include <Misc/HashTable.h>

#include <Collaboration2/MessageBuffer.h>
#include <Collaboration2/DataType.h>
#include <Collaboration2/PluginServer.h>
#include <Collaboration2/Plugins/KoinoniaProtocol.h>

/* Forward declarations: */
class MessageContinuation;

class KoinoniaServer:public PluginServer,public KoinoniaProtocol
	{
	/* Embedded classes: */
	private:
	struct SharedObject // Structure representing a globally-shared static object
		{
		/* Elements: */
		public:
		ObjectID id; // Unique ID of this shared object
		std::string name; // Unique name of this shared object
		DataType dataType; // Data type dictionary defining the shared object's type
		DataType::TypeID type; // The type of the shared object as defined by the data type dictionary
		VersionNumber version; // Version number of the shared object
		MessageBuffer* object; // Pointer to a message buffer holding the object's serialization as a ReplaceObjectNotification message
		ClientIDList clients; // List of IDs of clients sharing this object
		
		/* Constructors and destructors: */
		SharedObject(void) // Creates an uninitialized shared object
			:version(0),object(0)
			{
			}
		~SharedObject(void)
			{
			if(object!=0)
				object->unref();
			}
		};
	
	typedef Misc::HashTable<ObjectID,SharedObject*> SharedObjectMap; // Hash table mapping shared object IDs to shared objects
	typedef Misc::HashTable<std::string,SharedObject*> SharedObjectNameMap; // Hash table mapping shared object names to shared objects
	
	struct Namespace // Structure representing a shared namespace containing a set of dynamic objects
		{
		/* Embedded classes: */
		public:
		struct SharedObject // Structure representing a namespace-shared dynamic object
			{
			/* Elements: */
			public:
			ObjectID id; // Namespace-unique ID of this shared object
			DataType::TypeID type; // The type of this shared object as defined by the namespace's data type dictionary
			VersionNumber version; // Version number of the shared object
			MessageBuffer* object; // Pointer to a headerless message buffer holding the object's serialization
			
			/* Constructors and destructors: */
			SharedObject(ObjectID sId,DataType::TypeID sType,MessageBuffer* sObject)
				:id(sId),
				 type(sType),
				 version(0),object(sObject->ref())
				{
				}
			SharedObject(const SharedObject& source) // Copy constructor, to simplify hash table management
				:id(source.id),
				 type(source.type),
				 version(source.version),object(source.object->ref())
				{
				}
			~SharedObject(void)
				{
				object->unref();
				}
			};
		
		typedef Misc::HashTable<ObjectID,SharedObject> SharedObjectMap; // Hash table mapping shared object IDs to shared objects
		
		/* Elements: */
		NamespaceID id; // Unique ID of this shared namespace
		std::string name; // Unique name of this shared namespace
		DataType dataType; // Data type dictionary defining the types of objects in this namespace
		ObjectID lastObjectId; // ID that was assigned to the most recently created shared object
		SharedObjectMap sharedObjects; // Map of current shared objects
		ClientIDList clients; // List of IDs of clients sharing this namespace
		
		/* Constructors and destructors: */
		Namespace(void) // Creates an uninitialized namespace, to be filled in by message handler
			:lastObjectId(0),sharedObjects(17)
			{
			}
		};
	
	typedef Misc::HashTable<NamespaceID,Namespace*> NamespaceMap; // Hash table mapping shared namespace IDs to shared namespaces
	typedef Misc::HashTable<std::string,Namespace*> NamespaceNameMap; // Hash table mapping shared namespace names to shared namespaces
	
	/* Elements: */
	ObjectID lastObjectId; // ID that was assigned to the most recently created globally-shared static object
	SharedObjectMap sharedObjects; // Map of globally-shared static objects
	SharedObjectNameMap sharedObjectNames; // Secondary map from globally-shared static object names to globally-shared static objects
	NamespaceID lastNamespaceId; // ID that was assigned to the most recently created shared namespace
	NamespaceMap namespaces; // Map of shared namespaces
	NamespaceNameMap namespaceNames; // Secondary map from namespace names to shared namespaces
	
	/* Private methods: */
	void listObjectsCommand(const char* argumentsBegin,const char* argumentsEnd);
	void printObjectCommand(const char* argumentsBegin,const char* argumentsEnd);
	void saveObjectCommand(const char* argumentsBegin,const char* argumentsEnd);
	void loadObjectCommand(const char* argumentsBegin,const char* argumentsEnd);
	void deleteObjectCommand(const char* argumentsBegin,const char* argumentsEnd);
	
	MessageContinuation* createObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* replaceObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	
	void listNamespacesCommand(const char* argumentsBegin,const char* argumentsEnd);
	void listNamespaceObjectsCommand(const char* argumentsBegin,const char* argumentsEnd);
	void printNamespaceObjectCommand(const char* argumentsBegin,const char* argumentsEnd);
	void saveNamespaceCommand(const char* argumentsBegin,const char* argumentsEnd);
	void loadNamespaceCommand(const char* argumentsBegin,const char* argumentsEnd);
	void deleteNamespaceCommand(const char* argumentsBegin,const char* argumentsEnd);
	
	MessageContinuation* createNamespaceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* createNsObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* replaceNsObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* destroyNsObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	
	/* Constructors and destructors: */
	public:
	KoinoniaServer(Server* sServer);
	virtual ~KoinoniaServer(void);
	
	/* Methods from class PluginServer: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void clientDisconnected(unsigned int clientId);
	};

#endif
