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

#include "HLE.h"
#include "FunctionWrappers.h"
#include "../MIPS/MIPS.h"


const int PSP_LANGUAGE_JAPANESE = 0;
const int PSP_LANGUAGE_ENGLISH = 1;
const int PSP_LANGUAGE_FRENCH = 2;
const int PSP_LANGUAGE_SPANISH = 3;
const int PSP_LANGUAGE_GERMAN = 4;
const int PSP_LANGUAGE_ITALIAN = 5;
const int PSP_LANGUAGE_DUTCH = 6;
const int PSP_LANGUAGE_PORTUGUESE = 7;
const int PSP_LANGUAGE_RUSSIAN = 8;
const int PSP_LANGUAGE_KOREAN = 9;
const int PSP_LANGUAGE_TRADITIONAL_CHINESE = 10;
const int PSP_LANGUAGE_SIMPLIFIED_CHINESE = 11;

static u32 language = PSP_LANGUAGE_ENGLISH;
static u32 buttonValue = 0;
static u32 umdPopup = 0;

void __ImposeInit()
{
	language = PSP_LANGUAGE_ENGLISH;
	buttonValue = 0;
	umdPopup = 0;
}

void __ImposeDoState(PointerWrap &p)
{
	p.Do(language);
	p.Do(buttonValue);
	p.Do(umdPopup);
	p.DoMarker("sceImpose");
}

u32 sceImposeGetBatteryIconStatus(u32 chargingPtr, u32 iconStatusPtr)
{
	DEBUG_LOG(HLE, "sceImposeGetBatteryIconStatus(%08x, %08x)", chargingPtr, iconStatusPtr);
	if (Memory::IsValidAddress(chargingPtr))
		Memory::Write_U32(1, chargingPtr);
	if (Memory::IsValidAddress(iconStatusPtr))
		Memory::Write_U32(3, iconStatusPtr);
	return 0;
}

u32 sceImposeSetLanguageMode(u32 languageVal, u32 buttonVal)
{
	DEBUG_LOG(HLE, "sceImposeSetLanguageMode(%08x, %08x)", languageVal, buttonVal);
	language = languageVal;
	buttonValue = buttonVal;
	return 0;
}

u32 sceImposeGetLanguageMode(u32 languagePtr, u32 btnPtr)
{
	DEBUG_LOG(HLE, "sceImposeGetLanguageMode(%08x, %08x)", languagePtr, btnPtr);
	if (Memory::IsValidAddress(languagePtr))
		Memory::Write_U32(language, languagePtr);
	if (Memory::IsValidAddress(btnPtr))
		Memory::Write_U32(buttonValue, btnPtr);
	return 0;
}

u32 sceImposeSetUMDPopup(int value) {
	DEBUG_LOG(HLE, "sceImposeSetUMDPopup(%i)", value);
	umdPopup = value;
	return 0;
}

u32 sceImposeGetUMDPopup() {
	DEBUG_LOG(HLE, "sceImposeGetUMDPopup()");
	return umdPopup;
}

//OSD stuff? home button?
const HLEFunction sceImpose[] =
{
	{0x36aa6e91, WrapU_UU<sceImposeSetLanguageMode>, "sceImposeSetLanguageMode"},  // Seen
	{0x381bd9e7, 0, "sceImposeHomeButton"},
	{0x24fd7bcf, WrapU_UU<sceImposeGetLanguageMode>, "sceImposeGetLanguageMode"},
	{0x8c943191, WrapU_UU<sceImposeGetBatteryIconStatus>, "sceImposeGetBatteryIconStatus"},
	{0x72189C48, WrapU_I<sceImposeSetUMDPopup>, "sceImposeSetUMDPopup"},
	{0xE0887BC8, WrapU_V<sceImposeGetUMDPopup>, "sceImposeGetUMDPopup"},
};

void Register_sceImpose()
{
	RegisterModule("sceImpose", ARRAY_SIZE(sceImpose), sceImpose);
}
