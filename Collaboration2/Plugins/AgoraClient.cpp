/***********************************************************************
AgoraClient - Client for real-time audio chat plug-in protocol.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/AgoraClient.h>

#include <string.h>
#include <Misc/Utility.h>
#include <Misc/MessageLogger.h>
#include <pulse/operation.h>

#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/Client.h>

// DEBUGGING
#include <iostream>

#if LATENCYTEST
#include <IO/OpenFile.h>
#include <Sound/SoundDataFormat.h>
#endif

void* AgoraClient::RemoteClient::playbackThreadMethod(void)
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
			Misc::formattedUserError("AgoraClient::RemoteClient: Unable to create Opus audio decoder due to error %d (%s)",opusError,opus_strerror(opusError));
		}
	
	Sample* decoded=0;
	if(ok)
		{
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
		while(state==PlaybackThreadRunning)
			{
			/* Suspend this thread while the playback stream is stopped: */
			bool restartPlayback=false;
			{
			Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
			
			/* Check if the audio packet stream has been interrupted: */
			if(numMissedPackets>=20)
				{
				/* Stop the source: */
				pa_threaded_mainloop_lock(client->captureMainLoop);
				pa_operation_unref(pa_stream_flush(playbackStream,0,0));
				pa_threaded_mainloop_unlock(client->captureMainLoop);
				playbackStopped=true;
				}
			
			/* Wait while the source is stopped: */
			if(playbackStopped)
				Misc::formattedLogNote("AgoraClient::RemoteClient: Suspending audio playback at %fms",double(TimeStamp(now()-timebase))/1.0e3);
			restartPlayback=playbackStopped;
			while(state==PlaybackThreadRunning&&playbackStopped)
				sourceStateCond.wait(sourceStateLock);
			}
			
			/* Bail out if shut down: */
			if(state!=PlaybackThreadRunning)
				break;
			
			if(restartPlayback)
				{
				pa_threaded_mainloop_lock(client->captureMainLoop);
				
				/* Preload the playback stream with numSlots*0.5+minQueuedPeriods periods of silence: */
				size_t preloadSize=(jitterBuffer.getNumSlots()*playbackPacketSize)/2+(minQueuedPeriods*playbackPacketSize)/2;
				while(preloadSize>0)
					{
					void* writeBuffer;
					size_t writeBytes=preloadSize*1*sizeof(Sample);
					if(pa_stream_begin_write(playbackStream,&writeBuffer,&writeBytes)==0)
						{
						/* Zero out the returned buffer: */
						memset(writeBuffer,0,writeBytes);
						
						/* Commit the write: */
						pa_stream_write(playbackStream,writeBuffer,writeBytes,0,0,PA_SEEK_RELATIVE);
						
						/* Continue writing if necessary: */
						preloadSize-=writeBytes/(1*sizeof(Sample));
						}
					else
						{
						// DEBUGGING
						Misc::userError("AgoraClient::RemoteClient: Unable to pre-load playback stream");
						}
					}
				
				// DEBUGGING
				Misc::formattedLogNote("AgoraClient::RemoteClient: Resuming audio playback at %fms",double(TimeStamp(now()-timebase))/1.0e3);
				
				// DEBUGGING
				numMissing=0;
				numDequeued=0;
				
				/* Start playing back audio: */
				numMissedPackets=0;
				
				pa_threaded_mainloop_unlock(client->captureMainLoop);
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
			TimeStamp nowTs=now();
			Misc::formattedLogNote("Dequeue,%d,%f,%f",jitterBuffer.getHeadSequence(),double(TimeStamp(nowTs-timebase))/1.0e3,double(TimeStamp(nowTs-timebase))/1.0e3);
			
			packetBuffer=jitterBuffer.dequeue();
			
			/* Update the buffer latency conditioner: */
			headArrival+=period;
			}
			
			// DEBUGGING
			if(packetBuffer==0)
				++numMissing;
			if(++numDequeued==100)
				{
				Misc::formattedLogNote("AgoraClient::RemoteClient: Packet loss rate %d%%",numMissing);
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
					Misc::logWarning("AgoraClient::RemoteClient: Decoding error");
				
				numMissedPackets=0;
				}
			else
				{
				/* Generate gap filler: */
				int decodeResult=opus_decode(audioDecoder,0,0,decoded,playbackPacketSize,1);
				if(decodeResult!=int(playbackPacketSize))
					Misc::logWarning("AgoraClient::RemoteClient: Packet loss concealment error");
				
				++numMissedPackets;
				}
			
			/* Write the decoded packet to the playback stream: */
			pa_threaded_mainloop_lock(client->captureMainLoop);
			Sample* decodedPtr=decoded;
			size_t decodedSize=playbackPacketSize;
			while(decodedSize>0)
				{
				void* writeBuffer;
				size_t writeBytes=decodedSize*1*sizeof(Sample);
				if(pa_stream_begin_write(playbackStream,&writeBuffer,&writeBytes)==0)
					{
					/* Copy into the returned buffer: */
					memcpy(writeBuffer,decodedPtr,writeBytes);
					
					/* Commit the write: */
					pa_stream_write(playbackStream,writeBuffer,writeBytes,0,0,PA_SEEK_RELATIVE);
					
					/* Continue writing if necessary: */
					decodedPtr+=writeBytes/(1*sizeof(Sample));
					decodedSize-=writeBytes/(1*sizeof(Sample));
					}
				else
					{
					// DEBUGGING
					Misc::userError("AgoraClient::RemoteClient: Unable to write to playback stream");
					}
				}
			pa_threaded_mainloop_unlock(client->captureMainLoop);
			}
		}
	
	/* Delete the audio buffer: */
	delete[] decoded;
	
	if(audioDecoder!=0)
		{
		/* Destroy the Opus decoder: */
		opus_decoder_destroy(audioDecoder);
		}
	
	/* Disconnect the playback stream: */
	pa_threaded_mainloop_lock(client->captureMainLoop);
	pa_stream_disconnect(playbackStream);
	state=StreamDisconnecting;
	pa_threaded_mainloop_unlock(client->captureMainLoop);
	
	return 0;
	}

