/***********************************************************************
AudioDecoder - Class to decode received audio using the Opus decoder.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#ifndef AUDIODECODER_INCLUDED
#define AUDIODECODER_INCLUDED

#include <Misc/SizedTypes.h>
#include <Misc/RingBuffer.h>
#include <Threads/Spinlock.h>
#include <Threads/Thread.h>
#include <Threads/EventDispatcher.h>
#include <Sound/Linux/ALSAPCMDevice.h>
#include <opus/opus.h>

/* Forward declarations: */
class MessageBuffer;

class AudioDecoder
	{
	/* Embedded classes: */
	private:
	typedef Misc::RingBuffer<MessageBuffer*> JitterBuffer; // Type for queues of incoming audio packet messages
	
	/* Elements: */
	unsigned int sampleRate; // Sample rate of incoming encoded audio
	unsigned int numPacketFrames; // Number of audio frames per Opus packet
	Threads::Spinlock jitterBufferMutex; // Mutex protecting the jitter buffer
	JitterBuffer jitterBuffer; // Queue for incoming audio packet messages
	OpusDecoder* decoder; // Opus decoder state structure
	opus_int16* packetFrames; // Buffer for decoded audio frames to be written to the sink PCM device
	Sound::ALSAPCMDevice pcmDevice; // The sink PCM device
	volatile bool keepRunning; // Flag to keep the decoder thread running
	Threads::Thread decoderThread; // Thread to decode and play back audio in the background
	
	/* Private methods: */
	void* decoderThreadMethod(void); // Method running the decoder thread
	
	/* Constructors and destructors: */
	public:
	AudioDecoder(unsigned int sSampleRate,unsigned int sNumPacketFrames,const char* pcmDeviceName); // Creates an Opus decoder for the given sample rate and encoded audio packet size and PCM sink device
	~AudioDecoder(void); // Destroys the Opus decoder
	
	/* Methods: */
	unsigned int getSampleRate(void) const // Returns the incoming audio's sample rate
		{
		return sampleRate;
		}
	unsigned int getNumPacketFrames(void) const // Returns the number of audio frames per Opus packet
		{
		return numPacketFrames;
		}
	void start(void); // Starts audio decoding and playback
	void stop(void); // Stops decoding and playback
	void enqueuePacket(MessageBuffer* newPacket); // Enqueues a newly-arrived encoded audio packet for decoding and playback
	};

#endif
