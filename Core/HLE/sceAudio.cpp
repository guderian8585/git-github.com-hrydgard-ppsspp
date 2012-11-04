// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "../MIPS/MIPS.h"
#include "../Host.h"
#include "../../Core/CoreTiming.h"

#include "sceAudio.h"
#include "__sceAudio.h"
#include "HLE.h"

AudioChannel chans[8];

// Enqueues the buffer pointer on the channel. If channel buffer queue is full (2 items?) will block until it isn't.
// For solid audio output we'll need a queue length of 2 buffers at least, we'll try that first.

// Not sure about the range of volume, I often see 0x800 so that might be either
// max or 50%?
u32 sceAudioOutputPannedBlocking(u32 chan, u32 volume1, u32 volume2, u32 samplePtr)
{
  DEBUG_LOG(HLE,"sceAudioOutputPannedBlocking(%d,%d,%d, %08x )", chan, volume1, volume2, samplePtr);

  if (samplePtr == 0)
  {
    ERROR_LOG(HLE, "Sample pointer null");
    return 0;
  }

	if (chan < 0 || chan >= MAX_CHANNEL)
	{
		ERROR_LOG(HLE,"sceAudioOutputPannedBlocking() - BAD CHANNEL");
    return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
	else if (!chans[chan].reserved)
  {
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  else
	{
		chans[chan].running = true;
		chans[chan].leftVolume = volume1;
		chans[chan].rightVolume = volume2;
		chans[chan].sampleAddress = samplePtr;
    return __AudioEnqueue(chans[chan], chan, true);
	}
}

u32 sceAudioGetChannelRestLen(u32 chanId)
{
	ERROR_LOG(HLE,"UNIMPL sceAudioGetChannelRestLen(%i)", chanId);
	// Remaining samples in that channel buffer.
	return 0;
}

u32 sceAudioOutputPanned(u32 chan, u32 leftVol, u32 rightVol, u32 samplePtr)
{
  if (chan < 0 || chan >= MAX_CHANNEL)
  {
    ERROR_LOG(HLE,"sceAudioOutputPannedBlocking() - BAD CHANNEL");
    return SCE_ERROR_AUDIO_INVALID_CHANNEL;
  }
  else if (!chans[chan].reserved)
  {
    ERROR_LOG(HLE,"sceAudioOutputPanned(%d, %d, %d, %08x) - channel not reserved", chan,leftVol,rightVol,samplePtr);
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  else
  {
		chans[chan].running = true;
		chans[chan].leftVolume = leftVol;
		chans[chan].rightVolume = rightVol;
		chans[chan].sampleAddress = samplePtr;
    u32 retval = __AudioEnqueue(chans[chan], chan, false);
    DEBUG_LOG(HLE,"%08x=sceAudioOutputPanned(%d, %d, %d, %08x)",retval,chan,leftVol,rightVol,samplePtr);
    return retval;
	}
}

u32 sceAudioOutputBlocking(u32 chan, u32 vol, u32 samplePtr)
{
	if (chan < 0 || chan >= MAX_CHANNEL)
	{
		ERROR_LOG(HLE,"sceAudioOutputBlocking() - BAD CHANNEL");
    return SCE_ERROR_AUDIO_INVALID_CHANNEL;
	}
  else if (!chans[chan].reserved)
  {
    ERROR_LOG(HLE,"sceAudioOutputBlocking() - channel not reserved");
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  else
	{
    DEBUG_LOG(HLE,"sceAudioOutputPanned(%d, %d, %08x )",chan,vol,samplePtr);
		chans[chan].running = true;
		chans[chan].leftVolume = vol;
		chans[chan].rightVolume = vol;
		chans[chan].sampleAddress = samplePtr;
    return __AudioEnqueue(chans[chan], chan, true);
	}
}

static int GetFreeChannel()
{
	for (int i = 0; i < MAX_CHANNEL; i++)
  {
		if (!chans[i].reserved)
		{
			return i;
		}
	}
	return -1;
}

u32 sceAudioChReserve(u32 channel, u32 sampleCount, u32 format) //.Allocate sound channel
{
	if (channel == (u32)-1)
	{
		channel = GetFreeChannel();
	}
	else
	{
		ERROR_LOG(HLE,"sceAudioChReserve failed");
    return SCE_ERROR_AUDIO_NO_CHANNELS_AVAILABLE;
	}

  if (format != PSP_AUDIO_FORMAT_MONO && format != PSP_AUDIO_FORMAT_STEREO)
  {
    ERROR_LOG(HLE, "sceAudioChReserve(channel = %d, sampleCount = %d, format = %d): invalid format", channel, sampleCount, format);
    return SCE_ERROR_AUDIO_INVALID_FORMAT;
  }

	if (chans[channel].reserved)
  {
		WARN_LOG(HLE, "WARNING: Reserving already reserved channel. Error?");
	}
	DEBUG_LOG(HLE,"%i = sceAudioChReserve(%i, %i, %i)", channel, channel, sampleCount, format);

	chans[channel].sampleCount = sampleCount;
	chans[channel].reserved = true;
	return channel; //return handle
}

u32 sceAudioChRelease(u32 chan)
{
  if (!chans[chan].reserved)
  {
    ERROR_LOG(HLE,"sceAudioChRelease(%i): channel not reserved", chan);
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  chans[chan].triggered = false;
  chans[chan].running = false;
	chans[chan].reserved = false;

	DEBUG_LOG(HLE,"sceAudioChRelease(%i)", chan);
	return 1;
}

u32 sceAudioSetChannelDataLen(u32 chan, u32 len)
{
  if (chan < 0 || chan >= MAX_CHANNEL)
  {
    ERROR_LOG(HLE,"sceAudioSetChannelDataLen(%i, %i) - BAD CHANNEL", chan, len);
    return SCE_ERROR_AUDIO_INVALID_CHANNEL;
  }
  else if (!chans[chan].reserved)
  {
    ERROR_LOG(HLE,"sceAudioSetChannelDataLen(%i, %i) - channel not reserved", chan, len);
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  else
  {
    DEBUG_LOG(HLE,"sceAudioSetChannelDataLen(%i, %i)", chan, len);
    chans[chan].sampleCount = len;
    return 0;
  }
}

u32 sceAudioChangeChannelConfig(u32 chan, u32 format)
{
  if (chan < 0 || chan >= MAX_CHANNEL)
  {
    ERROR_LOG(HLE,"sceAudioChangeChannelConfig(%i, %i) - invalid channel number", chan, format);
    return SCE_ERROR_AUDIO_INVALID_CHANNEL;
  }
  else if (!chans[chan].reserved)
  {
    ERROR_LOG(HLE,"sceAudioChangeChannelConfig(%i, %i) - channel not reserved", chan, format);
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  else
  {
	  DEBUG_LOG(HLE,"sceAudioChangeChannelConfig(%i, %i)", chan, format);
    chans[chan].format = format;
    return 0;
  }
}

u32 sceAudioChangeChannelVolume(u32 chan, u32 lvolume, u32 rvolume)
{
  if (chan < 0 || chan >= MAX_CHANNEL)
  {
    ERROR_LOG(HLE,"sceAudioChangeChannelVolume(%i, %i, %i) - invalid channel number", chan, lvolume, rvolume);
    return SCE_ERROR_AUDIO_INVALID_CHANNEL;
  }
  else if (!chans[chan].reserved)
  {
    ERROR_LOG(HLE,"sceAudioChangeChannelVolume(%i, %i, %i) - channel not reserved", chan, lvolume, rvolume);
    return SCE_ERROR_AUDIO_CHANNEL_NOT_RESERVED;
  }
  else
  {
	  DEBUG_LOG(HLE,"sceAudioChangeChannelVolume(%i, %i, %i)", chan, lvolume, rvolume);
    chans[chan].leftVolume = lvolume;
    chans[chan].rightVolume = rvolume;
	  return 0;
  }
}
 
u32 sceAudioInit()
{
  ERROR_LOG(HLE,"UNIMPL sceAudioInit");
  return 0;
}

const HLEFunction sceAudio[] = 
{
  {0x01562ba3, 0, "sceAudioOutput2Reserve"},
  {0x2d53f36e, 0, "sceAudioOutput2OutputBlocking"},
  {0x63f2889c, 0, "sceAudioOutput2ChangeLength"},
  {0x647cef33, 0, "sceAudioOutput2GetRestSample"},	
  {0x43196845, 0, "sceAudioOutput2Release"},
  {0x210567F7, 0, "sceAudioEnd"},
  {0x38553111, 0, "sceAudioSRCChReserve"},
  {0x5C37C0AE, 0, "sceAudioSRCChRelease"},
  {0x80F1F7E0, Wrap<sceAudioInit>, "sceAudioInit"},
  {0x927AC32B, 0, "sceAudioSetVolumeOffset"},
  {0xA2BEAA6C, 0, "sceAudioSetFrequency"},
  {0xA633048E, 0, "sceAudioPollInputEnd"},
  {0xB011922F, 0, "sceAudioGetChannelRestLength"},
  {0xB61595C0, 0, "sceAudioLoopbackTest"},
  {0xE0727056, 0, "sceAudioSRCOutputBlocking"},
  {0xE926D3FB, 0, "sceAudioInputInitEx"},
  {0x8c1009b2, 0, "sceAudioOutput"}, 
  {0x136CAF51, Wrap<sceAudioOutputBlocking>, "sceAudioOutputBlocking"}, 
  {0xE2D56B2D, Wrap<sceAudioOutputPanned>, "sceAudioOutputPanned"}, 
  {0x13F592BC, Wrap<sceAudioOutputPannedBlocking>, "sceAudioOutputPannedBlocking"}, //(u32, u32, u32, void *)Output sound, blocking 
  {0x5EC81C55, Wrap<sceAudioChReserve>, "sceAudioChReserve"}, //(u32, u32 samplecount, u32) Initialize channel and allocate buffer  long, long samplecount, long);//init buffer? returns handle, minus if error
  {0x6FC46853, Wrap<sceAudioChRelease>, "sceAudioChRelease"}, //(long handle)Terminate channel and deallocate buffer //free buffer?
  {0xE9D97901, Wrap<sceAudioGetChannelRestLen>, "sceAudioGetChannelRestLen"}, 
  {0xCB2E439E, Wrap<sceAudioSetChannelDataLen>, "sceAudioSetChannelDataLen"}, //(u32, u32)
  {0x95FD0C2D, Wrap<sceAudioChangeChannelConfig>, "sceAudioChangeChannelConfig"}, 
  {0xB7E1D8E7, Wrap<sceAudioChangeChannelVolume>, "sceAudioChangeChannelVolume"}, 
  {0x41efade7, 0, "sceAudioOneshotOutput"},
  {0x086e5895, 0, "sceAudioInputBlocking"},	 
  {0x6d4bec68, 0, "sceAudioInput"},	 
  {0xa708c6a6, 0, "sceAudioGetInputLength"},
  {0x87b2e651, 0, "sceAudioWaitInputEnd"},
  {0x7de61688, 0, "sceAudioInputInit"},
  {0xb011922f, 0, "sceAudioGetChannelRestLength"},
};



void Register_sceAudio()
{
  RegisterModule("sceAudio", ARRAY_SIZE(sceAudio), sceAudio);
}
