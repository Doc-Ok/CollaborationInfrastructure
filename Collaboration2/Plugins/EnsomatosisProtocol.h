/***********************************************************************
EnsomatosisProtocol - Definition of the communication protocol between
an inverse kinematics user representation server and clients.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_ENSOMATOSISAPROTOCOL_INCLUDED
#define PLUGINS_ENSOMATOSISAPROTOCOL_INCLUDED

#include <string>
#include <Misc/SizedTypes.h>
#include <Misc/Marshaller.h>
#include <Misc/StandardMarshallers.h>
#include <Geometry/GeometryMarshallers.h>
#include <Vrui/IKAvatar.h>

#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageBuffer.h>
#include <Collaboration2/Plugins/VruiCoreProtocol.h>

/* Forward declarations: */
class MessageWriter;
class MessageReader;
class NonBlockSocket;

#define ENSOMATOSIS_PROTOCOLNAME "Ensomatosis"
#define ENSOMATOSIS_PROTOCOLVERSION 1U<<16

class EnsomatosisProtocol
	{
	/* Embedded classes: */
	protected:
	
	/* Protocol message IDs: */
	enum ClientMessages // Enumerated type for Ensomatosis protocol message IDs sent by clients
		{
		AvatarUpdateRequest=0,
		AvatarStateUpdateRequest,
		
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for Ensomatosis protocol message IDs sent by servers
		{
		AvatarUpdateNotification=0,
		AvatarStateUpdateNotification,
		
		NumServerMessages
		};
	
	public:
	typedef VruiCoreProtocol::Scalar Scalar;
	static const size_t scalarSize=VruiCoreProtocol::scalarSize;
	typedef VruiCoreProtocol::Point Point;
	static const size_t pointSize=VruiCoreProtocol::pointSize;
	typedef VruiCoreProtocol::Vector Vector;
	static const size_t vectorSize=VruiCoreProtocol::vectorSize;
	typedef VruiCoreProtocol::Rotation Rotation;
	static const size_t rotationSize=VruiCoreProtocol::rotationSize;
	typedef VruiCoreProtocol::ONTransform ONTransform;
	static const size_t onTransformSize=VruiCoreProtocol::onTransformSize;
	
	/* Protocol message data structure declarations: */
	protected:
	struct AvatarUpdateMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+onTransformSize+7*pointSize+8*scalarSize+scalarSize; // Size of the fixed message header
		ClientID clientId; // ID of the client
		Vrui::IKAvatar::Configuration configuration; // Configuration of the user's avatar
		Scalar scale; // Scale factor applied to the avatar representation after loading
		std::string avatarFileName; // Name of the VRML file containing the user's avatar representation
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId,const std::string& avatarFileName) // Returns a message buffer for a avatar update request or notification message
			{
			return MessageBuffer::create(messageId,size+Misc::Marshaller<std::string>::getSize(avatarFileName));
			}
		};
	
	struct AvatarStateUpdateMsg
		{
		/* Elements: */
		public:
		static const size_t size=sizeof(ClientID)+16*rotationSize; // Avatar state consists of 16 rotations
		ClientID clientId; // ID of the client
		// Vrui::IKAvatar::State state; // New state of the user's skeleton
		
		/* Methods: */
		static MessageBuffer* createMessage(unsigned int messageId) // Returns a message buffer for an avatar state update request or notification message
			{
			return MessageBuffer::create(messageId,size);
			}
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion;
	
	/* Protected methods: */
	template <class SinkParam>
	static void writeAvatarConfiguration(const Vrui::IKAvatar::Configuration& configuration,SinkParam& sink) // Writes an avatar configuration to a data sink
		{
		Misc::Marshaller<ONTransform>::write(ONTransform(configuration.headToDevice),sink);
		for(int armIndex=0;armIndex<2;++armIndex)
			{
			const Vrui::IKAvatar::Configuration::Arm& arm=configuration.arms[armIndex];
			Misc::Marshaller<Point>::write(Point(arm.claviclePos),sink);
			Misc::Marshaller<Point>::write(Point(arm.shoulderPos),sink);
			sink.write(Scalar(arm.upperLength));
			sink.write(Scalar(arm.lowerLength));
			}
		Misc::Marshaller<Point>::write(Point(configuration.pelvisPos),sink);
		for(int legIndex=0;legIndex<2;++legIndex)
			{
			const Vrui::IKAvatar::Configuration::Leg& leg=configuration.legs[legIndex];
			Misc::Marshaller<Point>::write(Point(leg.hipPos),sink);
			sink.write(Scalar(leg.upperLength));
			sink.write(Scalar(leg.lowerLength));
			}
		}
	template <class SourceParam>
	static void readAvatarConfiguration(SourceParam& source,Vrui::IKAvatar::Configuration& configuration) // Reads an avatar configuration from a data source
		{
		configuration.headToDevice=Vrui::ONTransform(Misc::Marshaller<ONTransform>::read(source));
		for(int armIndex=0;armIndex<2;++armIndex)
			{
			Vrui::IKAvatar::Configuration::Arm& arm=configuration.arms[armIndex];
			arm.claviclePos=Vrui::Point(Misc::Marshaller<Point>::read(source));
			arm.shoulderPos=Vrui::Point(Misc::Marshaller<Point>::read(source));
			arm.upperLength=Vrui::Scalar(source.template read<Scalar>());
			arm.lowerLength=Vrui::Scalar(source.template read<Scalar>());
			}
		configuration.pelvisPos=Vrui::Point(Misc::Marshaller<Point>::read(source));
		for(int legIndex=0;legIndex<2;++legIndex)
			{
			Vrui::IKAvatar::Configuration::Leg& leg=configuration.legs[legIndex];
			leg.hipPos=Vrui::Point(Misc::Marshaller<Point>::read(source));
			leg.upperLength=Vrui::Scalar(source.template read<Scalar>());
			leg.lowerLength=Vrui::Scalar(source.template read<Scalar>());
			}
		}
	template <class SinkParam>
	static void writeAvatarState(const Vrui::IKAvatar::State& state,SinkParam& sink) // Writes an avatar state to a data sink
		{
		Misc::Marshaller<Rotation>::write(Rotation(state.neck),sink);
		for(int armIndex=0;armIndex<2;++armIndex)
			{
			const Vrui::IKAvatar::State::Arm& arm=state.arms[armIndex];
			Misc::Marshaller<Rotation>::write(Rotation(arm.clavicle),sink);
			Misc::Marshaller<Rotation>::write(Rotation(arm.shoulder),sink);
			Misc::Marshaller<Rotation>::write(Rotation(arm.elbow),sink);
			Misc::Marshaller<Rotation>::write(Rotation(arm.wrist),sink);
			}
		Misc::Marshaller<Rotation>::write(Rotation(state.pelvis),sink);
		for(int legIndex=0;legIndex<2;++legIndex)
			{
			const Vrui::IKAvatar::State::Leg& leg=state.legs[legIndex];
			Misc::Marshaller<Rotation>::write(Rotation(leg.hip),sink);
			Misc::Marshaller<Rotation>::write(Rotation(leg.knee),sink);
			Misc::Marshaller<Rotation>::write(Rotation(leg.ankle),sink);
			}
		}
	template <class SourceParam>
	static void readAvatarState(SourceParam& source,Vrui::IKAvatar::State& state) // Reads an avatar state from a data source
		{
		state.neck=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
		for(int armIndex=0;armIndex<2;++armIndex)
			{
			Vrui::IKAvatar::State::Arm& arm=state.arms[armIndex];
			arm.clavicle=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			arm.shoulder=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			arm.elbow=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			arm.wrist=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			}
		state.pelvis=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
		for(int legIndex=0;legIndex<2;++legIndex)
			{
			Vrui::IKAvatar::State::Leg& leg=state.legs[legIndex];
			leg.hip=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			leg.knee=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			leg.ankle=Vrui::Rotation(Misc::Marshaller<Rotation>::read(source));
			}
		}
	};

#endif
