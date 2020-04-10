/***********************************************************************
VruiAgoraClient - Client for real-time audio chat plug-in protocol
working inside a Vrui environment.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/VruiAgoraClient.h>

#include <string.h>
#include <unistd.h>
#include <stdexcept>
#include <Misc/Utility.h>
#include <Misc/MessageLogger.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/Marshaller.h>
#include <Math/Math.h>
#include <Geometry/Point.h>
#include <Geometry/OrthogonalTransformation.h>
#include <Geometry/GeometryValueCoders.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/Margin.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Pager.h>
#include <GLMotif/Separator.h>
#include <GLMotif/Label.h>
#include <pulse/operation.h>
#include <Sound/SoundDataFormat.h>
#if ALSUPPORT_CONFIG_HAVE_OPENAL
#ifdef __APPLE__
#include <OpenAL/alc.h>
#else
#include <AL/alc.h>
#endif
#endif
#include <AL/ALTemplates.h>
#include <AL/ALContextData.h>
#include <AL/ALGeometryWrappers.h>
#include <Vrui/Vrui.h>
#include <Vrui/SoundContext.h>

#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/Plugins/VruiCoreClient.h>

#if LATENCYTEST
#include <IO/OpenFile.h>
#endif

/**********************************************
Methods of class VruiAgoraClient::RemoteClient:
**********************************************/

#if ALSUPPORT_CONFIG_HAVE_OPENAL

namespace {

/****************
Helper functions:
****************/

void stopSource(ALuint source) // Stops the given sound source and reclaims and deletes all its buffers
	{
	/* Stop the source: */
	alSourceStop(source);
	
	/* Reclaim and delete the source's audio buffers: */
	ALint numProcessedBuffers;
	alGetSourcei(source,AL_BUFFERS_PROCESSED,&numProcessedBuffers);
	while(numProcessedBuffers>0)
		{
		ALuint buffers[32]; // Way sufficient
		ALint reclaimed=Misc::min(numProcessedBuffers,32);
		alSourceUnqueueBuffers(source,reclaimed,buffers);
		alDeleteBuffers(reclaimed,buffers);
		numProcessedBuffers-=reclaimed;
		}
	}

}

void* VruiAgoraClient::RemoteClient::playbackThreadMethod(void)
	{
	/*********************************************************************
	Initialize decoding and playback facilities:
	*********************************************************************/
	
	bool ok=true;
	
	OpusDecoder* audioDecoder=0;
	if(ok)
		{
		/* Create the audio decoder: */
		int opusError;
		audioDecoder=opus_decoder_create(playbackFrequency,1,&opusError);
		ok=opusError==OPUS_OK;
		if(!ok)
			Misc::formattedUserError("VruiAgora: Unable to create Opus audio decoder for client %u due to error %d (%s)",vcClient->getId(),opusError,opus_strerror(opusError));
		}
	
	unsigned int jitterBufferSize(jitterBuffer.getNumSlots());
	Sample* silence=0;
	Sample* decoded=0;
	if(ok)
		{
		/* Allocate a buffer for silence: */
		unsigned int numSilenceSamples=Misc::max(playbackPacketSize,(jitterBufferSize*playbackPacketSize)/2U);
		silence=new Sample[numSilenceSamples];
		memset(silence,0,numSilenceSamples*sizeof(Sample));
		
		/* Allocate a buffer for decoded audio: */
		decoded=new Sample[playbackPacketSize*2];
		}
	
	if(ok)
		{
		// DEBUGGING
		unsigned int numMissing=0;
		unsigned int numDequeued=0;
		
		/* Keep decoding and playing back audio until interrupted: */
		unsigned int numMissedPackets=0;
		while(state==PlaybackThreadRunning||state==PlaybackThreadSuspended)
			{
			/* Suspend this thread while the playback source is stopped: */
			bool restartSource=false;
			{
			Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
			
			/* Check if the audio packet stream has been interrupted: */
			if(numMissedPackets>=20)
				{
				/* Stop the playback source and reclaim its audio buffers: */
				stopSource(playbackSource);
				state=PlaybackThreadSuspended;
				}
			
			/* Wait while the source is stopped: */
			if(state==PlaybackThreadSuspended)
				Misc::formattedLogNote("VruiAgora: Suspending audio playback for client %u at %fms",vcClient->getId(),double(TimeStamp(now()-timebase))/1.0e3);
			restartSource=state==PlaybackThreadSuspended;
			while(state==PlaybackThreadSuspended)
				sourceStateCond.wait(sourceStateLock);
			}
			
			/* Bail out if shut down: */
			if(state!=PlaybackThreadRunning)
				break;
			
			/* Check if there was a request to change jitter buffer size: */
			if(jitterBufferSize!=jitterBufferSizeRequest)
				{
				// DEBUGGING
				Misc::formattedLogNote("VruiAgora: Resizing jitter buffer for client %u from %u to %u",vcClient->getId(),jitterBufferSize,jitterBufferSizeRequest);
				
				/* Stop the playback source and reclaim its audio buffers: */
				stopSource(playbackSource);
				
				/* Resize the jitter buffer: */
				{
				Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
				jitterBufferSize=jitterBufferSizeRequest;
				jitterBuffer.setNumSlots(jitterBufferSize);
				}
				
				/* Re-allocate the silence buffer: */
				delete[] silence;
				unsigned int numSilenceSamples=Misc::max(playbackPacketSize,(jitterBufferSize*playbackPacketSize)/2U);
				silence=new Sample[numSilenceSamples];
				memset(silence,0,numSilenceSamples*sizeof(Sample));
				
				/* Recalculate the target latency: */
				targetLatency=(TimeStamp(jitterBufferSize)*period)/2; // From arrival to dequeueing/decoding
				
				/* Restart the source: */
				restartSource=true;
				}
			
			if(restartSource)
				{
				/* Preload the playback source with one buffer of numSlots*0.5 periods and minQueuedBuffers buffers of one period each: */
				ALuint buffers[10]; // Way sufficient
				alGenBuffers(1+minQueuedBuffers,buffers);
				alBufferData(buffers[0],AL_FORMAT_MONO16,silence,((jitterBufferSize*playbackPacketSize)/2)*sizeof(Sample),playbackFrequency);
				for(int i=0;i<minQueuedBuffers;++i)
					{
					alBufferData(buffers[i+1],AL_FORMAT_MONO16,silence,playbackPacketSize*sizeof(Sample),playbackFrequency);
					}
				alSourceQueueBuffers(playbackSource,1+minQueuedBuffers,buffers);
				
				/* Initialize the buffer latency conditioner: */
				playbackQueueLength=(jitterBufferSize*playbackPacketSize)/2+minQueuedBuffers*playbackPacketSize;
				
				// DEBUGGING
				Misc::formattedLogNote("VruiAgora: Resuming audio playback for client %u at %fms",vcClient->getId(),double(TimeStamp(now()-timebase))/1.0e3);
				
				// DEBUGGING
				numMissing=0;
				numDequeued=0;
				
				/* Start playing back audio: */
				alSourcePlay(playbackSource);
				numMissedPackets=0;
				}
			
			/* Sleep until targetLatency after the current head packet's expected arrival time: */
			TimeStamp sleep=TimeStamp(headArrival+targetLatency)-now();
			if(sleep>0)
				usleep(sleep);
			
			/* Dequeue the next packet from the jitter buffer: */
			MessageBuffer* packetBuffer;
			{
			Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
			
			// DEBUGGING
			// TimeStamp nowTs=now();
			// Misc::formattedLogNote("Dequeue,%d,%f,%f",jitterBuffer.getHeadSequence(),double(TimeStamp(nowTs-timebase))/1.0e3,double(TimeStamp(nowTs-timebase))/1.0e3);
			
			packetBuffer=jitterBuffer.dequeue();
			
			/* Update the buffer latency conditioner: */
			headArrival+=period;
			}
			
			// DEBUGGING
			if(packetBuffer==0)
				++numMissing;
			if(++numDequeued==1000)
				{
				Misc::formattedLogNote("VruiAgora: Packet loss rate for client %u = %d%%, source latency = %f",vcClient->getId(),numMissing,double(sourceLatency));
				numMissing=0;
				numDequeued=0;
				}
			
			/* Decode the dequeued packet: */
			if(packetBuffer!=0)
				{
				/* Skip the packet's header: */
				MessageReader packet(packetBuffer);
				packet.advanceReadPtr(sizeof(MessageID)+AudioPacketMsg::size);
				
				/* Decode the audio packet: */
				int decodeResult=opus_decode(audioDecoder,reinterpret_cast<const unsigned char*>(packet.getReadPtr()),packet.getUnread(),decoded,playbackPacketSize,0);
				if(decodeResult!=int(playbackPacketSize))
					Misc::formattedLogWarning("VruiAgoraClient: Packet decoding error for client %u",vcClient->getId());
				
				numMissedPackets=0;
				}
			else
				{
				/* Generate gap filler: */
				int decodeResult=opus_decode(audioDecoder,0,0,decoded,playbackPacketSize,1);
				if(decodeResult!=int(playbackPacketSize))
					Misc::formattedLogWarning("VruiAgora: Packet loss concealment error for client %u",vcClient->getId());
				
				++numMissedPackets;
				}
			
			/* Query playback source state: */
			ALint numQueuedBuffers,numProcessedBuffers;
			alGetSourcei(playbackSource,AL_BUFFERS_QUEUED,&numQueuedBuffers);
			alGetSourcei(playbackSource,AL_BUFFERS_PROCESSED,&numProcessedBuffers);
			
			/* Check if the source's latency is getting too low or high: */
			ALint numPendingBuffers=numQueuedBuffers-numProcessedBuffers;
			bool underrun=numPendingBuffers==0;
			sourceLatency=sourceLatency*0.99f+float(numPendingBuffers-minQueuedBuffers)*0.01f;
			if(latencyTooHigh)
				latencyTooHigh=sourceLatency>=0.5f;
			else
				latencyTooHigh=sourceLatency>=1.0f;
			
			// DEBUGGING
			// if(numQueuedBuffers!=minQueuedBuffers+1||numProcessedBuffers!=1)
			// 	Misc::formattedLogNote("VruiAgora: Source for client %u is sliding %d %d",vcClient->getId(),numQueuedBuffers,numProcessedBuffers);
			
			/* Dequeue all processed buffers from the source: */
			ALuint buffers[32]; // Way sufficient
			if(numProcessedBuffers>32)
				numProcessedBuffers=32;
			alSourceUnqueueBuffers(playbackSource,numProcessedBuffers,buffers);
			
			/* Update the source's playback queue length: */
			for(ALint i=0;i<numProcessedBuffers;++i)
				{
				ALint bufferSize;
				alGetBufferi(buffers[i],AL_SIZE,&bufferSize);
				playbackQueueLength-=bufferSize/sizeof(Sample);
				}
			
			ALuint* bufPtr=buffers;
			
			/* Try to recover from a source underrun: */
			if(underrun)
				{
				/* Start recovery by queueing a period of silence: */
				if(numProcessedBuffers==0)
					{
					/* Generate a new buffer: */
					alGenBuffers(1,bufPtr);
					++numProcessedBuffers;
					}
				alBufferData(bufPtr[0],AL_FORMAT_MONO16,silence,playbackPacketSize*sizeof(Sample),playbackFrequency);
				alSourceQueueBuffers(playbackSource,1,bufPtr);
				++bufPtr;
				--numProcessedBuffers;
				
				/* Update the source's playback queue length: */
				playbackQueueLength+=playbackPacketSize;
				}
			
			/* Upload decoded audio data into a buffer and queue it with the playback source: */
			if(numProcessedBuffers==0)
				{
				/* Generate a new buffer: */
				alGenBuffers(1,bufPtr);
				++numProcessedBuffers;
				}
			alBufferData(bufPtr[0],AL_FORMAT_MONO16,decoded,playbackPacketSize*sizeof(Sample),playbackFrequency);
			alSourceQueueBuffers(playbackSource,1,bufPtr);
			++bufPtr;
			--numProcessedBuffers;
			
			/* Update the source's playback queue length: */
			playbackQueueLength+=playbackPacketSize;
			
			/* Delete all reclaimed but unused buffers: */
			if(numProcessedBuffers>0)
				alDeleteBuffers(numProcessedBuffers,bufPtr);
			
			/* Try to recover from a source underrun: */
			if(underrun)
				{
				// DEBUGGING
				Misc::formattedLogNote("VruiAgora: Client %u restarting from source underrun, source latency=%f",vcClient->getId(),double(sourceLatency));
				
				/* Finish recovery by restarting the playback source: */
				alSourcePlay(playbackSource);
				}
			}
		
		/* Stop the playback source and reclaim its audio buffers: */
		stopSource(playbackSource);
		}
	
	/* Delete the audio buffers: */
	delete[] silence;
	delete[] decoded;
	
	if(audioDecoder!=0)
		{
		/* Destroy the Opus decoder: */
		opus_decoder_destroy(audioDecoder);
		}
	
	/* Mark the thread as finished, wake up the front end, and return: */
	state=PlaybackThreadTerminated;
	Vrui::requestUpdate();
	return 0;
	}

