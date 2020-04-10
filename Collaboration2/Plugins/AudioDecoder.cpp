/***********************************************************************
AudioDecoder - Class to decode received audio using the Opus decoder.
Copyright (c) 2019 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/Plugins/AudioDecoder.h>

#include <string.h>
#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Sound/SoundDataFormat.h>

#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>

/*****************************
Methods of class AudioDecoder:
*****************************/

void* AudioDecoder::decoderThreadMethod(void)
	{
	/* Decode audio until interrupted: */
	try
		{
		while(keepRunning)
			{
			/* Grab the next encoded audio packet from the jitter buffer: */
			MessageBuffer* message;
			{
			Threads::Spinlock::Lock jitterBufferLock(jitterBufferMutex);
			if(!jitterBuffer.empty())
				{
				message=jitterBuffer.front();
				jitterBuffer.pop_front();
				}
			else
				{
				/* Oops, audio drop-out: */
				message=0;
				}
			}
			
			/* Check if there is an audio packet: */
			opus_int32 decodeResult;
			if(message!=0)
				{
				/* Extract the encoded audio data from the audio packet message: */
				MessageReader reader(message); // Reader now holds the only reference to the message; will be released at end of this if() branch
				reader.advanceReadPtr(sizeof(MessageID)+sizeof(ClientID)+sizeof(Misc::SInt16)); // Skip irrelevant parts of the message header
				unsigned int packetLen=reader.read<Misc::UInt16>();
				
				/* Hand the encoded audio data to the Opus decoder: */
				decodeResult=opus_decode(decoder,reinterpret_cast<const unsigned char*>(reader.getReadPtr()),packetLen,packetFrames,numPacketFrames,0);
				}
			else
				{
				/* Try masking a dropped or delayed packet: */
				Misc::logWarning("AudioDecoder: Masking packet loss");
				decodeResult=opus_decode(decoder,0,0,packetFrames,numPacketFrames,1);
				}
			size_t numDecodedFrames;
			if(decodeResult>=0)
				numDecodedFrames=size_t(decodeResult);
			else
				{
				Misc::logWarning("AudioDecoder: Error during Opus decoding; inserting silence");
				memset(packetFrames,0,numPacketFrames*sizeof(opus_int16));
				numDecodedFrames=numPacketFrames;
				}
			
			try
				{
				/* Write the decoded audio packet (or filler packet) to the PCM device: */
				opus_int16* writePtr=packetFrames;
				size_t unwritten=numDecodedFrames;
				while(unwritten>0)
					{
					size_t written=pcmDevice.write(writePtr,unwritten);
					writePtr+=written;
					unwritten-=written;
					}
				}
			catch(const Sound::ALSAPCMDevice::UnderrunError& err)
				{
				/* Print a warning and restart the PCM: */
				Misc::logWarning("AudioDecoder: Underrun on PCM device. Restarting");
				pcmDevice.prepare();
				}
			}
		}
	catch(const std::runtime_error& err)
		{
		/* Print an error message and quit: */
		Misc::formattedUserWarning("AudioDecoder: Stopping audio playback due to exception %s",err.what());
		}
	
	return 0;
	}

AudioDecoder::AudioDecoder(unsigned int sSampleRate,unsigned int sNumPacketFrames,const char* pcmDeviceName)
	:sampleRate(sSampleRate),numPacketFrames(sNumPacketFrames),
	 jitterBuffer(3),
	 decoder(0),packetFrames(0),
	 pcmDevice(pcmDeviceName,false),
	 keepRunning(false)
	{
	/* Create an Opus decoder: */
	int opusError;
	decoder=opus_decoder_create(sampleRate,1,&opusError);
	if(opusError!=OPUS_OK)
		Misc::throwStdErr("AudioDecoder: Error %d (%s) while creating Opus decoder",opusError,opus_strerror(opusError));
	
	/* Set the sink PCM device's audio format: */
	Sound::SoundDataFormat audioFormat;
	audioFormat.bitsPerSample=16;
	audioFormat.bytesPerSample=2;
	audioFormat.signedSamples=true;
	audioFormat.samplesPerFrame=1;
	audioFormat.framesPerSecond=sampleRate;
	pcmDevice.setSoundDataFormat(audioFormat);
	
	/* Check the actually selected sample rate: */
	if((unsigned int)(audioFormat.framesPerSecond)!=sampleRate)
		{
		opus_decoder_destroy(decoder);
		Misc::throwStdErr("AudioDecoder: Unable to set PCM device's sample rate to %u; got %u",sampleRate,(unsigned int)(audioFormat.framesPerSecond));
		}
	
	/* Create a buffer for decoded audio data: */
	packetFrames=new opus_int16[numPacketFrames];
	
	/* Set the PCM device's buffer size: */
	pcmDevice.setBufferSize(numPacketFrames*2,numPacketFrames);
	}

AudioDecoder::~AudioDecoder(void)
	{
	/* Stop encoding if it's still running: */
	if(keepRunning)
		stop();
	
	/* Release allocated buffers: */
	delete[] packetFrames;
	
	/* Destroy the Opus decoder: */
	opus_decoder_destroy(decoder);
	}

void AudioDecoder::start(void)
	{
	/* Start the PCM device: */
	pcmDevice.prepare();
	pcmDevice.setStartThreshold(2*numPacketFrames);
	
	/* Start the decoding thread: */
	keepRunning=true;
	decoderThread.start(this,&AudioDecoder::decoderThreadMethod);
	}

void AudioDecoder::stop(void)
	{
	/* Bail out if not running: */
	if(!keepRunning)
		return;
	
	/* Stop the decoder thread: */
	keepRunning=false;
	decoderThread.join();
	
	/* Stop the PCM device: */
	pcmDevice.drop();
	
	/* Release all messages in the jitter buffer: */
	{
	Threads::Spinlock::Lock jitterBufferLock(jitterBufferMutex);
	for(JitterBuffer::iterator jbIt=jitterBuffer.begin();jbIt!=jitterBuffer.end();++jbIt)
		(*jbIt)->unref();
	}
	}

void AudioDecoder::enqueuePacket(MessageBuffer* newPacket)
	{
	Threads::Spinlock::Lock jitterBufferLock(jitterBufferMutex);
	
	/* Remove the oldest audio packet message if the buffer is full: */
	if(jitterBuffer.full())
		jitterBuffer.pop_front();
	
	/* Put the new packet into the jitter buffer: */
	jitterBuffer.push_back(newPacket->ref());
	}
