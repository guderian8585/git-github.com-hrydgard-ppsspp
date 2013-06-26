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
#include "CommonTypes.h"

// This bool is the key to having the HD remasters work.
// We keep it set to false by default in PSPLoaders.cpp
// in order to keep the 99% of other PSP games working happily.
extern bool g_RemasterMode;
extern bool g_DoubleTextureCoordinates;

struct HDRemaster {
	std::string gameID;
	u64 MemorySize;
	u64 MemoryEnd; //Seems to be different for each game as well
	bool DoubleTextureCoordinates;
};

// TODO: Are those BLJM* IDs really valid? They seem to be the physical PS3 disk IDs,
// but they're included for safety.
// TODO: Do all of the remasters aside from Monster Hunter use double texture coordinates?
// TODO: Are all remasters happy with this end address?
const struct HDRemaster g_HDRemasters[] = {
	{ "NPJB40001", 0x4000000, 0x0BBFFFFF, false }, // MONSTER HUNTER PORTABLE 3rd HD Ver.
	{ "BLJM85002", 0x4000000, 0x0BBFFFFF, true }, // K-ON Houkago Live HD Ver
	{ "NPJB40002", 0x4000000, 0x0BBFFFFF, true }, // K-ON Houkago Live HD Ver
	{ "BLJM85003", 0x4000000, 0x0BBFFFFF, true }, // Shin Sangoku Musou Multi Raid 2 HD Ver
	{ "NPJB40003", 0x4000000, 0x0BBFFFFF, true }, // Shin Sangoku Musou Multi Raid 2 HD Ver
	{ "BLJM85004", 0x4000000, 0x0BBFFFFF, true }, // Eiyuu Densetsu Sora no Kiseki FC Kai HD Edition
	{ "NPJB40004", 0x4000000, 0x0BBFFFFF, true }, // Eiyuu Densetsu Sora no Kiseki FC Kai HD Edition
	{ "BLJM85005", 0x4000000, 0x0BBFFFFF, true }, // Eiyuu Densetsu: Sora no Kiseki SC Kai HD Edition
	{ "NPJB40005", 0x4000000, 0x0BBFFFFF, true }, // Eiyuu Densetsu: Sora no Kiseki SC Kai HD Edition
};
