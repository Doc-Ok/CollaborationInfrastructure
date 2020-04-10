/***********************************************************************
MessageContinuation - Base class for objects holding message processing
states between invocations of a message handler.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef MESSAGECONTINUATION_INCLUDED
#define MESSAGECONTINUATION_INCLUDED

class MessageContinuation
	{
	/* Constructors and destructors: */
	public:
	virtual ~MessageContinuation(void)
		{
		}
	};

#endif
