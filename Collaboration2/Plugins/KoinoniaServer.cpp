/***********************************************************************
KoinoniaServer - Server for data sharing plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/KoinoniaServer.h>

#include <ctype.h>
#include <string.h>
#include <stdexcept>
#include <iostream>
#include <Misc/Utility.h>
#include <Misc/ThrowStdErr.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/StandardMarshallers.h>
#include <IO/File.h>
#include <IO/OpenFile.h>

#include <Collaboration2/DataType.h>
#include <Collaboration2/DataType.icpp>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageEditor.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/Server.h>

/*******************************
Methods of class KoinoniaServer:
*******************************/

void KoinoniaServer::listObjectsCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	std::cout<<"Koinonia::listObjects:"<<std::endl;
	for(SharedObjectMap::Iterator soIt=sharedObjects.begin();!soIt.isFinished();++soIt)
		{
		SharedObject& so=*soIt->getDest();
		std::cout<<"\tID "<<(unsigned int)(so.id)<<": "<<so.name<<", ";
		std::cout<<so.object->getBufferSize()<<" bytes, ";
		if(!so.clients.empty())
			{
			ClientIDList::iterator cIt=so.clients.begin();
			std::cout<<"shared by clients ("<<*cIt;
			for(++cIt;cIt!=so.clients.end();++cIt)
				std::cout<<", "<<*cIt;
			std::cout<<')';
			}
		else
			std::cout<<"unshared";
		std::cout<<std::endl;
		}
	}

void KoinoniaServer::printObjectCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the object to print: */
	ObjectID objectId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd));
	
	/* Access the object: */
	SharedObject& so=*sharedObjects.getEntry(objectId).getDest();
	
	/* Print the object: */
	std::cout<<"Koinonia::printObject: ID "<<(unsigned int)(so.id)<<' '<<so.name<<':'<<std::endl;
	std::cout<<'\t';
	
	/* Attach a message reader to the object's serialization to print it: */
	MessageReader reader(so.object->ref());
	
	/* Skip the message's header: */
	reader.advanceReadPtr(sizeof(MessageID)+sizeof(ObjectID)+sizeof(VersionNumber));
	if(!so.dataType.hasFixedSize(so.type))
		Misc::readVarInt32(reader);
	
	/* Print the object: */
	so.dataType.printSerialization(std::cout,so.type,reader);
	std::cout<<std::endl;
	}

void KoinoniaServer::saveObjectCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the object to save and the name of the file to which to save it: */
	ObjectID objectId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd,&argumentsBegin));
	const char* fnBegin=Misc::skipWhitespace(argumentsBegin,argumentsEnd);
	const char* fnEnd;
	for(fnEnd=argumentsEnd;fnEnd!=fnBegin&&isspace(fnEnd[-1]);--fnEnd)
		;
	std::string fileName(fnBegin,fnEnd);
	
	/* Access the object: */
	SharedObject& so=*sharedObjects.getEntry(objectId).getDest();
	
	/* Open the output file: */
	IO::FilePtr file=IO::openFile(fileName.c_str(),IO::File::WriteOnly);
	file->setEndianness(Misc::LittleEndian);
	
	/* Write the file header: */
	char header[32];
	memset(header,0,sizeof(header));
	strcpy(header,"Koinonia Object v1.0");
	file->writeRaw(header,sizeof(header));
	
	/* Write the object's name, data type dictionary, and data type: */
	Misc::write(so.name,*file);
	so.dataType.write(*file);
	file->write(so.type);
	
	/* Attach a message reader to the object's serialization to write it: */
	{
	MessageReader reader(so.object->ref());
	
	/* Skip the message's header: */
	reader.advanceReadPtr(sizeof(MessageID)+sizeof(ObjectID)+sizeof(VersionNumber));
	
	/* Determine the size of the object's serialization: */
	Misc::UInt32 objectSize(so.dataType.getMinSize(so.type));
	if(!so.dataType.hasFixedSize(so.type))
		{
		objectSize=Misc::readVarInt32(reader);
		
		/* Write the object's size: */
		Misc::writeVarInt32(objectSize,*file);
		}
	
	/* Write the object's serialization: */
	file->writeRaw(reader.getReadPtr(),objectSize);
	}
	}