void AgoraClient::RemoteClient::playbackStreamStateCallback(pa_stream* stream,void* userData)
	{
	RemoteClient* thisPtr=static_cast<RemoteClient*>(userData);
	
	/* Check the stream's new state: */
	if(pa_stream_get_state(stream)==PA_STREAM_READY)
		{
		// DEBUGGING
		Misc::logNote("AgoraClient::RemoteClient::playbackStreamStateCallback: Playback stream is ready; starting playback thread");
		
		thisPtr->state=PlaybackThreadRunning;
		thisPtr->playbackThread.start(thisPtr,&AgoraClient::RemoteClient::playbackThreadMethod);
		}
	else if(pa_stream_get_state(stream)==PA_STREAM_TERMINATED)
		{
		// DEBUGGING
		Misc::logNote("AgoraClient::RemoteClient::playbackStreamStateCallback: Deleting remote client");
		
		/* Release the stream: */
		pa_stream_unref(thisPtr->playbackStream);
		thisPtr->playbackStream=0;
		thisPtr->state=StreamDisconnected;
		
		/* Delete the remote client state object: */
		delete thisPtr;
		}
	else if(pa_stream_get_state(stream)==PA_STREAM_FAILED)
		{
		Misc::userError("AgoraClient::RemoteClient: Unable to connect remote audio playback stream to PulseAudio sink");
		
		/* Release the stream: */
		pa_stream_unref(thisPtr->playbackStream);
		thisPtr->playbackStream=0;
		
		thisPtr->state=Created;
		}
	}

void AgoraClient::RemoteClient::playbackStreamUnderflowCallback(pa_stream* stream,void* userData)
	{
	// RemoteClient* thisPtr=static_cast<RemoteClient*>(userData);
	
	// DEBUGGING
	Misc::logWarning("AgoraClient::RemoteClient: Underflow on playback stream");
	}