#endif

VruiAgoraClient::RemoteClient::RemoteClient(unsigned int sPlaybackFrequency,unsigned int sPlaybackPacketSize,const VruiAgoraProtocol::Point& sMouthPos,int sJitterBufferSize,int sMinQueuedBuffers)
	:vcClient(0),
	 playbackFrequency(sPlaybackFrequency),playbackPacketSize(sPlaybackPacketSize),
	 period(TimeStamp((long(playbackPacketSize)*1000000L+long(playbackFrequency)/2)/long(playbackFrequency))),
	 mouthPos(sMouthPos),
	 state(Created),
	 jitterBuffer(sJitterBufferSize),jitterBufferSizeRequest(sJitterBufferSize),minQueuedBuffers(sMinQueuedBuffers),
	 arrivalFilterGain(int(0.01*65536.0+0.5)),sourceLatency(0),latencyTooHigh(false),
	 #if ALSUPPORT_CONFIG_HAVE_OPENAL
	 playbackSource(0),
	 #endif
	 muted(false),gain(1)
	{
	/* Initialize the buffer latency conditioner: */
	targetLatency=(TimeStamp(jitterBuffer.getNumSlots())*period)/2; // From arrival to dequeueing/decoding
	}

void VruiAgoraClient::RemoteClient::startPlayback(void)
	{
	#if ALSUPPORT_CONFIG_HAVE_OPENAL
	
	/* Create a playback source: */
	alGenSources(1,&playbackSource);
	if(alGetError()!=AL_NO_ERROR)
		{
		/* Print an error and bail out: */
		playbackSource=0;
		Misc::formattedUserError("VruiAgora: Unable to create OpenAL playback source for new client %u",vcClient->getId());
		return;
		}
	
	/* Configure the playback source: */
	alSourceGain(playbackSource,1.0f);
	
	// DEBUGGING
	timebase=now();
	
	/* Start the audio decoding and playback thread, but suspend it until the first audio packet arrives: */
	state=PlaybackThreadSuspended;
	playbackThread.start(this,&VruiAgoraClient::RemoteClient::playbackThreadMethod);
	
	#endif
	}

VruiAgoraClient::RemoteClient::~RemoteClient(void)
	{
	#if ALSUPPORT_CONFIG_HAVE_OPENAL
	
	{
	Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
	if(state==PlaybackThreadRunning||state==PlaybackThreadSuspended)
		{
		/* Tell the audio decoding and playback thread to pack it in: */
		state=PlaybackThreadTerminating;
		sourceStateCond.signal(); // Signal just in case the playback thread was suspended
		}
	}
	
	if(!playbackThread.isJoined())
		{
		/* Shut down the audio decoding and playback thread: */
		playbackThread.join();
		}
	
	if(playbackSource!=0)
		{
		/* Delete the playback source: */
		alDeleteSources(1,&playbackSource);
		}
	
	#endif
	}

void VruiAgoraClient::RemoteClient::enqueuePacket(const Threads::EventDispatcher::Time& time,VruiAgoraProtocol::Sequence sequenceNumber,MessageBuffer* packet)
	{
	/* Bail out if the playback source hasn't been created yet: */
	if(playbackSource==0)
		return;
	
	/* Calculate the packet's arrival time stamp: */
	TimeStamp arrival=TimeStamp(long(time.tv_sec)*1000000L+long(time.tv_usec));
	
	/* Check if the playback source is currently stopped or active: */
	{
	Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
	if(state==PlaybackThreadSuspended)
		{
		/* Initialize the jitter buffer and latency conditioner: */
		jitterBuffer.init(sequenceNumber,packet->ref());
		
		// DEBUGGING
		// Misc::formattedLogNote("Enqueue,%d,%f,%f",sequenceNumber,double(TimeStamp(arrival-timebase))/1.0e3,double(TimeStamp(arrival-timebase))/1.0e3);
		
		/* Wake up the decoding and playback thread: */
		state=PlaybackThreadRunning;
		sourceStateCond.signal();
		
		headArrival=arrival;
		}
	else if(state==PlaybackThreadRunning)
		{
		/* Enqueue the packet into the jitter buffer and update the latency conditioner: */
		jitterBuffer.enqueue(sequenceNumber,packet->ref());
		TimeStamp expectedArrival=headArrival+TimeStamp(Sequence(sequenceNumber-jitterBuffer.getHeadSequence()))*period;
		
		// DEBUGGING
		// Misc::formattedLogNote("Enqueue,%d,%f,%f",sequenceNumber,double(TimeStamp(arrival-timebase))/1.0e3,double(TimeStamp(expectedArrival-timebase))/1.0e3);
		
		headArrival+=TimeStamp(TimeStamp(TimeStamp(arrival-expectedArrival)*arrivalFilterGain)+32768)>>16;
		}
	}
	}

