/***********************************************************************
AgoraClient - Client for real-time audio chat plug-in protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_AGORACLIENT_INCLUDED
#define PLUGINS_AGORACLIENT_INCLUDED

#include <Misc/SizedTypes.h>
#include <Threads/MutexCond.h>
#include <Threads/Thread.h>
#include <Threads/EventDispatcher.h>
#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/stream.h>
#include <opus/opus.h>

#include <Collaboration2/PluginClient.h>
#include <Collaboration2/Plugins/JitterBuffer.h>
#include <Collaboration2/Plugins/AgoraProtocol.h>
#include <Collaboration2/Plugins/AudioEncoder.h>
#include <Collaboration2/Plugins/AudioDecoder.h>

#define LATENCYTEST 1

#if LATENCYTEST
#include <Sound/WAVFile.h>
#endif

/* Forward declarations: */
class MessageReader;
class MessageContinuation;

class AgoraClient:public PluginClient,public AgoraProtocol
	{
	/* Embedded classes: */
	private:
	class RemoteClient:public PluginClient::RemoteClient
		{
		friend class AgoraClient;
		
		/* Embedded classes: */
		private:
		enum State // Enumerated type for remote client states
			{
			Created=0,
			StreamCreated,
			StreamConnecting,
			PlaybackThreadRunning,
			PlaybackThreadTerminating,
			StreamDisconnecting,
			StreamDisconnected
			};
		typedef Misc::SInt32 TimeStamp; // Type for cyclic time stamps with microsecond resolution
		
		/* Elements: */
		private:
		AgoraClient* client; // Pointer back to the Agora client
		State state; // Current remote client state
		unsigned int playbackFrequency; // Audio playback frequency
		unsigned int playbackPacketSize; // Number of audio frames in each received, decoded, and played back audio packet
		TimeStamp period; // Playback length of a decoded audio packet
		Threads::MutexCond sourceStateCond; // Condition variable/mutex serializing access to the jitter buffer and related state and signaling wake-ups to the decoding and playback thread
		JitterBuffer jitterBuffer; // Buffer to de-jitter incoming audio packets
		int minQueuedPeriods; // Minimum number of periods to hold in the PulseAudio server's playback buffer at all times
		TimeStamp headArrival; // Absolute time at which the audio packet currently at the head of the jitter buffer was expected to arrive
		int arrivalFilterGain; // Gain factor for the expected packet arrival time filter as a 16-bit fixed-point number
		TimeStamp targetLatency; // Target buffer latency between packets' expected arrival times and their queue times to maximize jitter buffer coverage
		pa_stream* playbackStream; // A PulseAudio capture stream
		bool playbackStopped; // Flag if the playback stream is currently stopped
		Threads::Thread playbackThread; // Thread running audio decoding and playback
		
		// DEBUGGING
		TimeStamp timebase; // Time at which the remote client object was created
		
		/* Private methods: */
		static TimeStamp now(void) // Returns a time stamp for the current time
			{
			/* Get the current time as an absolute time: */
			Threads::EventDispatcher::Time now=Threads::EventDispatcher::Time::now();
			
			/* Convert the absolute time to a time stamp: */
			return TimeStamp(long(now.tv_sec)*1000000L+long(now.tv_usec));
			}
		void* playbackThreadMethod(void); // Method running the audio decoding and playback thread
		static void playbackStreamStateCallback(pa_stream* stream,void* userData); // Callback called when the PulseAudio playback stream changes state
		static void playbackStreamUnderflowCallback(pa_stream* stream,void* userData); // Callback called when the PulseAudio playback stream stalls due to underflow
		
		/* Constructors and destructors: */
		RemoteClient(AgoraClient* sClient,unsigned int sPlaybackFrequency,unsigned int sPlaybackPacketSize);
		virtual ~RemoteClient(void);
			
		/* Methods: */
		void enqueuePacket(const Threads::EventDispatcher::Time& time,Sequence sequenceNumber,MessageBuffer* packet); // Enqueues the given audio packet with the given sequence number
		};
	
	friend class RemoteClient;
	
	/* Elements: */
	
	/* Audio capture state: */
	unsigned int captureFrequency; // Audio capture frequency
	unsigned int capturePacketSize; // Number of audio frames in each captured and encoded audio packet
	pa_threaded_mainloop* captureMainLoop; // A PulseAudio threaded mainloop running audio capture
	pa_context* captureContext; // A PulseAudio context connected to a local PulseAudio server
	pa_stream* captureStream; // A PulseAudio capture stream
	Sample* captureBuffer; // A buffer to hold a packet of captured audio
	Sample* captureEnd; // Pointer to the end of the capture buffer
	Sample* capturePtr; // Current write position in the capture buffer
	OpusEncoder* captureEncoder; // An Opus audio encoder for captured audio
	Sequence captureSequence; // Sequence number for next transmitted audio packet
	Threads::EventDispatcher::ListenerKey captureContextReadySignalKey; // Key for the signal to signal that the capture context is ready
	bool capturePaused; // Flag to pause/unpause audio capture and encoding, i.e., mute the client's microphone
	#if LATENCYTEST
	Sound::WAVFile* wavFile; // WAV file containing recorded audio data
	#endif
	
	/* Private methods: */
	
	/* Methods receiving messages from the server: */
	MessageContinuation* connectNotificationCallback(unsigned int messageId,MessageContinuation* continuation); // Callback when a new client participating in the Agora protocol connects
	MessageContinuation* audioPacketReplyCallback(unsigned int messageId,MessageContinuation* continuation); // Callback when an audio packet arrives on the TCP socket
	void udpAudioPacketReplyCallback(unsigned int messageId,MessageReader& message); // Callback when an audio packet arrives on the UDP socket
	
	/* Methods handling audio capture: */
	bool captureContextReadySignalCallback(Threads::EventDispatcher::ListenerKey signalKey,void* signalData); // Callback when the audio processing context has connected to a PulseAudio server
	static void captureStreamStateCallback(pa_stream* stream,void* userData); // Callback called when the PulseAudio capture stream changes state
	static void captureReadCallback(pa_stream* stream,size_t nbytes,void* userData); // Callback called when audio data can be read from the PulseAudio capture stream
	static void captureContextStateCallback(pa_context* context,void* userData); // Callback called when the PulseAudio capture context changes state
	
	/* Constructors and destructors: */
	public:
	AgoraClient(Client* sClient);
	virtual ~AgoraClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	};

#endif
