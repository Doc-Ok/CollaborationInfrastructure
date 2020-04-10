/***********************************************************************
AudioEncoder - Class to encode audio from a PCM device for transmission
using the Opus encoder.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef AUDIOENCODER_INCLUDED
#define AUDIOENCODER_INCLUDED

#include <Misc/SizedTypes.h>
#include <Threads/Thread.h>
#include <Threads/EventDispatcher.h>
#include <Sound/Linux/ALSAPCMDevice.h>
#include <opus/opus.h>

class AudioEncoder
	{
	/* Elements: */
	private:
	Sound::ALSAPCMDevice pcmDevice; // The source PCM device
	unsigned int sampleRate; // Selected sample rate on the source PCM device
	unsigned int numPacketFrames; // Number of audio frames per Opus packet
	opus_int16* packetFrames; // Buffer for unencoded audio frames captured from the source PCM device
	OpusEncoder* encoder; // Opus encoder state structure
	size_t audioPacketSize; // Size of the buffer for the encoded audio packet excluding its header in bytes
	unsigned int audioMessageId; // Message ID assigned to encoded audio messages
	unsigned int destClientId; // ID of client to whom to send audio; 0 for broadcast to all other clients
	Misc::SInt16 frameNumber; // Frame sequence number for the next encoded audio packet
	volatile bool keepRunning; // Flag to keep the encoder thread running
	Threads::Thread encoderThread; // Thread to capture and encode audio in the background
	Threads::EventDispatcher* dispatcher; // Event dispatcher to signal when a new Opus packet has been encoded
	Threads::EventDispatcher::ListenerKey signalKey; // Key for the signal to notify the main thread of a newly-encoded Opus packet
	
	/* Private methods: */
	void* encoderThreadMethod(void); // Method running the encoder thread
	
	/* Constructors and destructors: */
	public:
	AudioEncoder(const char* sPCMDeviceName,unsigned int sSampleRate); // Creates an Opus encoder for the PCM recording device of the givn name and the given sample rate
	~AudioEncoder(void); // Destroys the Opus encoder
	
	/* Methods: */
	unsigned int getSampleRate(void) const // Returns the PCM device's sample rate
		{
		return sampleRate;
		}
	unsigned int getNumPacketFrames(void) const // Returns the number of audio frames per Opus packet
		{
		return numPacketFrames;
		}
	void setBitRate(unsigned int newBitRate); // Sets the target bit rate for the Opus encoder in bits per second
	void setDestClientId(unsigned int newDestClientId); // Sets the ID of the client to whom to send audio; 0 for broadcast
	void start(unsigned int newAudioMessageId,Threads::EventDispatcher& newDispatcher,Threads::EventDispatcher::ListenerKey newSignalKey); // Starts audio capture and encoding and registers a signal to be triggered when a new Opus packet has been encoded and
	void stop(void); // Stops recording and encoding
	};

#endif