AgoraClient::RemoteClient::RemoteClient(AgoraClient* sClient,unsigned int sPlaybackFrequency,unsigned int sPlaybackPacketSize)
	:client(sClient),state(Created),
	 playbackFrequency(sPlaybackFrequency),playbackPacketSize(sPlaybackPacketSize),
	 period(TimeStamp((long(playbackPacketSize)*1000000L+long(playbackFrequency)/2)/long(playbackFrequency))),
	 jitterBuffer(1),minQueuedPeriods(1),
	 arrivalFilterGain(int(0.05*65536.0+0.5)),
	 playbackStream(0),
	 playbackStopped(true)
	{
	// DEBUGGING
	timebase=now();
	
	/* Initialize the buffer latency conditioner: */
	targetLatency=(TimeStamp(jitterBuffer.getNumSlots())*period)/2; // Latency between packet arriving and being dequeued
	
	bool ok=true;
	
	/* Set the playback sample format: */
	pa_sample_spec sampleSpec;
	sampleSpec.format=PA_SAMPLE_S16LE;
	sampleSpec.rate=playbackFrequency;
	sampleSpec.channels=1;
	
	/* Define playback buffer attributes to minimize latency: */
	pa_buffer_attr bufferAttrs;
	size_t periodBytes=playbackPacketSize*1*sizeof(Misc::SInt16);
	bufferAttrs.fragsize=Misc::UInt32(-1);
	bufferAttrs.maxlength=(jitterBuffer.getNumSlots()*periodBytes)/2+(minQueuedPeriods+1)*periodBytes;
	bufferAttrs.prebuf=periodBytes;
	bufferAttrs.minreq=periodBytes;
	bufferAttrs.tlength=minQueuedPeriods*periodBytes;
	
	// DEBUGGING
	Misc::logNote("AgoraClient::RemoteClient: Starting remote audio playback");
	
	/* Create the playback stream: */
	pa_threaded_mainloop_lock(client->captureMainLoop);
	if(ok)
		{
		// DEBUGGING
		Misc::logNote("AgoraClient::RemoteClient: Creating playback stream");
		
		playbackStream=pa_stream_new(client->captureContext,"Playback",&sampleSpec,0);
		ok=playbackStream!=0;
		}
	if(ok)
		state=StreamCreated;
	if(ok)
		{
		// DEBUGGING
		Misc::logNote("AgoraClient::RemoteClient: Registering playback callbacks");
		
		pa_stream_set_state_callback(playbackStream,&AgoraClient::RemoteClient::playbackStreamStateCallback,this);
		pa_stream_set_underflow_callback(playbackStream,&AgoraClient::RemoteClient::playbackStreamUnderflowCallback,this);
		}
	if(ok)
		{
		// DEBUGGING
		Misc::logNote("AgoraClient::RemoteClient: Connecting playback stream to PulseAudio sink");
		
		/* Connect the playback stream to a PulseAudio sink: */
		pa_stream_flags_t flags=PA_STREAM_ADJUST_LATENCY;
		ok=pa_stream_connect_playback(playbackStream,0,&bufferAttrs,flags,0,0)>=0;
		}
	if(ok)
		state=StreamConnecting;
	
	/* Clean up if things went south: */
	if(!ok)
		{
		Misc::userError("AgoraClient::RemoteClient: Unable to create playback stream for remote audio");
		if(playbackStream!=0)
			pa_stream_unref(playbackStream);
		playbackStream=0;
		state=Created;
		}
	pa_threaded_mainloop_unlock(client->captureMainLoop);
	}

AgoraClient::RemoteClient::~RemoteClient(void)
	{
	if(state>=PlaybackThreadRunning)
		{
		/* Stop the playback thread if it is still running: */
		if(state==PlaybackThreadRunning)
			{
			state=RemoteClient::PlaybackThreadTerminating;
			sourceStateCond.signal();
			}
		
		/* Join the playback thread: */
		playbackThread.join();
		}
	}

