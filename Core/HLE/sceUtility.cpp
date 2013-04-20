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
#include "../MIPS/MIPS.h"
#include "Core/Reporting.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceUtility.h"

#include "sceCtrl.h"
#include "../Util/PPGeDraw.h"
#include "../Dialog/PSPSaveDialog.h"
#include "../Dialog/PSPMsgDialog.h"
#include "../Dialog/PSPPlaceholderDialog.h"
#include "../Dialog/PSPOskDialog.h"

const int SCE_ERROR_MODULE_BAD_ID = 0x80111101;
const int SCE_ERROR_AV_MODULE_BAD_ID = 0x80110F01;

enum UtilityDialogType {
	UTILITY_DIALOG_NONE,
	UTILITY_DIALOG_SAVEDATA,
	UTILITY_DIALOG_MSG,
	UTILITY_DIALOG_OSK,
	UTILITY_DIALOG_NET,
};

// Only a single dialog is allowed at a time.
static UtilityDialogType currentDialogType;
static bool currentDialogActive;
static PSPSaveDialog saveDialog;
static PSPMsgDialog msgDialog;
static PSPOskDialog oskDialog;
static PSPPlaceholderDialog netDialog;

void __UtilityInit()
{
	currentDialogType = UTILITY_DIALOG_NONE;
	currentDialogActive = false;
	SavedataParam::Init();
}

void __UtilityDoState(PointerWrap &p)
{
	p.Do(currentDialogType);
	p.Do(currentDialogActive);
	saveDialog.DoState(p);
	msgDialog.DoState(p);
	oskDialog.DoState(p);
	netDialog.DoState(p);
	p.DoMarker("sceUtility");
}

void __UtilityShutdown()
{
	saveDialog.Shutdown();
	msgDialog.Shutdown();
	oskDialog.Shutdown();
	netDialog.Shutdown();
}

