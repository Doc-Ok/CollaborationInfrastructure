/***********************************************************************
VruiAgoraClient - Client for real-time audio chat plug-in protocol
working inside a Vrui environment.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_VRUIAGORACLIENT_INCLUDED
#define PLUGINS_VRUIAGORACLIENT_INCLUDED

#include <stddef.h>
#include <string>
#include <vector>
#include <Misc/SizedTypes.h>
#include <Misc/RingBuffer.h>
#include <Misc/HashTable.h>
#include <Misc/ConfigurationFile.h>
#include <Threads/MutexCond.h>
#include <Threads/Thread.h>
#include <Threads/EventDispatcher.h>
#include <GLMotif/ToggleButton.h>
#include <GLMotif/Slider.h>
#include <GLMotif/TextFieldSlider.h>
#include <GLMotif/ListBox.h>
#include <GLMotif/FileSelectionDialog.h>
#include <GLMotif/FileSelectionHelper.h>
#include <pulse/thread-mainloop.h>
#include <pulse/context.h>
#include <pulse/stream.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>
#include <opus/opus.h>
#include <Sound/WAVFile.h>
#include <AL/Config.h>

#include <Collaboration2/VruiPluginClient.h>
#include <Collaboration2/Plugins/VruiCoreProtocol.h>
#include <Collaboration2/Plugins/VruiCoreClient.h>
#include <Collaboration2/Plugins/JitterBuffer.h>
#include <Collaboration2/Plugins/VruiAgoraProtocol.h>

#define LATENCYTEST 0

/* Forward declarations: */
class MessageReader;
class MessageContinuation;