void AgoraClient::RemoteClient::enqueuePacket(const Threads::EventDispatcher::Time& time,AgoraProtocol::Sequence sequenceNumber,MessageBuffer* packet)
	{
	/* Bail out if the playback thread is not running: */
	if(state!=PlaybackThreadRunning)
		return;
	
	/* Calculate the packet's arrival time stamp: */
	TimeStamp arrival=TimeStamp(long(time.tv_sec)*1000000L+long(time.tv_usec));
	
	/* Check if the playback source is currently stopped: */
	{
	Threads::MutexCond::Lock sourceStateLock(sourceStateCond);
	if(playbackStopped)
		{
		/* Initialize the jitter buffer and latency conditioner: */
		jitterBuffer.init(sequenceNumber,packet->ref());
		headArrival=arrival;
		
		// DEBUGGING
		Misc::formattedLogNote("Enqueue,%d,%f,%f",sequenceNumber,double(arrival-timebase)/1.0e3,double(arrival-timebase)/1.0e3);
		
		/* Wake up the playback thread: */
		playbackStopped=false;
		sourceStateCond.signal();
		}
	else
		{
		/* Enqueue the packet into the jitter buffer and update the latency conditioner: */
		jitterBuffer.enqueue(sequenceNumber,packet->ref());
		TimeStamp expectedArrival=headArrival+TimeStamp(Sequence(sequenceNumber-jitterBuffer.getHeadSequence()))*period;
		headArrival+=TimeStamp(TimeStamp(TimeStamp(arrival-expectedArrival)*arrivalFilterGain)+32768)>>16;
		
		// DEBUGGING
		Misc::formattedLogNote("Enqueue,%d,%f,%f",sequenceNumber,double(arrival-timebase)/1.0e3,double(expectedArrival-timebase)/1.0e3);
		}
	}
	}

/****************************
Methods of class AgoraClient:
****************************/

MessageContinuation* AgoraClient::connectNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the connect notification message: */
	unsigned int clientId=socket.read<ClientID>();
	unsigned int sampleRate=socket.read<Misc::UInt32>();
	unsigned int numPacketFrames=socket.read<Misc::UInt32>();
	
	// DEBUGGING
	Misc::formattedLogNote("AgoraClient: Connecting new client with sample rate %u, packet size %u",sampleRate,numPacketFrames);
	try
		{
		/* Create a new remote client structure and add it to the list: */
		RemoteClient* agoraClient=new RemoteClient(this,sampleRate,numPacketFrames);
		addClientToList(clientId);
		
		/* Set the new client's Agora state: */
		client->getRemoteClient(clientId)->setPlugin(clientProtocolIndex,agoraClient);
		}
	catch(const std::runtime_error& err)
		{
		Misc::formattedUserError("AgoraClient: Unable to connect new client due to exception %s",err.what());
		}
	
	/* Done with the message: */
	return 0;
	}

MessageContinuation* AgoraClient::audioPacketReplyCallback(unsigned int messageId,MessageContinuation* continuation)
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
		Client::RemoteClient* rc=client->getRemoteClient(cont->sourceClientId);
		RemoteClient* arc=rc->getPlugin<RemoteClient>(clientProtocolIndex);
		
		/* Enqueue the message with the remote client's audio decoder: */
		if(arc!=0)
			arc->enqueuePacket(client->getDispatcher().getCurrentTime(),cont->sequence,cont->message.getBuffer());
		
		/* Done with the message: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void AgoraClient::udpAudioPacketReplyCallback(unsigned int messageId,MessageReader& message)
	{
	/* Give the message a basic smell test: */
	if(message.getUnread()<AudioPacketMsg::size)
		throw std::runtime_error("AgoraClient: Truncated audio packet message");
	
	/* Read the message header: */
	unsigned int sourceClientId=message.read<ClientID>();
	Sequence sequenceNumber=message.read<Sequence>();
	size_t audioPacketLen=message.read<Misc::UInt16>();
	
	/* Check if the message is complete: */
	if(message.getUnread()!=audioPacketLen)
		throw std::runtime_error("AgoraClient: Audio packet message has wrong size");
	
	/* Check if the server has a different endianness: */
	if(message.getSwapOnRead())
		{
		/* Rewrite the message header to correct endianness: */
		MessageWriter writer(message.getBuffer()->ref());
		writer.write(ClientID(sourceClientId));
		writer.write(sequenceNumber);
		writer.write(Misc::UInt16(audioPacketLen));
		}
	
	/* Get the source client's state: */
	Client::RemoteClient* rc=client->getRemoteClient(sourceClientId);
	RemoteClient* arc=rc->getPlugin<RemoteClient>(clientProtocolIndex);
	
	/* Enqueue the message with the remote client's audio decoder: */
	if(arc!=0)
		arc->enqueuePacket(client->getDispatcher().getCurrentTime(),sequenceNumber,message.getBuffer());
	}