void VruiAgoraClient::RemoteClient::stopPlayback(void)
	{
	#if ALSUPPORT_CONFIG_HAVE_OPENAL
	
	/* Tell the audio decoding and playback thread to pack it in: */
	{
	Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
	if(state<PlaybackThreadTerminating)
		state=PlaybackThreadTerminating;
	sourceStateCond.signal(); // Signal just in case the playback thread was suspended
	}
	
	#else
	
	/* Mark the remote client as shut down: */
	state=PlaybackThreadTerminated;
	
	#endif
	}

void VruiAgoraClient::RemoteClient::waitForShutdown(void)
	{
	#if ALSUPPORT_CONFIG_HAVE_OPENAL
	
	if(!playbackThread.isJoined())
		{
		/* Shut down the audio decoding and playback thread: */
		playbackThread.join();
		}
	
	if(playbackSource!=0)
		{
		/* Delete the playback source: */
		alDeleteSources(1,&playbackSource);
		}
	playbackSource=0;
	
	#endif
	}

/********************************
Methods of class VruiAgoraClient:
********************************/

void VruiAgoraClient::captureSetupFailedNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Disable the mute/unmute button: */
	muteMicrophoneToggle->setToggle(false);
	muteMicrophoneToggle->setEnabled(false);
	}

void VruiAgoraClient::sourceVolumeNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Set the volume slider's value range: */
	unsigned int minVolume=message.read<Misc::UInt32>();
	unsigned int maxVolume=message.read<Misc::UInt32>();
	unsigned int numVolumeSteps=message.read<Misc::UInt32>();
	microphoneVolumeSlider->setValueRange(double(minVolume),double(maxVolume),double(maxVolume-minVolume)/double(numVolumeSteps-1));
	
	/* Set the volume slider's value: */
	microphoneVolumeSlider->setValue(double(message.read<Misc::UInt32>()));
	
	/* Enable the volume slider: */
	microphoneVolumeSlider->setEnabled(true);
	}

void VruiAgoraClient::musicInjectionDoneNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Join the music injection thread: */
	keepInjectionThreadRunning=false;
	injectionThread.join();
	
	/* Close the WAV file: */
	delete injectionFile;
	injectionFile=0;
	
	/* Check if local audio capture is enabled: */
	if(state==StreamConnected)
		{
		/* Re-enable the mute/unmute button: */
		muteMicrophoneToggle->setEnabled(true);
		}
	
	/* Re-enable the music injection button: */
	injectMusicButton->setEnabled(true);
	}

void VruiAgoraClient::frontendMuteClientNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the message: */
	unsigned int mutingClientId=message.read<ClientID>();
	bool muted=message.read<Bool>()!=Bool(0);
	if(muted)
		Misc::formattedUserNote("VruiAgora: Remote client %u muted you because you were talking rubbish",mutingClientId);
	else
		Misc::formattedUserNote("VruiAgora: Remote client %u unmuted you again",mutingClientId);
	}

void VruiAgoraClient::frontendConnectNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the new remote client's ID and client structure: */
	unsigned int clientId=message.read<ClientID>();
	RemoteClient* rc=message.read<RemoteClient*>();
	
	/* Add the new remote client to the list: */
	remoteClients.push_back(rc);
	
	/* Set the new remote client's Vrui Core pointer: */
	rc->vcClient=vruiCore->getRemoteClient(clientId);
	
	/* Start playback for the new remote client: */
	rc->startPlayback();
	
	/* Potentially update per-client widgets on the Vrui Agora settings page: */
	if(rc==getSelectedClient())
		{
		jitterBufferSizeSlider->setEnabled(true);
		jitterBufferSizeSlider->setValue(rc->jitterBufferSizeRequest);
		muteClientToggle->setEnabled(true);
		muteClientToggle->setToggle(rc->muted);
		clientVolumeSlider->setEnabled(true);
		clientVolumeSlider->setValue(rc->gain>0.0f?10.0*Math::log(double(rc->gain))/Math::log(10.0):-30.0);
		}
	}

MessageContinuation* VruiAgoraClient::backendConnectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the new client's configuration from the connect notification message: */
	unsigned int remoteClientId=socket.read<ClientID>();
	unsigned int playbackFrequency=socket.read<Misc::UInt32>();
	unsigned int playbackPacketSize=socket.read<Misc::UInt32>();
	Point mouthPos=Misc::Marshaller<Point>::read(socket);
	
	// DEBUGGING
	Misc::formattedLogNote("VruiAgora: New remote client with ID %u",remoteClientId);
	
	try
		{
		/* Get remote client settings from configuration section: */
		int jitterBufferSize=vruiAgoraConfig.retrieveValue<int>("./jitterBufferSize",2);
		int minQueuedBuffers=vruiAgoraConfig.retrieveValue<int>("./minQueuedBuffers",2);
		
		/* Create a new remote client structure: */
		RemoteClient* newClient=new RemoteClient(playbackFrequency,playbackPacketSize,mouthPos,jitterBufferSize,minQueuedBuffers);
		
		/* Add the new remote client structure to the remote client map: */
		remoteClientMap.setEntry(RemoteClientMap::Entry(remoteClientId,newClient));
		
		/* Forward the connect notification to the front end: */
		{
		MessageWriter connectNotification(MessageBuffer::create(serverMessageBase+ConnectNotification,sizeof(ClientID)+sizeof(RemoteClient*)));
		connectNotification.write(ClientID(remoteClientId));
		connectNotification.write(newClient);
		client->queueFrontendMessage(connectNotification.getBuffer());
		}
		}
	catch(const std::runtime_error& err)
		{
		Misc::formattedUserError("VruiAgora: Unable to create remote client structure for client %u due to exception %s",remoteClientId,err.what());
		}
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* VruiAgoraClient::audioPacketReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Elements: */
		public:
		unsigned int sourceClientId;
		Sequence sequence;
		MessageWriter message; // Reassembled audio packet message to hand to audio decoder
		
		/* Constructors and destructors: */
		Cont(unsigned int sSourceClientId,Sequence sSequence,size_t audioPacketLen)
			:sourceClientId(sSourceClientId),sequence(sSequence),
			 message(AudioPacketMsg::createMessage(AudioPacketReply,audioPacketLen))
			{
			}
		};
	
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Read the message header: */
		ClientID sourceClientId=socket.read<ClientID>();
		Sequence sequenceNumber=socket.read<Sequence>();
		Misc::UInt16 audioPacketLen=socket.read<Misc::UInt16>();
		
		/* Create a continuation structure: */
		cont=new Cont(sourceClientId,sequenceNumber,audioPacketLen);
		
		/* Write the message header into the continuation structure: */
		cont->message.write(sourceClientId);
		cont->message.write(sequenceNumber);
		cont->message.write(audioPacketLen);
		}
	
	/* Read a chunk of the audio packet: */
	size_t readSize=Misc::min(socket.getUnread(),cont->message.getSpace());
	socket.read(cont->message.getWritePtr(),readSize);
	cont->message.advanceWritePtr(readSize);
	
	/* Check if the message was read completely: */
	if(cont->message.eof())
		{
		/* Get the source client's state: */
		RemoteClientMap::Iterator rcIt=remoteClientMap.findEntry(cont->sourceClientId);
		if(!rcIt.isFinished())
			{
			/* Hand the audio packet to the remote client: */
			rcIt->getDest()->enqueuePacket(client->getDispatcher().getCurrentTime(),cont->sequence,cont->message.getBuffer());
			}
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void VruiAgoraClient::udpAudioPacketReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the message header: */
	unsigned int sourceClientId=message.read<ClientID>();
	Sequence sequenceNumber=message.read<Sequence>();
	
	/* Get the source client's state: */
	RemoteClientMap::Iterator rcIt=remoteClientMap.findEntry(sourceClientId);
	if(!rcIt.isFinished())
		{
		/* Hand the audio packet to the remote client: */
		rcIt->getDest()->enqueuePacket(client->getDispatcher().getCurrentTime(),sequenceNumber,message.getBuffer());
		}
	}

void VruiAgoraClient::captureSetupFailed(void)
	{
	/* Show an error message: */
	Misc::userError("VruiAgora: Unable to capture local audio");
	
	/* Send a capture setup failed message to the front end: */
	MessageWriter captureSetupFailedNotification(MessageBuffer::create(serverMessageBase+CaptureSetupFailedNotification,0));
	client->queueFrontendMessage(captureSetupFailedNotification.getBuffer());
	}

