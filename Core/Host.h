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

#include <cstring>
#include <string>
#include "../Globals.h"


class PMixer
{
public:
	PMixer() {}
	virtual ~PMixer() {}
	virtual int Mix(short *stereoout, int numSamples) {memset(stereoout,0,numSamples*2*sizeof(short)); return numSamples;}
};



class Host
{
public:
	virtual ~Host() {}
	//virtual void StartThread()
	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual void InitGL() = 0;
	virtual void BeginFrame() {}
	virtual void EndFrame() {}
	virtual void ShutdownGL() = 0;

	virtual void InitSound(PMixer *mixer) = 0;
	virtual void UpdateSound() {}
	virtual void ShutdownSound() = 0;

	//this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {}
	virtual void PrepareShutdown() {}

	virtual bool IsDebuggingEnabled() {return true;}
	virtual bool AttemptLoadSymbolMap() {return false;}
	virtual void SetWindowTitle(const char *message) {}

	// Used for headless.
	virtual void SendDebugOutput(const std::string &output) {}
};

extern Host *host;
