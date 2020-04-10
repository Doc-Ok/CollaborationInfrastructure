/***********************************************************************
ChatProtocol - Definition of the communication protocol between a text
chat client and a server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_CHATPROTOCOL_INCLUDED
#define PLUGINS_CHATPROTOCOL_INCLUDED

#include <Misc/SizedTypes.h>
#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>

#define CHAT_PROTOCOLNAME "Chat"
#define CHAT_PROTOCOLVERSION 1U<<16

class ChatProtocol
	{
	/* Embedded classes: */
	protected:
	
	/* Protocol message IDs: */
	enum ClientMessages // Enumerated type for chat protocol message IDs sent by clients
		{
		MessageRequest=0,
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for chat protocol message IDs sent by servers
		{
		MessageReply=0,
		NumServerMessages
		};
	
	/* Protocol message data structure declarations: */
	struct MessageRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Misc::UInt16); // Size up to and including the message length
		ClientID destination; // ID of destination client, or 0 for broadcast message
		Misc::UInt16 messageLen; // Length of chat message in characters
		// Char message[messageLen]; // Chat message
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageBase,size_t messageLen) // Returns a message buffer for a message request message with the given chat message length
			{
			return MessageBuffer::create(messageBase+MessageRequest,size+messageLen*sizeof(Char));
			}
		};
	
	struct MessageReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Misc::UInt8)+sizeof(Misc::UInt16); // Size up to and including the message length
		ClientID source; // ID of source client
		Misc::UInt8 privateMessage; // Flag if the message was a private message
		Misc::UInt16 messageLen; // Length of chat message in characters
		// Char message[messageLen]; // Chat message
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageBase,size_t messageLen) // Returns a message buffer for a message reply message with the given chat message length
			{
			return MessageBuffer::create(messageBase+MessageRequest,size+messageLen*sizeof(Char));
			}
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion;
	};

#endif