void VruiAgoraClient::encodeAndSendAudioPacket(const VruiAgoraProtocol::Sample* audioPacketBuffer)
	{
	/* Create a new audio packet message: */
	{
	const size_t maxAudioPacketSize=4000; // Recommendation from Opus docs
	MessageWriter audioPacketMessage(AudioPacketMsg::createMessage(clientMessageBase+AudioPacketRequest,maxAudioPacketSize));
	
	/* Reserve room for the packet header: */
	audioPacketMessage.advanceWritePtr(sizeof(ClientID)+sizeof(Sequence)+sizeof(Misc::UInt16));
	
	/* Encode the captured audio packet: */
	int encodedSize=opus_encode(captureEncoder,audioPacketBuffer,capturePacketSize,reinterpret_cast<unsigned char*>(audioPacketMessage.getWritePtr()),maxAudioPacketSize);
	if(encodedSize>1) // Packets of one byte indicate silence and do not need to be transmitted
		{
		audioPacketMessage.advanceWritePtr(encodedSize);
		audioPacketMessage.finishMessage();
		
		/* Write the audio packet header: */
		audioPacketMessage.rewind();
		audioPacketMessage.write(ClientID(0));
		audioPacketMessage.write(captureSequence);
		audioPacketMessage.write(Misc::UInt16(encodedSize));
		
		/* Hand the audio packet to the communication thread: */
		if(client->haveUDP())
			client->queueServerUDPMessage(audioPacketMessage.getBuffer());
		else
			client->queueServerMessage(audioPacketMessage.getBuffer());
		}
	else if(encodedSize<0)
		Misc::formattedLogWarning("VruiAgora: Error %d (%s) while encoding an audio packet",encodedSize,opus_strerror(encodedSize));
	}
	
	/* Advance the audio packet sequence number: */
	++captureSequence;
	}

void VruiAgoraClient::captureReadCallback(pa_stream* stream,size_t nbytes,void* userData)
	{
	VruiAgoraClient* thisPtr=static_cast<VruiAgoraClient*>(userData);
	
	/* Read all data available on the stream: */
	const void* readBuffer;
	size_t readSize=(thisPtr->captureEnd-thisPtr->capturePtr)*sizeof(Sample);
	pa_stream_peek(thisPtr->captureStream,&readBuffer,&readSize);
	const Sample* rbPtr=static_cast<const Sample*>(readBuffer);
	readSize/=sizeof(Sample);
	while(readSize>0)
		{
		/* Copy some data: */
		size_t copySize=(thisPtr->captureEnd-thisPtr->capturePtr);
		if(copySize>readSize)
			copySize=readSize;
		memcpy(thisPtr->capturePtr,rbPtr,copySize*sizeof(Sample));
		thisPtr->capturePtr+=copySize;
		rbPtr+=copySize;
		readSize-=copySize;
		
		/* Check if the capture buffer was completed: */
		if(thisPtr->capturePtr==thisPtr->captureEnd)
			{
			#if LATENCYTEST
			if(thisPtr->wavFile->getNumAudioFrames()<thisPtr->captureFrequency*2)
				{
				/* Zero out the capture buffer: */
				memset(thisPtr->captureBuffer,0,thisPtr->capturePacketSize*sizeof(Sample));
				
				if(thisPtr->wavFile->getNumAudioFrames()==thisPtr->captureFrequency*2-thisPtr->capturePacketSize)
					{
					Misc::logNote("Click!");
					thisPtr->captureBuffer[thisPtr->capturePacketSize-1]=Sample(-32767);
					}
				}
			else if(thisPtr->wavFile->getNumAudioFrames()==thisPtr->captureFrequency*2)
				thisPtr->captureBuffer[0]=Sample(32767);
			
			/* Write the capture buffer to the WAV file: */
			thisPtr->wavFile->writeAudioFrames(thisPtr->captureBuffer,thisPtr->capturePacketSize);
			#endif
			
			/* Encode and send the captured audio packet to the server: */
			thisPtr->encodeAndSendAudioPacket(thisPtr->captureBuffer);
			
			/* Reset the capture buffer: */
			thisPtr->capturePtr=thisPtr->captureBuffer;
			}
		}
	
	/* Mark the current capture fragment as complete: */
	pa_stream_drop(thisPtr->captureStream);
	}

void VruiAgoraClient::captureSourceInfoCallback(pa_context* context,const pa_source_info* info,int eol,void* userData)
	{
	VruiAgoraClient* thisPtr=static_cast<VruiAgoraClient*>(userData);
	
	if(info!=0)
		{
		/* Store some relevant information: */
		thisPtr->captureSourceNumChannels=info->volume.channels;
		
		/* Send a source volume notification to the front end: */
		{
		MessageWriter sourceVolumeNotification(MessageBuffer::create(thisPtr->serverMessageBase+SourceVolumeNotification,4*sizeof(Misc::UInt32)));
		sourceVolumeNotification.write(Misc::UInt32(PA_VOLUME_MUTED));
		sourceVolumeNotification.write(Misc::UInt32(PA_VOLUME_NORM));
		sourceVolumeNotification.write(Misc::UInt32(info->n_volume_steps));
		sourceVolumeNotification.write(Misc::UInt32(pa_cvolume_avg(&info->volume)));
		thisPtr->client->queueFrontendMessage(sourceVolumeNotification.getBuffer());
		}
		}
	}

void VruiAgoraClient::captureContextSubscriptionCallback(pa_context* context,pa_subscription_event_type eventType,uint32_t index,void* userData)
	{
	VruiAgoraClient* thisPtr=static_cast<VruiAgoraClient*>(userData);
	
	/* Check for source events: */
	if((eventType&PA_SUBSCRIPTION_EVENT_FACILITY_MASK)==PA_SUBSCRIPTION_EVENT_SOURCE)
		{
		/* Check for property change events on our capture source: */
		if(index==thisPtr->captureSourceIndex&&(eventType&PA_SUBSCRIPTION_EVENT_TYPE_MASK)==PA_SUBSCRIPTION_EVENT_CHANGE)
			{
			/* Query the source for information, to get current volume and volume range: */
			pa_operation_unref(pa_context_get_source_info_by_index(thisPtr->captureContext,thisPtr->captureSourceIndex,&VruiAgoraClient::captureSourceInfoCallback,thisPtr));
			}
		}
	}

void VruiAgoraClient::captureStreamStateCallback(pa_stream* stream,void* userData)
	{
	VruiAgoraClient* thisPtr=static_cast<VruiAgoraClient*>(userData);
	
	/* Check the stream's new state: */
	if(pa_stream_get_state(stream)==PA_STREAM_READY)
		{
		/* Retrieve the index of the PulseAudio source to which the stream is connected: */
		thisPtr->captureSourceIndex=pa_stream_get_device_index(stream);
		
		/* Query the source for information, to get current volume and volume range: */
		pa_operation_unref(pa_context_get_source_info_by_index(thisPtr->captureContext,thisPtr->captureSourceIndex,&VruiAgoraClient::captureSourceInfoCallback,thisPtr));
		
		/* Register a callback to get notified when the source's volume is changed by someone else: */
		pa_context_set_subscribe_callback(thisPtr->captureContext,&VruiAgoraClient::captureContextSubscriptionCallback,thisPtr);
		pa_operation_unref(pa_context_subscribe(thisPtr->captureContext,PA_SUBSCRIPTION_MASK_SOURCE,0,0));
		
		/* Read encoder settings from configuration section: */
		std::string opusMode=thisPtr->vruiAgoraConfig.retrieveString("./encoderMode","Voice");
		int opusApplicationMode=OPUS_APPLICATION_VOIP;
		if(opusMode=="Audio")
			opusApplicationMode=OPUS_APPLICATION_AUDIO;
		else if(opusMode=="LowDelay")
			opusApplicationMode=OPUS_APPLICATION_RESTRICTED_LOWDELAY;
		
		/* Create the Opus audio encoder: */
		int opusError;
		thisPtr->captureEncoder=opus_encoder_create(thisPtr->captureFrequency,1,opusApplicationMode,&opusError);
		if(opusError==OPUS_OK)
			{
			/* Set the Opus encoder bitrate: */
			opus_encoder_ctl(thisPtr->captureEncoder,OPUS_SET_BITRATE(thisPtr->vruiAgoraConfig.retrieveValue<int>("./encoderBitrate",32000)));
			
			/* Set the Opus encoder complexity: */
			opus_encoder_ctl(thisPtr->captureEncoder,OPUS_SET_COMPLEXITY(thisPtr->vruiAgoraConfig.retrieveValue<int>("./encoderComplexity",1)));
			
			/* Create a capture buffer: */
			thisPtr->captureBuffer=new Sample[thisPtr->capturePacketSize];
			thisPtr->captureEnd=thisPtr->captureBuffer+thisPtr->capturePacketSize;
			thisPtr->capturePtr=thisPtr->captureBuffer;
			
			/* Audio capture is up and running: */
			thisPtr->state=StreamConnected;
			}
		else
			{
			/* Shut down audio capture: */
			thisPtr->state=StreamDisconnecting;
			pa_stream_disconnect(thisPtr->captureStream);
			
			/* Notify the front end: */
			thisPtr->captureSetupFailed();
			}
		}
	else if(pa_stream_get_state(stream)==PA_STREAM_TERMINATED)
		{
		if(thisPtr->captureEncoder!=0)
			{
			/* Destroy the Opus audio encoder: */
			opus_encoder_destroy(thisPtr->captureEncoder);
			}
		thisPtr->captureEncoder=0;
		
		/* Release the capture buffer: */
		delete[] thisPtr->captureBuffer;
		thisPtr->captureBuffer=0;
		
		/* Release the stream and disconnect the context: */
		pa_stream_unref(thisPtr->captureStream);
		thisPtr->captureStream=0;
		if(thisPtr->state<ContextDisconnecting)
			{
			thisPtr->state=ContextDisconnecting;
			pa_context_disconnect(thisPtr->captureContext);
			}
		}
	else if(pa_stream_get_state(stream)==PA_STREAM_FAILED)
		{
		/* Release the stream and disconnect the context: */
		pa_stream_unref(thisPtr->captureStream);
		thisPtr->captureStream=0;
		if(thisPtr->state<ContextDisconnecting)
			{
			thisPtr->state=ContextDisconnecting;
			pa_context_disconnect(thisPtr->captureContext);
			}
		
		/* Notify the front end: */
		thisPtr->captureSetupFailed();
		}
	}

