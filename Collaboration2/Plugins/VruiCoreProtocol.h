/***********************************************************************
VruiCoreProtocol - Definition of the communication protocol between a
Vrui core protocol client and server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_VRUICOREPROTOCOL_INCLUDED
#define PLUGINS_VRUICOREPROTOCOL_INCLUDED

#include <Misc/SizedTypes.h>
#include <Misc/Marshaller.h>
#include <Geometry/Point.h>
#include <Geometry/Vector.h>
#include <Geometry/Plane.h>
#include <Geometry/Rotation.h>
#include <Geometry/OrthonormalTransformation.h>
#include <Geometry/OrthogonalTransformation.h>
#include <Geometry/GeometryMarshallers.h>

#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>

#define VRUICORE_PROTOCOLNAME "VruiCore"
#define VRUICORE_PROTOCOLVERSION 1U<<16

class VruiCoreProtocol
	{
	/* Embedded classes: */
	protected:
	
	/* Protocol message IDs: */
	enum ClientMessages // Enumerated type for Vrui Core protocol message IDs sent by clients
		{
		ConnectRequest=0,
		EnvironmentUpdateRequest,
		ViewerConfigUpdateRequest,
		ViewerUpdateRequest,
		StartNavSequenceRequest,
		NavTransformUpdateRequest,
		StopNavSequenceRequest,
		LockNavTransformRequest,
		UnlockNavTransformRequest,
		CreateInputDeviceRequest,
		UpdateInputDeviceRayRequest,
		UpdateInputDeviceRequest,
		DisableInputDeviceRequest,
		EnableInputDeviceRequest,
		DestroyInputDeviceRequest,
		
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for Vrui Core protocol message IDs sent by servers
		{
		/* Pseudo-messages not from the server, but from the back end to the front end: */
		StartNotification=0,
		RegisterProtocolNotification,
		NameChangeReplyNotification,
		ClientConnectNotification,
		ClientNameChangeNotification,
		ClientDisconnectNotification,
		
		/* Messages from the server, forwarded from the back end to the front end: */
		ConnectReply,
		ConnectNotification,
		EnvironmentUpdateNotification,
		ViewerConfigUpdateNotification,
		ViewerUpdateNotification,
		StartNavSequenceReply,
		StartNavSequenceNotification,
		NavTransformUpdateNotification,
		StopNavSequenceNotification,
		LockNavTransformReply,
		LockNavTransformNotification,
		UnlockNavTransformNotification,
		CreateInputDeviceNotification,
		UpdateInputDeviceRayNotification,
		UpdateInputDeviceNotification,
		DisableInputDeviceNotification,
		EnableInputDeviceNotification,
		DestroyInputDeviceNotification,
		
		NumServerMessages
		};
	
	/* Protocol data type declarations: */
	public:
	typedef Misc::UInt8 InputDeviceID; // Type for ID numbers of input devices
	typedef Misc::Float32 Scalar; // Scalar type for 3D geometry
	static const size_t scalarSize=sizeof(Scalar); // Wire size of a scalar
	typedef Geometry::Point<Scalar,3> Point; // Type for affine points
	static const size_t pointSize=3*scalarSize; // Wire size of an affine point
	typedef Geometry::Vector<Scalar,3> Vector; // Type for 3D vectors
	static const size_t vectorSize=3*scalarSize; // Wire size of a 3D vector
	typedef Geometry::Plane<Scalar,3> Plane; // Type for 3D planes
	static const size_t planeSize=vectorSize+scalarSize; // Wire size of a 3D plane
	typedef Geometry::Rotation<Scalar,3> Rotation; // Type for 3D rotations
	static const size_t rotationSize=4*scalarSize; // Wire size of a 3D rotation
	typedef Geometry::OrthonormalTransformation<Scalar,3> ONTransform; // Type for 3D rigid-body transformations
	static const size_t onTransformSize=vectorSize+rotationSize; // Wire size of a 3D rigid-body transformation
	typedef Geometry::OrthogonalTransformation<Scalar,3> OGTransform; // Type for 3D uniformly-scaled rigid-body transformations
	static const size_t ogTransformSize=vectorSize+rotationSize+scalarSize; // Wire size of a 3D uniformly-scaled rigid-body transformation
	typedef Geometry::OrthogonalTransformation<Misc::Float64,3> NavTransform; // Type for Vrui navigation transformations; need to be double precision
	static const size_t navTransformSize=3*sizeof(Misc::Float64)+4*sizeof(Misc::Float64)+sizeof(Misc::Float64); // Wire size of a Vrui navigation transformation
	
	struct ClientEnvironment // Semi-static definition of a client's physical-space environment
		{
		/* Elements: */
		public:
		static const size_t size=scalarSize+pointSize+scalarSize+2*vectorSize+planeSize;
		Scalar inchFactor; // Lenght of an inch expressed in physical coordinate space unit
		Point displayCenter; // Center of physical space
		Scalar displaySize; // Size of physical space in physical coordinate space units
		Vector forwardDirection; // Direction vector pointing "forward"
		Vector upDirection; // Direction vector pointing up
		Plane floorPlane; // Floor plane equation
		
		/* Methods: */
		template <class SourceParam>
		ClientEnvironment& read(SourceParam& source) // Reads a client environment structure from a binary source
			{
			/* Read environment components from the source: */
			inchFactor=source.template read<Scalar>();
			Misc::read(source,displayCenter);
			displaySize=source.template read<Scalar>();
			Misc::read(source,forwardDirection);
			Misc::read(source,upDirection);
			Misc::read(source,floorPlane);
			return *this;
			}
		template <class SinkParam>
		void write(SinkParam& sink) const // Writes a client environment structure to a binary sink
			{
			/* Write environment components to the sink: */
			sink.template write(inchFactor);
			Misc::write(displayCenter,sink);
			sink.template write(displaySize);
			Misc::write(forwardDirection,sink);
			Misc::write(upDirection,sink);
			Misc::write(floorPlane,sink);
			}
		};
	
	struct ClientViewerConfig // Semi-static definition of a client's main viewer
		{
		/* Elements: */
		public:
		static const size_t size=2*vectorSize+2*pointSize;
		Vector viewDirection; // Viewing direction in viewer's local coordinates
		Vector upDirection; // Up direction in viewer's local coordinates
		Point eyePositions[2]; // Viewer's left and right eye positions in viewer's local coordinates
		
		/* Methods: */
		template <class SourceParam>
		ClientViewerConfig& read(SourceParam& source) // Reads a client viewer configuration structure from a binary source
			{
			/* Read viewer configuration components from the source: */
			Misc::read(source,viewDirection);
			Misc::read(source,upDirection);
			for(int i=0;i<2;++i)
				Misc::read(source,eyePositions[i]);
			return *this;
			}
		template <class SinkParam>
		void write(SinkParam& sink) const // Writes a client viewer configuration structure to a binary sink
			{
			/* Write viewer configuration components to the sink: */
			Misc::write(viewDirection,sink);
			Misc::write(upDirection,sink);
			for(int i=0;i<2;++i)
				Misc::write(eyePositions[i],sink);
			}
		};
	
	struct ClientViewerState // Dynamic state of a client's main viewer
		{
		/* Elements: */
		public:
		static const size_t size=onTransformSize;
		ONTransform headTransform; // Position and orientation of client's main viewer in client's physical space
		
		/* Methods: */
		template <class SourceParam>
		ClientViewerState& read(SourceParam& source) // Reads a client viewer state structure from a binary source
			{
			/* Read viewer state components from the source: */
			Misc::read(source,headTransform);
			return *this;
			}
		template <class SinkParam>
		void write(SinkParam& sink) const // Writes a client viewer state structure to a binary sink
			{
			/* Write viewer state components to the sink: */
			Misc::write(headTransform,sink);
			}
		};
	
	struct ClientInputDeviceState // Dynamic state of one of a client's input devices
		{
		/* Elements: */
		public:
		unsigned int id; // ID of the input device assigned by the client
		Vector rayDirection; // Device's pointing direction in local coordinates
		Scalar rayStart; // Origin of device's pointing ray along its pointing direction
		bool enabled; // Enable flag to temporarily disable devices due to tracking outages etc.
		ONTransform transform; // Device's current position and orientation in client's physical space
		};
	
	/* Protocol message data structure declarations: */
	protected:
	struct ConnectRequestMsg
		{
		/* Elements: */
		public:
		static const size_t nameLength=32;
		static const size_t size=nameLength*sizeof(Char)+ClientEnvironment::size+ClientViewerConfig::size+ClientViewerState::size+navTransformSize;
		Char environmentName[nameLength]; // Name of a shared physical environment of which the client is part (need better way to communicate this)
		ClientEnvironment environment; // Client's environment definition
		ClientViewerConfig viewerConfig; // Client's viewer configuration
		ClientViewerState viewerState; // Client's viewer state
		NavTransform navTransform; // Client's navigation transformation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int clientMessageBase) // Returns a message buffer for a connect request message
			{
			return MessageBuffer::create(clientMessageBase+ConnectRequest,size);
			}
		};
	
	struct ConnectReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(Misc::UInt16)+navTransformSize;
		Misc::UInt16 sharedEnvironmentId; // ID of the shared environment of which the client is a part
		NavTransform environmentNavTransform; // Current fully-resolved navigation transformation of the shared environment of which the client is a part
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a connect reply message
			{
			return MessageBuffer::create(serverMessageBase+ConnectReply,size);
			}
		};
	
	struct ConnectNotificationMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Misc::UInt16)+ClientEnvironment::size+ClientViewerConfig::size+ClientViewerState::size+navTransformSize;
		ClientID sourceClientID; // Source client of this message
		Misc::UInt16 sharedEnvironmentId; // ID of the shared physical environment of which this client is a part, or 0
		ClientEnvironment environment; // Client's environment definition
		ClientViewerConfig viewerConfig; // Client's viewer configuration
		ClientViewerState viewerState; // Client's viewer state
		NavTransform navTransform; // Client's fully-resolved navigation transformation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a connect notification message
			{
			return MessageBuffer::create(serverMessageBase+ConnectNotification,size);
			}
		};
	
	struct EnvironmentUpdateMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+ClientEnvironment::size;
		ClientID sourceClientID; // Source client of this message
		ClientEnvironment environment; // Client's environment definition
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a environment update request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct ViewerConfigUpdateMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+ClientViewerConfig::size;
		ClientID sourceClientID; // Source client of this message
		ClientViewerConfig viewerConfig; // Client's viewer configuration
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a viewer update request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct ViewerUpdateMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+ClientViewerState::size;
		ClientID sourceClientID; // Source client of this message
		ClientViewerState viewerState; // Client's viewer state
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a viewer update request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct NavSequenceRequestMsg
		{
		/* Elements: */
		static const size_t size=0;
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a start or stop navigation sequence request message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct StartNavSequenceReplyMsg
		{
		/* Elements: */
		static const size_t size=sizeof(Bool);
		Bool granted; // Flag if the request was granted
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a start navigation sequence reply message
			{
			return MessageBuffer::create(serverMessageBase+StartNavSequenceReply,size);
			}
		};
	
	struct NavSequenceNotificationMsg
		{
		/* Elements: */
		static const size_t size=sizeof(ClientID);
		ClientID clientId; // ID of client who is starting/stopping a navigation sequence
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a start or stop navigation sequence notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct NavTransformUpdateMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+navTransformSize;
		ClientID sourceClientID; // Source client of this message
		NavTransform navTransform; // Client's navigation transformation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a navigation transformation update request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct LockNavTransformMsg
		{
		/* Elements: */
		public:
		static const size_t size=2*sizeof(ClientID)+navTransformSize;
		ClientID sourceClientID; // Source client of this message
		ClientID lockClientId; // ID of client to whom to lock the client's navigation transformation
		NavTransform navTransform; // Client's navigation transformation relative to the locking client
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a lock navigation transformation request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct LockNavTransformReplyMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(Bool)+navTransformSize;
		ClientID lockClientId; // ID of client from nav transform locking request
		Bool granted; // Flag if the lock request was granted
		NavTransform navTransform; // New navigation transformation for the client
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int serverMessageBase) // Returns a message buffer for a lock navigation transformation reply message
			{
			return MessageBuffer::create(serverMessageBase+LockNavTransformReply,size);
			}
		};
	
	struct UnlockNavTransformMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+navTransformSize;
		ClientID clientID; // ID of client from nav transform locking request if a request message; ID of unlocking client if a notification message
		NavTransform navTransform; // Client's navigation transformation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for an unlock navigation transformation request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct CreateInputDeviceMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(InputDeviceID)+vectorSize+scalarSize;
		ClientID sourceClientID; // Source client of this message
		InputDeviceID deviceId; // ID of new input device
		Vector rayDirection; // Device's pointing direction in local coordinates
		Scalar rayStart; // Origin of device's pointing ray along its pointing direction
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a create input device request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct UpdateInputDeviceRayMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(InputDeviceID)+vectorSize+scalarSize;
		ClientID sourceClientID; // Source client of this message
		InputDeviceID deviceId; // ID of affected input device
		Vector rayDirection; // The device's new pointing ray direction
		Scalar rayStart; // The device's new pointing ray origin
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for an update input device ray request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct UpdateInputDeviceMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(InputDeviceID)+onTransformSize;
		ClientID sourceClientID; // Source client of this message
		InputDeviceID deviceId; // ID of affected input device
		ONTransform transform; // The device's new position and orientation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for an update input device request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct DisableInputDeviceMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(InputDeviceID);
		ClientID sourceClientID; // Source client of this message
		InputDeviceID deviceId; // ID of affected input device
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a disable input device request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct EnableInputDeviceMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(InputDeviceID)+onTransformSize;
		ClientID sourceClientID; // Source client of this message
		InputDeviceID deviceId; // ID of affected input device
		ONTransform transform; // The device's new position and orientation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for an enable input device request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	struct DestroyInputDeviceMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+sizeof(InputDeviceID);
		ClientID sourceClientID; // Source client of this message
		InputDeviceID deviceId; // ID of affected input device
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for a destroy input device request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion;
	};

#endif
