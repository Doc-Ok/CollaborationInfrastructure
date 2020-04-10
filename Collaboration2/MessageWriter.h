/***********************************************************************
MessageWriter - Class to write binary data into a message buffer.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef MESSAGEWRITER_INCLUDED
#define MESSAGEWRITER_INCLUDED

#include <string.h>
#include <Misc/Endianness.h>

#include <Collaboration2/MessageBuffer.h>

class MessageWriter
	{
	/* Elements: */
	private:
	MessageBuffer* buffer; // Pointer to the message buffer being written into
	char* bufferEnd; // Pointer to the end of the buffer
	char* writePtr; // Position to write next data
	
	/* Constructors and destructors: */
	public:
	MessageWriter(void) // Creates a writer without a target buffer
		:buffer(0),bufferEnd(0),writePtr(0)
		{
		}
	MessageWriter(MessageBuffer* sBuffer) // Creates a writer for the given message buffer, which must not be null; takes over the caller's buffer reference
		:buffer(sBuffer),
		 bufferEnd(buffer->getBuffer()+buffer->getBufferSize()),
		 writePtr(buffer->getBuffer())
		{
		/* If the message has a valid message ID, write it into the message body: */
		if(buffer->getMessageId()!=~0x0U)
			write(MessageID(buffer->getMessageId()));
		}
	~MessageWriter(void) // Destroys the writer and unreferences an attached message buffer
		{
		/* Unreference the buffer: */
		if(buffer!=0)
			buffer->unref();
		}
	
	/* Methods: */
	MessageBuffer* getBuffer(void) // Returns the buffer being written into
		{
		return buffer;
		}
	void setBuffer(MessageBuffer* newBuffer) // Attaches a new message buffer, which must not be null; takes over the caller's buffer reference
		{
		/* Release the current buffer: */
		if(buffer!=0)
			buffer->unref();
		
		/* Attach the new buffer: */
		buffer=newBuffer;
		
		/* Reset the write pointers: */
		bufferEnd=buffer->getBuffer()+buffer->getBufferSize();
		writePtr=buffer->getBuffer();
		
		/* If the message has a valid message ID, write it into the message body: */
		if(buffer->getMessageId()!=~0x0U)
			write(MessageID(buffer->getMessageId()));
		}
	void releaseBuffer(void) // Releases and unreferences the current message buffer
		{
		/* Release the current buffer: */
		if(buffer!=0)
			buffer->unref();
		buffer=0;
		
		/* Reset the write pointers: */
		bufferEnd=0;
		writePtr=0;
		}
	void rewind(void) // Rewinds the message buffer's write pointer
		{
		/* Reset the write pointer: */
		writePtr=buffer!=0?buffer->getBuffer():0;
		
		/* If the buffer is valid and the message has a valid message ID, write it into the message body: */
		if(buffer!=0&&buffer->getMessageId()!=~0x0U)
			write(MessageID(buffer->getMessageId()));
		}
	size_t getSpace(void) const // Returns the amount of space left in the buffer
		{
		return bufferEnd-writePtr;
		}
	bool eof(void) const // Returns true if the message buffer has been filled
		{
		return writePtr==bufferEnd;
		}
	
	/* Raw binary write interface: */
	char* getWritePtr(void) // Returns the current write pointer
		{
		return writePtr;
		}
	void advanceWritePtr(size_t writeSize) // Advances the write pointer after data has been written by outside means
		{
		writePtr+=writeSize;
		}
	size_t finishMessage(void) // Finishes the current message at the current write position and returns its total size
		{
		/* Update the message buffer size: */
		bufferEnd=writePtr;
		buffer->setBufferSize(writePtr-buffer->getBuffer());
		return buffer->getBufferSize();
		}
	
	/* Type-safe binary write interface: */
	template <class DataParam>
	void write(const DataParam& data) // Writes the given value of the given data type into the buffer
		{
		memcpy(writePtr,&data,sizeof(DataParam));
		writePtr+=sizeof(DataParam);
		}
	template <class DataParam>
	void write(const DataParam* items,size_t numItems) // Writes the given array of values of the given data type into the buffer
		{
		memcpy(writePtr,items,numItems*sizeof(DataParam));
		writePtr+=numItems*sizeof(DataParam);
		}
	};

#endif