void VruiAgoraClient::connectStream(const char* sourceName)
	{
	/* Define capture buffer attributes to minimize latency: */
	pa_buffer_attr bufferAttrs;
	size_t periodBytes=capturePacketSize*1*sizeof(Sample);
	bufferAttrs.fragsize=periodBytes;
	bufferAttrs.maxlength=periodBytes;
	bufferAttrs.prebuf=0;
	bufferAttrs.minreq=Misc::UInt32(-1);
	bufferAttrs.tlength=Misc::UInt32(-1);
	
	/* Connect the capture stream to the requested PulseAudio source: */
	pa_stream_flags_t flags=PA_STREAM_ADJUST_LATENCY;
	if(capturePaused)
		flags=pa_stream_flags_t(flags|PA_STREAM_START_CORKED);
	if(pa_stream_connect_record(captureStream,sourceName,&bufferAttrs,flags)>=0)
		state=StreamConnecting;
	else
		{
		/* Disconnect the context: */
		#if 0
		pa_stream_unref(captureStream);
		captureStream=0;
		#endif
		state=ContextDisconnecting;
		pa_context_disconnect(captureContext);
		
		/* Notify the front end: */
		captureSetupFailed();
		}
	}

void VruiAgoraClient::captureSourceInfoListCallback(pa_context* context,const pa_source_info* info,int eol,void* userData)
	{
	VruiAgoraClient* thisPtr=static_cast<VruiAgoraClient*>(userData);
	
	if(thisPtr->state==SearchingSource)
		{
		if(info!=0)
			{
			/* Check if this source's description matches the requested one's: */
			if(thisPtr->captureSourceName==info->description)
				{
				/* Connect to the found source: */
				thisPtr->connectStream(info->name);
				}
			}
		else if(eol)
			{
			/* Show an error message and disable audio capture: */
			Misc::formattedUserError("VruiAgora: Capture source \"%s\" not found",thisPtr->captureSourceName.c_str());
			
			/* Disconnect the context: */
			#if 0
			pa_stream_unref(thisPtr->captureStream);
			thisPtr->captureStream=0;
			#endif
			thisPtr->state=ContextDisconnecting;
			pa_context_disconnect(thisPtr->captureContext);
			
			/* Notify the front end: */
			thisPtr->captureSetupFailed();
			}
		}
	}

void VruiAgoraClient::captureContextStateCallback(pa_context* context,void* userData)
	{
	VruiAgoraClient* thisPtr=static_cast<VruiAgoraClient*>(userData);
	
	/* Check the context's new state: */
	if(pa_context_get_state(context)==PA_CONTEXT_READY)
		{
		bool ok=true;
		if(ok)
			{
			/* Set the capture sample format: */
			pa_sample_spec sampleSpec;
			sampleSpec.format=PA_SAMPLE_S16LE;
			sampleSpec.rate=thisPtr->captureFrequency;
			sampleSpec.channels=1;
			
			/* Create the capture stream: */
			thisPtr->captureStream=pa_stream_new(context,"Capture",&sampleSpec,0);
			ok=thisPtr->captureStream!=0;
			}
		if(ok)
			{
			pa_stream_set_state_callback(thisPtr->captureStream,&VruiAgoraClient::captureStreamStateCallback,thisPtr);
			pa_stream_set_read_callback(thisPtr->captureStream,&VruiAgoraClient::captureReadCallback,thisPtr);
			}
		if(ok)
			{
			/* Check if a named capture source was requested: */
			if(thisPtr->captureSourceName!="Default")
				{
				/* List all existing capture sources to find the requested one: */
				thisPtr->state=SearchingSource;
				pa_operation_unref(pa_context_get_source_info_list(context,&VruiAgoraClient::captureSourceInfoListCallback,thisPtr));
				}
			else
				{
				/* Connect to the default source: */
				thisPtr->connectStream(0);
				}
			}
		
		/* Clean up if things went south: */
		if(!ok)
			{
			/* Disconnect the context: */
			#if 0
			if(thisPtr->captureStream!=0)
				pa_stream_unref(thisPtr->captureStream);
			thisPtr->captureStream=0;
			#endif
			thisPtr->state=ContextDisconnecting;
			pa_context_disconnect(context);
			
			/* Notify the front end: */
			thisPtr->captureSetupFailed();
			}
		}
	else if(pa_context_get_state(context)==PA_CONTEXT_TERMINATED)
		{
		/* Release the context: */
		pa_context_unref(thisPtr->captureContext);
		thisPtr->captureContext=0;
		
		/* Terminate the main loop: */
		pa_threaded_mainloop_get_api(thisPtr->captureMainLoop)->quit(pa_threaded_mainloop_get_api(thisPtr->captureMainLoop),0);
		thisPtr->state=MainLoopTerminating;
		}
	else if(pa_context_get_state(context)==PA_CONTEXT_FAILED)
		{
		/* Terminate the main loop: */
		pa_threaded_mainloop_get_api(thisPtr->captureMainLoop)->quit(pa_threaded_mainloop_get_api(thisPtr->captureMainLoop),1);
		thisPtr->state=MainLoopTerminating;
		
		/* Notify the front end: */
		thisPtr->captureSetupFailed();
		}
	}

/****************
Helper functions:
****************/

namespace {

template <class SourceSampleParam>
inline
void
adaptSamples(
	const SourceSampleParam* sourceSamples,
	int sourceNumSamples,
	int sourceNumChannels,
	Misc::SInt16* destSamples,
	int destNumSamples)
	{
	}

template <>
inline
void
adaptSamples<Misc::UInt8>(
	const Misc::UInt8* sourceSamples,
	int sourceNumSamples,
	int sourceNumChannels,
	Misc::SInt16* destSamples,
	int destNumSamples)
	{
	/* Resample, downmix, and bit-shift the source audio: */
	for(int i=0;i<destNumSamples;++i)
		{
		int j=(i*sourceNumSamples+destNumSamples/2)/destNumSamples;
		const Misc::UInt8* sPtr=sourceSamples+(j*sourceNumChannels);
		int sum=0;
		for(int k=0;k<sourceNumChannels;++k,++sPtr)
			sum+=int(*sPtr);
		sum=(sum+sourceNumChannels/2)/sourceNumChannels-128;
		sum=(sum<<8)|sum;
		destSamples[i]=Misc::SInt16(sum);
		}
	}

template <>
inline
void
adaptSamples<Misc::SInt16>(
	const Misc::SInt16* sourceSamples,
	int sourceNumSamples,
	int sourceNumChannels,
	Misc::SInt16* destSamples,
	int destNumSamples)
	{
	/* Resample and downmix the source audio: */
	for(int i=0;i<destNumSamples;++i)
		{
		int j=(i*sourceNumSamples+destNumSamples/2)/destNumSamples;
		const Misc::SInt16* sPtr=sourceSamples+(j*sourceNumChannels);
		int sum=0;
		for(int k=0;k<sourceNumChannels;++k,++sPtr)
			sum+=int(*sPtr);
		sum=(sum+sourceNumChannels/2)/sourceNumChannels;
		destSamples[i]=Misc::SInt16(sum);
		}
	}

}

