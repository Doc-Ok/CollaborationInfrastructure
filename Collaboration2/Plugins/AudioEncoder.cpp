/***********************************************************************
AudioEncoder - Class to encode audio from a PCM device for transmission
using the Opus encoder.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/AudioEncoder.h>

#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Sound/SoundDataFormat.h>

#include <Collaboration2/Protocol.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/Plugins/AgoraProtocol.h>

/*****************************
Methods of class AudioEncoder:
*****************************/

void* AudioEncoder::encoderThreadMethod(void)
	{
	/* Capture and encode audio packets until interrupted: */
	try
		{
		while(keepRunning)
			{
			/* Capture the next audio packet from the PCM device: */
			size_t unread=numPacketFrames;
			opus_int16* framePtr=packetFrames;
			do
				{
				size_t framesRead=pcmDevice.read(framePtr,unread);
				framePtr+=framesRead;
				unread-=framesRead;
				}
			while(unread!=0);
			
			/* Create a new audio packet message: */
			MessageWriter audioPacketMessage(AgoraProtocol::AudioPacketMsg::createMessage(audioMessageId,audioPacketSize));
			
			/* Write a placeholder for the packet header: */
			audioPacketMessage.write(ClientID(0));
			audioPacketMessage.write(Misc::SInt16(0));
			audioPacketMessage.write(Misc::UInt16(0)); // Placeholder for encoded audio packet size
			
			/* Encode the captured audio packet: */
			int encodedSize=opus_encode(encoder,packetFrames,numPacketFrames,reinterpret_cast<unsigned char*>(audioPacketMessage.getWritePtr()),audioPacketSize);
			if(encodedSize>1) // Packets of one byte indicate silence and do not need to be transmitted
				{
				audioPacketMessage.advanceWritePtr(encodedSize);
				audioPacketMessage.finishMessage();
				
				/* Re-write the audio packet header: */
				audioPacketMessage.rewind();
				audioPacketMessage.write(ClientID(destClientId));
				audioPacketMessage.write(frameNumber);
				audioPacketMessage.write(Misc::UInt16(encodedSize));
				
				/* Hand the audio packet to the main thread: */
				dispatcher->signal(signalKey,audioPacketMessage.getBuffer()->ref());
				}
			else if(encodedSize<0)
				Misc::throwStdErr("Encoder: Error %d (%s) while encoding an audio packet",encodedSize,opus_strerror(encodedSize));
			
			/* Advance the frame counter: */
			++frameNumber;
			}
		}
	catch(const std::runtime_error& err)
		{
		/* Print an error message and quit: */
		Misc::formattedUserWarning("AudioEncoder: Stopping audio capture due to exception %s",err.what());
		}
	
	return 0;
	}

AudioEncoder::AudioEncoder(const char* sPCMDeviceName,unsigned int sSampleRate)
	:pcmDevice(sPCMDeviceName,true),
	 sampleRate(sSampleRate),
	 packetFrames(0),encoder(0),
	 audioPacketSize(4000), // Opus recommendation
	 audioMessageId(0),destClientId(0),
	 frameNumber(0),
	 keepRunning(false)
	{
	/* Create an Opus encoder: */
	int opusError;
	encoder=opus_encoder_create(sampleRate,1,OPUS_APPLICATION_VOIP,&opusError);
	if(opusError!=OPUS_OK)
		Misc::throwStdErr("AudioEncoder: Error %d (%s) while creating Opus encoder",opusError,opus_strerror(opusError));
	
	/* Set the source PCM device's audio format: */
	Sound::SoundDataFormat audioFormat;
	audioFormat.bitsPerSample=16;
	audioFormat.bytesPerSample=2;
	audioFormat.signedSamples=true;
	audioFormat.samplesPerFrame=1;
	audioFormat.framesPerSecond=sampleRate;
	pcmDevice.setSoundDataFormat(audioFormat);
	
	/* Retrieve the actually selected sample rate: */
	sampleRate=audioFormat.framesPerSecond;
	
	/* Create a buffer for unencoded audio data: */
	numPacketFrames=sampleRate/100; // 10ms packet duration
	packetFrames=new opus_int16[numPacketFrames];
	
	/* Set the PCM device's buffer size: */
	pcmDevice.setBufferSize(numPacketFrames*4,numPacketFrames);
	}

AudioEncoder::~AudioEncoder(void)
	{
	/* Stop encoding if it's still running: */
	if(keepRunning)
		stop();
	
	/* Release allocated buffers: */
	delete[] packetFrames;
	
	/* Destroy the Opus encoder: */
	opus_encoder_destroy(encoder);
	}

void AudioEncoder::setBitRate(unsigned int newBitRate)
	{
	/* Set the encoder's bit rate: */
	opus_encoder_ctl(encoder,OPUS_SET_BITRATE(newBitRate));
	}

void AudioEncoder::setDestClientId(unsigned int newDestClientId)
	{
	destClientId=newDestClientId;
	}

void AudioEncoder::start(unsigned int newAudioMessageId,Threads::EventDispatcher& newDispatcher,Threads::EventDispatcher::ListenerKey newSignalKey)
	{
	/* Remember the audio message ID: */
	audioMessageId=newAudioMessageId;
	
	/* Remember the signal: */
	dispatcher=&newDispatcher;
	signalKey=newSignalKey;
	
	/* Start the PCM device: */
	pcmDevice.prepare();
	pcmDevice.start();
	
	/* Start the encoding thread: */
	keepRunning=true;
	encoderThread.start(this,&AudioEncoder::encoderThreadMethod);
	}

void AudioEncoder::stop(void)
	{
	/* Bail out if not running: */
	if(!keepRunning)
		return;
	
	/* Stop the encoder thread: */
	keepRunning=false;
	encoderThread.join();
	
	/* Stop the PCM device: */
	pcmDevice.drop();
	}
