/***********************************************************************
StringContinuation - Helper class to read a string of unknown length
from a non-blocking socket.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#ifndef STRINGCONTINUATION_INCLUDED
#define STRINGCONTINUATION_INCLUDED

#include <string>
#include <stdexcept>
#include <Misc/Utility.h>
#include <Misc/SizedTypes.h>
#include <Misc/VarIntMarshaller.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/Protocol.h>

class StringContinuation:public MessageContinuation
	{
	/* Embedded classes: */
	private:
	enum State // Enumerated type for continuation states
		{
		ReadLengthFirst, // Reading the first byte of the VarInt32 length tag
		ReadLengthRemaining, // Reading the remaining bytes of the VarInt32 length tag
		ReadCharacters, // Reading the string's characters
		Done
		};
	
	/* Elements: */
	private:
	State state; // Current continuation state
	Misc::UInt32 stringLength; // Length of the string to read
	std::string& string; // String into which to read
	size_t remaining; // Number of length bytes / string characters remaining to be read
	
	/* Constructors and destructors: */
	public:
	StringContinuation(std::string& sString) // Prepares to read into the given string
		:state(ReadLengthFirst),
		 stringLength(0),
		 string(sString),remaining(0)
		{
		}
	
	/* Methods: */
	bool read(NonBlockSocket& socket) // Continues reading into the string
		{
		if(state==ReadLengthFirst&&socket.getUnread()>=sizeof(Misc::UInt8))
			{
			/* Read the length tag's first byte and determine how many more bytes to read: */
			remaining=Misc::readVarInt32First(socket,stringLength);
			
			/* Read the rest of the length tag (might be empty): */
			state=ReadLengthRemaining;
			}
		
		if(state==ReadLengthRemaining&&socket.getUnread()>=remaining*sizeof(Misc::UInt8))
			{
			/* Read the length tag's remaining bytes: */
			Misc::readVarInt32Remaining(socket,remaining,stringLength);
			
			/* Read the string's characters: */
			remaining=stringLength;
			string.clear();
			string.reserve(remaining);
			state=ReadCharacters;
			}
		
		if(state==ReadCharacters)
			{
			/* Read as many characters as available: */
			size_t length=Misc::min(socket.getUnread()/sizeof(Char),remaining);
			remaining-=length;
			
			/* Read the string's characters in chunks: */
			while(length>0)
				{
				char buffer[256];
				size_t readSize=Misc::min(length,sizeof(buffer));
				socket.read(buffer,readSize);
				string.append(buffer,buffer+readSize);
				length-=readSize;
				}
			
			/* Check if the string has been completely read: */
			if(remaining==size_t(0))
				{
				/* Done here: */
				state=Done;
				}
			}
		
		return state==Done;
		}
	};

#endif
