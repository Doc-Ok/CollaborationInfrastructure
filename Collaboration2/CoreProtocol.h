/***********************************************************************
CoreProtocol - Definition of the core communication protocol between a
client and a server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef COREPROTOCOL_INCLUDED
#define COREPROTOCOL_INCLUDED

#include <Misc/SizedTypes.h>

#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>

class CoreProtocol // Class defining core protocol types and messages
	{
	/* Embedded classes: */
	
	/* Protocol message IDs: */
	protected:
	enum ClientMessages // Enumerated type for core protocol message IDs sent by clients
		{
		DisconnectRequest=0,
		UDPConnectRequest, // This ID must not be 0 because it's used for endianness detection
		PingRequest,
		NameChangeRequest,
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for core protocol message IDs sent by servers
		{
		ConnectReply=0,
		ConnectReject,
		UDPConnectReply,
		PingReply,
		NameChangeReply,
		ClientConnectNotification,
		NameChangeNotification,
		ClientDisconnectNotification,
		NumServerMessages
		};
	
	/* Protocol message data structure declarations: */
	struct PasswordRequestMsg // Password request message from the server
		{
		/* Elements: */
		static const size_t nonceLength=16;
		static const size_t size=2*sizeof(Misc::UInt32)+nonceLength;
		Misc::UInt32 endiannessMarker;
		Misc::UInt32 protocolVersion; // Server core protocol version
		Byte nonce[nonceLength]; // Random nonce
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a password request message
			{
			return MessageBuffer::create(size);
			}
		};
	
	struct ConnectRequestMsg // Connect request from a client
		{
		/* Embedded classes: */
		public:
		struct ProtocolRequest // Request for a plug-in protocol
			{
			/* Elements: */
			public:
			static const size_t nameLength=32;
			static const size_t size=nameLength*sizeof(Char)+sizeof(Misc::UInt32);
			Char protocolName[nameLength];
			Misc::UInt32 protocolVersion;
			};
		
		/* Elements: */
		static const size_t hashLength=16; // MD5 hashes are 128 bits or 16 bytes
		static const size_t nameLength=32;
		static const size_t size=2*sizeof(Misc::UInt32)+hashLength+nameLength*sizeof(Char)+sizeof(Misc::UInt16); // Size up to and including the number of protocol requests
		Misc::UInt32 endiannessMarker;
		Misc::UInt32 protocolVersion; // Client core protocol version
		Byte hash[hashLength]; // Hashed password+nonce
		Char clientName[nameLength]; // Client's name
		Misc::UInt16 numProtocols; // Number of requested plug-in protocols
		// ProtocolRequest protocols[numProtocols];
		
		/* Methods: */
		static MessageBuffer* createMessage(size_t numProtocols) // Returns a message buffer for a connect request message with the given number of plug-in protocols
			{
			return MessageBuffer::create(size+numProtocols*ProtocolRequest::size);
			}
		};
	
	struct ConnectReplyMsg // Connect reply from a server
		{
		/* Embedded classes: */
		public:
		struct ProtocolReply // Reply to a protocol request
			{
			/* Embedded classes: */
			enum ReplyStatus // Status codes for a protocol reply
				{
				Success=0, // Protocol was successfully negotiated
				UnknownProtocol, // Server doesn't know the requested protocol
				WrongVersion // Server doesn't support requested protocol version
				};
			
			/* Elements: */
			public:
			static const size_t size=sizeof(Misc::UInt8)+sizeof(Misc::UInt32)+sizeof(Misc::UInt16)+2*sizeof(MessageID);
			Misc::UInt8 replyStatus;
			Misc::UInt32 protocolVersion;
			Misc::UInt16 protocolIndex;
			MessageID clientMessageBase;
			MessageID serverMessageBase;
			};
		
		/* Elements: */
		static const size_t nameLength=32;
		static const size_t size=nameLength*sizeof(Char)+sizeof(ClientID)+nameLength*sizeof(Char)+sizeof(Misc::UInt32)+sizeof(Misc::UInt16); // Size up to and including the number of protocol replies
		Char serverName[nameLength]; // Server name
		ClientID clientId; // Unique ID assigned to the client
		Char clientName[nameLength]; // Name assigned to the client; not identical to requested name if that was not unique
		Misc::UInt32 udpTicket; // A random ticket to authenticate a UDP connection to the server
		Misc::UInt16 numProtocols; // Number of negotiated plug-in protocols
		// ProtocolReply protocols[numProtocols];
		
		/* Methods: */
		static MessageBuffer* createMessage(size_t numProtocols) // Returns a message buffer for a connect reply message with the given number of plug-in protocols
			{
			return MessageBuffer::create(ConnectReply,size+numProtocols*ProtocolReply::size);
			}
		};
	
	struct UDPConnectRequestMsg // Connection request to a server's UDP socket
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Misc::UInt32);
		ClientID clientId;
		Misc::UInt32 udpConnectionTicket;
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a UDP connection request message
			{
			return MessageBuffer::create(UDPConnectRequest,size);
			}
		};
	
	struct UDPConnectReplyMsg // Connection reply from a server's UDP socket
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(Misc::UInt32);
		Misc::UInt32 udpConnectionTicket;
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a UDP connection reply message
			{
			return MessageBuffer::create(UDPConnectReply,size);
			}
		};
	
	struct PingMsg // Ping request and reply
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(Misc::SInt16)+2*sizeof(Misc::SInt64);
		Misc::SInt16 sequence; // Ping packet's sequence number
		Misc::SInt64 seconds; // Seconds of sender's wall-clock time
		Misc::SInt64 nanoseconds; // Nanoseconds of sender's wall-clock time
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a ping request or reply message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct NameChangeRequestMsg // Request from a client to change its own name
		{
		/* Elements: */
		public:
		static const size_t nameLength=32;
		static const size_t size=nameLength*sizeof(Char);
		Char name[nameLength]; // Requested new name
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a name change request
			{
			return MessageBuffer::create(NameChangeRequest,size);
			}
		};
	
	struct NameChangeReplyMsg // Reply from the server to a client's request to change its own name
		{
		/* Elements: */
		public:
		static const size_t nameLength=32;
		static const size_t size=sizeof(Bool)+nameLength*sizeof(Char);
		Bool granted; // !=0 if the name change request was granted
		Char name[nameLength]; // Requested new name, or old name if request was denied
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a name change reply
			{
			return MessageBuffer::create(NameChangeReply,size);
			}
		};
	
	struct NameChangeNotificationMsg // Notification from the server that a client changed its name
		{
		/* Elements: */
		public:
		static const size_t nameLength=32;
		static const size_t size=sizeof(ClientID)+nameLength*sizeof(Char);
		ClientID clientId; // Client's unique ID
		Char name[nameLength]; // Client's new name
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a name change notification
			{
			return MessageBuffer::create(NameChangeNotification,size);
			}
		};
	
	struct ClientConnectNotificationMsg // Notification that a new client succcessfully connected
		{
		/* Elements: */
		public:
		static const size_t nameLength=32;
		static const size_t size=sizeof(ClientID)+nameLength*sizeof(Char)+sizeof(Misc::UInt16);
		ClientID clientId; // Client's unique ID
		Char clientName[nameLength];
		Misc::UInt16 numProtocols;
		// Misc::UInt16 protocolIndices[numProtocols];
		
		/* Methods: */
		static MessageBuffer* createMessage(size_t numProtocols) // Returns a message buffer for a client connect notification message for the given number of protocols
			{
			return MessageBuffer::create(ClientConnectNotification,size+numProtocols*sizeof(Misc::UInt16));
			}
		};
	
	struct ClientDisconnectNotificationMsg // Notification that a client disconnected
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID);
		ClientID clientId; // Client's unique ID
		
		/* Methods: */
		static MessageBuffer* createMessage(void) // Returns a message buffer for a client disconnect notification message
			{
			return MessageBuffer::create(ClientDisconnectNotification,size);
			}
		};
	
	/* Elements: */
	static const unsigned int protocolVersion=1U<<16; // Version 1.0
	};

#endif