void KoinoniaServer::loadObjectCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the name of the file from which to load an object: */
	const char* fnBegin=argumentsBegin;
	const char* fnEnd;
	for(fnEnd=argumentsEnd;fnEnd!=fnBegin&&isspace(fnEnd[-1]);--fnEnd)
		;
	std::string fileName(fnBegin,fnEnd);
	
	/* Open the input file: */
	IO::FilePtr file=IO::openFile(fileName.c_str());
	file->setEndianness(Misc::LittleEndian);
	
	/* Read the file header: */
	char header[32];
	file->readRaw(header,sizeof(header));
	if(strcmp(header,"Koinonia Object v1.0")!=0)
		Misc::throwStdErr("KoinoniaServer::loadObject: File %s is not a Koinonia v1.0 object file",fileName.c_str());
	
	/* Read the object's name, data type dictionary, and data type: */
	std::string name;
	Misc::read(*file,name);
	DataType dataType(*file);
	DataType::TypeID type=file->read<DataType::TypeID>();
	
	/* Check if an object of the given name already exists: */
	SharedObjectNameMap::Iterator sonIt=sharedObjectNames.findEntry(name);
	if(sonIt.isFinished())
		{
		/* Get an unused object ID: */
		do
			{
			++lastObjectId;
			}
		while(lastObjectId==0||sharedObjects.isEntry(lastObjectId));
		
		/* Determine the size of the object's serialization: */
		Misc::UInt32 objectSize(dataType.getMinSize(type));
		bool explicitSize=!dataType.hasFixedSize(type);
		if(explicitSize)
			objectSize=Misc::readVarInt32(*file);
		
		/* Read the object's serialization into a new ReplaceObjectNotification message: */
		{
		MessageWriter objectWriter(ReplaceObjectNotificationMsg::createMessage(serverMessageBase,explicitSize,objectSize));
		objectWriter.write(lastObjectId);
		objectWriter.write(VersionNumber(0));
		if(explicitSize)
			Misc::writeVarInt32(objectSize,objectWriter);
		file->readRaw(objectWriter.getWritePtr(),objectSize);
		
		/* Create a new shared object: */
		SharedObject* so=new SharedObject;
		so->id=lastObjectId;
		so->name=name;
		so->dataType=dataType;
		so->type=type;
		so->version=VersionNumber(0);
		so->object=objectWriter.getBuffer()->ref();
		
		/* Add the new shared object to the maps: */
		sharedObjects.setEntry(SharedObjectMap::Entry(lastObjectId,so));
		sharedObjectNames.setEntry(SharedObjectNameMap::Entry(name,so));
		}
		}
	else
		{
		/* Access the existing shared object: */
		SharedObject& so=*sonIt->getDest();
		
		/* Check if the loaded object's type matches the existing shared object: */
		if(!(so.dataType==dataType)||so.type!=type)
			Misc::throwStdErr("KoinoniaServer::loadObject: Object %s from file %s does not match existing object",name.c_str(),fileName.c_str());
		
		/* Determine the size of the object's serialization: */
		Misc::UInt32 objectSize(so.dataType.getMinSize(so.type));
		bool explicitSize=!so.dataType.hasFixedSize(so.type);
		if(explicitSize)
			objectSize=Misc::readVarInt32(*file);
		
		/* Read the object's serialization into a new ReplaceObjectNotification message: */
		{
		MessageWriter objectWriter(ReplaceObjectNotificationMsg::createMessage(serverMessageBase,explicitSize,objectSize));
		objectWriter.write(so.id);
		VersionNumber newVersion(so.version+1);
		objectWriter.write(newVersion);
		if(explicitSize)
			Misc::writeVarInt32(objectSize,objectWriter);
		file->readRaw(objectWriter.getWritePtr(),objectSize);
		
		/* Replace the existing object: */
		if(so.object!=0)
			so.object->unref();
		so.version=newVersion;
		so.object=objectWriter.getBuffer()->ref();
		}
		
		/* Send the new value of the shared object to all clients sharing it: */
		for(ClientIDList::iterator cIt=so.clients.begin();cIt!=so.clients.end();++cIt)
			server->queueMessage(*cIt,so.object);
		}
	}

void KoinoniaServer::deleteObjectCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the object to delete: */
	ObjectID objectId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd));
	
	/* Access the object: */
	SharedObject* so=sharedObjects.getEntry(objectId).getDest();
	
	/* Ensure that the object is not currently shared by any clients: */
	if(!so->clients.empty())
		Misc::throwStdErr("Object %u (%s) still being shared by %u client(s)",(unsigned int)(objectId),so->name.c_str(),(unsigned int)(so->clients.size()));
	
	/* Delete the object: */
	sharedObjectNames.removeEntry(so->name);
	sharedObjects.removeEntry(objectId);
	delete so;
	}