bool AgoraClient::captureContextReadySignalCallback(Threads::EventDispatcher::ListenerKey signalKey,void* signalData)
	{
	// DEBUGGING
	Misc::logNote("AgoraClient: Sending connect request to server");
	
	/* Send a connect request message to the server: */
	{
	MessageWriter connectRequest(ConnectRequestMsg::createMessage(clientMessageBase));
	connectRequest.write(Misc::UInt32(captureFrequency));
	connectRequest.write(Misc::UInt32(capturePacketSize));
	client->queueMessage(connectRequest.getBuffer());
	}
	
	/* No further need for this signal: */
	return true;
	}

void AgoraClient::captureStreamStateCallback(pa_stream* stream,void* userData)
	{
	AgoraClient* thisPtr=static_cast<AgoraClient*>(userData);
	
	/* Check the stream's new state: */
	if(pa_stream_get_state(stream)==PA_STREAM_READY)
		{
		/* Read encoder settings from configuration section: */
		// int opusApplicationMode=OPUS_APPLICATION_VOIP;
		// int opusApplicationMode=OPUS_APPLICATION_AUDIO;
		int opusApplicationMode=OPUS_APPLICATION_RESTRICTED_LOWDELAY;
		
		// DEBUGGING
		Misc::logNote("AgoraClient: Creating audio encoder");
		
		/* Create the Opus audio encoder: */
		int opusError;
		thisPtr->captureEncoder=opus_encoder_create(thisPtr->captureFrequency,1,opusApplicationMode,&opusError);
		if(opusError==OPUS_OK)
			{
			/* Set the Opus encoder bitrate: */
			opus_encoder_ctl(thisPtr->captureEncoder,OPUS_SET_BITRATE(24000));
			
			/* Set the Opus encoder complexity: */
			opus_encoder_ctl(thisPtr->captureEncoder,OPUS_SET_COMPLEXITY(1));
			
			/* Create a capture buffer: */
			thisPtr->captureBuffer=new Sample[thisPtr->capturePacketSize];
			thisPtr->captureEnd=thisPtr->captureBuffer+thisPtr->capturePacketSize;
			thisPtr->capturePtr=thisPtr->captureBuffer;
			}
		else
			{
			Misc::userError("AgoraClient: Unable to capture local audio");
			
			/* Shut down audio capture: */
			pa_stream_disconnect(thisPtr->captureStream);
			}
		}
	else if(pa_stream_get_state(stream)==PA_STREAM_TERMINATED)
		{
		if(thisPtr->captureEncoder!=0)
			{
			// DEBUGGING
			Misc::logNote("AgoraClient: Destroying audio encoder");
			
			/* Destroy the Opus audio encoder: */
			opus_encoder_destroy(thisPtr->captureEncoder);
			}
		thisPtr->captureEncoder=0;
		
		/* Release the capture buffer: */
		delete[] thisPtr->captureBuffer;
		thisPtr->captureBuffer=0;
		
		// DEBUGGING
		Misc::logNote("AgoraClient: Releasing capture stream");
		
		/* Release the stream and disconnect the context: */
		pa_stream_unref(thisPtr->captureStream);
		thisPtr->captureStream=0;
		pa_context_disconnect(thisPtr->captureContext);
		}
	else if(pa_stream_get_state(stream)==PA_STREAM_FAILED)
		{
		Misc::userError("AgoraClient: Unable to capture local audio");
		
		/* Release the stream: */
		pa_stream_unref(thisPtr->captureStream);
		thisPtr->captureStream=0;
		}
	}

