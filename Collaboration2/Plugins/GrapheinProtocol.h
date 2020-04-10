/***********************************************************************
GrapheinProtocol - Definition of the communication protocol between a
collaborative sketching client and a server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_GRAPHEINPROTOCOL_INCLUDED
#define PLUGINS_GRAPHEINPROTOCOL_INCLUDED

#include <string>
#include <Misc/SizedTypes.h>
#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>
#include <Collaboration2/DataType.h>

#define GRAPHEIN_PROTOCOLNAME "Graphein"
#define GRAPHEIN_PROTOCOLVERSION 1U<<16

class GrapheinProtocol
	{
	/* Embedded classes: */
	private:
	typedef Misc::UInt16 ObjectID; // Type for shared sketch object IDs
	typedef Misc::UInt8 TypeID; // Type for shared sketch object type IDs
	
	/* Protocol message IDs: */
	enum ClientMessages // Enumerated type for Graphein protocol message IDs sent by clients
		{
		CreateObjectRequest=0,
		ContinueObjectRequest,
		FinishObjectRequest,
		DestroyObjectRequest,
		
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for Graphein protocol message IDs sent by servers
		{
		CreateObjectReply=0,
		CreateObjectNotification,
		ContinueObjectNotification,
		FinishObjectNotification,
		DestroyObjectNotification,
		
		NumServerMessages
		};
	
	/* Protocol message data structure declarations: */
	struct CreateObjectRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ObjectID)+sizeof(TypeID);
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase) // Returns a message buffer for a create object request message
			{
			return MessageBuffer::create(clientMessageBase+CreateObjectRequest,size);
			}
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion;
	};

#endif
