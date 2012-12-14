// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "__sceAudio.h"
#include "sceAudio.h"
#include "sceKernel.h"
#include "sceKernelThread.h"
#include "StdMutex.h"
#include "CommonTypes.h"
#include "../CoreTiming.h"
#include "../MemMap.h"
#include "../Host.h"
#include "../Config.h"
#include "FixedSizeQueue.h"
#include "Common/Thread.h"

// While buffers == MAX_BUFFERS, block on blocking write
// non-blocking writes will return busy, I guess

#define MAX_BUFFERS 2
#define MIN_BUFFERS 1

std::recursive_mutex section;

int eventAudioUpdate = -1;
int eventHostAudioUpdate = -1;
int mixFrequency = 44100;
const int hwSampleRate = 44100;
const int hwBlockSize = 480;
const int hostAttemptBlockSize = 64;
const int audioIntervalUs = (int)(1000000ULL * hwBlockSize / hwSampleRate);
const int audioHostIntervalUs = (int)(1000000ULL * hostAttemptBlockSize / hwSampleRate);

// High and low watermarks, basically.
const int chanQueueMaxSizeFactor = 24;
const int chanQueueMinSizeFactor = 1;

FixedSizeQueue<s16, hwBlockSize * 8> outAudioQueue;


void hleAudioUpdate(u64 userdata, int cyclesLate)
{
	__AudioUpdate();

	CoreTiming::ScheduleEvent(usToCycles(audioIntervalUs), eventAudioUpdate, 0);
}

void hleHostAudioUpdate(u64 userdata, int cyclesLate)
{
	host->UpdateSound();
	CoreTiming::ScheduleEvent(usToCycles(audioHostIntervalUs), eventHostAudioUpdate, 0);
}

void __AudioInit()
{
	mixFrequency = 44100;

	eventAudioUpdate = CoreTiming::RegisterEvent("AudioUpdate", &hleAudioUpdate);
	eventHostAudioUpdate = CoreTiming::RegisterEvent("AudioUpdateHost", &hleHostAudioUpdate);

	CoreTiming::ScheduleEvent(usToCycles(audioIntervalUs), eventAudioUpdate, 0);
	CoreTiming::ScheduleEvent(usToCycles(audioHostIntervalUs), eventHostAudioUpdate, 0);
	for (int i = 0; i < 8; i++)
		chans[i].clear();
}

void __AudioShutdown()
{
	for (int i = 0; i < 8; i++)
		chans[i].clear();
}

u32 __AudioEnqueue(AudioChannel &chan, int chanNum, bool blocking)
{
	section.lock();
	if (chan.sampleAddress == 0)
		return SCE_ERROR_AUDIO_NOT_OUTPUT;
	if (chan.sampleQueue.size() > chan.sampleCount*2*chanQueueMaxSizeFactor) {
		// Block!
		if (blocking) {
			chan.waitingThread = __KernelGetCurThread();
			// WARNING: This changes currentThread so must grab waitingThread before (line above).
			__KernelWaitCurThread(WAITTYPE_AUDIOCHANNEL, (SceUID)chanNum, 0, 0, false);
			section.unlock();
			return 0;
		}
		else
		{
			chan.waitingThread = 0;
			return SCE_ERROR_AUDIO_CHANNEL_BUSY;
		}
	}
	if (chan.format == PSP_AUDIO_FORMAT_STEREO)
	{
		for (u32 i = 0; i < chan.sampleCount * 2; i++)
		{
			chan.sampleQueue.push((s16)Memory::Read_U16(chan.sampleAddress + 2 * i));
		}
	}
	else if (chan.format == PSP_AUDIO_FORMAT_MONO)
	{
		for (u32 i = 0; i < chan.sampleCount; i++)
		{
			// Expand to stereo
			s16 sample = (s16)Memory::Read_U16(chan.sampleAddress + 2 * i);
			chan.sampleQueue.push(sample);
			chan.sampleQueue.push(sample);
		}
	}
	section.unlock();
	return 0;
}