void* VruiAgoraClient::injectionThreadMethod(void)
	{
	/* Retrieve the WAV file's sound data format: */
	const Sound::SoundDataFormat& sourceFormat=injectionFile->getFormat();
	
	/* Retrieve the WAV file's total number of audio frames: */
	size_t sourceNumFrames=injectionFile->getNumAudioFrames();
	
	/* Calculate the size of periods that need to be read from the WAV file: */
	size_t sourcePeriodSize=(size_t(capturePacketSize)*size_t(sourceFormat.framesPerSecond)+size_t(captureFrequency)/2)/size_t(captureFrequency);
	
	/* Create a reading and an encoding source buffer: */
	void* sourceBuffer=malloc(sourcePeriodSize*sourceFormat.samplesPerFrame*sourceFormat.bytesPerSample);
	Sample* encodingBuffer=new Sample[capturePacketSize];
	
	/* Create a timer to send audio packets at regular intervals: */
	TimeStamp nextSendTs=now();
	TimeStamp periodTs=TimeStamp(((long(capturePacketSize)*1000000L)+long(captureFrequency)/2)/long(captureFrequency));
	
	/* Inject all complete periods of audio from the WAV file: */
	while(keepInjectionThreadRunning&&sourceNumFrames>=sourcePeriodSize)
		{
		/* Read the next period of audio from the WAV file: */
		injectionFile->readAudioFrames(sourceBuffer,sourcePeriodSize);
		
		/* Resample/downmix/bit-shift/etc. the source period into the encoding buffer: */
		if(sourceFormat.bitsPerSample==8&&!sourceFormat.signedSamples)
			adaptSamples(static_cast<const Misc::UInt8*>(sourceBuffer),sourcePeriodSize,sourceFormat.samplesPerFrame,encodingBuffer,capturePacketSize);
		else if(sourceFormat.bitsPerSample==16&&sourceFormat.signedSamples)
			adaptSamples(static_cast<const Misc::SInt16*>(sourceBuffer),sourcePeriodSize,sourceFormat.samplesPerFrame,encodingBuffer,capturePacketSize);
		
		/* Encode and send the read audio packet to the server: */
		encodeAndSendAudioPacket(encodingBuffer);
		
		/* Sleep until the next audio packet is due: */
		nextSendTs+=periodTs;
		usleep(TimeStamp(nextSendTs-now()));
		
		sourceNumFrames-=sourcePeriodSize;
		}
	
	/* Notify the front end that injection is done: */
	{
	MessageWriter musicInjectionDoneNotification(MessageBuffer::create(serverMessageBase+MusicInjectionDoneNotification,0));
	client->queueFrontendMessage(musicInjectionDoneNotification.getBuffer());
	}
	
	/* Clean up and return: */
	free(sourceBuffer);
	delete[] encodingBuffer;
	return 0;
	}

VruiAgoraClient::RemoteClient* VruiAgoraClient::getSelectedClient(void)
	{
	/* Access the remote client currently selected in the list box and bail out if there is none: */
	VruiCoreClient::RemoteClient* vcClient=vruiCore->getSelectedRemoteClient();
	if(vcClient==0)
		return 0;
	
	/* Find the Vrui Agora client representing the same client: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if((*rcIt)->vcClient==vcClient)
			return *rcIt;
	
	return 0;
	}

void VruiAgoraClient::muteMicrophoneValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	/* Set the paused flag: */
	capturePaused=cbData->set;
	
	if(state==StreamConnected)
		{
		/* Cork or uncork the audio capture stream: */
		pa_threaded_mainloop_lock(captureMainLoop);
		pa_operation_unref(pa_stream_cork(captureStream,capturePaused?1:0,0,0));
		pa_threaded_mainloop_unlock(captureMainLoop);
		}
	}

void VruiAgoraClient::microphoneVolumeValueChangedCallback(GLMotif::Slider::ValueChangedCallbackData* cbData)
	{
	/* Create a volume structure: */
	pa_cvolume volume;
	pa_cvolume_init(&volume);
	pa_cvolume_set(&volume,captureSourceNumChannels,pa_volume_t(cbData->value+0.5));
	
	/* Set the capture source's volume: */
	pa_threaded_mainloop_lock(captureMainLoop);
	pa_operation_unref(pa_context_set_source_volume_by_index(captureContext,captureSourceIndex,&volume,0,0));
	pa_threaded_mainloop_unlock(captureMainLoop);
	}

void VruiAgoraClient::injectWAVFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData)
	{
	try
		{
		/* Open the WAV file: */
		injectionFile=new Sound::WAVFile(cbData->selectedDirectory->openFile(cbData->selectedFileName));
		}
	catch(std::runtime_error& err)
		{
		/* Show an error message and bail out: */
		Misc::formattedUserError("VruiAgora: Unable to inject music file %s due to exception %s",cbData->getSelectedPath().c_str());
		return;
		}
	
	/* Check if the WAV file's format is compatible with injection: */
	const Sound::SoundDataFormat& sourceFormat=injectionFile->getFormat();
	if((sourceFormat.bitsPerSample==8&&!sourceFormat.signedSamples&&sourceFormat.bytesPerSample==sizeof(Misc::UInt8))
	   ||(sourceFormat.bitsPerSample==16&&sourceFormat.signedSamples&&sourceFormat.bytesPerSample==sizeof(Misc::SInt16)))
		{
		/* Check if audio capture is active: */
		if(state==StreamConnected)
			{
			/* Pause audio capture if it isn't already: */
			if(!capturePaused)
				{
				/* Set the mute button: */
				muteMicrophoneToggle->setToggle(true);
				capturePaused=true;
				
				/* Cork the audio capture stream: */
				pa_threaded_mainloop_lock(captureMainLoop);
				pa_operation_unref(pa_stream_cork(captureStream,1,0,0));
				pa_threaded_mainloop_unlock(captureMainLoop);
				}
			
			/* Disable the mute button and the music injection button: */
			muteMicrophoneToggle->setEnabled(false);
			injectMusicButton->setEnabled(false);
			}
		
		/* Start the music injection thread: */
		keepInjectionThreadRunning=true;
		injectionThread.start(this,&VruiAgoraClient::injectionThreadMethod);
		}
	else
		{
		/* Show an error message and close the WAV file: */
		Misc::formattedUserError("VruiAgora: Unable to inject music file %s due to incompatible sample format",cbData->getSelectedPath().c_str());
		delete injectionFile;
		injectionFile=0;
		}
	}

void VruiAgoraClient::remoteClientListValueChangedCallback(GLMotif::ListBox::ValueChangedCallbackData* cbData)
	{
	RemoteClient* rc=getSelectedClient();
	
	/* Enable or disable per-client widgets: */
	jitterBufferSizeSlider->setEnabled(rc!=0);
	muteClientToggle->setEnabled(rc!=0);
	clientVolumeSlider->setEnabled(rc!=0);
	
	if(rc!=0)
		{
		/* Update per-client settings: */
		jitterBufferSizeSlider->setValue(rc->jitterBufferSizeRequest);
		muteClientToggle->setToggle(rc->muted);
		clientVolumeSlider->setValue(rc->gain>0.0f?10.0*Math::log(double(rc->gain))/Math::log(10.0):-30.0);
		}
	else
		{
		/* Reset per-client setting widgets: */
		jitterBufferSizeSlider->setValue(1);
		muteClientToggle->setToggle(false);
		clientVolumeSlider->setValue(0.0);
		}
	}

void VruiAgoraClient::jitterBufferSizeValueChangedCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData)
	{
	RemoteClient* rc=getSelectedClient();
	if(rc!=0)
		{
		/* Request a new jitter buffer size: */
		rc->jitterBufferSizeRequest=(unsigned int)(Math::floor(cbData->value+0.5));
		}
	}

void VruiAgoraClient::muteClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	RemoteClient* rc=getSelectedClient();
	if(rc!=0)
		{
		if(rc->muted!=cbData->set)
			{
			/* Send a mute client request to the server: */
			{
			MessageWriter muteClientRequest(MuteClientRequestMsg::createMessage(clientMessageBase));
			muteClientRequest.write(MessageID(rc->vcClient->getId()));
			muteClientRequest.write(cbData->set?Bool(1):Bool(0));
			// muteClientRequest.write(Bool(1)); // Be passive-aggressive about it, why not?
			muteClientRequest.write(Bool(0)); // Let's be polite and not notify the remote client
			client->queueServerMessage(muteClientRequest.getBuffer());
			}
			
			rc->muted=cbData->set;
			}
		}
	}

void VruiAgoraClient::clientVolumeValueChangedCallback(GLMotif::Slider::ValueChangedCallbackData* cbData)
	{
	RemoteClient* rc=getSelectedClient();
	if(rc!=0)
		{
		/* Update the remote client's gain factor: */
		rc->gain=float(cbData->value>-30.0?Math::pow(10.0,cbData->value/10.0):0.0);
		}
	}