class VruiAgoraClient:public VruiPluginClient,public VruiAgoraProtocol
	{
	/* Embedded classes: */
	private:
	typedef Misc::SInt32 TimeStamp; // Type for cyclic time stamps with microsecond resolution
	
	class RemoteClient:public PluginClient::RemoteClient
		{
		friend class VruiAgoraClient;
		
		/* Embedded classes: */
		enum State // Enumerated type for remote client states
			{
			Created=0,
			PlaybackThreadRunning,
			PlaybackThreadSuspended, // Playback thread is running, but the source is stopped for lack of audio data
			PlaybackThreadTerminating,
			PlaybackThreadTerminated
			};
		
		/* Elements: */
		private:
		const VruiCoreClient::RemoteClient* vcClient; // Pointer to the Vrui Core remote client state representing this client
		unsigned int playbackFrequency; // Frequency of decoded audio
		unsigned int playbackPacketSize; // Number of audio frames in each decoded audio packet
		TimeStamp period; // Playback length of a decoded audio packet
		Point mouthPos; // Position of remote client's "mouth" in its main viewer's head coordinate system
		Threads::MutexCond sourceStateCond; // Condition variable/mutex serializing access to the jitter buffer and related state and signaling wake-ups to the decoding and playback thread
		volatile State state; // Current remote client state
		JitterBuffer jitterBuffer; // Buffer to de-jitter incoming audio packets
		volatile unsigned int jitterBufferSizeRequest; // Jitter buffer size requested by the front end
		int minQueuedBuffers; // Minimum number of OpenAL buffers to be queued with playback source at all times
		TimeStamp headArrival; // Absolute time at which the audio packet currently at the head of the jitter buffer was expected to arrive
		int arrivalFilterGain; // Gain factor for the expected packet arrival time filter as a 16-bit fixed-point number
		TimeStamp targetLatency; // Target buffer latency between packets' expected arrival times and their queue times to maximize jitter buffer coverage
		float sourceLatency; // Averaged latency of remote client's audio source, expected to be 0.0
		bool latencyTooHigh; // Flag if source latency is currently too high
		#if ALSUPPORT_CONFIG_HAVE_OPENAL
		ALuint playbackSource; // OpenAL audio source to play back decoded audio
		ALint playbackQueueLength; // Total number of audio samples currently in the playback source's queue
		#endif
		Threads::Thread playbackThread; // Thread running audio decoding and playback
		
		bool muted; // Flag if the remote client is muted
		float gain; // Gain factor to adjust the remote client's playback volume
		
		// DEBUGGING
		TimeStamp timebase; // Time base point for pretty printing of event times
		
		/* Private methods: */
		
		#if ALSUPPORT_CONFIG_HAVE_OPENAL
		void* playbackThreadMethod(void); // Method running the audio decoding and playback thread
		#endif
		
		/* Constructors and destructors: */
		RemoteClient(unsigned int sPlaybackFrequency,unsigned int sPlaybackPacketSize,const Point& sMouthPos,int sJitterBufferSize,int sMinQueuedBuffers);
		virtual ~RemoteClient(void);
		
		/* Methods: */
		void startPlayback(void); // Starts the remote client's playback thread
		void enqueuePacket(const Threads::EventDispatcher::Time& time,Sequence sequenceNumber,MessageBuffer* packet); // Enqueues the given audio packet with the given sequence number
		void stopPlayback(void); // Stops audio processing for the remote client after the back end receives a client disconnect notification
		void waitForShutdown(void); // Waits until the remote client has stopped audio processing and the playback thread has terminated
		};
	
	typedef std::vector<RemoteClient*> RemoteClientList; // Type for lists of remote client state structures
	typedef Misc::HashTable<unsigned int,RemoteClient*> RemoteClientMap; // Type for hash tables mapping remote client IDs to remote client structures
	
	enum State // Enumerated type for Agora client states
		{
		Created=0,
		ContextConnecting,
		MainLoopRunning,
		SearchingSource,
		StreamConnecting,
		StreamConnected,
		StreamDisconnecting,
		ContextDisconnecting,
		MainLoopTerminating
		};
	
	/* Elements: */
	VruiCoreClient* vruiCore; // Pointer to the Vrui Core protocol client
	Misc::ConfigurationFileSection vruiAgoraConfig; // Configuration file section for Vrui Agora protocol
	
	/* Audio capture state: */
	unsigned int captureFrequency; // Audio capture frequency
	unsigned int capturePacketSize; // Number of audio frames in each captured and encoded audio packet
	Point captureMouthPos; // Position of this client's "mouth" in its main viewer's head coordinate system
	volatile State state; // Current state of the Agora client
	std::string captureSourceName; // Name of the requested PulseAudio source
	pa_threaded_mainloop* captureMainLoop; // A PulseAudio threaded mainloop running audio capture
	pa_context* captureContext; // A PulseAudio context connected to a local PulseAudio server
	pa_stream* captureStream; // A PulseAudio capture stream
	unsigned int captureSourceIndex; // Index of the source to which the capture stream is connected, for volume control etc.
	unsigned int captureSourceNumChannels; // Number of channels in the capture source
	Sample* captureBuffer; // A buffer to hold a packet of captured audio
	Sample* captureEnd; // Pointer to the end of the capture buffer
	Sample* capturePtr; // Current write position in the capture buffer
	OpusEncoder* captureEncoder; // An Opus audio encoder for captured audio
	Sequence captureSequence; // Sequence number for next transmitted audio packet
	bool capturePaused; // Flag to pause/unpause audio capture and encoding, i.e., mute the client's microphone
	
	/* Music injection state: */
	Sound::WAVFile* injectionFile; // Pointer to WAV file containing music currently injected into the outgoing audio stream
	volatile bool keepInjectionThreadRunning; // Flag to keep the music injection thread running
	Threads::Thread injectionThread; // Thread running music injection
	
	/* Remote client representation state: */
	RemoteClientList remoteClients; // List of remote clients used by the front end
	RemoteClientMap remoteClientMap; // Map from remote client IDs to remote client structures used by the back end
	
	/* UI state: */
	GLMotif::Slider* microphoneVolumeSlider; // Slider to control the volume of the local audio capture source
	GLMotif::ToggleButton* muteMicrophoneToggle; // Toggle button to mute local audio capture
	GLMotif::FileSelectionHelper wavHelper; // Helper object to select WAV files for music injection
	GLMotif::Button* injectMusicButton; // Button to select a WAV music file to inject into the audio stream
	GLMotif::TextFieldSlider* jitterBufferSizeSlider; // Slide to adjust a remote client's jitter buffer size
	GLMotif::ToggleButton* muteClientToggle; // Toggle button to mute a remote client
	GLMotif::Slider* clientVolumeSlider; // Slider to adjust a remote client's volume
	
	#if LATENCYTEST
	Sound::WAVFile* wavFile; // WAV file containing recorded audio data
	#endif
	
	/* Private methods: */
	static TimeStamp now(void) // Returns a time stamp for the current time
		{
		/* Get the current time as an absolute time: */
		Threads::EventDispatcher::Time now=Threads::EventDispatcher::Time::now();
		
		/* Convert the absolute time to a time stamp: */
		return TimeStamp(long(now.tv_sec)*1000000L+long(now.tv_usec));
		}
	
	/* Methods receiving status messages from the back end: */
	void captureSetupFailedNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that local audio capture could not be started
	void sourceVolumeNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that the volume of the local audio capture source has changed
	void musicInjectionDoneNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that a music file has stopped playing
	void frontendConnectNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that a new remote Agora client structure was created
	void frontendMuteClientNotificationCallback(unsigned int messageId,MessageReader& message); // Callback when another client mutes this client and is passive-aggressive about it
	
	/* Methods receiving messages from the server: */
	MessageContinuation* backendConnectNotificationCallback(unsigned int messageId,MessageContinuation* continuation); // Callback when a connect notification message for a new remote client arrives
	MessageContinuation* audioPacketReplyCallback(unsigned int messageId,MessageContinuation* continuation); // Callback when an audio packet arrives on the TCP socket
	void udpAudioPacketReplyCallback(unsigned int messageId,MessageReader& message); // Callback when an audio packet arrives on the UDP socket
	
	/* Methods handling audio capture: */
	void captureSetupFailed(void); // Notifies the front end that local audio capture could not be set up
	void encodeAndSendAudioPacket(const Sample* audioPacketBuffer); // Encodes and sends the given audio packet to the server
	static void captureReadCallback(pa_stream* stream,size_t nbytes,void* userData); // Callback called when audio data can be read from the PulseAudio capture stream
	static void captureSourceInfoCallback(pa_context* context,const pa_source_info* info,int eol,void* userData); // Callback called when information about the capture source is received
	static void captureContextSubscriptionCallback(pa_context* context,pa_subscription_event_type eventType,uint32_t index,void* userData); // Callback called when a source-related event happens on the server to which the capture context is connected
	static void captureStreamStateCallback(pa_stream* stream,void* userData); // Callback called when the PulseAudio capture stream changes state
	void connectStream(const char* sourceName); // Connects the capture stream to the PulseAudio source of the given name
	static void captureSourceInfoListCallback(pa_context* context,const pa_source_info* info,int eol,void* userData); // Callback called with information about a PulseAudio source, to find the requested recording device's name
	static void captureContextStateCallback(pa_context* context,void* userData); // Callback called when the PulseAudio capture context changes state
	
	/* Methods handling music injection: */
	void* injectionThreadMethod(void); // Method running the music injection thread
	
	/* User interface methods: */
	RemoteClient* getSelectedClient(void); // Returns the remote client currently selected in the remote client list box, or null
	void muteMicrophoneValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void microphoneVolumeValueChangedCallback(GLMotif::Slider::ValueChangedCallbackData* cbData);
	void injectWAVFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData);
	void remoteClientListValueChangedCallback(GLMotif::ListBox::ValueChangedCallbackData* cbData);
	void jitterBufferSizeValueChangedCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData);
	void muteClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void clientVolumeValueChangedCallback(GLMotif::Slider::ValueChangedCallbackData* cbData);
	void createSettingsPage(void); // Creates an Agora protocol settings page in the Vrui Core client's collaboration dialog
	
	/* Constructors and destructors: */
	public:
	VruiAgoraClient(Client* sClient);
	virtual ~VruiAgoraClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	
	/* Methods from class VruiPluginClient: */
	virtual void frontendStart(void);
	virtual void frontendClientDisconnected(unsigned int clientId);
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void sound(ALContextData& contextData) const;
	virtual void shutdown(void);
	};

#endif
