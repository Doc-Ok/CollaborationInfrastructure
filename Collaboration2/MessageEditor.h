/***********************************************************************
MessageEditor - Class to edit-in-place binary data in a message buffer.
Copyright (c) 2020 Oliver Kreylos
***********************************************************************/

#ifndef MESSAGEEDITOR_INCLUDED
#define MESSAGEEDITOR_INCLUDED

#include <string.h>

#include <Collaboration2/MessageBuffer.h>

class MessageEditor
	{
	/* Elements: */
	private:
	MessageBuffer* buffer; // Pointer to the message buffer being edited
	char* bufferEnd; // Pointer to the end of the buffer
	char* editPtr; // Position to read/edit next data
	
	/* Constructors and destructors: */
	public:
	MessageEditor(MessageBuffer* sBuffer) // Creates a reader for the given message buffer (which is allowed to be null); takes over the caller's buffer reference
		:buffer(sBuffer),
		 bufferEnd(buffer!=0?buffer->getBuffer()+buffer->getBufferSize():0),
		 editPtr(buffer!=0?buffer->getBuffer():0)
		{
		}
	~MessageEditor(void) // Destroys the reader and unreferences an attached message buffer
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
	void rewind(void) // Rewinds the message buffer's edit pointer
		{
		/* Reset the edit pointer: */
		editPtr=buffer!=0?buffer->getBuffer():0;
		}
	size_t getUnedited(void) const // Returns the amount of unedited data in the buffer
		{
		return bufferEnd-editPtr;
		}
	bool eof(void) const // Returns true if the message buffer has been completely edited
		{
		return editPtr==bufferEnd;
		}
	
	/* Raw binary editing interface: */
	char* getEditPtr(void) // Returns the current edit pointer
		{
		return editPtr;
		}
	void advanceEditPtr(size_t editSize) // Advances the edit pointer after data has been edited by outside means
		{
		editPtr+=editSize;
		}
	
	/* Type-safe binary read interface: */
	template <class DataParam>
	DataParam read(void) // Reads the next value of the given data type from the buffer
		{
		DataParam result;
		memcpy(&result,editPtr,sizeof(DataParam));
		editPtr+=sizeof(DataParam);
		return result;
		}
	template <class DataParam>
	DataParam& read(DataParam& data) // Ditto
		{
		memcpy(&data,editPtr,sizeof(DataParam));
		editPtr+=sizeof(DataParam);
		return data;
		}
	template <class DataParam>
	void read(DataParam* items,size_t numItems) // Reads an array of values of the given data type from the buffer
		{
		memcpy(items,editPtr,numItems*sizeof(DataParam));
		editPtr+=numItems*sizeof(DataParam);
		}
	
	/* Type-safe binary write interface: */
	template <class DataParam>
	void write(const DataParam& data) // Writes the given value of the given data type into the buffer
		{
		memcpy(editPtr,&data,sizeof(DataParam));
		editPtr+=sizeof(DataParam);
		}
	template <class DataParam>
	void write(const DataParam* items,size_t numItems) // Writes the given array of values of the given data type into the buffer
		{
		memcpy(editPtr,items,numItems*sizeof(DataParam));
		editPtr+=numItems*sizeof(DataParam);
		}
	};

#endif
