/***********************************************************************
KoinoniaProtocol - Definition of the communication protocol between a
data sharing client and a server.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/KoinoniaProtocol.h>

#include <Misc/Utility.h>
#include <Misc/VarIntMarshaller.h>
#include <Collaboration2/MessageEditor.h>
#include <Collaboration2/NonBlockSocket.h>

/*************************************************
Methods of class KoinoniaProtocol::ReadObjectCont:
*************************************************/

void KoinoniaProtocol::ReadObjectCont::startReadingObject(void)
	{
	/* Create a message buffer to hold the object's serialization: */
	size_t bufferSize=headerSize;
	if(explicitSize)
		bufferSize+=Misc::getVarInt32Size(objectSize);
	bufferSize+=objectSize;
	object=MessageBuffer::create(bufferSize);
	
	/* Prepare to read the object's serialization: */
	objectWriter.setBuffer(object->ref());
	
	/* Skip the header to be written later: */
	objectWriter.advanceWritePtr(headerSize);
	
	/* Write the object's size as a VarInt if the size is not fixed: */
	if(explicitSize)
		Misc::writeVarInt32(objectSize,objectWriter);
	
	/* Start reading the object: */
	remaining=objectSize;
	state=ReadObject;
	}

KoinoniaProtocol::ReadObjectCont::~ReadObjectCont(void)
	{
	/* Unreference the object's serialization: */
	if(object!=0)
		object->unref();
	}

bool KoinoniaProtocol::ReadObjectCont::read(NonBlockSocket& socket)
	{
	/* Check if the object's size is not completely read: */
	if(state==ReadObjectSizeFirst&&socket.getUnread()>=remaining)
		{
		/* Read the object size's first byte and determine the number of remaining bytes to read: */
		remaining=Misc::readVarInt32First(socket,objectSize);
		state=ReadObjectSize;
		}
	
	if(state==ReadObjectSize&&socket.getUnread()>=remaining)
		{
		/* Read the remaining bytes of the object size (might be zero): */
		Misc::readVarInt32Remaining(socket,remaining,objectSize);
		
		/* Start reading the object's serialization: */
		startReadingObject();
		}
	
	/* Check if the object's serialization is not completely read: */
	if(state==ReadObject)
		{
		/* Read as much data as possible: */
		size_t readSize=Misc::min(socket.getUnread(),remaining);
		socket.readRaw(objectWriter.getWritePtr(),readSize);
		objectWriter.advanceWritePtr(readSize);
		remaining-=readSize;
		
		/* Check if the object has been read completely: */
		if(remaining==0)
			state=Done;
		}
	
	return state==Done;
	}

MessageBuffer* KoinoniaProtocol::ReadObjectCont::finishObject(const DataType& dataType,DataType::TypeID type,bool swapEndianness)
	{
	/* Attach a message editor to the object's representation: */
	MessageEditor editor(object->ref());
	
	/* Skip the message header: */
	editor.advanceEditPtr(headerSize);
	
	/* Skip the object's size if it is explicitly encoded: */
	if(explicitSize)
		Misc::readVarInt32(editor);
	
	/* Check and/or endianness-swap the object's serialization: */
	if(swapEndianness)
		dataType.swapEndianness(type,editor);
	else
		dataType.checkSerialization(type,editor);
	
	/* Return the object's serialization: */
	return object;
	}

/*****************************************
Static elements of class KoinoniaProtocol:
*****************************************/

const char* KoinoniaProtocol::protocolName=KOINONIA_PROTOCOLNAME;
const unsigned int KoinoniaProtocol::protocolVersion=KOINONIA_PROTOCOLVERSION;