int sceUtilitySavedataInitStart(u32 paramAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(HLE, "sceUtilitySavedataInitStart(%08x): wrong dialog type", paramAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_SAVEDATA;
	currentDialogActive = true;

	DEBUG_LOG(HLE,"sceUtilitySavedataInitStart(%08x)", paramAddr);
	return saveDialog.Init(paramAddr);
}

int sceUtilitySavedataShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(HLE, "sceUtilitySavedataShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(HLE,"sceUtilitySavedataShutdownStart()");
	return saveDialog.Shutdown();
}

int sceUtilitySavedataGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		DEBUG_LOG(HLE, "sceUtilitySavedataGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = saveDialog.GetStatus();
	DEBUG_LOG(HLE, "%08x=sceUtilitySavedataGetStatus()", status);
	return status;
}

int sceUtilitySavedataUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_SAVEDATA)
	{
		WARN_LOG(HLE, "sceUtilitySavedataUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(HLE,"sceUtilitySavedataUpdate(%d)", animSpeed);
	int result = saveDialog.Update();
	if (result >= 0)
		return hleDelayResult(result, "savedata update", 300);
	return result;
}

#define PSP_AV_MODULE_AVCODEC		0
#define PSP_AV_MODULE_SASCORE		1
#define PSP_AV_MODULE_ATRAC3PLUS	2 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MPEGBASE		3 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MP3		4
#define PSP_AV_MODULE_VAUDIO		5
#define PSP_AV_MODULE_AAC		6
#define PSP_AV_MODULE_G729		7

u32 sceUtilityLoadAvModule(u32 module)
{
	if (module > 7)
	{
		ERROR_LOG_REPORT(HLE, "sceUtilityLoadAvModule(%i): invalid module id", module);
		return SCE_ERROR_AV_MODULE_BAD_ID;
	}

	DEBUG_LOG(HLE,"sceUtilityLoadAvModule(%i)", module);
	return hleDelayResult(0, "utility av module loaded", 25000);
}

u32 sceUtilityUnloadAvModule(u32 module)
{
	DEBUG_LOG(HLE,"sceUtilityUnloadAvModule(%i)", module);
	return hleDelayResult(0, "utility av module unloaded", 800);
}

u32 sceUtilityLoadModule(u32 module)
{
	// TODO: Not all modules between 0x100 and 0x601 are valid.
	if (module < 0x100 || module > 0x601)
	{
		ERROR_LOG_REPORT(HLE, "sceUtilityLoadModule(%i): invalid module id", module);
		return SCE_ERROR_MODULE_BAD_ID;
	}

	DEBUG_LOG(HLE, "sceUtilityLoadModule(%i)", module);
	// TODO: Each module has its own timing, technically, but this is a low-end.
	// Note: Some modules have dependencies, but they still resched.
	if (module == 0x3FF)
		return hleDelayResult(0, "utility module loaded", 130);
	else
		return hleDelayResult(0, "utility module loaded", 25000);
}

u32 sceUtilityUnloadModule(u32 module)
{
	// TODO: Not all modules between 0x100 and 0x601 are valid.
	if (module < 0x100 || module > 0x601)
	{
		ERROR_LOG_REPORT(HLE, "sceUtilityUnloadModule(%i): invalid module id", module);
		return SCE_ERROR_MODULE_BAD_ID;
	}

	DEBUG_LOG(HLE, "sceUtilityUnloadModule(%i)", module);
	// TODO: Each module has its own timing, technically, but this is a low-end.
	// Note: If not loaded, it should not reschedule actually...
	if (module == 0x3FF)
		return hleDelayResult(0, "utility module unloaded", 110);
	else
		return hleDelayResult(0, "utility module unloaded", 400);
}

int sceUtilityMsgDialogInitStart(u32 structAddr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(HLE, "sceUtilityMsgDialogInitStart(%08x): wrong dialog type", structAddr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_MSG;
	currentDialogActive = true;

	DEBUG_LOG(HLE, "sceUtilityMsgDialogInitStart(%08x)", structAddr);
	return msgDialog.Init(structAddr);
}

int sceUtilityMsgDialogShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(HLE, "sceUtilityMsgDialogShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(HLE, "sceUtilityMsgDialogShutdownStart()");
	return msgDialog.Shutdown();
}

int sceUtilityMsgDialogUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(HLE, "sceUtilityMsgDialogUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(HLE,"sceUtilityMsgDialogUpdate(%i)", animSpeed);
	return msgDialog.Update();
}

int sceUtilityMsgDialogGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		DEBUG_LOG(HLE, "sceUtilityMsgDialogGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = msgDialog.GetStatus();
	DEBUG_LOG(HLE, "%08x=sceUtilityMsgDialogGetStatus()", status);
	return status;
}

int sceUtilityMsgDialogAbort()
{
	if (currentDialogType != UTILITY_DIALOG_MSG)
	{
		WARN_LOG(HLE, "sceUtilityMsgDialogShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(HLE, "sceUtilityMsgDialogAbort()");
	return msgDialog.Abort();
}


// On screen keyboard

int sceUtilityOskInitStart(u32 oskPtr)
{
	if (currentDialogActive && currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(HLE, "sceUtilityOskInitStart(%08x): wrong dialog type", oskPtr);
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogType = UTILITY_DIALOG_OSK;
	currentDialogActive = true;

	DEBUG_LOG(HLE, "sceUtilityOskInitStart(%08x)", oskPtr);
	return oskDialog.Init(oskPtr);
}

int sceUtilityOskShutdownStart()
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(HLE, "sceUtilityOskShutdownStart(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}
	currentDialogActive = false;

	DEBUG_LOG(HLE, "sceUtilityOskShutdownStart()");
	return oskDialog.Shutdown();
}

int sceUtilityOskUpdate(int animSpeed)
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		WARN_LOG(HLE, "sceUtilityMsgDialogUpdate(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	DEBUG_LOG(HLE, "sceUtilityOskUpdate(%i)", animSpeed);
	return oskDialog.Update();
}

int sceUtilityOskGetStatus()
{
	if (currentDialogType != UTILITY_DIALOG_OSK)
	{
		DEBUG_LOG(HLE, "sceUtilityOskGetStatus(): wrong dialog type");
		return SCE_ERROR_UTILITY_WRONG_TYPE;
	}

	int status = oskDialog.GetStatus();

	// Seems that 4 is the cancelled status for OSK?
	if (status == 4)
	{
		status = 5;
	}

	DEBUG_LOG(HLE, "%08x=sceUtilityOskGetStatus()", status);
	return status;
}


int sceUtilityNetconfInitStart(u32 structAddr)
{
	ERROR_LOG(HLE, "UNIMPL sceUtilityNetconfInitStart(%08x)", structAddr);
	return netDialog.Init();
}

int sceUtilityNetconfShutdownStart(unsigned int unknown)
{
	ERROR_LOG(HLE, "UNIMPL sceUtilityNetconfShutdownStart(%i)", unknown);
	return netDialog.Shutdown();
}

int sceUtilityNetconfUpdate(int animSpeed)
{
	ERROR_LOG(HLE, "UNIMPL sceUtilityNetconfUpdate(%i)", animSpeed);
	return netDialog.Update();
}

int sceUtilityNetconfGetStatus()
{
	ERROR_LOG(HLE, "UNIMPL sceUtilityNetconfGetStatus()");
	return netDialog.GetStatus();
}

int sceUtilityScreenshotGetStatus()
{
	u32 retval =  0;//__UtilityGetStatus();
	ERROR_LOG(HLE, "UNIMPL %i=sceUtilityScreenshotGetStatus()", retval);
	return retval;
}

void sceUtilityGamedataInstallInitStart(u32 unkown)
{
	ERROR_LOG(HLE, "UNIMPL sceUtilityGamedataInstallInitStart(%i)", unkown);
}

int sceUtilityGamedataInstallGetStatus()
{
	u32 retval = 0;//__UtilityGetStatus();
	ERROR_LOG(HLE, "UNIMPL %i=sceUtilityGamedataInstallGetStatus()", retval);
	return retval;
}

//TODO: should save to config file
u32 sceUtilitySetSystemParamString(u32 id, u32 strPtr)
{
	WARN_LOG(HLE, "sceUtilitySetSystemParamString(%i, %08x)", id, strPtr);
	return 0;
}

u32 sceUtilityGetSystemParamString(u32 id, u32 destaddr, int destSize)
{
	DEBUG_LOG(HLE, "sceUtilityGetSystemParamString(%i, %08x, %i)", id, destaddr, destSize);
	char *buf = (char *)Memory::GetPointer(destaddr);
	switch (id) {
	case PSP_SYSTEMPARAM_ID_STRING_NICKNAME:
		// If there's not enough space for the string and null terminator, fail.
		if (destSize <= (int)g_Config.sNickName.length())
			return PSP_SYSTEMPARAM_RETVAL_STRING_TOO_LONG;
		strncpy(buf, g_Config.sNickName.c_str(), destSize);
		break;

	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	return 0;
}

u32 sceUtilityGetSystemParamInt(u32 id, u32 destaddr)
{
	DEBUG_LOG(HLE,"sceUtilityGetSystemParamInt(%i, %08x)", id,destaddr);
	u32 param = 0;
	switch (id) {
	case PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL:
		param = g_Config.iWlanAdhocChannel;
		break;
	case PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE:
		param = g_Config.bWlanPowerSave?PSP_SYSTEMPARAM_WLAN_POWERSAVE_ON:PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF;
		break;
	case PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT:
		param = g_Config.iDateFormat;
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT:
		param = g_Config.itimeformat?PSP_SYSTEMPARAM_TIME_FORMAT_12HR:PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
		break;
	case PSP_SYSTEMPARAM_ID_INT_TIMEZONE:
		param = g_Config.iTimeZone;
		break;
	case PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS:
		param = g_Config.bDayLightSavings?PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING:PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD;
		break;
	case PSP_SYSTEMPARAM_ID_INT_LANGUAGE:
		param = g_Config.ilanguage;
		break;
	case PSP_SYSTEMPARAM_ID_INT_BUTTON_PREFERENCE:
		param = g_Config.bButtonPreference?PSP_SYSTEMPARAM_BUTTON_CROSS:PSP_SYSTEMPARAM_BUTTON_CIRCLE;
		break;
	case PSP_SYSTEMPARAM_ID_INT_LOCK_PARENTAL_LEVEL:
		param = g_Config.iLockParentalLevel;
		break;
	default:
		return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	Memory::Write_U32(param, destaddr);

	return 0;
}

u32 sceUtilityLoadNetModule(u32 module)
{
	DEBUG_LOG(HLE,"FAKE: sceUtilityLoadNetModule(%i)", module);
	return 0;
}

u32 sceUtilityUnloadNetModule(u32 module)
{
	DEBUG_LOG(HLE,"FAKE: sceUtilityUnloadNetModule(%i)", module);
	return 0;
}

void sceUtilityInstallInitStart(u32 unknown)
{
	DEBUG_LOG(HLE,"FAKE sceUtilityInstallInitStart()");
}

const HLEFunction sceUtility[] = 
{
	{0x1579a159, &WrapU_U<sceUtilityLoadNetModule>, "sceUtilityLoadNetModule"},
	{0x64d50c56, &WrapU_U<sceUtilityUnloadNetModule>, "sceUtilityUnloadNetModule"},


	{0xf88155f6, &WrapI_U<sceUtilityNetconfShutdownStart>, "sceUtilityNetconfShutdownStart"},
	{0x4db1e739, &WrapI_U<sceUtilityNetconfInitStart>, "sceUtilityNetconfInitStart"},
	{0x91e70e35, &WrapI_I<sceUtilityNetconfUpdate>, "sceUtilityNetconfUpdate"},
	{0x6332aa39, &WrapI_V<sceUtilityNetconfGetStatus>, "sceUtilityNetconfGetStatus"},
	{0x5eee6548, 0, "sceUtilityCheckNetParam"},
	{0x434d4b3a, 0, "sceUtilityGetNetParam"},
	{0x4FED24D8, 0, "sceUtilityGetNetParamLatestID"},

	{0x67af3428, &WrapI_V<sceUtilityMsgDialogShutdownStart>, "sceUtilityMsgDialogShutdownStart"},
	{0x2ad8e239, &WrapI_U<sceUtilityMsgDialogInitStart>, "sceUtilityMsgDialogInitStart"},
	{0x95fc253b, &WrapI_I<sceUtilityMsgDialogUpdate>, "sceUtilityMsgDialogUpdate"},
	{0x9a1c91d7, &WrapI_V<sceUtilityMsgDialogGetStatus>, "sceUtilityMsgDialogGetStatus"},
	{0x4928bd96, &WrapI_V<sceUtilityMsgDialogAbort>, "sceUtilityMsgDialogAbort"},

	{0x9790b33c, &WrapI_V<sceUtilitySavedataShutdownStart>, "sceUtilitySavedataShutdownStart"},
	{0x50c4cd57, &WrapI_U<sceUtilitySavedataInitStart>, "sceUtilitySavedataInitStart"},
	{0xd4b95ffb, &WrapI_I<sceUtilitySavedataUpdate>, "sceUtilitySavedataUpdate"},
	{0x8874dbe0, &WrapI_V<sceUtilitySavedataGetStatus>, "sceUtilitySavedataGetStatus"},

	{0x3dfaeba9, &WrapI_V<sceUtilityOskShutdownStart>, "sceUtilityOskShutdownStart"},
	{0xf6269b82, &WrapI_U<sceUtilityOskInitStart>, "sceUtilityOskInitStart"},
	{0x4b85c861, &WrapI_I<sceUtilityOskUpdate>, "sceUtilityOskUpdate"},
	{0xf3f76017, &WrapI_V<sceUtilityOskGetStatus>, "sceUtilityOskGetStatus"},

	{0x41e30674, &WrapU_UU<sceUtilitySetSystemParamString>, "sceUtilitySetSystemParamString"},
	{0x45c18506, 0, "sceUtilitySetSystemParamInt"}, 
	{0x34b78343, &WrapU_UUI<sceUtilityGetSystemParamString>, "sceUtilityGetSystemParamString"},
	{0xA5DA2406, &WrapU_UU<sceUtilityGetSystemParamInt>, "sceUtilityGetSystemParamInt"},


	{0xc492f751, 0, "sceUtilityGameSharingInitStart"},
	{0xefc6f80f, 0, "sceUtilityGameSharingShutdownStart"},
	{0x7853182d, 0, "sceUtilityGameSharingUpdate"},
	{0x946963f3, 0, "sceUtilityGameSharingGetStatus"},

	{0x2995d020, 0, "sceUtility_2995d020"},
	{0xb62a4061, 0, "sceUtility_b62a4061"},
	{0xed0fad38, 0, "sceUtility_ed0fad38"},
	{0x88bc7406, 0, "sceUtility_88bc7406"},

	{0xbda7d894, 0, "sceUtilityHtmlViewerGetStatus"},
	{0xcdc3aa41, 0, "sceUtilityHtmlViewerInitStart"},
	{0xf5ce1134, 0, "sceUtilityHtmlViewerShutdownStart"},
	{0x05afb9e4, 0, "sceUtilityHtmlViewerUpdate"},

	{0xc629af26, &WrapU_U<sceUtilityLoadAvModule>, "sceUtilityLoadAvModule"},
	{0xf7d8d092, &WrapU_U<sceUtilityUnloadAvModule>, "sceUtilityUnloadAvModule"},

	{0x2a2b3de0, &WrapU_U<sceUtilityLoadModule>, "sceUtilityLoadModule"},
	{0xe49bfe92, &WrapU_U<sceUtilityUnloadModule>, "sceUtilityUnloadModule"},

	{0x0251B134, 0, "sceUtilityScreenshotInitStart"},
	{0xF9E0008C, 0, "sceUtilityScreenshotShutdownStart"},
	{0xAB083EA9, 0, "sceUtilityScreenshotUpdate"},
	{0xD81957B7, &WrapI_V<sceUtilityScreenshotGetStatus>, "sceUtilityScreenshotGetStatus"},
	{0x86A03A27, 0, "sceUtilityScreenshotContStart"},

	{0x0D5BC6D2, 0, "sceUtilityLoadUsbModule"},
	{0xF64910F0, 0, "sceUtilityUnloadUsbModule"},

	{0x24AC31EB, &WrapV_U<sceUtilityGamedataInstallInitStart>, "sceUtilityGamedataInstallInitStart"},
	{0x32E32DCB, 0, "sceUtilityGamedataInstallShutdownStart"},
	{0x4AECD179, 0, "sceUtilityGamedataInstallUpdate"},
	{0xB57E95D9, &WrapI_V<sceUtilityGamedataInstallGetStatus>, "sceUtilityGamedataInstallGetStatus"},
	{0x180F7B62, 0, "sceUtilityGamedataInstallAbort"},

	{0x16D02AF0, 0, "sceUtilityNpSigninInitStart"},
	{0xE19C97D6, 0, "sceUtilityNpSigninShutdownStart"},
	{0xF3FBC572, 0, "sceUtilityNpSigninUpdate"},
	{0x86ABDB1B, 0, "sceUtilityNpSigninGetStatus"},

	{0x1281DA8E, &WrapV_U<sceUtilityInstallInitStart>, "sceUtilityInstallInitStart"},
	{0x5EF1C24A, 0, "sceUtilityInstallShutdownStart"},
	{0xA03D29BA, 0, "sceUtilityInstallUpdate"},
	{0xC4700FA3, 0, "sceUtilityInstallGetStatus"},

};

void Register_sceUtility()
{
	RegisterModule("sceUtility", ARRAY_SIZE(sceUtility), sceUtility);
}
