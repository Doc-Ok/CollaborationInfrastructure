/***********************************************************************
KoinoniaClient - Client for data sharing plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/KoinoniaClient.h>

#include <stdexcept>
#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Misc/VarIntMarshaller.h>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageEditor.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/Client.h>

/*********************************************
Methods of class KoinoniaClient::SharedObject:
*********************************************/

KoinoniaClient::SharedObject::SharedObject(KoinoniaProtocol::ObjectID sClientId,const std::string& sName,const DataType& sDataType,DataType::TypeID sType,void* sObject)
	:clientId(sClientId),serverId(0),
	 name(sName),
	 dataType(sDataType),type(sType),
	 version(0),object(sObject),
	 sharedObjectUpdatedCallback(0),sharedObjectUpdatedCallbackData(0)
	{
	}

KoinoniaClient::SharedObject::~SharedObject(void)
	{
	}

/******************************************
Methods of class KoinoniaClient::Namespace:
******************************************/

KoinoniaClient::Namespace::Namespace(KoinoniaProtocol::NamespaceID sClientId,const std::string& sName,const DataType& sDataType,KoinoniaClient::CreateNsObjectFunction sCreateNsObjectFunction,void* sCreateNsObjectFunctionData)
	:clientId(sClientId),serverId(0),
	 name(sName),
	 dataType(sDataType),
	 lastObjectId(0),clientSharedObjects(17),serverSharedObjects(17),
	 createNsObjectFunction(sCreateNsObjectFunction),createNsObjectFunctionData(sCreateNsObjectFunctionData),
	 nsObjectCreatedCallback(0),nsObjectCreatedCallbackData(0),
	 nsObjectReplacedCallback(0),nsObjectReplacedCallbackData(0),
	 nsObjectDestroyedCallback(0),nsObjectDestroyedCallbackData(0)
	{
	}

KoinoniaClient::Namespace::~Namespace(void)
	{
	/* Delete all shared objects: */
	{
	Threads::Mutex::Lock objectMapLock(objectMapMutex);
	for(SharedObjectMap::Iterator csoIt=clientSharedObjects.begin();!csoIt.isFinished();++csoIt)
		delete csoIt->getDest();
	}
	
	/* Delete all unsent start-up messages: */
	{
	Threads::Mutex::Lock startupLock(startupMutex);
	for(std::vector<MessageBuffer*>::iterator smIt=startupMessages.begin();smIt!=startupMessages.end();++smIt)
		(*smIt)->unref();
	}
	}

/*******************************
Methods of class KoinoniaClient:
*******************************/

/*********************************************************************
Methods processing messages related to globally-shared static objects:
*********************************************************************/

void KoinoniaClient::frontendReplaceObjectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the server-side object ID and access the shared object: */
	SharedObject* so=getServerSharedObject(message.read<ObjectID>());
	
	/* Read the rest of the message header: */
	so->version=message.read<VersionNumber>();
	if(!so->dataType.hasFixedSize(so->type))
		Misc::readVarInt32(message);
	
	/* Update the shared object's memory representation: */
	so->dataType.read(message,so->type,so->object);
	
	/* Call the object update callback if it exists: */
	if(so->sharedObjectUpdatedCallback!=0)
		so->sharedObjectUpdatedCallback(this,so->clientId,so->object,so->sharedObjectUpdatedCallbackData);
	}

