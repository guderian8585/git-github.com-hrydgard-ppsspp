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

#pragma once
#include "sceAudio.h"
#ifndef SCE_AUDIO_NO_MIX
#define SCE_AUDIO_NO_MIX -1
#endif


// Easy interface for sceAudio to write to, to keep the complexity in check.

void __AudioInit();
void __AudioDoState(PointerWrap &p);
void __AudioUpdate();
void __AudioShutdown();
void __AudioSetOutputFrequency(int freq);

// May return SCE_ERROR_AUDIO_CHANNEL_BUSY if buffer too large
u32 __AudioEnqueue(AudioChannel &chan, int chanNum, bool blocking);
void __AudioWakeThreads(AudioChannel &chan, int result, int step);
void __AudioWakeThreads(AudioChannel &chan, int result);

int __AudioMix(short *outstereo, int numSamples);