// Mix samples from the various audio channels into a single sample queue.
// This single sample queue is where __AudioMix should read from. If the sample queue is full, we should
// just sleep the main emulator thread a little.
void __AudioUpdate()
{
	// Audio throttle doesn't really work on the PSP since the mixing intervals are so closely tied
	// to the CPU. Much better to throttle the frame rate on frame display and just throw away audio
	// if the buffer somehow gets full.

	s32 mixBuffer[hwBlockSize * 2];
	memset(mixBuffer, 0, sizeof(mixBuffer));

	for (int i = 0; i < MAX_CHANNEL; i++)
	{
		if (!chans[i].reserved)
			continue;
		if (!chans[i].sampleQueue.size()) {
			// DEBUG_LOG(HLE, "No queued samples, skipping channel %i", i);
			continue;
		}

		for (int s = 0; s < hwBlockSize; s++)
		{
			if (chans[i].sampleQueue.size() >= 2)
			{
				s16 sampleL = chans[i].sampleQueue.front();
				s16 sampleR = chans[i].sampleQueue.front();
				chans[i].sampleQueue.pop();
				chans[i].sampleQueue.pop();
				mixBuffer[s * 2] += sampleL;
				mixBuffer[s * 2 + 1] += sampleR;
			} 
			else
			{
				ERROR_LOG(HLE, "channel %i buffer underrun at %i of %i", i, s, hwBlockSize);
				break;
			}
		}

		if (chans[i].sampleQueue.size() < chans[i].sampleCount * 2 * chanQueueMinSizeFactor)
		{
			// Ask the thread to send more samples until next time, queue is being drained.
			if (chans[i].waitingThread) {
				SceUID waitingThread = chans[i].waitingThread;
				chans[i].waitingThread = 0;
				// DEBUG_LOG(HLE, "Woke thread %i for some buffer filling", waitingThread);
				__KernelResumeThreadFromWait(waitingThread);
			}
		}
	}

	section.lock();

	if (g_Config.bEnableSound && outAudioQueue.room() >= hwBlockSize * 2) {
		// Push the mixed samples onto the output audio queue.
		for (int i = 0; i < hwBlockSize; i++) {
			s32 sampleL = mixBuffer[i * 2] >> 2;  // TODO - what factor?
			s32 sampleR = mixBuffer[i * 2 + 1] >> 2;

			outAudioQueue.push((s16)sampleL);
			outAudioQueue.push((s16)sampleR);
		}
	}
	
	section.unlock();
}

void __AudioSetOutputFrequency(int freq)
{
	mixFrequency = freq;
}

// numFrames is number of stereo frames.
int __AudioMix(short *outstereo, int numFrames)
{
	// TODO: if mixFrequency != the actual output frequency, resample!

	section.lock();
	int underrun = -1;
	s16 sampleL = 0;
	s16 sampleR = 0;
	bool anythingToPlay = false;
	for (int i = 0; i < numFrames; i++) {
		if (outAudioQueue.size() >= 2)
		{
			sampleL = outAudioQueue.front();
			outAudioQueue.pop();
			sampleR = outAudioQueue.front();
			outAudioQueue.pop();
			outstereo[i * 2] = sampleL;
			outstereo[i * 2 + 1] = sampleR;
			anythingToPlay = true;
		} else {
			if (underrun == -1) underrun = i;
			outstereo[i * 2] = sampleL;  // repeat last sample, can reduce clicking
			outstereo[i * 2 + 1] = sampleR;  // repeat last sample, can reduce clicking
		}
	}
	if (anythingToPlay && underrun >= 0) {
		DEBUG_LOG(HLE, "audio out buffer UNDERRUN at %i of %i", underrun, numFrames);
	} else {
		// DEBUG_LOG(HLE, "No underrun, mixed %i samples fine", numFrames);
	}
	section.unlock();
	return numFrames;
}