void VruiAgoraClient::createSettingsPage(void)
	{
	const GLMotif::StyleSheet& ss=*Vrui::getUiStyleSheet();
	
	vruiCore->getSettingsPager()->setNextPageName("Agora");
	
	GLMotif::Margin* vruiAgoraSettingsMargin=new GLMotif::Margin("VruiAgoraSettingsMargin",vruiCore->getSettingsPager(),false);
	vruiAgoraSettingsMargin->setAlignment(GLMotif::Alignment(GLMotif::Alignment::HFILL,GLMotif::Alignment::TOP));
	
	GLMotif::RowColumn* vruiAgoraSettingsPanel=new GLMotif::RowColumn("VruiAgoraSettingsPanel",vruiAgoraSettingsMargin,false);
	vruiAgoraSettingsPanel->setOrientation(GLMotif::RowColumn::VERTICAL);
	vruiAgoraSettingsPanel->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	vruiAgoraSettingsPanel->setNumMinorWidgets(1);
	
	/* Create widgets for plug-in-wide settings: */
	
	new GLMotif::Label("MicrophoneMuteVolumeLabel",vruiAgoraSettingsPanel,"Microphone Mute / Volume");
	
	GLMotif::RowColumn* muteVolumeBox=new GLMotif::RowColumn("MuteVolumeBox",vruiAgoraSettingsPanel,false);
	muteVolumeBox->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	muteVolumeBox->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	muteVolumeBox->setNumMinorWidgets(1);
	
	muteMicrophoneToggle=new GLMotif::ToggleButton("MuteMicrophoneToggle",muteVolumeBox,"");
	muteMicrophoneToggle->setBorderWidth(0.0f);
	muteMicrophoneToggle->setBorderType(GLMotif::Widget::PLAIN);
	muteMicrophoneToggle->setHAlignment(GLFont::Left);
	muteMicrophoneToggle->setToggle(capturePaused);
	muteMicrophoneToggle->getValueChangedCallbacks().add(this,&VruiAgoraClient::muteMicrophoneValueChangedCallback);
	
	microphoneVolumeSlider=new GLMotif::Slider("MicrophoneVolumeSlider",muteVolumeBox,GLMotif::Slider::HORIZONTAL,ss.fontHeight*10.0f);
	microphoneVolumeSlider->setValueRange(0.0,100.0,1.0);
	microphoneVolumeSlider->setValue(50.0);
	microphoneVolumeSlider->getValueChangedCallbacks().add(this,&VruiAgoraClient::microphoneVolumeValueChangedCallback);
	microphoneVolumeSlider->setEnabled(false);
	
	muteVolumeBox->setColumnWeight(1,1.0f);
	muteVolumeBox->manageChild();
	
	injectMusicButton=new GLMotif::Button("InjectMusicButton",vruiAgoraSettingsPanel,"Inject Music File...");
	wavHelper.addLoadCallback(injectMusicButton,Misc::createFunctionCall(this,&VruiAgoraClient::injectWAVFileCallback));
	
	/* Insert a separator for per-client settings: */
	GLMotif::RowColumn* perClientSeparator=new GLMotif::RowColumn("PerClientSeparator",vruiAgoraSettingsPanel,false);
	perClientSeparator->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	perClientSeparator->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	perClientSeparator->setNumMinorWidgets(1);
	
	new GLMotif::Separator("Sep1",perClientSeparator,GLMotif::Separator::HORIZONTAL,ss.fontHeight*2.0f,GLMotif::Separator::LOWERED);
	new GLMotif::Label("Label",perClientSeparator,"Per-Client Settings");
	new GLMotif::Separator("Sep2",perClientSeparator,GLMotif::Separator::HORIZONTAL,ss.fontHeight*2.0f,GLMotif::Separator::LOWERED);
	
	perClientSeparator->setColumnWeight(0,1.0f);
	perClientSeparator->setColumnWeight(2,1.0f);
	perClientSeparator->manageChild();
	
	/* Create widgets for per-client settings: */
	new GLMotif::Label("JitterBufferSizeLabel",vruiAgoraSettingsPanel,"Jitter Buffer Size");
	
	jitterBufferSizeSlider=new GLMotif::TextFieldSlider("JitterBufferSize",vruiAgoraSettingsPanel,3,ss.fontHeight*5.0f);
	jitterBufferSizeSlider->setSliderMapping(GLMotif::TextFieldSlider::LINEAR);
	jitterBufferSizeSlider->setValueType(GLMotif::TextFieldSlider::UINT);
	jitterBufferSizeSlider->setValueRange(1.0,20.0,1.0);
	jitterBufferSizeSlider->setValue(2);
	jitterBufferSizeSlider->getValueChangedCallbacks().add(this,&VruiAgoraClient::jitterBufferSizeValueChangedCallback);
	jitterBufferSizeSlider->setEnabled(false);
	
	new GLMotif::Label("ClientMuteVolumeLabel",vruiAgoraSettingsPanel,"Client Mute / Volume (dB)");
	
	GLMotif::RowColumn* clientMuteVolumeBox=new GLMotif::RowColumn("ClientMuteVolumeBox",vruiAgoraSettingsPanel,false);
	clientMuteVolumeBox->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	clientMuteVolumeBox->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	clientMuteVolumeBox->setNumMinorWidgets(1);
	
	muteClientToggle=new GLMotif::ToggleButton("MuteClientToggle",clientMuteVolumeBox,"");
	muteClientToggle->setBorderWidth(0.0f);
	muteClientToggle->setBorderType(GLMotif::Widget::PLAIN);
	muteClientToggle->setHAlignment(GLFont::Left);
	muteClientToggle->setToggle(false);
	muteClientToggle->getValueChangedCallbacks().add(this,&VruiAgoraClient::muteClientValueChangedCallback);
	muteClientToggle->setEnabled(false);
	
	clientVolumeSlider=new GLMotif::Slider("ClientVolumeSlider",clientMuteVolumeBox,GLMotif::Slider::HORIZONTAL,ss.fontHeight*10.0f);
	clientVolumeSlider->setValueRange(-30.0,10.0,0.1); // Logarithmic scale in dB
	clientVolumeSlider->setValue(0.0);
	clientVolumeSlider->addNotch(0.0);
	clientVolumeSlider->getValueChangedCallbacks().add(this,&VruiAgoraClient::clientVolumeValueChangedCallback);
	clientVolumeSlider->setEnabled(false);
	
	clientMuteVolumeBox->setColumnWeight(1,1.0f);
	clientMuteVolumeBox->manageChild();
	
	vruiAgoraSettingsPanel->manageChild();
	
	vruiAgoraSettingsMargin->manageChild();
	}

VruiAgoraClient::VruiAgoraClient(Client* sClient)
	:VruiPluginClient(sClient),
	 vruiCore(VruiCoreClient::requestClient(client)),
	 vruiAgoraConfig(vruiCore->getProtocolConfig(protocolName)),
	 captureFrequency(48000U),
	 captureMouthPos(Point::origin),
	 state(Created),
	 captureMainLoop(0),captureContext(0),captureStream(0),
	 captureBuffer(0),captureEncoder(0),captureSequence(0),
	 capturePaused(false),
	 injectionFile(0),keepInjectionThreadRunning(false),
	 remoteClientMap(17),
	 muteMicrophoneToggle(0),
	 wavHelper(Vrui::getWidgetManager(),"",".wav"),
	 injectMusicButton(0)
	{
	/* Request OpenAL sound processing from Vrui: */
	Vrui::requestSound();
	
	/* Access the Agora configuration file section: */
	captureFrequency=vruiAgoraConfig.retrieveValue<unsigned int>("./captureFrequency",captureFrequency);
	capturePacketSize=(captureFrequency*vruiAgoraConfig.retrieveValue<unsigned int>("./capturePeriod",10U)+500U)/1000U;
	captureMouthPos=vruiAgoraConfig.retrieveValue<Point>("./captureMouthPos",captureMouthPos);
	capturePaused=vruiAgoraConfig.retrieveValue<bool>("./muteCapture",capturePaused);
	
	#if LATENCYTEST
	std::string wavFileName="RecordedAudio-";
	wavFileName.append(client->getClientName());
	wavFileName.append(".wav");
	Sound::SoundDataFormat format;
	format.setStandardSampleFormat(16,true,Sound::SoundDataFormat::LittleEndian);
	format.samplesPerFrame=1;
	format.framesPerSecond=captureFrequency;
	wavFile=new Sound::WAVFile(IO::openFile(wavFileName.c_str(),IO::File::WriteOnly),format);
	#endif
	}