MessageContinuation* KoinoniaServer::createObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Embedded classes: */
		public:
		enum State
			{
			ReadName,
			ReadDataType,
			ReadObject
			};
		
		/* Elements: */
		public:
		State state;
		ObjectID clientObjectId;
		SharedObject* so;
		MessageContinuation* subCont; // Message continuation object to read the data type dictionary or the shared object's initial value
		size_t remaining; // Number of bytes or elements left to read in current state
		
		/* Constructors and destructors: */
		Cont(NonBlockSocket& socket)
			:so(new SharedObject),
			 subCont(0)
			{
			/* Read the client-side object ID: */
			clientObjectId=socket.read<ObjectID>();
			
			/* Read the shared object's type: */
			so->type=socket.read<DataType::TypeID>();
			
			/* Read the shared object's name's length: */
			remaining=socket.read<Misc::UInt16>();
			
			/* Start reading the shared object's name: */
			so->name.reserve(remaining);
			state=ReadName;
			}
		virtual ~Cont(void)
			{
			/* Delete the shared object if it hasn't been extracted: */
			delete so;
			
			/* Delete a potential sub-continuation object: */
			delete subCont;
			}
		};
	
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Create a continuation object: */
		cont=new Cont(socket);
		}
	
	/* Check if the object name is not completely read: */
	if(cont->state==Cont::ReadName)
		{
		/* Read all available characters up to the object's name's length: */
		size_t numChars=Misc::min(socket.getUnread(),cont->remaining);
		
		cont->remaining-=numChars;
		for(;numChars>0;--numChars)
			cont->so->name.push_back(std::string::value_type(socket.read<Char>()));
		
		/* Bail out if more characters need to be read: */
		if(cont->remaining>0)
			return cont;
		
		/* Check if the new object's name is valid: */
		if(cont->so->name.empty())
			throw std::runtime_error("Koinonia::createObjectRequest: Attempt to create shared object with invalid name");
		
		/* Start reading the shared object's data type definition: */
		cont->state=Cont::ReadDataType;
		}
	
	/* Check if the object's data type dictionary is not completely read: */
	if(cont->state==Cont::ReadDataType)
		{
		/* Read into the new object's data type dictionary: */
		cont->subCont=cont->so->dataType.read(socket,cont->subCont);
		
		/* Bail out if the data type reader is waiting for more data: */
		if(cont->subCont!=0)
			return cont;
		
		/* Check if the new object's data type is valid: */
		if(!cont->so->dataType.isDefined(cont->so->type))
			throw std::runtime_error("Koinonia::createObjectRequest: Attempt to create shared object with invalid data type");
		
		/* Check if the object has a fixed size: */
		Misc::UInt32 objectSize=0;
		if(cont->so->dataType.hasFixedSize(cont->so->type))
			objectSize=Misc::UInt32(cont->so->dataType.getMinSize(cont->so->type));
		
		/* Start reading the shared object's initial value: */
		cont->subCont=new ReadObjectCont(sizeof(MessageID)+sizeof(ObjectID)+sizeof(VersionNumber),objectSize);
		cont->state=Cont::ReadObject;
		}
	
	/* Check if the object is not completely read: */
	if(cont->state==Cont::ReadObject)
		{
		/* Access the object reader: */
		ReadObjectCont* roCont=static_cast<ReadObjectCont*>(cont->subCont);
		
		/* Continue reading the object's initial value and bail out if it is not complete: */
		if(!roCont->read(socket))
			return cont;
		
		/* Check if a shared object with the requested name already exists: */
		SharedObjectNameMap::Iterator sonIt=sharedObjectNames.findEntry(cont->so->name);
		if(!sonIt.isFinished())
			{
			/* Access the existing shared object: */
			SharedObject* so=sonIt->getDest();
			
			/* Check if the requested initial object value is valid: */
			roCont->finishObject(cont->so->dataType,cont->so->type,socket.getSwapOnRead());
			
			/* Check if the object creation request matches the existing shared object: */
			bool grantRequest=cont->so->dataType==so->dataType&&cont->so->type==so->type;
			
			/* Send a CreateObjectReply message: */
			{
			MessageWriter createObjectReply(CreateObjectReplyMsg::createMessage(serverMessageBase));
			createObjectReply.write(cont->clientObjectId);
			createObjectReply.write(grantRequest?so->id:ObjectID(0));
			client->queueMessage(createObjectReply.getBuffer());
			}
			
			if(grantRequest)
				{
				/* Add the client to the existing object's share list: */
				so->clients.push_back(clientId);
				
				/* Send the existing object's current value to the requesting client: */
				client->queueMessage(so->object);
				}
			}
		else
			{
			/* Find an unused object ID for the new shared object: */
			SharedObject* so=cont->so;
			do
				{
				++lastObjectId;
				}
			while(lastObjectId==ObjectID(0)||sharedObjects.isEntry(lastObjectId));
			so->id=lastObjectId;
			
			/* Check and finalize the new shared object's initial value: */
			so->object=roCont->finishObject(so->dataType,so->type,socket.getSwapOnRead())->ref();
			
			/* Write the correct message header into the object's representation: */
			so->object->setMessageId(serverMessageBase+ReplaceObjectNotification);
			{
			MessageWriter writer(so->object->ref());
			writer.write(so->id);
			writer.write(so->version);
			}
			
			/* Add the new shared object to the shared object and shared object names maps: */
			sharedObjects.setEntry(SharedObjectMap::Entry(lastObjectId,so));
			sharedObjectNames.setEntry(SharedObjectNameMap::Entry(so->name,so));
			
			/* Send a CreateObjectReply message: */
			{
			MessageWriter createObjectReply(CreateObjectReplyMsg::createMessage(serverMessageBase));
			createObjectReply.write(cont->clientObjectId);
			createObjectReply.write(so->id);
			client->queueMessage(createObjectReply.getBuffer());
			}
			
			/* Add the client to the new object's share list: */
			so->clients.push_back(clientId);
			
			/* Remove the new shared object from the continuation so it doesn't get deleted: */
			cont->so=0;
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

MessageContinuation* KoinoniaServer::replaceObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public ReadObjectCont
		{
		/* Elements: */
		public:
		SharedObject* so;
		VersionNumber objectVersion;
		
		/* Constructors and destructors: */
		Cont(Misc::UInt32 sObjectSize,SharedObject* sSo,VersionNumber sObjectVersion)
			:ReadObjectCont(sizeof(MessageID)+sizeof(ObjectID)+sizeof(VersionNumber),sObjectSize),
			 so(sSo),
			 objectVersion(sObjectVersion)
			{
			}
		};
	
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the object ID and access the existing shared object: */
		SharedObject* so=sharedObjects.getEntry(socket.read<ObjectID>()).getDest();
		
		/* Read the client's version number: */
		VersionNumber objectVersion=socket.read<VersionNumber>();
		
		/* Check if the shared object has a fixed size: */
		Misc::UInt32 objectSize=0;
		if(so->dataType.hasFixedSize(so->type))
			objectSize=Misc::UInt32(so->dataType.getMinSize(so->type));
		
		/* Create a continuation object: */
		cont=new Cont(objectSize,so,objectVersion);
		}
	
	/* Read the object and check if it is complete: */
	if(cont->read(socket))
		{
		/* Check and finalize the updated object value: */
		SharedObject* so=cont->so;
		MessageBuffer* object=cont->finishObject(so->dataType,so->type,socket.getSwapOnRead());
		
		/* Check if the client's version number matches the current object's: */
		bool grantRequest=cont->objectVersion==so->version;
		
		/* Send a request object reply message: */
		{
		MessageWriter replaceObjectReply(ReplaceObjectReplyMsg::createMessage(serverMessageBase));
		replaceObjectReply.write(so->id);
		replaceObjectReply.write(cont->objectVersion);
		replaceObjectReply.write(grantRequest?Bool(1):Bool(0));
		client->queueMessage(replaceObjectReply.getBuffer());
		}
		
		if(grantRequest)
			{
			/* Replace the shared object's value: */
			if(so->object!=0)
				so->object->unref();
			++so->version;
			
			/* Write the correct message header into the new object's representation: */
			object->setMessageId(serverMessageBase+ReplaceObjectNotification);
			{
			MessageWriter writer(object->ref());
			writer.write(so->id);
			writer.write(so->version);
			}
			
			/* Store the new object representation: */
			so->object=object->ref();
			
			/* Send the new value of the shared object to all clients sharing it: */
			for(ClientIDList::iterator cIt=so->clients.begin();cIt!=so->clients.end();++cIt)
				if(*cIt!=clientId)
					server->queueMessage(*cIt,so->object);
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void KoinoniaServer::listNamespacesCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	std::cout<<"Koinonia::listNamespaces:"<<std::endl;
	for(NamespaceMap::Iterator nsIt=namespaces.begin();!nsIt.isFinished();++nsIt)
		{
		Namespace& ns=*nsIt->getDest();
		
		std::cout<<"\tID "<<(unsigned int)(ns.id)<<": "<<ns.name<<", ";
		if(!ns.clients.empty())
			{
			ClientIDList::iterator cIt=ns.clients.begin();
			std::cout<<"shared by clients ("<<*cIt;
			for(++cIt;cIt!=ns.clients.end();++cIt)
				std::cout<<", "<<*cIt;
			std::cout<<')';
			}
		else
			std::cout<<"unshared";
		std::cout<<std::endl;
		
		std::cout<<"\tContains "<<ns.sharedObjects.getNumEntries()<<" shared objects"<<std::endl;
		}
	}

void KoinoniaServer::listNamespaceObjectsCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the namespace whose objects to list: */
	NamespaceID namespaceId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd));
	
	/* Access the namespace: */
	Namespace* ns=namespaces.getEntry(namespaceId).getDest();
	
	/* List the namespace's objects: */
	std::cout<<"Koinonia::listNamespaceObjects for namespace "<<(unsigned int)(ns->id)<<", "<<ns->name<<':'<<std::endl;
	for(Namespace::SharedObjectMap::Iterator soIt=ns->sharedObjects.begin();!soIt.isFinished();++soIt)
		{
		Namespace::SharedObject& so=soIt->getDest();
		std::cout<<"\tID "<<(unsigned int)(so.id)<<", type "<<(unsigned int)(so.type)<<", size "<<so.object->getBufferSize()<<std::endl;
		}
	}