void AgoraClient::captureReadCallback(pa_stream* stream,size_t nbytes,void* userData)
	{
	AgoraClient* thisPtr=static_cast<AgoraClient*>(userData);
	
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
			
			/* Create a new audio packet message: */
			{
			const size_t maxAudioPacketSize=4000; // Recommendation from Opus docs
			MessageWriter audioPacketMessage(AgoraProtocol::AudioPacketMsg::createMessage(thisPtr->clientMessageBase+AudioPacketRequest,maxAudioPacketSize));
			
			/* Reserve room for the packet header: */
			audioPacketMessage.advanceWritePtr(sizeof(ClientID)+sizeof(Sequence)+sizeof(Misc::UInt16));
			
			/* Encode the captured audio packet: */
			int encodedSize=opus_encode(thisPtr->captureEncoder,thisPtr->captureBuffer,thisPtr->capturePacketSize,reinterpret_cast<unsigned char*>(audioPacketMessage.getWritePtr()),maxAudioPacketSize);
			if(encodedSize>1) // Packets of one byte indicate silence and do not need to be transmitted
				{
				audioPacketMessage.advanceWritePtr(encodedSize);
				audioPacketMessage.finishMessage();
				
				/* Write the audio packet header: */
				audioPacketMessage.rewind();
				audioPacketMessage.write(ClientID(0));
				audioPacketMessage.write(thisPtr->captureSequence);
				audioPacketMessage.write(Misc::UInt16(encodedSize));
				
				/* Send the audio packet to the server over UDP or TCP: */
				if(thisPtr->client->haveUDP())
					thisPtr->client->queueServerUDPMessage(audioPacketMessage.getBuffer());
				else
					thisPtr->client->queueServerMessage(audioPacketMessage.getBuffer());
				}
			else if(encodedSize<0)
				Misc::formattedLogWarning("AgoraClient: Error %d (%s) while encoding an audio packet",encodedSize,opus_strerror(encodedSize));
			}
			
			/* Advance the audio packet sequence number: */
			++thisPtr->captureSequence;
			
			/* Reset the capture buffer: */
			thisPtr->capturePtr=thisPtr->captureBuffer;
			}
		}
	
	/* Mark the current capture fragment as complete: */
	pa_stream_drop(thisPtr->captureStream);
	}

