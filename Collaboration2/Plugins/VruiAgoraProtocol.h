/***********************************************************************
VruiAgoraProtocol - Definition of the communication protocol between a
real-time audio chat server and clients running in a Vrui environment.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_VRUIAGORAPROTOCOL_INCLUDED
#define PLUGINS_VRUIAGORAPROTOCOL_INCLUDED

#include <Misc/SizedTypes.h>

#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>
#include <Collaboration2/Plugins/VruiCoreProtocol.h>

#define VRUIAGORA_PROTOCOLNAME "VruiAgora"
#define VRUIAGORA_PROTOCOLVERSION 1U<<16

class VruiAgoraProtocol
	{
	/* Embedded classes: */
	protected:
	
	/* Protocol message IDs: */
	enum ClientMessages // Enumerated type for VruiAgora protocol message IDs sent by clients
		{
		/* Messages sent to the server: */
		ConnectRequest=0,
		AudioPacketRequest,
		MuteClientRequest,
		
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for VruiAgora protocol message IDs sent by servers
		{
		/* Pseudo-messages sent from the back end to the front end: */
		CaptureSetupFailedNotification=0,
		SourceVolumeNotification,
		MusicInjectionDoneNotification,
		
		/* Messages sent from the server: */
		ConnectNotification,
		AudioPacketReply,
		MuteClientNotification,
		
		NumServerMessages
		};
	
	public:
	typedef VruiCoreProtocol::Point Point;
	static const size_t pointSize=VruiCoreProtocol::pointSize;
	
	/* Protocol message data structure declarations: */
	protected:
	struct ConnectRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=2*sizeof(Misc::UInt32)+pointSize;
		Misc::UInt32 sampleRate; // Client's audio sample rate
		Misc::UInt32 numPacketFrames; // Number of audio frames in each encoded audio packet
		Point mouthPos; // Position of client's "mouth" in main viewer coordinates
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase) // Returns a message buffer for a connect request message
			{
			return MessageBuffer::create(clientMessageBase+ConnectRequest,size);
			}
		};
	
	struct ConnectNotificationMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+2*sizeof(Misc::UInt32)+pointSize;
		ClientID clientId; // ID of newly connected client
		Misc::UInt32 sampleRate; // Client's audio sample rate
		Misc::UInt32 numPacketFrames; // Number of audio frames in each encoded audio packet
		Point mouthPos; // Position of client's "mouth" in main viewer coordinates
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a connect notification message
			{
			return MessageBuffer::create(serverMessageBase+ConnectNotification,size);
			}
		};
	
	typedef Misc::SInt16 Sequence; // Type for audio packet sequence numbers
	typedef Misc::SInt16 Sample; // Type for audio samples
	
	struct AudioPacketMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Sequence)+sizeof(Misc::UInt16); // Size up to and including the audio packet length
		ClientID destinationOrSourceId; // ID of destination client (or 0 for broadcast) for AudioPacketRequest message, or ID of source client for AudioPacketReply message
		Sequence sequenceNumber; // Audio frame sequence number
		Misc::UInt16 audioPacketLen; // Length of encoded audio packet in bytes
		// Byte audioPacket[audioPacketLen]; // Encoded audio packet
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId,size_t audioPacketLen) // Returns a message buffer for an audio packet request or reply message with the given encoded audio packet size
			{
			return MessageBuffer::create(messageId,size+audioPacketLen);
			}
		};
	
	struct MuteClientRequestMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+2*sizeof(Bool);
		ClientID clientId; // ID of client the requesting client wants to mute
		Bool mute; // Flag whether the client wants to mute (true) or unmute (false) the other client
		Bool notifyClient; // Flag whether the requesting client wants the other client to be notified
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase) // Returns a message buffer for a mute client request message
			{
			return MessageBuffer::create(clientMessageBase+MuteClientRequest,size);
			}
		};
	
	struct MuteClientNotificationMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Bool);
		ClientID clientId; // ID of client who muted the notified client
		Bool mute; // Flag whether the notified client has been muted (true) or unmuted (false)
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a mute client notification message
			{
			return MessageBuffer::create(serverMessageBase+MuteClientNotification,size);
			}
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion;
	};

#endif
