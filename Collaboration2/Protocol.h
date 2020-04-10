/***********************************************************************
Protocol - Basic type declarations and common helper functions to
implement client/server protocols in the second-generation collaboration
infrastructure.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PROTOCOL_INCLUDED
#define PROTOCOL_INCLUDED

#include <stddef.h>
#include <string>
#include <Misc/SizedTypes.h>

/********************************
Basic protocol type declarations:
********************************/

typedef Misc::UInt8 Bool; // Type for boolean flags in protocol messages
typedef Misc::UInt8 Byte; // Type for raw bytes in protocol messages, sizeof(Byte) is guaranteed to be 1
typedef Misc::UInt8 Char; // Type for string characters in protocol messages
typedef Misc::UInt16 MessageID; // Type for message IDs in protocol messages
typedef Misc::UInt16 ClientID; // Type for client IDs in protocol messages

/***********************
Common helper functions:
***********************/

template <class SourceParam>
inline
bool readBool(SourceParam& source) // Reads a boolean flag from a source
	{
	return source.template read<Bool>()!=Bool(0);
	}

template <class SinkParam>
inline
void writeBool(bool value,SinkParam& sink) // Writes a boolean flag to a sink
	{
	sink.write(Bool(value?1:0));
	}

template <class SourceParam>
inline
void 
charBufferToString( // Reads from a fixed-size buffer of network characters into a C++ string
	SourceParam& source,
	size_t bufferSize,
	std::string& string)
	{
	/* Read until the end of the string, up to buffer size: */
	while(bufferSize>0)
		{
		std::string::value_type c(source.template read<Char>());
		--bufferSize;
		if(c=='\0')
			break;
		string.push_back(c);
		}
	
	/* Skip the rest of the buffer: */
	while(bufferSize>0)
		{
		source.template read<Char>();
		--bufferSize;
		}
	}

template <class SinkParam>
inline
void
stringToCharBuffer( // Writes from a C string into a fixed-size buffer of network characters; truncates string if too long
	const char* string,
	SinkParam& sink,
	size_t bufferSize)
	{
	/* Write the string to the buffer, up to buffer size: */
	for(const char* sPtr=string;*sPtr!='\0'&&bufferSize>0;++sPtr,--bufferSize)
		sink.template write(Char(*sPtr));
	
	/* Fill the rest of the buffer with zeros: */
	for(;bufferSize>0;--bufferSize)
		sink.template write(Char(0));
	}

template <class SinkParam>
inline
void
stringToCharBuffer( // Writes from a C++ string into a fixed-size buffer of network characters; truncates string if too long
	const std::string& string,
	SinkParam& sink,
	size_t bufferSize)
	{
	/* Write the string to the buffer, up to buffer size: */
	for(std::string::const_iterator sIt=string.begin();sIt!=string.end()&&bufferSize>0;++sIt,--bufferSize)
		sink.template write(Char(*sIt));
	
	/* Fill the rest of the buffer with zeros: */
	for(;bufferSize>0;--bufferSize)
		sink.template write(Char(0));
	}

#endif