VruiAgoraClient::~VruiAgoraClient(void)
	{
	/* Delete all remote client representations: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		delete *rcIt;
	
	#if LATENCYTEST
	delete wavFile;
	#endif
	}

const char* VruiAgoraClient::getName(void) const
	{
	return protocolName;
	}

unsigned int VruiAgoraClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int VruiAgoraClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int VruiAgoraClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void VruiAgoraClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Add front-end message handlers to handle signals from the back end: */
	client->setFrontendMessageHandler(serverMessageBase+CaptureSetupFailedNotification,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::captureSetupFailedNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+SourceVolumeNotification,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::sourceVolumeNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+MusicInjectionDoneNotification,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::musicInjectionDoneNotificationCallback>,this);
	client->setFrontendMessageHandler(serverMessageBase+ConnectNotification,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::frontendConnectNotificationCallback>,this);
	
	/* Register forwarded front-end message handlers: */
	client->setMessageForwarder(serverMessageBase+MuteClientNotification,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::frontendMuteClientNotificationCallback>,this,MuteClientNotificationMsg::size);
	
	/* Register back-end message handlers: */
	client->setTCPMessageHandler(serverMessageBase+ConnectNotification,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::backendConnectNotificationCallback>,this,ConnectNotificationMsg::size);
	client->setTCPMessageHandler(serverMessageBase+AudioPacketReply,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::audioPacketReplyCallback>,this,AudioPacketMsg::size);
	client->setUDPMessageHandler(serverMessageBase+AudioPacketReply,Client::wrapMethod<VruiAgoraClient,&VruiAgoraClient::udpAudioPacketReplyCallback>,this);
	}

void VruiAgoraClient::start(void)
	{
	/* Register this protocol with Vrui Core: */
	vruiCore->registerDependentProtocol(this);
	}

void VruiAgoraClient::clientConnected(unsigned int clientId)
	{
	/* Do nothing; back end will create remote client structure and enter it into map when connect notification message is received */
	}

void VruiAgoraClient::clientDisconnected(unsigned int clientId)
	{
	/* Stop playback on the disconnected client and remove it from the remote client map: */
	RemoteClientMap::Iterator rcIt=remoteClientMap.findEntry(clientId);
	if(!rcIt.isFinished())
		{
		/* Tell the remote client to stop audio processing: */
		rcIt->getDest()->stopPlayback();
		
		/* Remove the map entry: */
		remoteClientMap.removeEntry(rcIt);
		}
	
	/* Wake up the front end: */
	Vrui::requestUpdate();
	}

void VruiAgoraClient::frontendStart(void)
	{
	/* Send an Agora connect request message to the server: */
	{
	MessageWriter connectRequest(ConnectRequestMsg::createMessage(clientMessageBase));
	connectRequest.write(Misc::UInt32(captureFrequency));
	connectRequest.write(Misc::UInt32(capturePacketSize));
	Misc::write(captureMouthPos,connectRequest);
	client->queueServerMessage(connectRequest.getBuffer());
	}
	
	/* Create a settings page in the Vrui Core protocol client's collaboration dialog: */
	createSettingsPage();
	
	/* Register a callback with Vrui Core's remote client list box: */
	vruiCore->getRemoteClientList()->getValueChangedCallbacks().add(this,&VruiAgoraClient::remoteClientListValueChangedCallback);
	
	/* Start audio capture: */
	bool ok=Vrui::getNumSoundContexts()>0;
	if(ok)
		{
		/* Retrieve the capture device's name from Vrui: */
		captureSourceName=Vrui::getSoundContext(0)->getRecordingDeviceName();
		}
	if(ok)
		{
		/* Create a PulseAudio threaded mainloop: */
		captureMainLoop=pa_threaded_mainloop_new();
		ok=captureMainLoop!=0;
		}
	if(ok)
		{
		/* Create a PulseAudio context: */
		captureContext=pa_context_new(pa_threaded_mainloop_get_api(captureMainLoop),"Vrui Agora");
		ok=captureContext!=0;
		}
	if(ok)
		{
		/* Register a context state change callback: */
		pa_context_set_state_callback(captureContext,&VruiAgoraClient::captureContextStateCallback,this);
		}
	if(ok)
		{
		/* Connect the context to the default PulseAudio server: */
		ok=pa_context_connect(captureContext,0,PA_CONTEXT_NOFLAGS,0)>=0;
		}
	if(ok)
		{
		state=ContextConnecting;
		
		/* Run the mainloop: */
		ok=pa_threaded_mainloop_start(captureMainLoop)>=0;
		}
	if(ok)
		state=MainLoopRunning;
	
	/* Check for errors: */
	if(!ok)
		{
		if(state>=ContextConnecting)
			pa_context_disconnect(captureContext);
		else if(captureContext!=0)
			pa_context_unref(captureContext);
		captureContext=0;
		if(captureMainLoop!=0)
			pa_threaded_mainloop_free(captureMainLoop);
		captureMainLoop=0;
		state=Created;
		
		/* Show an error message and disable the mute/unmute button: */
		Misc::userError("VruiAgora: Unable to capture local audio");
		muteMicrophoneToggle->setToggle(false);
		muteMicrophoneToggle->setEnabled(false);
		}
	}

void VruiAgoraClient::frontendClientDisconnected(unsigned int clientId)
	{
	/* Retrieve the disconnected client's Vrui Core state: */
	const VruiCoreClient::RemoteClient* vcrc=vruiCore->getRemoteClient(clientId);
	
	/* Find the remote client state representing the same client: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if((*rcIt)->vcClient==vcrc)
			{
			(*rcIt)->vcClient=0;
			
			/* Stop looking: */
			break;
			}
	}

void VruiAgoraClient::frame(void)
	{
	/* Delete all remote clients that have disconnected and are done playing audio: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if((*rcIt)->state==RemoteClient::PlaybackThreadTerminated)
			{
			/* Delete the remote client object and remove it from the list: */
			delete *rcIt;
			*rcIt=remoteClients.back();
			remoteClients.pop_back();
			--rcIt;
			}
	}

void VruiAgoraClient::display(GLContextData& contextData) const
	{
	}

void VruiAgoraClient::sound(ALContextData& contextData) const
	{
	#if ALSUPPORT_CONFIG_HAVE_OPENAL
	
	/* Get the current navigation transformation: */
	const Vrui::NavTransform& nav=Vrui::getNavigationTransformation();
	
	/* Update the source positions of all remote clients that are still connected: */
	for(RemoteClientList::const_iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		if((*rcIt)->vcClient!=0)
			{
			/* Transform the remote client's mouth position to the remote client's physical space: */
			VruiCoreProtocol::Point mouthPos=(*rcIt)->vcClient->getHeadTransform().transform((*rcIt)->mouthPos);
			
			/* Transform the mouth position to shared navigational space: */
			mouthPos=(*rcIt)->vcClient->getNavTransform().inverseTransform(mouthPos);
			
			/* Set the source position transformed to physical coordinates: */
			alSourcePosition((*rcIt)->playbackSource,ALContextData::Point(nav.transform(mouthPos)));
			
			#if 0
			/* Try to mitigate high source latency by slightly compressing audio playback: */
			ALfloat pitch(1);
			if((*rcIt)->sourceLatency<-0.05f)
				pitch=ALfloat(0.95);
			else if((*rcIt)->sourceLatency>0.05f)
				pitch=ALfloat(1.05);
			alSourcePitch((*rcIt)->playbackSource,pitch);
			#endif
			
			/* Set the source's gain factor: */
			alSourceGain((*rcIt)->playbackSource,(*rcIt)->gain);
			
			/* Set the source's distance attenuation parameters (they don't change, but this is the best place to do it): */
			alSourceReferenceDistance((*rcIt)->playbackSource,contextData.getReferenceDistance());
			alSourceRolloffFactor((*rcIt)->playbackSource,contextData.getRolloffFactor());
			}
	
	#endif
	}

void VruiAgoraClient::shutdown(void)
	{
	#if ALSUPPORT_CONFIG_HAVE_OPENAL
	
	/* Tell all remote clients to stop audio processing: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		(*rcIt)->stopPlayback();
	
	/* Wait until all remote clients have stopped audio processing: */
	for(RemoteClientList::iterator rcIt=remoteClients.begin();rcIt!=remoteClients.end();++rcIt)
		(*rcIt)->waitForShutdown();
	
	#endif
	
	/* Shut down the music injection thread if it is running: */
	if(!injectionThread.isJoined())
		{
		keepInjectionThreadRunning=false;
		injectionThread.join();
		}
	
	/* Delete a potentially left over injection file: */
	delete injectionFile;
	injectionFile=0;
	
	if(state>=MainLoopRunning)
		{
		pa_threaded_mainloop_lock(captureMainLoop);
		if(state<StreamDisconnecting)
			{
			/* Disconnect the capture stream to shut down audio capture: */
			state=StreamDisconnecting;
			pa_stream_disconnect(captureStream);
			}
		pa_threaded_mainloop_unlock(captureMainLoop);
		
		/* Wait until the main loop shuts down: */
		while(state<MainLoopTerminating)
			usleep(1000);
		pa_threaded_mainloop_get_retval(captureMainLoop);
		
		/* Stop and release the main loop: */
		pa_threaded_mainloop_stop(captureMainLoop);
		pa_threaded_mainloop_free(captureMainLoop);
		captureMainLoop=0;
		}
	state=Created;
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginClient* createObject(PluginClientLoader& objectLoader,Client* client)
	{
	return new VruiAgoraClient(client);
	}

void destroyObject(PluginClient* object)
	{
	delete object;
	}

}