void AgoraClient::captureContextStateCallback(pa_context* context,void* userData)
	{
	AgoraClient* thisPtr=static_cast<AgoraClient*>(userData);
	
	/* Check the context's new state: */
	if(pa_context_get_state(context)==PA_CONTEXT_READY)
		{
		/* Notify the communication thread that the audio processing context is ready: */
		thisPtr->client->getDispatcher().signal(thisPtr->captureContextReadySignalKey,0);
		
		/* Set the capture sample format: */
		pa_sample_spec sampleSpec;
		sampleSpec.format=PA_SAMPLE_S16LE;
		sampleSpec.rate=thisPtr->captureFrequency;
		sampleSpec.channels=1;
		
		/* Define capture buffer attributes to minimize latency: */
		pa_buffer_attr bufferAttrs;
		size_t periodBytes=thisPtr->capturePacketSize*1*sizeof(Misc::SInt16);
		bufferAttrs.fragsize=periodBytes;
		bufferAttrs.maxlength=periodBytes;
		bufferAttrs.prebuf=0;
		bufferAttrs.minreq=Misc::UInt32(-1);
		bufferAttrs.tlength=Misc::UInt32(-1);
		
		// DEBUGGING
		Misc::logNote("AgoraClient: Starting local audio capture");
		
		/* Create the capture stream: */
		bool ok=true;
		if(ok)
			{
			thisPtr->captureStream=pa_stream_new(context,"Capture",&sampleSpec,0);
			ok=thisPtr->captureStream!=0;
			}
		if(ok)
			{
			pa_stream_set_state_callback(thisPtr->captureStream,&AgoraClient::captureStreamStateCallback,thisPtr);
			pa_stream_set_read_callback(thisPtr->captureStream,&AgoraClient::captureReadCallback,thisPtr);
			}
		if(ok)
			{
			/* Connect the capture stream to a PulseAudio source: */
			pa_stream_flags_t flags=PA_STREAM_ADJUST_LATENCY;
			if(thisPtr->capturePaused)
				flags=pa_stream_flags_t(flags|PA_STREAM_START_CORKED);
			ok=pa_stream_connect_record(thisPtr->captureStream,0,&bufferAttrs,flags)>=0;
			}
		
		/* Clean up if things went south: */
		if(!ok)
			{
			Misc::userError("AgoraClient: Unable to capture local audio");
			if(thisPtr->captureStream!=0)
				pa_stream_unref(thisPtr->captureStream);
			thisPtr->captureStream=0;
			}
		}
	else if(pa_context_get_state(context)==PA_CONTEXT_TERMINATED)
		{
		// DEBUGGING
		Misc::logNote("AgoraClient: Stopping capture main loop");
		
		/* Release the context: */
		pa_context_unref(thisPtr->captureContext);
		thisPtr->captureContext=0;
		
		/* Terminate the main loop: */
		pa_threaded_mainloop_get_api(thisPtr->captureMainLoop)->quit(pa_threaded_mainloop_get_api(thisPtr->captureMainLoop),0);
		
		}
	else if(pa_context_get_state(context)==PA_CONTEXT_FAILED)
		{
		Misc::userError("AgoraClient: Unable to capture or play back audio");
		
		/* Release the context: */
		pa_context_unref(thisPtr->captureContext);
		thisPtr->captureContext=0;
		
		/* Terminate the main loop: */
		pa_threaded_mainloop_get_api(thisPtr->captureMainLoop)->quit(pa_threaded_mainloop_get_api(thisPtr->captureMainLoop),1);
		}
	}

AgoraClient::AgoraClient(Client* sClient)
	:PluginClient(sClient),
	 captureFrequency(48000U),
	 capturePacketSize((captureFrequency*10U+500U)/1000U), // 10ms period size
	 captureMainLoop(0),captureContext(0),captureStream(0),
	 captureBuffer(0),captureEncoder(0),captureSequence(0),
	 capturePaused(false)
	{
	// DEBUGGING
	Misc::logNote("AgoraClient: Creating Agora plug-in");
	
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

AgoraClient::~AgoraClient(void)
	{
	// DEBUGGING
	Misc::logNote("AgoraClient: Destroying client plug-in");
	
	if(captureMainLoop!=0)
		{
		pa_threaded_mainloop_lock(captureMainLoop);
		
		/* Disconnect the capture stream to shut down audio capture: */
		if(captureStream!=0)
			{
			// DEBUGGING
			Misc::logNote("AgoraClient: Disconnecting capture stream");
			
			pa_stream_disconnect(captureStream);
			}
		pa_threaded_mainloop_unlock(captureMainLoop);
		
		usleep(100000);
		
		/* Wait until the main loop shuts down: */
		pa_threaded_mainloop_get_retval(captureMainLoop);
		
		/* Release the main loop: */
		pa_threaded_mainloop_free(captureMainLoop);
		}
	
	#if LATENCYTEST
	delete wavFile;
	#endif
	}

const char* AgoraClient::getName(void) const
	{
	return protocolName;
	}

unsigned int AgoraClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int AgoraClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int AgoraClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void AgoraClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register a signal handler for when the audio processing context is established: */
	captureContextReadySignalKey=client->getDispatcher().addSignalListener(Threads::EventDispatcher::wrapMethod<AgoraClient,&AgoraClient::captureContextReadySignalCallback>,this);
	
	/* Register message handlers: */
	client->setTCPMessageHandler(serverMessageBase+ConnectNotification,Client::wrapMethod<AgoraClient,&AgoraClient::connectNotificationCallback>,this,ConnectNotificationMsg::size);
	client->setTCPMessageHandler(serverMessageBase+AudioPacketReply,Client::wrapMethod<AgoraClient,&AgoraClient::audioPacketReplyCallback>,this,AudioPacketMsg::size);
	client->setUDPMessageHandler(serverMessageBase+AudioPacketReply,Client::wrapMethod<AgoraClient,&AgoraClient::udpAudioPacketReplyCallback>,this);
	}

