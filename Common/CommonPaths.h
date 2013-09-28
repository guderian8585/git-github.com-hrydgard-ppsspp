// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _COMMON_PATHS_H_
#define _COMMON_PATHS_H_

// Make sure we pick up USER_DIR if set in config.h
#include "Common.h"

// Directory seperators, do we need this?
#define DIR_SEP "/"
#ifdef _WIN32
	#define DIR_SEP_CHRS "/\\"
#else
	#define DIR_SEP_CHRS "/"
#endif

// The user data dir
#define ROOT_DIR "."
#ifdef _WIN32
	#define USERDATA_DIR "User"
	#define DOLPHIN_DATA_DIR "Dolphin"
#elif defined __APPLE__
	// On OS X, USERDATA_DIR exists within the .app, but *always* reference
	// the copy in Application Support instead! (Copied on first run)
	// You can use the File::GetUserPath() util for this
	#define USERDATA_DIR "Contents/Resources/User"
	#define DOLPHIN_DATA_DIR "Library/Application Support/Dolphin"
#else
	#define USERDATA_DIR "user"
	#ifdef USER_DIR
		#define DOLPHIN_DATA_DIR USER_DIR
	#else
		#define DOLPHIN_DATA_DIR ".dolphin"
	#endif
#endif

// Shared data dirs (Sys and shared User for linux)
#ifdef _WIN32
	#define SYSDATA_DIR "Sys"
#elif defined __APPLE__
	#define SYSDATA_DIR "Contents/Resources/Sys"
	#define SHARED_USER_DIR	File::GetBundleDirectory() + \
				DIR_SEP USERDATA_DIR DIR_SEP
#else
	#ifdef DATA_DIR
		#define SYSDATA_DIR DATA_DIR "sys"
		#define SHARED_USER_DIR  DATA_DIR USERDATA_DIR DIR_SEP
	#else
		#define SYSDATA_DIR "sys"
		#define SHARED_USER_DIR  ROOT_DIR DIR_SEP USERDATA_DIR DIR_SEP
	#endif
#endif

// Subdirs in the User dir returned by GetUserPath(D_USER_IDX)
#define CONFIG_DIR		"Config"
#define SCREENSHOTS_DIR	"ScreenShots"
#define LOGS_DIR		"Logs"

// Filenames
// Files in the directory returned by GetUserPath(D_CONFIG_IDX)
#define CONFIG_FILE "ppsspp.ini"

// Files in the directory returned by GetUserPath(D_LOGS_IDX)
#define MAIN_LOG	"ppsspp.log"

#endif // _COMMON_PATHS_H_