void KoinoniaServer::printNamespaceObjectCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the namespace and the ID of the object to print: */
	NamespaceID namespaceId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd,&argumentsBegin));
	argumentsBegin=Misc::skipWhitespace(argumentsBegin,argumentsEnd);
	ObjectID objectId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd,&argumentsBegin));
	
	/* Access the namespace and the namespace object: */
	Namespace* ns=namespaces.getEntry(namespaceId).getDest();
	Namespace::SharedObject& so=ns->sharedObjects.getEntry(objectId).getDest();
	
	/* Print the object: */
	std::cout<<"Koinonia::printNamespaceObject: Namespace "<<(unsigned int)(ns->id)<<" ("<<ns->name<<"), object "<<so.id<<", type "<<(unsigned int)(so.type)<<':'<<std::endl;
	std::cout<<'\t';
	
	/* Attach a message reader to the object's serialization to print it: */
	MessageReader reader(so.object->ref());
	
	/* Skip the message's header: */
	if(!ns->dataType.hasFixedSize(so.type))
		Misc::readVarInt32(reader);
	
	/* Print the object: */
	ns->dataType.printSerialization(std::cout,so.type,reader);
	std::cout<<std::endl;
	}

void KoinoniaServer::saveNamespaceCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the namespace to save and the name of the file to which to save it: */
	NamespaceID namespaceId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd,&argumentsBegin));
	const char* fnBegin=Misc::skipWhitespace(argumentsBegin,argumentsEnd);
	const char* fnEnd;
	for(fnEnd=argumentsEnd;fnEnd!=fnBegin&&isspace(fnEnd[-1]);--fnEnd)
		;
	std::string fileName(fnBegin,fnEnd);
	
	/* Access the namespace: */
	Namespace& ns=*namespaces.getEntry(namespaceId).getDest();
	
	/* Open the output file: */
	IO::FilePtr file=IO::openFile(fileName.c_str(),IO::File::WriteOnly);
	file->setEndianness(Misc::LittleEndian);
	
	/* Write the file header: */
	char header[32];
	memset(header,0,sizeof(header));
	strcpy(header,"Koinonia Namespace v1.0");
	file->writeRaw(header,sizeof(header));
	
	/* Write the namespace's name and data type dictionary: */
	Misc::write(ns.name,*file);
	ns.dataType.write(*file);
	
	/* Write all objects currently existing inside the namespace: */
	for(Namespace::SharedObjectMap::Iterator soIt=ns.sharedObjects.begin();!soIt.isFinished();++soIt)
		{
		/* Access the shared object: */
		Namespace::SharedObject& so=soIt->getDest();
		
		/* Write the object's type: */
		file->write(so.type);
		
		/* Attach a message reader to the object's serialization to write it: */
		{
		MessageReader reader(so.object->ref());
		
		/* Determine the size of the object's serialization: */
		Misc::UInt32 objectSize(ns.dataType.getMinSize(so.type));
		if(!ns.dataType.hasFixedSize(so.type))
			{
			objectSize=Misc::readVarInt32(reader);
			
			/* Write the object's size: */
			Misc::writeVarInt32(objectSize,*file);
			}
		
		/* Write the object's serialization: */
		file->writeRaw(reader.getReadPtr(),objectSize);
		}
		}
	}