void AgoraClient::start(void)
	{
	/* Start audio processing and capture: */
	bool ok=true;
	
	// DEBUGGING
	Misc::logNote("AgoraClient: Starting audio processing");
	
	if(ok)
		{
		/* Create a PulseAudio threaded mainloop: */
		captureMainLoop=pa_threaded_mainloop_new();
		ok=captureMainLoop!=0;
		}
	if(ok)
		{
		/* Create a PulseAudio context: */
		captureContext=pa_context_new(pa_threaded_mainloop_get_api(captureMainLoop),"Agora");
		ok=captureContext!=0;
		}
	if(ok)
		{
		/* Register a context state change callback: */
		pa_context_set_state_callback(captureContext,&AgoraClient::captureContextStateCallback,this);
		}
	if(ok)
		{
		/* Connect the context to the default PulseAudio server: */
		ok=pa_context_connect(captureContext,0,PA_CONTEXT_NOFLAGS,0)>=0;
		}
	if(ok)
		{
		/* Run the mainloop: */
		ok=pa_threaded_mainloop_start(captureMainLoop)>=0;
		}
	
	/* Check for errors: */
	if(!ok)
		{
		Misc::userError("AgoraClient: Unable to capture or play back audio");
		if(captureContext!=0)
			pa_context_unref(captureContext);
		captureContext=0;
		if(captureMainLoop!=0)
			pa_threaded_mainloop_free(captureMainLoop);
		captureMainLoop=0;
		}
	}

void AgoraClient::clientConnected(unsigned int clientId)
	{
	/* Don't do anything; connection happens when the server sends the new client's encoder parameters */
	}

void AgoraClient::clientDisconnected(unsigned int clientId)
	{
	// DEBUGGING
	Misc::logNote("AgoraClient::clientDisconnected: Disconnecting remote client");
	
	/* Remove the client from the list: */
	removeClientFromList(clientId);
	
	/* Retrieve the remote client state object: */
	Client::RemoteClient* rc=client->getRemoteClient(clientId);
	RemoteClient* arc=rc->getPlugin<RemoteClient>(clientProtocolIndex);
	if(arc!=0)
		{
		pa_threaded_mainloop_lock(captureMainLoop);
		
		/* Check the state of the remote client object: */
		if(arc->state==RemoteClient::PlaybackThreadRunning)
			{
			/* Release the remote client state object from the client so it can delete itself on its own time: */
			rc->releasePlugin(clientProtocolIndex);
			
			/* Shut down the playback thread: */
			arc->state=RemoteClient::PlaybackThreadTerminating;
			arc->sourceStateCond.signal();
			}
		else if(arc->state>=RemoteClient::StreamCreated)
			{
			/* Release the playback stream: */
			pa_stream_unref(arc->playbackStream);
			arc->playbackStream=0;
			
			arc->state=RemoteClient::Created;
			}
		
		pa_threaded_mainloop_unlock(captureMainLoop);
		}
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginClient* createObject(PluginClientLoader& objectLoader,Client* client)
	{
	return new AgoraClient(client);
	}

void destroyObject(PluginClient* object)
	{
	delete object;
	}

}