MessageContinuation* KoinoniaClient::createObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the client- and server-side object IDs: */
	ObjectID clientId=socket.read<ObjectID>();
	ObjectID serverId=socket.read<ObjectID>();
	
	/* Lock the shared object maps: */
	{
	Threads::Mutex::Lock objectMapLock(objectMapMutex);
	
	/* Access the shared object: */
	SharedObject* so=clientSharedObjects.getEntry(clientId).getDest();
	
	/* Check whether the object was successfully created or accessed: */
	if(serverId!=0)
		{
		/* Set the shared object's server-side ID: */
		so->serverId=serverId;
		serverSharedObjects.setEntry(SharedObjectMap::Entry(serverId,so));
		}
	else
		{
		/* Need to call an error callback or something: */
		// ...
		Misc::formattedUserError("Koinonia: Unable to share object %s with server",so->name.c_str());
		}
	}
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* KoinoniaClient::replaceObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the object ID, request version, and granted flag: */
	socket.read<ObjectID>();
	socket.read<VersionNumber>();
	socket.read<Bool>();
	
	/* Could call a callback here if the request was denied, but it might not even be necessary */
	// ...
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* KoinoniaClient::replaceObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public ReadObjectCont
		{
		/* Elements: */
		public:
		SharedObject* so; // The shared object to be replaced
		VersionNumber newVersion; // The shared object's new version number
		
		/* Constructors and destructors: */
		Cont(Misc::UInt32 sObjectSize,SharedObject* sSo,VersionNumber sNewVersion)
			:ReadObjectCont(sizeof(MessageID)+sizeof(ObjectID)+sizeof(VersionNumber),sObjectSize),
			 so(sSo),
			 newVersion(sNewVersion)
			{
			}
		};

	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the server-side object ID and access the shared object: */
		SharedObject* so=getServerSharedObject(socket.read<ObjectID>());
		
		/* Read the shared object's new version number: */
		VersionNumber newObjectVersion=socket.read<VersionNumber>();
		
		/* Create a continuation object to read the object's new value: */
		Misc::UInt32 objectSize=0;
		if(so->dataType.hasFixedSize(so->type))
			objectSize=so->dataType.getMinSize(so->type);
		cont=new Cont(objectSize,so,newObjectVersion);
		}
	
	/* Continue reading the shared object's new value and check if it's done: */
	if(cont->read(socket))
		{
		/* Check and/or endianness-swap the shared object's new value: */
		SharedObject* so=cont->so;
		MessageBuffer* object=cont->finishObject(so->dataType,so->type,socket.getSwapOnRead());
		
		/* Check if there is a front end: */
		if(client->haveFrontend())
			{
			/* Write the correct message header into the object's representation: */
			object->setMessageId(serverMessageBase+ReplaceObjectNotification);
			{
			MessageWriter writer(object->ref());
			writer.write(so->serverId);
			writer.write(cont->newVersion);
			}
			
			/* Forward the object replace notification to the front end: */
			client->queueFrontendMessage(object);
			}
		else
			{
			/* Update the shared object from its serialization: */
			{
			MessageReader reader(object->ref());
			
			/* Skip the message header: */
			reader.advanceReadPtr(sizeof(MessageID)+sizeof(ObjectID)+sizeof(VersionNumber));
			if(!so->dataType.hasFixedSize(so->type))
				Misc::readVarInt32(reader);
			
			/* Update the shared object's memory representation: */
			so->dataType.read(reader,so->type,so->object);
			}
			
			/* Call the object update callback if it exists: */
			if(so->sharedObjectUpdatedCallback!=0)
				so->sharedObjectUpdatedCallback(this,so->clientId,so->object,so->sharedObjectUpdatedCallbackData);
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

/******************************************************************************
Methods processing messages related to namespaces and namespace-shared objects:
******************************************************************************/

void KoinoniaClient::frontendCreateNsObjectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the server-side namespace ID and access the namespace: */
	Namespace* ns=getServerNamespace(message.read<NamespaceID>());
	
	/* Read the rest of the message header: */
	ObjectID serverId=message.read<ObjectID>();
	DataType::TypeID type=message.read<DataType::TypeID>();
	if(!ns->dataType.hasFixedSize(type))
		Misc::readVarInt32(message);
	
	/* Assign an unused client-side ID to the new object and add a new shared object to the maps: */
	Namespace::SharedObject* so=0;
	{
	Threads::Mutex::Lock objectMapLock(ns->objectMapMutex);
	so=new Namespace::SharedObject(ns->getObjectId(),serverId,type);
	ns->clientSharedObjects.setEntry(Namespace::SharedObjectMap::Entry(so->clientId,so));
	ns->serverSharedObjects.setEntry(Namespace::SharedObjectMap::Entry(so->serverId,so));
	}
	
	/* Call the namespace object creation function: */
	so->object=ns->createNsObjectFunction(this,ns->clientId,so->clientId,so->type,ns->createNsObjectFunctionData);
	
	/* Initialize the new shared object from its serialization: */
	ns->dataType.read(message,so->type,so->object);
	
	/* Call the namespace object creation callback if it exists: */
	if(ns->nsObjectCreatedCallback!=0)
		ns->nsObjectCreatedCallback(this,ns->clientId,so->clientId,so->object,ns->nsObjectCreatedCallbackData);
	}

void KoinoniaClient::frontendReplaceNsObjectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the namespace ID and access the namespace: */
	Namespace* ns=getServerNamespace(message.read<NamespaceID>());
	
	/* Read the object ID and access the object: */
	Namespace::SharedObject* so=ns->getServerSharedObject(message.read<ObjectID>());
	
	/* Update the shared object's version number: */
	so->version=message.read<VersionNumber>();
	
	/* Read the rest of the message header: */
	if(!ns->dataType.hasFixedSize(so->type))
		Misc::readVarInt32(message);
	
	/* Update the shared object's memory representation: */
	ns->dataType.read(message,so->type,so->object);
	
	/* Call the namespace object replacement callback if it exists: */
	if(ns->nsObjectReplacedCallback!=0)
		ns->nsObjectReplacedCallback(this,ns->clientId,so->clientId,so->version,so->object,ns->nsObjectReplacedCallbackData);
	}

