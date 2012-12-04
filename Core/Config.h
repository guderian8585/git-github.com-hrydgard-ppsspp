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

#include <string>


struct SState
{
	bool bEmuThreadStarted;	// is anything loaded into the emulator?
	bool bBooted;
};

struct CConfig
{
public:
	CConfig();
	~CConfig();

	// Many of these are currently broken.
	bool bEnableSound;
	bool bAutoLoadLast;
	bool bSaveSettings;
	bool bFirstRun;
	bool bAutoRun;
	bool bSpeedLimit;
	bool bConfirmOnQuit;
	bool bIgnoreBadMemAccess;
	bool bDisplayFramebuffer;
	bool bBufferedRendering;
	bool bShowDebuggerOnLoad;	 
	bool bShowAnalogStick;
	bool bShowFPSCounter;
	bool bShowDebugStats;
	int iWindowZoom;  // for Windows
	int iCpuCore;

	std::string currentDirectory;

	void Load(const char *iniFileName = "ppsspp.ini");
	void Save();
private:
	std::string iniFilename_;
};


extern SState g_State;
extern CConfig g_Config;