void KoinoniaServer::loadNamespaceCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the name of the file from which to load a namespace: */
	const char* fnBegin=argumentsBegin;
	const char* fnEnd;
	for(fnEnd=argumentsEnd;fnEnd!=fnBegin&&isspace(fnEnd[-1]);--fnEnd)
		;
	std::string fileName(fnBegin,fnEnd);
	
	/* Open the input file: */
	IO::FilePtr file=IO::openFile(fileName.c_str());
	file->setEndianness(Misc::LittleEndian);
	
	/* Read the file header: */
	char header[32];
	file->readRaw(header,sizeof(header));
	if(strcmp(header,"Koinonia Namespace v1.0")!=0)
		Misc::throwStdErr("KoinoniaServer::loadNamespace: File %s is not a Koinonia v1.0 namespace file",fileName.c_str());
	
	/* Read the namespace's name and data type dictionary: */
	std::string name;
	Misc::read(*file,name);
	DataType dataType(*file);
	
	/* Check if a namespace of the given name already exists: */
	Namespace* ns=0;
	NamespaceNameMap::Iterator nnIt=namespaceNames.findEntry(name);
	if(nnIt.isFinished())
		{
		/* Get an unused namespace ID: */
		do
			{
			++lastNamespaceId;
			}
		while(lastNamespaceId==0||namespaces.isEntry(lastNamespaceId));
		
		/* Create a new namespace: */
		ns=new Namespace;
		ns->id=lastNamespaceId;
		ns->name=name;
		ns->dataType=dataType;
		
		/* Add the new namespace to the maps: */
		namespaces.setEntry(NamespaceMap::Entry(ns->id,ns));
		namespaceNames.setEntry(NamespaceNameMap::Entry(ns->name,ns));
		}
	else
		{
		/* Access the existing namespace: */
		ns=nnIt->getDest();
		
		/* Check if the loaded namespace's type matches the existing one's: */
		if(!(ns->dataType==dataType))
			Misc::throwStdErr("KoinoniaServer::loadNamespace: Namespace %s from file %s does not match existing namespace",name.c_str(),fileName.c_str());
		}
	
	/* Read all namespace objects contained in the file: */
	while(!file->eof())
		{
		/* Get an unused object ID for the next object: */
		do
			{
			++ns->lastObjectId;
			}
		while(ns->lastObjectId==0||ns->sharedObjects.isEntry(ns->lastObjectId));
		
		/* Read the next object's type: */
		DataType::TypeID type=file->read<DataType::TypeID>();
	
		/* Determine the size of the next object's serialization: */
		Misc::UInt32 objectSize(ns->dataType.getMinSize(type));
		bool explicitSize=!ns->dataType.hasFixedSize(type);
		if(explicitSize)
			objectSize=Misc::readVarInt32(*file);
		
		/* Read the object's serialization into a new header-less message: */
		{
		size_t bufferSize=objectSize;
		if(explicitSize)
			bufferSize+=Misc::getVarInt32Size(objectSize);
		MessageWriter objectWriter(MessageBuffer::create(bufferSize));
		if(explicitSize)
			Misc::writeVarInt32(objectSize,objectWriter);
		file->readRaw(objectWriter.getWritePtr(),objectSize);
		
		/* Create a new shared object: */
		ns->sharedObjects.setEntry(Namespace::SharedObjectMap::Entry(ns->lastObjectId,Namespace::SharedObject(ns->lastObjectId,type,objectWriter.getBuffer())));
		
		/* Send the new shared object to all clients sharing the namespace: */
		if(!ns->clients.empty())
			{
			/* Create a message header for a CreateNsObjectNotification message: */
			MessageWriter headerWriter(MessageBuffer::create(serverMessageBase+CreateNsObjectNotification,CreateNsObjectMsg::size));
			headerWriter.write(ns->id);
			headerWriter.write(ns->lastObjectId);
			headerWriter.write(type);
			
			/* Send the message header/body combination to all clients sharing the namespace: */
			for(ClientIDList::iterator cIt=ns->clients.begin();cIt!=ns->clients.end();++cIt)
				{
				/* Send the message header and body separately: */
				Server::Client* c=server->getClient(*cIt);
				c->queueMessage(headerWriter.getBuffer());
				c->queueMessage(objectWriter.getBuffer());
				}
			}
		}
		}
	}

void KoinoniaServer::deleteNamespaceCommand(const char* argumentsBegin,const char* argumentsEnd)
	{
	/* Retrieve the ID of the namespace to delete: */
	NamespaceID namespaceId(Misc::ValueCoder<unsigned int>::decode(argumentsBegin,argumentsEnd));
	
	/* Access the namespace: */
	Namespace* ns=namespaces.getEntry(namespaceId).getDest();
	
	/* Ensure that the namespace is not currently shared by any clients: */
	if(!ns->clients.empty())
		Misc::throwStdErr("Namespace %u (%s) still being shared by %u client(s)",(unsigned int)(namespaceId),ns->name.c_str(),(unsigned int)(ns->clients.size()));
	
	/* Delete the namespace: */
	namespaceNames.removeEntry(ns->name);
	namespaces.removeEntry(namespaceId);
	delete ns;
	}