void KoinoniaClient::frontendDestroyNsObjectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the namespace ID and access the namespace: */
	Namespace* ns=getServerNamespace(message.read<NamespaceID>());
	
	/* Read the object ID and access the shared object: */
	Namespace::SharedObject* so=0;
	{
	Threads::Mutex::Lock objectMapLock(ns->objectMapMutex);
	so=ns->serverSharedObjects.getEntry(message.read<ObjectID>()).getDest();
	
	/* Remove the shared object from the namespace's maps: */
	ns->clientSharedObjects.removeEntry(so->clientId);
	ns->serverSharedObjects.removeEntry(so->serverId);
	}
	
	/* Call the namespace object destruction callback if it exists: */
	if(ns->nsObjectDestroyedCallback!=0)
		ns->nsObjectDestroyedCallback(this,ns->clientId,so->clientId,so->object,ns->nsObjectDestroyedCallbackData);
	
	/* Delete the shared object: */
	delete so;
	}

MessageContinuation* KoinoniaClient::createNamespaceReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the client- and server-side namespace IDs: */
	NamespaceID clientId=socket.read<NamespaceID>();
	NamespaceID serverId=socket.read<NamespaceID>();
	
	/* Access the namespace: */
	Namespace* ns=0;
	{
	Threads::Mutex::Lock namespaceMapLock(namespaceMapMutex);
	ns=clientNamespaces.getEntry(clientId).getDest();
	
	/* Check whether the namespace was successfully created or accessed: */
	if(serverId!=0)
		{
		/* Set the namespace's server-side ID: */
		ns->serverId=serverId;
		serverNamespaces.setEntry(NamespaceMap::Entry(serverId,ns));
		}
	else
		{
		/* Need to call an error callback or something: */
		// ...
		Misc::formattedUserError("Koinonia: Unable to share namespace %s with server",ns->name.c_str());
		}
	}
	
	if(serverId!=0)
		{
		/* Send all queued start-up messages to the server: */
		Threads::Mutex::Lock startupLock(ns->startupMutex);
		for(std::vector<MessageBuffer*>::iterator smIt=ns->startupMessages.begin();smIt!=ns->startupMessages.end();++smIt)
			{
			/* Enter the new server-side namespace ID into the message buffer and send it to the server: */
			MessageWriter headerWriter(*smIt); // This takes the message list's reference, so the message will be deleted when the message writer destructs
			headerWriter.advanceWritePtr(sizeof(MessageID));
			headerWriter.write(serverId);
			client->queueMessage(headerWriter.getBuffer());
			}
		}
	
	/* Clear the start-up message list: */
	ns->startupMessages.clear();
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* KoinoniaClient::createNsObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the server-side namespace ID and access the namespace: */
	Namespace* ns=getServerNamespace(socket.read<NamespaceID>());
	
	/* Read the client- and server-side shared object IDs: */
	ObjectID clientId=socket.read<ObjectID>();
	ObjectID serverId=socket.read<ObjectID>();
	
	/* Lock the namespace's shared object maps: */
	{
	Threads::Mutex::Lock nsObjectMapLock(ns->objectMapMutex);
	
	/* Access the shared object: */
	Namespace::SharedObject* so=ns->clientSharedObjects.getEntry(clientId).getDest();
	
	/* Check whether the shared object was successfully created or accessed: */
	if(serverId!=0)
		{
		/* Set the shared object's server-side ID: */
		so->serverId=serverId;
		ns->serverSharedObjects.setEntry(Namespace::SharedObjectMap::Entry(serverId,so));
		}
	else
		{
		/* Need to call an error callback or something: */
		// ...
		Misc::formattedUserError("Koinonia: Unable to share object with local ID %u in namespace %s",(unsigned int)(clientId),ns->name.c_str());
		}
	}
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* KoinoniaClient::createNsObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public ReadObjectCont
		{
		/* Elements: */
		public:
		Namespace* ns; // The namespace in which the new object is to be created
		ObjectID serverId; // The server-side ID of the new object
		DataType::TypeID type; // The type of the new object, as defined by the namespace's data type library
		
		/* Constructors and destructors: */
		Cont(Misc::UInt32 sObjectSize,Namespace* sNs,ObjectID sServerId,DataType::TypeID sType)
			:ReadObjectCont(sizeof(MessageID)+sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(DataType::TypeID),sObjectSize),
			 ns(sNs),
			 serverId(sServerId),type(sType)
			{
			}
		};

	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the server-side namespace ID and access the namespace: */
		Namespace* ns=getServerNamespace(socket.read<NamespaceID>());
		
		/* Read the new object's server-side ID and type: */
		ObjectID serverId=socket.read<ObjectID>();
		DataType::TypeID type=socket.read<DataType::TypeID>();
		
		/* Create a continuation object to read the object's new value: */
		Misc::UInt32 objectSize=0;
		if(ns->dataType.hasFixedSize(type))
			objectSize=ns->dataType.getMinSize(type);
		cont=new Cont(objectSize,ns,serverId,type);
		}
	
	/* Continue reading the new shared object's value and check if it's done: */
	if(cont->read(socket))
		{
		Namespace* ns=cont->ns;
		
		/* Check and/or endianness-swap the new shared object's value: */
		MessageBuffer* object=cont->finishObject(ns->dataType,cont->type,socket.getSwapOnRead());
		
		/* Check if the client has front-end forwarding: */
		if(client->haveFrontend())
			{
			/* Write the correct message header into the object's representation: */
			object->setMessageId(serverMessageBase+CreateNsObjectNotification);
			{
			MessageWriter writer(object->ref());
			writer.write(ns->serverId);
			writer.write(cont->serverId);
			writer.write(cont->type);
			}
			
			/* Forward the namespace object create notification to the front end: */
			client->queueFrontendMessage(object);
			}
		else
			{
			/* Assign an unused client-side ID to the new object and add a new shared object to the maps: */
			Namespace::SharedObject* so=0;
			{
			Threads::Mutex::Lock objectMapLock(ns->objectMapMutex);
			so=new Namespace::SharedObject(ns->getObjectId(),cont->serverId,cont->type);
			ns->clientSharedObjects.setEntry(Namespace::SharedObjectMap::Entry(so->clientId,so));
			ns->serverSharedObjects.setEntry(Namespace::SharedObjectMap::Entry(so->serverId,so));
			}
			
			/* Call the namespace object creation function: */
			so->object=ns->createNsObjectFunction(this,ns->clientId,so->clientId,so->type,ns->createNsObjectFunctionData);
			
			/* Initialize the new shared object from its serialization: */
			{
			MessageReader reader(object->ref());
			
			/* Skip the message header: */
			reader.advanceReadPtr(sizeof(MessageID)+sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(DataType::TypeID));
			if(!ns->dataType.hasFixedSize(so->type))
				Misc::readVarInt32(reader);
			
			/* Update the shared object's memory representation: */
			ns->dataType.read(reader,so->type,so->object);
			}
			
			/* Call the namespace object creation callback if it exists: */
			if(ns->nsObjectCreatedCallback!=0)
				ns->nsObjectCreatedCallback(this,ns->clientId,so->clientId,so->object,ns->nsObjectCreatedCallbackData);
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

MessageContinuation* KoinoniaClient::replaceNsObjectReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the namespace ID, object ID, request version, and granted flag: */
	socket.read<NamespaceID>();
	socket.read<ObjectID>();
	socket.read<VersionNumber>();
	socket.read<Bool>();
	
	/* Could call a callback here if the request was denied, but it might not even be necessary */
	// ...
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* KoinoniaClient::replaceNsObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public ReadObjectCont
		{
		/* Elements: */
		public:
		Namespace* ns; // The namespace in which the object is to be replaced
		Namespace::SharedObject* so; // The shared object whose value is to be replaced
		VersionNumber newVersion; // The new version number of the shared object
		
		/* Constructors and destructors: */
		Cont(Misc::UInt32 sObjectSize,Namespace* sNs,Namespace::SharedObject* sSo,VersionNumber sNewVersion)
			:ReadObjectCont(sizeof(MessageID)+sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(VersionNumber),sObjectSize),
			 ns(sNs),so(sSo),
			 newVersion(sNewVersion)
			{
			}
		};

	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the server-side namespace ID and access the namespace: */
		Namespace* ns=getServerNamespace(socket.read<NamespaceID>());
		
		/* Read the server-side object ID and access the shared object: */
		Namespace::SharedObject* so=ns->getServerSharedObject(socket.read<ObjectID>());
		
		/* Read the shared object's new version number: */
		VersionNumber newVersion=socket.read<VersionNumber>();
		
		/* Create a continuation object to read the object's new value: */
		Misc::UInt32 objectSize=0;
		if(ns->dataType.hasFixedSize(so->type))
			objectSize=ns->dataType.getMinSize(so->type);
		cont=new Cont(objectSize,ns,so,newVersion);
		}
	
	/* Continue reading the new shared object's value and check if it's done: */
	if(cont->read(socket))
		{
		Namespace* ns=cont->ns;
		Namespace::SharedObject* so=cont->so;
		
		/* Check and/or endianness-swap the shared object's new value: */
		MessageBuffer* object=cont->finishObject(ns->dataType,so->type,socket.getSwapOnRead());
		
		/* Check if the client has front-end forwarding: */
		if(client->haveFrontend())
			{
			/* Write the correct message header into the object's representation: */
			object->setMessageId(serverMessageBase+ReplaceNsObjectNotification);
			{
			MessageWriter writer(object->ref());
			writer.write(ns->serverId);
			writer.write(so->serverId);
			writer.write(cont->newVersion);
			}
			
			/* Forward the namespace object replace notification to the front end: */
			client->queueFrontendMessage(object);
			}
		else
			{
			/* Update the shared object's version number: */
			so->version=cont->newVersion;
			
			/* Replace the shared object's memory representation from its serialization: */
			{
			MessageReader reader(object->ref());
			
			/* Skip the message header: */
			reader.advanceReadPtr(sizeof(MessageID)+sizeof(NamespaceID)+sizeof(ObjectID)+sizeof(VersionNumber));
			if(!ns->dataType.hasFixedSize(so->type))
				Misc::readVarInt32(reader);
			
			/* Update the shared object's memory representation: */
			ns->dataType.read(reader,so->type,so->object);
			}
			
			/* Call the namespace object replacement callback if it exists: */
			if(ns->nsObjectReplacedCallback!=0)
				ns->nsObjectReplacedCallback(this,ns->clientId,so->clientId,so->version,so->object,ns->nsObjectReplacedCallbackData);
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

MessageContinuation* KoinoniaClient::destroyNsObjectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if the client has front-end forwarding: */
	if(client->haveFrontend())
		{
		/* Forward the message to the front end: */
		MessageWriter destroyNsObjectNotification(DestroyNsObjectMsg::createMessage(serverMessageBase+DestroyNsObjectNotification));
		destroyNsObjectNotification.write(socket.read<NamespaceID>());
		destroyNsObjectNotification.write(socket.read<ObjectID>());
		client->queueFrontendMessage(destroyNsObjectNotification.getBuffer());
		}
	else
		{
		/* Read the namespace ID and access the namespace: */
		Namespace* ns=getServerNamespace(socket.read<NamespaceID>());
		
		/* Read the object ID and access the shared object: */
		Namespace::SharedObject* so=0;
		{
		Threads::Mutex::Lock objectMapLock(ns->objectMapMutex);
		so=ns->serverSharedObjects.getEntry(socket.read<ObjectID>()).getDest();
		
		/* Remove the shared object from the namespace's maps: */
		ns->clientSharedObjects.removeEntry(so->clientId);
		ns->serverSharedObjects.removeEntry(so->serverId);
		}
		
		/* Call the namespace object destruction callback if it exists: */
		if(ns->nsObjectDestroyedCallback!=0)
			ns->nsObjectDestroyedCallback(this,ns->clientId,so->clientId,so->object,ns->nsObjectDestroyedCallbackData);
		
		/* Delete the shared object: */
		delete so;
		}
	
	/* Done with message: */
	return 0;
	}

KoinoniaClient::KoinoniaClient(Client* sClient)
	:PluginClient(sClient),
	 lastObjectId(0),clientSharedObjects(17),serverSharedObjects(17),sharedObjectNames(17),
	 lastNamespaceId(0),clientNamespaces(17),serverNamespaces(17),namespaceNames(17),
	 started(false)
	{
	}

KoinoniaClient::~KoinoniaClient(void)
	{
	/* Destroy all shared objects: */
	{
	Threads::Mutex::Lock objectMapLock(objectMapMutex);
	for(SharedObjectMap::Iterator soIt=clientSharedObjects.begin();!soIt.isFinished();++soIt)
		delete soIt->getDest();
	}
	
	/* Destroy all namespaces: */
	{
	Threads::Mutex::Lock namespaceMapLock(namespaceMapMutex);
	for(NamespaceMap::Iterator nsIt=clientNamespaces.begin();!nsIt.isFinished();++nsIt)
		delete nsIt->getDest();
	}
	
	/* Remove any queued-up start-up messages if the protocol was never started: */
	{
	Threads::Mutex::Lock startupLock(startupMutex);
	for(std::vector<MessageBuffer*>::iterator smIt=startupMessages.begin();smIt!=startupMessages.end();++smIt)
		(*smIt)->unref();
	}
	}

const char* KoinoniaClient::getName(void) const
	{
	return protocolName;
	}

unsigned int KoinoniaClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int KoinoniaClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int KoinoniaClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void KoinoniaClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Check if the client has front-end forwarding: */
	if(client->haveFrontend())
		{
		/* Add front-end message handlers to handle signals from the back end: */
		client->setFrontendMessageHandler(serverMessageBase+ReplaceObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::frontendReplaceObjectNotificationCallback>,this);
		
		client->setFrontendMessageHandler(serverMessageBase+CreateNsObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::frontendCreateNsObjectNotificationCallback>,this);
		client->setFrontendMessageHandler(serverMessageBase+ReplaceNsObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::frontendReplaceNsObjectNotificationCallback>,this);
		client->setFrontendMessageHandler(serverMessageBase+DestroyNsObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::frontendDestroyNsObjectNotificationCallback>,this);
		}
	
	/* Register message handlers: */
	client->setTCPMessageHandler(serverMessageBase+CreateObjectReply,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::createObjectReplyCallback>,this,CreateObjectReplyMsg::size);
	client->setTCPMessageHandler(serverMessageBase+ReplaceObjectReply,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::replaceObjectReplyCallback>,this,ReplaceObjectReplyMsg::size);
	client->setTCPMessageHandler(serverMessageBase+ReplaceObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::replaceObjectNotificationCallback>,this,ReplaceObjectNotificationMsg::size);
	
	client->setTCPMessageHandler(serverMessageBase+CreateNamespaceReply,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::createNamespaceReplyCallback>,this,CreateNamespaceReplyMsg::size);
	client->setTCPMessageHandler(serverMessageBase+CreateNsObjectReply,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::createNsObjectReplyCallback>,this,CreateNsObjectReplyMsg::size);
	client->setTCPMessageHandler(serverMessageBase+CreateNsObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::createNsObjectNotificationCallback>,this,CreateNsObjectMsg::size);
	client->setTCPMessageHandler(serverMessageBase+ReplaceNsObjectReply,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::replaceNsObjectReplyCallback>,this,ReplaceNsObjectReplyMsg::size);
	client->setTCPMessageHandler(serverMessageBase+ReplaceNsObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::replaceNsObjectNotificationCallback>,this,ReplaceNsObjectMsg::size);
	client->setTCPMessageHandler(serverMessageBase+DestroyNsObjectNotification,Client::wrapMethod<KoinoniaClient,&KoinoniaClient::destroyNsObjectNotificationCallback>,this,DestroyNsObjectMsg::size);
	}

void KoinoniaClient::start(void)
	{
	/* Send all queued-up start-up messages to the server: */
	{
	Threads::Mutex::Lock startupLock(startupMutex);
	for(std::vector<MessageBuffer*>::iterator smIt=startupMessages.begin();smIt!=startupMessages.end();++smIt)
		{
		/* Fix the message's ID, as the client message base was not yet known when it was written, and send it to the server: */
		(*smIt)->setMessageId((*smIt)->getMessageId()+clientMessageBase);
		MessageEditor editor(*smIt);
		editor.write(MessageID((*smIt)->getMessageId()));
		client->queueMessage(editor.getBuffer());
		}
	
	/* Clear the start-up message list: */
	startupMessages.clear();
	
	/* Mark the protocol as running: */
	started=true;
	}
	}

KoinoniaProtocol::ObjectID KoinoniaClient::shareObject(const std::string& name,const DataType& dataType,DataType::TypeID type,void* object,KoinoniaClient::SharedObjectUpdatedCallback newCallback,void* newCallbackData)
	{
	/* Ensure that the shared object name isn't too long: */
	if(name.length()>=1U<<16)
		throw std::runtime_error("KoinoniaClient::shareObject: Shared object name is too long");
	
	SharedObject* so=0;
	{
	/* Lock the shared object maps: */
	Threads::Mutex::Lock objectMapLock(objectMapMutex);
	
	/* Check if an object of the same name already exists: */
	if(sharedObjectNames.isEntry(name))
		Misc::throwStdErr("KoinoniaClient::shareObject: Shared object of name %s already exists",name.c_str());
	
	/* Create a new shared object with a new unique client-side ID: */
	so=new SharedObject(getObjectId(),name,dataType,type,object);
	
	/* Set the shared object's update callback: */
	so->sharedObjectUpdatedCallback=newCallback;
	so->sharedObjectUpdatedCallbackData=newCallbackData;
	
	/* Add the new shared object to the object maps: */
	clientSharedObjects.setEntry(SharedObjectMap::Entry(so->clientId,so));
	sharedObjectNames.setEntry(NameSet::Entry(name));
	}
	
	/* Create a CreateObjectRequest message: */
	{
	bool explicitSize=!dataType.hasFixedSize(type);
	Misc::UInt32 objectSize=explicitSize?dataType.calcSize(type,object):dataType.getMinSize(type);
	
	MessageWriter createObjectRequest(CreateObjectRequestMsg::createMessage(clientMessageBase,name,dataType,explicitSize,objectSize));
	createObjectRequest.write(so->clientId);
	createObjectRequest.write(type);
	createObjectRequest.write(Misc::UInt16(name.length()));
	stringToCharBuffer(name,createObjectRequest,name.length());
	dataType.write(createObjectRequest);
	if(explicitSize)
		Misc::writeVarInt32(objectSize,createObjectRequest);
	dataType.write(type,object,createObjectRequest);
	
	/* Check if the protocol is already running: */
	{
	Threads::Mutex::Lock startupLock(startupMutex);
	if(started)
		{
		/* Send the CreateObjectRequest message to the server immediately: */
		client->queueServerMessage(createObjectRequest.getBuffer());
		}
	else
		{
		/* Queue the CreateObjectRequest message to be sent once the protocol starts: */
		startupMessages.push_back(createObjectRequest.getBuffer()->ref());
		}
	}
	}
	
	return so->clientId;
	}

void KoinoniaClient::replaceSharedObject(KoinoniaProtocol::ObjectID objectId)
	{
	/* Access the shared object: */
	SharedObject* so=getClientSharedObject(objectId);
	
	/* Check if the object's server-side ID is already known: */
	if(so->serverId!=ObjectID(0))
		{
		/* Send a ReplaceObjectRequest message to the server: */
		{
		bool explicitSize=!so->dataType.hasFixedSize(so->type);
		Misc::UInt32 objectSize=explicitSize?so->dataType.calcSize(so->type,so->object):so->dataType.getMinSize(so->type);
		MessageWriter replaceObjectRequest(ReplaceObjectRequestMsg::createMessage(clientMessageBase,explicitSize,objectSize));
		replaceObjectRequest.write(so->serverId);
		replaceObjectRequest.write(so->version);
		if(explicitSize)
			Misc::writeVarInt32(objectSize,replaceObjectRequest);
		so->dataType.write(so->type,so->object,replaceObjectRequest);
		client->queueServerMessage(replaceObjectRequest.getBuffer());
		}
		
		/* Update the shared object's version number: */
		++so->version;
		}
	else
		Misc::throwStdErr("KoinoniaClient::replaceSharedObject: Shared object %u (%s)'s server-side ID is not yet known",(unsigned int)(so->clientId),so->name.c_str());
	}

KoinoniaProtocol::NamespaceID
KoinoniaClient::shareNamespace(
	const std::string& name,const DataType& dataType,
	KoinoniaClient::CreateNsObjectFunction createNsObjectFunction,void* createNsObjectFunctionData,
	KoinoniaClient::NsObjectCreatedCallback nsObjectCreatedCallback,void* nsObjectCreatedCallbackData,
	KoinoniaClient::NsObjectReplacedCallback nsObjectReplacedCallback,void* nsObjectReplacedCallbackData,
	KoinoniaClient::NsObjectDestroyedCallback nsObjectDestroyedCallback,void* nsObjectDestroyedCallbackData)
	{
	/* Ensure that the namespace's name isn't too long: */
	if(name.length()>=1U<<16)
		throw std::runtime_error("KoinoniaClient::shareNamespace: Namespace name is too long");
	
	Namespace* ns=0;
	{
	Threads::Mutex::Lock namespaceMapLock(namespaceMapMutex);
	
	/* Check if an object of the same name already exists: */
	if(namespaceNames.isEntry(name))
		Misc::throwStdErr("KoinoniaClient::shareNamespace: Namespace of name %s already exists",name.c_str());
	
	/* Create a new namespace with a new unique client-side ID and the given name and data type dictionary: */
	ns=new Namespace(getNamespaceId(),name,dataType,createNsObjectFunction,createNsObjectFunctionData);
	
	/* Set the new namespace's callbacks: */
	ns->nsObjectCreatedCallback=nsObjectCreatedCallback;
	ns->nsObjectCreatedCallbackData=nsObjectCreatedCallbackData;
	ns->nsObjectReplacedCallback=nsObjectReplacedCallback;
	ns->nsObjectReplacedCallbackData=nsObjectReplacedCallbackData;
	ns->nsObjectDestroyedCallback=nsObjectDestroyedCallback;
	ns->nsObjectDestroyedCallbackData=nsObjectDestroyedCallbackData;
	
	/* Add the new namespace to the client-side namespace maps: */
	clientNamespaces.setEntry(NamespaceMap::Entry(ns->clientId,ns));
	namespaceNames.setEntry(NameSet::Entry(name));
	}
	
	/* Create a CreateNamespaceRequest message: */
	{
	MessageWriter createNamespaceRequest(CreateNamespaceRequestMsg::createMessage(clientMessageBase,name,dataType));
	createNamespaceRequest.write(ns->clientId);
	createNamespaceRequest.write(Misc::UInt16(name.length()));
	stringToCharBuffer(name,createNamespaceRequest,name.length());
	dataType.write(createNamespaceRequest);
	
	/* Check if the protocol is already running: */
	{
	Threads::Mutex::Lock startupLock(startupMutex);
	if(started)
		{
		/* Send the CreateNamespaceRequest message to the server immediately: */
		client->queueServerMessage(createNamespaceRequest.getBuffer());
		}
	else
		{
		/* Queue the CreateNamespaceRequest message to be sent once the protocol starts: */
		startupMessages.push_back(createNamespaceRequest.getBuffer()->ref());
		}
	}
	}
	
	return ns->clientId;
	}

KoinoniaProtocol::ObjectID KoinoniaClient::createNsObject(KoinoniaProtocol::NamespaceID namespaceId,DataType::TypeID type,void* object)
	{
	/* Access the namespace in which to create the new object: */
	Namespace* ns=getClientNamespace(namespaceId);
	
	/* Create a new namespace object with an unused client-side ID and add it to the namespace's maps: */
	Namespace::SharedObject* so=0;
	{
	Threads::Mutex::Lock objectMapLock(ns->objectMapMutex);
	so=new Namespace::SharedObject(ns->getObjectId(),ObjectID(0),type);
	so->object=object;
	ns->clientSharedObjects.setEntry(Namespace::SharedObjectMap::Entry(so->clientId,so));
	}
	
	/* Create a CreateNsObjectRequest message to send to the server: */
	{
	bool explicitSize=!ns->dataType.hasFixedSize(so->type);
	Misc::UInt32 objectSize=explicitSize?ns->dataType.calcSize(so->type,so->object):ns->dataType.getMinSize(so->type);
	MessageWriter createNsObjectRequest(CreateNsObjectMsg::createMessage(clientMessageBase+CreateNsObjectRequest,explicitSize,objectSize));
	createNsObjectRequest.write(NamespaceID(0));
	createNsObjectRequest.write(so->clientId);
	createNsObjectRequest.write(so->type);
	if(explicitSize)
		Misc::writeVarInt32(objectSize,createNsObjectRequest);
	ns->dataType.write(so->type,so->object,createNsObjectRequest);
	
	/* Check if the namespace's server-side ID is already known: */
	{
	Threads::Mutex::Lock startupLock(ns->startupMutex);
	if(ns->serverId!=ObjectID(0))
		{
		/* Fix the server-side namespace ID and send the CreateNsObjectRequest message to the server: */
		createNsObjectRequest.rewind();
		createNsObjectRequest.write(ns->serverId);
		client->queueServerMessage(createNsObjectRequest.getBuffer());
		}
	else
		{
		/* Queue the CreateNsObjectRequest message to be sent once the namespace receives its server-side ID: */
		ns->startupMessages.push_back(createNsObjectRequest.getBuffer()->ref());
		}
	}
	}
	
	return so->clientId;
	}

void KoinoniaClient::replaceNsObject(KoinoniaProtocol::NamespaceID namespaceId,KoinoniaProtocol::ObjectID objectId)
	{
	/* Access the namespace containing the replaced object and the replaced object: */
	Namespace* ns=getClientNamespace(namespaceId);
	Namespace::SharedObject* so=ns->getClientSharedObject(objectId);
	
	/* Check if the shared object's server-side ID is already known: */
	if(so->serverId!=ObjectID(0))
		{
		/* Send a ReplaceNsObjectRequest message to the server: */
		{
		bool explicitSize=!ns->dataType.hasFixedSize(so->type);
		Misc::UInt32 objectSize=explicitSize?ns->dataType.calcSize(so->type,so->object):ns->dataType.getMinSize(so->type);
		MessageWriter replaceNsObjectRequest(ReplaceNsObjectMsg::createMessage(clientMessageBase+ReplaceNsObjectRequest,explicitSize,objectSize));
		replaceNsObjectRequest.write(ns->serverId);
		replaceNsObjectRequest.write(so->clientId);
		replaceNsObjectRequest.write(so->version);
		if(explicitSize)
			Misc::writeVarInt32(objectSize,replaceNsObjectRequest);
		ns->dataType.write(so->type,so->object,replaceNsObjectRequest);
		client->queueServerMessage(replaceNsObjectRequest.getBuffer());
		}
		
		/* Update the shared object's version number: */
		++so->version;
		}
	else
		Misc::throwStdErr("KoinoniaClient::replaceNsObject: Shared object %u in namespace %u (%s)'s server-side ID is not yet known",(unsigned int)(so->clientId),(unsigned int)(ns->clientId),ns->name.c_str());
	}

void KoinoniaClient::destroyNsObject(KoinoniaProtocol::NamespaceID namespaceId,KoinoniaProtocol::ObjectID objectId)
	{
	/* Access the namespace containing the destroyed object: */
	Namespace* ns=getClientNamespace(namespaceId);
	
	/* Access the destroyed object: */
	{
	Threads::Mutex::Lock objectMapLock(ns->objectMapMutex);
	Namespace::SharedObjectMap::Iterator soIt=ns->clientSharedObjects.findEntry(objectId);
	Namespace::SharedObject* so=soIt->getDest();
	
	/* Check if the destroyed object's server-side ID is already known: */
	if(so->serverId!=ObjectID(0))
		{
		/* Send a DestroyNsObjectRequest message to the server: */
		{
		MessageWriter destroyNsObjectRequest(DestroyNsObjectMsg::createMessage(clientMessageBase+DestroyNsObjectRequest));
		destroyNsObjectRequest.write(ns->serverId);
		destroyNsObjectRequest.write(so->serverId);
		client->queueServerMessage(destroyNsObjectRequest.getBuffer());
		}
		
		/* Remove the shared object from the namespace's maps and delete it: */
		ns->clientSharedObjects.removeEntry(soIt);
		ns->serverSharedObjects.removeEntry(so->serverId);
		delete so;
		}
	else
		Misc::throwStdErr("KoinoniaClient::destroyNsObject: Shared object %u in namespace %u (%s)'s server-side ID is not yet known",(unsigned int)(so->clientId),(unsigned int)(ns->clientId),ns->name.c_str());
	}
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginClient* createObject(PluginClientLoader& objectLoader,Client* client)
	{
	return new KoinoniaClient(client);
	}

void destroyObject(PluginClient* object)
	{
	delete object;
	}

}
