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

#include "../Globals.h"
#include "../Common/ChunkFile.h"
#include <deque>

enum DisplayListStatus
{
	PSP_GE_LIST_DONE     = 0, // reached finish+end
	PSP_GE_LIST_QUEUED   = 1, // in queue, not stalled
	PSP_GE_LIST_DRAWING  = 2, // drawing or stalled
	PSP_GE_LIST_STALLING = 3, // stalled, never set, only returned by ListSync
	PSP_GE_LIST_PAUSED   = 4, // after break
};

struct DisplayList
{
	u32 pc;
	u32 stall;
	int status;
	int subIntrBase;
	u32 stack[32];
	int stackptr;
	bool interrupted;
	bool queued;

	bool stalled() {
		return pc == stall;
	}
};

class GPUInterface
{
public:
	virtual ~GPUInterface() {}

	static const int DisplayListMaxCount = 64;

	// Initialization
	virtual void InitClear() = 0;

	// Draw queue management
	// TODO: Much of this should probably be shared between the different GPU implementations.
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head) = 0;
	virtual u32  DequeueList(int listid) = 0;
	virtual u32  UpdateStall(int listid, u32 newstall) = 0;
	virtual u32  DrawSync(int mode) = 0;
	virtual u32  Continue() = 0;
	virtual u32  Break(int mode) = 0;

	virtual void PreExecuteOp(u32 op, u32 diff) = 0;
	virtual void ExecuteOp(u32 op, u32 diff) = 0;
	virtual int  ListStatus(int listid) = 0;

	// Framebuffer management
	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) = 0;
	virtual void BeginFrame() = 0;  // Can be a good place to draw the "memory" framebuffer for accelerated plugins
	virtual void CopyDisplayToOutput() = 0;

	// Tells the GPU to update the gpuStats structure.
	virtual void UpdateStats() = 0;

	// Invalidate any cached content sourced from the specified range.
	// If size = -1, invalidate everything.
	virtual void InvalidateCache(u32 addr, int size) = 0;

	// Internal hack to avoid interrupts from "PPGe" drawing (utility UI, etc)
	virtual void EnableInterrupts(bool enable) = 0;

	virtual void DeviceLost() = 0;
	virtual void Flush() = 0;
	virtual void DoState(PointerWrap &p) = 0;

	// Debugging
	virtual void DumpNextFrame() = 0;
};
