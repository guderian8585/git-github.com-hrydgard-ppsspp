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

void sceKernelCancelSema(SceUID id, int newCount, u32 numWaitThreadsPtr);
void sceKernelCreateSema(const char* name, u32 attr, int initVal, int maxVal, u32 optionPtr);
void sceKernelDeleteSema(SceUID id);
void sceKernelPollSema(SceUID id, int wantedCount);
void sceKernelReferSemaStatus(SceUID id, u32 infoPtr);
void sceKernelSignalSema(SceUID id, int signal);
void sceKernelWaitSema(SceUID semaid, int signal, u32 timeoutPtr);
void sceKernelWaitSemaCB(SceUID semaid, int signal, u32 timeoutPtr);

void __KernelSemaTimeout(u64 userdata, int cycleslate);
