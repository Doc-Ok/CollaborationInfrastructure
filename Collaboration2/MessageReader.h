/***********************************************************************
MessageReader - Class to read binary data from a message buffer with
endianness correction where necessary.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef MESSAGEREADER_INCLUDED
#define MESSAGEREADER_INCLUDED

#include <string.h>
#include <Misc/Endianness.h>

#include <Collaboration2/MessageBuffer.h>

class MessageReader
	{
	/* Elements: */
	private:
	MessageBuffer* buffer; // Pointer to the message buffer being read from
	const char* bufferEnd; // Pointer to the end of the buffer
	const char* readPtr; // Position to read next data
	bool swapOnRead; // Flag whether data must be endianness-swapped on read
	
	/* Constructors and destructors: */
	public:
	MessageReader(MessageBuffer* sBuffer,bool sSwapOnRead =false) // Creates a reader for the given message buffer (which is allowed to be null); takes over the caller's buffer reference
		:buffer(sBuffer),
		 bufferEnd(buffer!=0?buffer->getBuffer()+buffer->getBufferSize():0),
		 readPtr(buffer!=0?buffer->getBuffer():0),
		 swapOnRead(sSwapOnRead)
		{
		}
	~MessageReader(void) // Destroys the reader and unreferences an attached message buffer
		{
		/* Unreference the buffer: */
		if(buffer!=0)
			buffer->unref();
		}
	
	/* Methods: */
	MessageBuffer* getBuffer(void) // Returns the buffer being read from
		{
		return buffer;
		}
	size_t getSize(void) const // Returns the message size
		{
		return buffer!=0?buffer->getBufferSize():0;
		}
	bool getSwapOnRead(void) const // Returns true if values must be endianness-swapped after reading
		{
		return swapOnRead;
		}
	void setSwapOnRead(bool newSwapOnRead) // Sets whether values must be endianness-swapped after reading
		{
		swapOnRead=newSwapOnRead;
		}
	void rewind(void) // Rewinds the message buffer's read pointer
		{
		/* Reset the read pointer: */
		readPtr=buffer!=0?buffer->getBuffer():0;
		}
	size_t getUnread(void) const // Returns the amount of unread data in the buffer
		{
		return bufferEnd-readPtr;
		}
	bool eof(void) const // Returns true if the message buffer has been completely read
		{
		return readPtr==bufferEnd;
		}
	
	/* Raw binary read interface: */
	const char* getReadPtr(void) // Returns the current read pointer
		{
		return readPtr;
		}
	void advanceReadPtr(size_t readSize) // Advances the read pointer after data has been read by outside means
		{
		readPtr+=readSize;
		}
	
	/* Endianness- and type-safe binary read interface: */
	template <class DataParam>
	DataParam read(void) // Reads the next value of the given data type from the buffer
		{
		DataParam result;
		memcpy(&result,readPtr,sizeof(DataParam));
		readPtr+=sizeof(DataParam);
		if(swapOnRead)
			Misc::swapEndianness(result);
		return result;
		}
	template <class DataParam>
	DataParam& read(DataParam& data) // Ditto
		{
		memcpy(&data,readPtr,sizeof(DataParam));
		readPtr+=sizeof(DataParam);
		if(swapOnRead)
			Misc::swapEndianness(data);
		return data;
		}
	template <class DataParam>
	void read(DataParam* items,size_t numItems) // Reads an array of values of the given data type from the buffer
		{
		memcpy(items,readPtr,numItems*sizeof(DataParam));
		readPtr+=numItems*sizeof(DataParam);
		if(swapOnRead)
			Misc::swapEndianness(items,numItems);
		}
	};

#endif