MessageContinuation* KoinoniaServer::createNamespaceRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Embedded classes: */
		public:
		enum State
			{
			ReadName,
			ReadDataType
			};
		
		/* Elements: */
		public:
		State state;
		Namespace* ns; // Pointer to the new shared namespace
		MessageContinuation* subCont; // Message continuation object to read the data type dictionary
		size_t remaining; // Number of bytes left to read in current state
		
		/* Constructors and destructors: */
		Cont(NonBlockSocket& socket)
			:state(ReadName),
			 ns(new Namespace),
			 subCont(0)
			{
			/* Read the client-side namespace ID: */
			ns->id=socket.read<NamespaceID>();
			
			/* Read the shared namespace's name's length: */
			remaining=socket.read<Misc::UInt16>();
			ns->name.reserve(remaining);
			}
		virtual ~Cont(void)
			{
			/* Delete the namespace if it hasn't been extracted: */
			delete ns;
			
			/* Delete a potential sub-continuation object: */
			delete subCont;
			}
		};
	
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Create a continuation object: */
		cont=new Cont(socket);
		}
	
	/* Check if the namespace name is not completely read: */
	if(cont->state==Cont::ReadName)
		{
		/* Read all available characters up to the namespace's name's length: */
		size_t numChars=Misc::min(socket.getUnread(),cont->remaining);
		
		cont->remaining-=numChars;
		for(;numChars>0;--numChars)
			cont->ns->name.push_back(std::string::value_type(socket.read<Char>()));
		
		/* Bail out if more characters need to be read: */
		if(cont->remaining>0)
			return cont;
		
		/* Check if the new namespace's name is valid: */
		if(cont->ns->name.empty())
			throw std::runtime_error("Koinonia::createNamespaceRequest: Attempt to create shared namespace with invalid name");
		
		/* Start reading the shared namespace's data type definition: */
		cont->state=Cont::ReadDataType;
		}
	
	/* Check if the namespace's data type dictionary is not completely read: */
	if(cont->state==Cont::ReadDataType)
		{
		/* Read into the new namespace's data type dictionary: */
		cont->subCont=cont->ns->dataType.read(socket,cont->subCont);
		
		/* Bail out if the data type reader is waiting for more data: */
		if(cont->subCont!=0)
			return cont;
		
		/* Check if a shared namespace with the requested name already exists: */
		NamespaceNameMap::Iterator nsnIt=namespaceNames.findEntry(cont->ns->name);
		if(!nsnIt.isFinished())
			{
			/* Access the existing shared namespace: */
			Namespace* ns=nsnIt->getDest();
			
			/* Check if the namespace creation request matches the existing shared namespace: */
			bool grantRequest=cont->ns->dataType==ns->dataType;
			
			/* Send a CreateNamespaceReply message: */
			{
			MessageWriter createNamespaceReply(CreateNamespaceReplyMsg::createMessage(serverMessageBase));
			createNamespaceReply.write(cont->ns->id);
			createNamespaceReply.write(grantRequest?ns->id:NamespaceID(0));
			client->queueMessage(createNamespaceReply.getBuffer());
			}
			
			if(grantRequest)
				{
				/* Add the client to the existing namespace's share list: */
				ns->clients.push_back(clientId);
				
				/* Send the existing namespace's shared objects to the requesting client: */
				for(Namespace::SharedObjectMap::Iterator soIt=ns->sharedObjects.begin();!soIt.isFinished();++soIt)
					{
					Namespace::SharedObject& so=soIt->getDest();
					
					/* Create and send a message buffer containing the header of a CreateNsObjectNotification message: */
					{
					MessageWriter headerWriter(MessageBuffer::create(serverMessageBase+CreateNsObjectNotification,CreateNsObjectMsg::size));
					headerWriter.write(ns->id);
					headerWriter.write(so.id);
					headerWriter.write(so.type);
					client->queueMessage(headerWriter.getBuffer());
					}
					
					/* Send the shared object's representation as the CreateNsObjectNotification message's body: */
					client->queueMessage(so.object);
					}
				}
			}
		else
			{
			/* Find an unused namespace ID for the new shared namespace: */
			NamespaceID clientSideId=cont->ns->id;
			do
				{
				++lastNamespaceId;
				}
			while(lastNamespaceId==NamespaceID(0)||namespaces.isEntry(lastNamespaceId));
			cont->ns->id=lastNamespaceId;
			
			/* Add the new namespace to the shared namespace and shared namespace names maps: */
			namespaces.setEntry(NamespaceMap::Entry(lastNamespaceId,cont->ns));
			namespaceNames.setEntry(NamespaceNameMap::Entry(cont->ns->name,cont->ns));
			
			/* Send a CreateNamespaceReply message: */
			{
			MessageWriter createNamespaceReply(CreateNamespaceReplyMsg::createMessage(serverMessageBase));
			createNamespaceReply.write(clientSideId);
			createNamespaceReply.write(cont->ns->id);
			client->queueMessage(createNamespaceReply.getBuffer());
			}
			
			/* Add the client to the new namespace's share list: */
			cont->ns->clients.push_back(clientId);
			
			/* Remove the new namespace from the continuation so it doesn't get deleted: */
			cont->ns=0;
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

MessageContinuation* KoinoniaServer::createNsObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public ReadObjectCont
		{
		/* Elements: */
		public:
		Namespace* ns; // Pointer to namespace to which to add the object
		ObjectID objectId; // Client-side ID of the new object
		DataType::TypeID type; // Type of the new object
		
		/* Constructors and destructors: */
		Cont(Misc::UInt32 sObjectSize,Namespace* sNs,ObjectID sObjectId,DataType::TypeID sType)
			:ReadObjectCont(0,sObjectSize),
			 ns(sNs),objectId(sObjectId),type(sType)
			{
			}
		};
	
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the namespace ID and access the existing namespace: */
		Namespace* ns=namespaces.getEntry(socket.read<NamespaceID>()).getDest();
		
		/* Read the object ID and the object's type: */
		ObjectID objectId=socket.read<ObjectID>();
		DataType::TypeID type=socket.read<DataType::TypeID>();
		
		/* Check if the new object's data type is valid: */
		if(!ns->dataType.isDefined(type))
			throw std::runtime_error("Koinonia::createNsObjectRequest: Attempt to create shared object with invalid data type");
		
		/* Check if the shared object has a fixed size: */
		Misc::UInt32 objectSize=0;
		if(ns->dataType.hasFixedSize(type))
			objectSize=Misc::UInt32(ns->dataType.getMinSize(type));
		
		/* Create a continuation object: */
		cont=new Cont(objectSize,ns,objectId,type);
		}
	
	/* Read the object and check if it is complete: */
	if(cont->read(socket))
		{
		/* Find an unused object ID for the new shared object: */
		Namespace* ns=cont->ns;
		do
			{
			++ns->lastObjectId;
			}
		while(ns->lastObjectId==ObjectID(0)||ns->sharedObjects.isEntry(ns->lastObjectId));
		
		/* Check and finalize the new shared object's initial value: */
		MessageBuffer* object=cont->finishObject(ns->dataType,cont->type,socket.getSwapOnRead());
		
		/* Add a new shared object to the namespace's shared object map: */
		ns->sharedObjects.setEntry(Namespace::SharedObjectMap::Entry(ns->lastObjectId,Namespace::SharedObject(ns->lastObjectId,cont->type,object)));
		
		/* Send a CreateNsObjectReply message to the requesting client: */
		{
		MessageWriter createNsObjectReply(CreateNsObjectReplyMsg::createMessage(serverMessageBase));
		createNsObjectReply.write(ns->id);
		createNsObjectReply.write(cont->objectId);
		createNsObjectReply.write(ns->lastObjectId);
		client->queueMessage(createNsObjectReply.getBuffer());
		}
		
		/* Send CreateNsObjectNotification messages to all other clients sharing the namespace: */
		{
		MessageWriter headerWriter(MessageBuffer::create(serverMessageBase+CreateNsObjectNotification,CreateNsObjectMsg::size));
		headerWriter.write(ns->id);
		headerWriter.write(ns->lastObjectId);
		headerWriter.write(cont->type);
		
		for(ClientIDList::iterator cIt=ns->clients.begin();cIt!=ns->clients.end();++cIt)
			if(*cIt!=clientId)
				{
				/* Send the message header and body separately: */
				Server::Client* c=server->getClient(*cIt);
				c->queueMessage(headerWriter.getBuffer());
				c->queueMessage(object);
				}
		}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

MessageContinuation* KoinoniaServer::replaceNsObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public ReadObjectCont
		{
		/* Elements: */
		public:
		Namespace* ns; // Pointer to namespace in which to replace the object
		Namespace::SharedObject& so; // The object that is to be replaced
		VersionNumber clientVersion; // Client's version number of the shared object
		
		/* Constructors and destructors: */
		Cont(Misc::UInt32 sObjectSize,Namespace* sNs,Namespace::SharedObject& sSo,VersionNumber sClientVersion)
			:ReadObjectCont(0,sObjectSize),
			 ns(sNs),so(sSo),clientVersion(sClientVersion)
			{
			}
		};
	
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the namespace ID and access the existing namespace: */
		Namespace* ns=namespaces.getEntry(socket.read<NamespaceID>()).getDest();
		
		/* Read the object ID and access the existing shared object: */
		Namespace::SharedObject& so=ns->sharedObjects.getEntry(socket.read<ObjectID>()).getDest();
		
		/* Read the client's object version: */
		VersionNumber clientVersion=socket.read<VersionNumber>();
		
		/* Check if the shared object has a fixed size: */
		Misc::UInt32 objectSize=0;
		if(ns->dataType.hasFixedSize(so.type))
			objectSize=Misc::UInt32(ns->dataType.getMinSize(so.type));
		
		/* Create a continuation object: */
		cont=new Cont(objectSize,ns,so,clientVersion);
		}
	
	/* Read the object and check if it is complete: */
	if(cont->read(socket))
		{
		Namespace* ns=cont->ns;
		Namespace::SharedObject& so=cont->so;
		
		/* Check and finalize the shared object's new value: */
		MessageBuffer* object=cont->finishObject(ns->dataType,so.type,socket.getSwapOnRead());
		
		/* Check if the client's version number matches the current object's: */
		bool grantRequest=cont->clientVersion==so.version;
		
		/* Send a replace namespace object reply message: */
		{
		MessageWriter replaceNsObjectReply(ReplaceNsObjectReplyMsg::createMessage(serverMessageBase));
		replaceNsObjectReply.write(ns->id);
		replaceNsObjectReply.write(so.id);
		replaceNsObjectReply.write(cont->clientVersion);
		replaceNsObjectReply.write(grantRequest?Bool(1):Bool(0));
		client->queueMessage(replaceNsObjectReply.getBuffer());
		}
		
		if(grantRequest)
			{
			/* Replace the shared object's value: */
			if(so.object!=0)
				so.object->unref();
			++so.version;
			
			/* Store the new object representation: */
			so.object=object->ref();
			
			/* Send a ReplaceNsObjectNotification message to all clients sharing the namespace: */
			{
			MessageWriter headerWriter(MessageBuffer::create(serverMessageBase+ReplaceNsObjectNotification,ReplaceNsObjectMsg::size));
			headerWriter.write(ns->id);
			headerWriter.write(so.id);
			headerWriter.write(so.version);
			
			for(ClientIDList::iterator cIt=ns->clients.begin();cIt!=ns->clients.end();++cIt)
				if(*cIt!=clientId)
					{
					/* Send the message header and body separately: */
					Server::Client* c=server->getClient(*cIt);
					c->queueMessage(headerWriter.getBuffer());
					c->queueMessage(so.object);
					}
			}
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

MessageContinuation* KoinoniaServer::destroyNsObjectRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the namespace ID and access the namespace: */
	Namespace* ns=namespaces.getEntry(socket.read<NamespaceID>()).getDest();
	
	/* Read the object ID: */
	ObjectID objectId=socket.read<ObjectID>();
	
	/* Remove the shared object from the namespace's shared object map: */
	ns->sharedObjects.removeEntry(objectId);
	
	/* Send a destroy namespace object notification message to all clients sharing the namespace: */
	{
	MessageWriter destroyNsObjectNotification(DestroyNsObjectMsg::createMessage(serverMessageBase+DestroyNsObjectNotification));
	destroyNsObjectNotification.write(ns->id);
	destroyNsObjectNotification.write(objectId);
	
	for(ClientIDList::iterator cIt=ns->clients.begin();cIt!=ns->clients.end();++cIt)
		if(*cIt!=clientId)
			server->queueMessage(*cIt,destroyNsObjectNotification.getBuffer());
	}
	
	/* Done with the message: */
	return 0;
	}

KoinoniaServer::KoinoniaServer(Server* server) 
	:PluginServer(server),
	 lastObjectId(0),
	 sharedObjects(17),sharedObjectNames(17),
	 lastNamespaceId(0),
	 namespaces(17),namespaceNames(17)
	{
	}

KoinoniaServer::~KoinoniaServer(void)
	{
	/* Delete all shared objects: */
	for(SharedObjectMap::Iterator soIt=sharedObjects.begin();!soIt.isFinished();++soIt)
		delete soIt->getDest();
	
	/* Delete all namespaces: */
	for(NamespaceMap::Iterator nsIt=namespaces.begin();!nsIt.isFinished();++nsIt)
		delete nsIt->getDest();
	
	/* Unregister console command handlers: */
	Misc::CommandDispatcher& cd=server->getCommandDispatcher();
	cd.removeCommandCallback("Koinonia::listObjects");
	cd.removeCommandCallback("Koinonia::printObject");
	cd.removeCommandCallback("Koinonia::saveObject");
	cd.removeCommandCallback("Koinonia::loadObject");
	cd.removeCommandCallback("Koinonia::deleteObject");
	
	cd.removeCommandCallback("Koinonia::listNamespaces");
	cd.removeCommandCallback("Koinonia::listNamespaceObjects");
	cd.removeCommandCallback("Koinonia::printNamespaceObject");
	cd.removeCommandCallback("Koinonia::saveNamespace");
	cd.removeCommandCallback("Koinonia::loadNamespace");
	cd.removeCommandCallback("Koinonia::deleteNamespace");
	}

const char* KoinoniaServer::getName(void) const
	{
	return protocolName;
	}

unsigned int KoinoniaServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int KoinoniaServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int KoinoniaServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void KoinoniaServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+CreateObjectRequest,Server::wrapMethod<KoinoniaServer,&KoinoniaServer::createObjectRequestCallback>,this,CreateObjectRequestMsg::size);
	server->setMessageHandler(clientMessageBase+ReplaceObjectRequest,Server::wrapMethod<KoinoniaServer,&KoinoniaServer::replaceObjectRequestCallback>,this,ReplaceObjectRequestMsg::size);
	
	server->setMessageHandler(clientMessageBase+CreateNamespaceRequest,Server::wrapMethod<KoinoniaServer,&KoinoniaServer::createNamespaceRequestCallback>,this,CreateNamespaceRequestMsg::size);
	server->setMessageHandler(clientMessageBase+CreateNsObjectRequest,Server::wrapMethod<KoinoniaServer,&KoinoniaServer::createNsObjectRequestCallback>,this,CreateNsObjectMsg::size);
	server->setMessageHandler(clientMessageBase+ReplaceNsObjectRequest,Server::wrapMethod<KoinoniaServer,&KoinoniaServer::replaceNsObjectRequestCallback>,this,ReplaceNsObjectMsg::size);
	server->setMessageHandler(clientMessageBase+DestroyNsObjectRequest,Server::wrapMethod<KoinoniaServer,&KoinoniaServer::destroyNsObjectRequestCallback>,this,DestroyNsObjectMsg::size);
	
	/* Register console command handlers: */
	Misc::CommandDispatcher& cd=server->getCommandDispatcher();
	cd.addCommandCallback("Koinonia::listObjects",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::listObjectsCommand>,this,0,"Lists all currently defined shared objects");
	cd.addCommandCallback("Koinonia::printObject",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::printObjectCommand>,this,"<object ID>","Prints the shared object of the given ID");
	cd.addCommandCallback("Koinonia::saveObject",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::saveObjectCommand>,this,"<object ID> <file name>","Saves the shared object of the given ID to a binary file of the given name");
	cd.addCommandCallback("Koinonia::loadObject",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::loadObjectCommand>,this,"<file name>","Loads the shared object contained in the binary file of the given name");
	cd.addCommandCallback("Koinonia::deleteObject",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::deleteObjectCommand>,this,"<object ID>","Deletes the shared object of the given ID");
	
	cd.addCommandCallback("Koinonia::listNamespaces",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::listNamespacesCommand>,this,0,"Lists all currently defined shared namespaces");
	cd.addCommandCallback("Koinonia::listNamespaceObjects",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::listNamespaceObjectsCommand>,this,"<namespace ID>","Lists all currently defined shared objects in the shared namespace of the given ID");
	cd.addCommandCallback("Koinonia::printNamespaceObject",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::printNamespaceObjectCommand>,this,"<namespace ID> <object ID>","Prints the object of the given ID inside the namespace of the given ID");
	cd.addCommandCallback("Koinonia::saveNamespace",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::saveNamespaceCommand>,this,"<namespace ID> <file name>","Saves the namespace of the given ID, and all objects within it, to a binary file of the given name");
	cd.addCommandCallback("Koinonia::loadNamespace",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::loadNamespaceCommand>,this,"<file name>","Loads the namespace and all objects contained in the binary file of the given name");
	cd.addCommandCallback("Koinonia::deleteNamespace",Misc::CommandDispatcher::wrapMethod<KoinoniaServer,&KoinoniaServer::deleteNamespaceCommand>,this,"<namespace ID>","Deletes the shared namespace of the given ID");
	}

void KoinoniaServer::clientDisconnected(unsigned int clientId)
	{
	/* Call the base class method: */
	PluginServer::clientDisconnected(clientId);
	
	/* Remove the disconnected client from all shared objects: */
	for(SharedObjectMap::Iterator soIt=sharedObjects.begin();!soIt.isFinished();++soIt)
		removeClientFromList(soIt->getDest()->clients,clientId);
	
	/* Remove the disconnected client from all shared namespaces: */
	for(NamespaceMap::Iterator nsIt=namespaces.begin();!nsIt.isFinished();++nsIt)
		removeClientFromList(nsIt->getDest()->clients,clientId);
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new KoinoniaServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}
