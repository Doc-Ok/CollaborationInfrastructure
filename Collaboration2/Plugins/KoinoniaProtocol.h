/***********************************************************************
KoinoniaProtocol - Definition of the communication protocol between a
data sharing client and a server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_KOINONIAPROTOCOL_INCLUDED
#define PLUGINS_KOINONIAPROTOCOL_INCLUDED

#include <string>
#include <Misc/SizedTypes.h>
#include <Misc/VarIntMarshaller.h>
#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/DataType.h>

/* Forward declarations: */
class NonBlockSocket;

#define KOINONIA_PROTOCOLNAME "Koinonia"
#define KOINONIA_PROTOCOLVERSION 1U<<16

class KoinoniaProtocol
	{
	/* Embedded classes: */
	public:
	typedef Misc::UInt8 NamespaceID; // Type for IDs of shared namespaces
	typedef Misc::UInt16 ObjectID; // Type for shared object IDs
	typedef Misc::UInt8 VersionNumber; // Type for shared object version numbers, to reject updates from stale data
	
	/* Protocol message IDs: */
	protected:
	enum ClientMessages // Enumerated type for Koinonia protocol message IDs sent by clients
		{
		/* Messages for globally-shared static objects: */
		CreateObjectRequest=0,
		ReplaceObjectRequest,
		
		/* Messages for dynamically created objects: */
		CreateNamespaceRequest,
		CreateNsObjectRequest,
		ReplaceNsObjectRequest,
		DestroyNsObjectRequest,
		
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for Koinonia protocol message IDs sent by servers
		{
		/* Messages for globally-shared static objects: */
		CreateObjectReply=0,
		ReplaceObjectReply,
		ReplaceObjectNotification,
		
		/* Messages for dynamically created objects: */
		CreateNamespaceReply,
		CreateNsObjectReply,
		CreateNsObjectNotification,
		ReplaceNsObjectReply,
		ReplaceNsObjectNotification,
		DestroyNsObjectNotification,
		
		NumServerMessages
		};
	
	/* Protocol message data structure declarations: */
	struct CreateObjectRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ObjectID)+sizeof(DataType::TypeID)+sizeof(Misc::UInt16); // Size of the fixed message prefix
		ObjectID clientObjectId; // Client-side ID for the new shared object
		DataType::TypeID type; // The shared object's type (moved to front to simplify reading)
		Misc::UInt16 nameLength; // Globally-unique name of the shared object; variable length because we might need long names
		// Char name[nameLength]; // Globally unique variable-length name of the shared object
		// DataType dataType; // Wire representation of the shared object's data type definition
		// VarInt32 objectSize; // Size of the shared object's wire representation if type is not fixed size
		// Object object; // Wire representation of the shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase,const std::string& name,const DataType& dataType,bool explicitSize,Misc::UInt32 objectSize) // Returns a message buffer for a create object request message
			{
			/* Calculate the message body size: */
			size_t bodySize=size;
			bodySize+=name.length()*sizeof(Char);
			bodySize+=dataType.calcDataTypeSize();
			if(explicitSize)
				bodySize+=Misc::getVarInt32Size(objectSize);
			bodySize+=objectSize;
			return MessageBuffer::create(clientMessageBase+CreateObjectRequest,bodySize);
			}
		};
	
	struct CreateObjectReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=2*sizeof(ObjectID);
		ObjectID clientObjectId; // Client-side object ID that was sent in the object creation request
		ObjectID serverObjectId; // Server-side ID of newly created or accessed shared object; 0 if object could not be created or accessed due to mismatching data type definition
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a create object reply message
			{
			return MessageBuffer::create(serverMessageBase+CreateObjectReply,size);
			}
		};
	
	struct ReplaceObjectRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ObjectID)+sizeof(VersionNumber); // Size of the fixed message prefix
		ObjectID objectId; // Server-side ID for the shared object to be replaced
		VersionNumber objectVersion; // Version number of shared object on the client's side
		// VarInt32 objectSize; // Size of the shared object's wire representation if type is not fixed size
		// Object object; // Wire representation of the shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase,bool explicitSize,Misc::UInt32 objectSize) // Returns a message buffer for a replace object request message
			{
			/* Calculate the message body size: */
			size_t bodySize=size;
			if(explicitSize)
				bodySize+=Misc::getVarInt32Size(objectSize);
			bodySize+=objectSize;
			return MessageBuffer::create(clientMessageBase+ReplaceObjectRequest,bodySize);
			}
		};
	
	struct ReplaceObjectReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ObjectID)+sizeof(VersionNumber)+sizeof(Bool);
		ObjectID objectId; // Server-side ID of object from request
		VersionNumber objectVersion; // Version number of shared object from request message
		Bool granted; // Flag if the object was successfully replaced; if not, a replace object notification message will arrive soon
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a create object reply message
			{
			return MessageBuffer::create(serverMessageBase+ReplaceObjectReply,size);
			}
		};
	
	struct ReplaceObjectNotificationMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ObjectID)+sizeof(VersionNumber); // Size of the fixed message prefix
		ObjectID objectId; // Server-side ID for the shared object to be replaced
		VersionNumber objectVersion; // New version number of shared object
		// VarInt32 objectSize; // Size of the shared object's wire representation if type is not fixed size
		// Object object; // Wire representation of the shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase,bool explicitSize,Misc::UInt32 objectSize) // Returns a message buffer for a replace object notification message
			{
			/* Calculate the message body size: */
			size_t bodySize=size;
			if(explicitSize)
				bodySize+=Misc::getVarInt32Size(objectSize);
			bodySize+=objectSize;
			return MessageBuffer::create(serverMessageBase+ReplaceObjectNotification,bodySize);
			}
		};
	
	struct CreateNamespaceRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+sizeof(Misc::UInt16); // Size of the fixed message prefix
		NamespaceID clientNamespaceId; // Client-side ID for the new shared namespace
		Misc::UInt16 nameLength; // Globally-unique name of the shared namespace; variable length because we might need long names
		// Char name[nameLength]; // Globally unique variable-length name of the shared namespace
		// DataType dataType; // Wire representation of the shared namespace's data type dictionary
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase,const std::string& name,const DataType& dataType) // Returns a message buffer for a create namespace request message
			{
			/* Calculate the message body size: */
			size_t bodySize=size;
			bodySize+=name.length()*sizeof(Char);
			bodySize+=dataType.calcDataTypeSize();
			return MessageBuffer::create(clientMessageBase+CreateNamespaceRequest,bodySize);
			}
		};
	
	struct CreateNamespaceReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=2*sizeof(NamespaceID);
		NamespaceID clientNamespaceId; // Client-side namespace ID that was sent in the namespace creation request
		NamespaceID serverNamespaceId; // Server-side ID of newly created or accessed shared namespace; 0 if namespace could not be created or accessed due to mismatching data type definition
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a create object reply message
			{
			return MessageBuffer::create(serverMessageBase+CreateNamespaceReply,size);
			}
		};
	
	struct CreateNsObjectMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(DataType::TypeID); // Size of the fixed message prefix
		NamespaceID namespaceId; // ID of namespace in which to create the shared object
		ObjectID objectId; // If CreateNsObjectRequest: client-side object ID to associate with the CreateNsObjectReply message; otherwise: server-side object ID
		DataType::TypeID type; // Type of the shared object
		// VarInt32 objectSize; // Size of the shared object's wire representation if type is not fixed size
		// Object object; // Wire representation of the shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId,bool explicitSize,Misc::UInt32 objectSize) // Returns a message buffer for a create namespace object request or notification message
			{
			/* Calculate the message body size: */
			size_t bodySize=size;
			if(explicitSize)
				bodySize+=Misc::getVarInt32Size(objectSize);
			bodySize+=objectSize;
			return MessageBuffer::create(messageId,bodySize);
			}
		};
	
	struct CreateNsObjectReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+2*sizeof(ObjectID);
		NamespaceID namespaceId; // ID of namespace in which the shared object was created
		ObjectID clientObjectId; // Client-side object ID from the create request
		ObjectID serverObjectId; // Server-side object ID assigned to the new shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a create namespace object reply message
			{
			return MessageBuffer::create(serverMessageBase+CreateNsObjectReply,size);
			}
		};
	
	struct CreateNsObjectNotificationMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(DataType::TypeID);
		NamespaceID namespaceId; // ID of namespace in which the shared object was created
		ObjectID objectId; // Object ID assigned to the new shared object
		DataType::TypeID type; // Type of the new shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a create namespace object notification message
			{
			return MessageBuffer::create(serverMessageBase+CreateNsObjectNotification,size);
			}
		};
	
	struct ReplaceNsObjectMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(VersionNumber); // Size of the fixed message prefix
		NamespaceID namespaceId; // ID of namespace containing the shared object
		ObjectID objectId; // ID of the shared object
		VersionNumber version; // Client's version number of the shared object
		// VarInt32 objectSize; // Size of the shared object's wire representation if type is not fixed size
		// Object object; // Wire representation of the shared object
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId,bool explicitSize,Misc::UInt32 objectSize) // Returns a message buffer for a replace namespace object request or notification message
			{
			/* Calculate the message body size: */
			size_t bodySize=size;
			if(explicitSize)
				bodySize+=Misc::getVarInt32Size(objectSize);
			bodySize+=objectSize;
			return MessageBuffer::create(messageId,bodySize);
			}
		};
	
	struct ReplaceNsObjectReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(VersionNumber)+sizeof(Bool);
		NamespaceID namespaceId; // ID of namespace containing the shared object
		ObjectID objectId; // ID of the shared object
		VersionNumber objectVersion; // Version number of shared object from request message
		Bool granted; // Flag if the object was successfully replaced; if not, a replace namespace object notification message will arrive soon
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a replace namespace object reply message
			{
			return MessageBuffer::create(serverMessageBase+ReplaceNsObjectReply,size);
			}
		};
	
	struct DestroyNsObjectMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(NamespaceID)+sizeof(ObjectID);
		NamespaceID namespaceId; // ID of namespace containing the shared object to be destroyed
		ObjectID objectId; // ID of the shared object to be destroyed
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a destroy namespace object request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	/* Helper classes: */
	class ReadObjectCont:public MessageContinuation // Class to read an object from a non-blocking socket
		{
		/* Embedded classes: */
		private:
		enum State
			{
			ReadObjectSizeFirst,
			ReadObjectSize,
			ReadObject,
			Done
			};
		
		/* Elements: */
		size_t headerSize; // Amount of bytes to reserve for a message header written into the object message later on
		bool explicitSize; // Flag whether the size of the object is encoded into the object message
		Misc::UInt32 objectSize; // Size of the object's serialization
		State state;
		MessageBuffer* object; // Message buffer to hold the object's serialization as a ReplaceObjectNotification message
		MessageWriter objectWriter; // A writer to write into the object's serialization
		size_t remaining; // Number of bytes or elements left to read in current state
		
		/* Private methods: */
		void startReadingObject(void); // Starts reading the object's serialization
		
		/* Constructors and destructors: */
		public:
		ReadObjectCont(size_t sHeaderSize,Misc::UInt32 sObjectSize =0) // Creates a reader for objects of the given size, or of explicitly encoded size is sObjectSize is 0
			:headerSize(sHeaderSize),
			 explicitSize(sObjectSize==0),
			 objectSize(sObjectSize),object(0)
			{
			/* Initialize reader state based on whether object size is known a-priori: */
			if(explicitSize)
				{
				/* Start reading the object's size: */
				state=ReadObjectSizeFirst;
				remaining=sizeof(Misc::UInt8);
				}
			else
				{
				/* Start reading the object's serialization immediately: */
				startReadingObject();
				}
			}
		virtual ~ReadObjectCont(void);
		
		/* Methods: */
		bool read(NonBlockSocket& socket); // Continues reading the object; returns true if complete
		MessageBuffer* finishObject(const DataType& dataType,DataType::TypeID type,bool swapEndianness); // Finishes the read object's representation after it has been read completely; returns the message buffer containing the object's serialization
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion;
	};

#endif
