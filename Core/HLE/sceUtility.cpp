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

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceUtility.h"


enum SceUtilitySavedataType
{
	SCE_UTILITY_SAVEDATA_TYPE_AUTOLOAD		= 0,
	SCE_UTILITY_SAVEDATA_TYPE_AUTOSAVE		= 1,
	SCE_UTILITY_SAVEDATA_TYPE_LOAD			= 2,
	SCE_UTILITY_SAVEDATA_TYPE_SAVE			= 3,
	SCE_UTILITY_SAVEDATA_TYPE_LISTLOAD		= 4,
	SCE_UTILITY_SAVEDATA_TYPE_LISTSAVE		= 5,
	SCE_UTILITY_SAVEDATA_TYPE_LISTDELETE	= 6,
	SCE_UTILITY_SAVEDATA_TYPE_DELETE		= 7,
	SCE_UTILITY_SAVEDATA_TYPE_SIZES			= 8	
} ;

#define SCE_UTILITY_SAVEDATA_ERROR_TYPE					(0x80110300)

#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_MS			(0x80110301)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_EJECT_MS		(0x80110302)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_ACCESS_ERROR	(0x80110305)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN		(0x80110306)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA			(0x80110307)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM			(0x80110308)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_INTERNAL		(0x8011030b)

#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_MS			(0x80110381)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_EJECT_MS		(0x80110382)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_NOSPACE		(0x80110383)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_PROTECTED	(0x80110384)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_ACCESS_ERROR	(0x80110385)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM			(0x80110388)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_UMD			(0x80110389)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_WRONG_UMD		(0x8011038a)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_INTERNAL		(0x8011038b)

#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_MS			(0x80110341)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_EJECT_MS		(0x80110342)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_MS_PROTECTED	(0x80110344)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_ACCESS_ERROR	(0x80110345)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA		(0x80110347)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_PARAM			(0x80110348)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_INTERNAL		(0x8011034b)

#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_MS			(0x801103C1)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_EJECT_MS		(0x801103C2)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_ACCESS_ERROR	(0x801103C5)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA		(0x801103C7)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_PARAM			(0x801103C8)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_UMD			(0x801103C9)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_WRONG_UMD		(0x801103Ca)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_INTERNAL		(0x801103Cb)

#define SCE_UTILITY_STATUS_NONE 		0
#define SCE_UTILITY_STATUS_INITIALIZE	1
#define SCE_UTILITY_STATUS_RUNNING 		2
#define SCE_UTILITY_STATUS_FINISHED 	3
#define SCE_UTILITY_STATUS_SHUTDOWN 	4


/** title, savedataTitle, detail: parts of the unencrypted SFO
data, it contains what the VSH and standard load screen shows */
typedef struct PspUtilitySavedataSFOParam
{
	char title[0x80];
	char savedataTitle[0x80];
	char detail[0x400];
	unsigned char parentalLevel;
	unsigned char unknown[3];
} PspUtilitySavedataSFOParam;

typedef struct PspUtilitySavedataFileData {
	void *buf;
	SceSize bufSize;
	SceSize size;	/* ??? - why are there two sizes? */
	int unknown;
} PspUtilitySavedataFileData;

/** Structure to hold the parameters for the ::sceUtilitySavedataInitStart function.
*/
typedef struct SceUtilitySavedataParam
{
	/** Size of the structure */
	SceSize size;

	int language;

	int buttonSwap;

	int unknown[4];
	int result;
	int unknown2[4];

	/** mode: 0 to load, 1 to save */
	int mode;
	int bind;

	/** unknown13 use 0x10 */
	int overwriteMode;

	/** gameName: name used from the game for saves, equal for all saves */
	char gameName[16];
	/** saveName: name of the particular save, normally a number */
	char saveName[24];
	/** fileName: name of the data file of the game for example DATA.BIN */
	char fileName[16];

	/** pointer to a buffer that will contain data file unencrypted data */
	void *dataBuf;
	/** size of allocated space to dataBuf */
	SceSize dataBufSize;
	SceSize dataSize;

	PspUtilitySavedataSFOParam sfoParam;

	PspUtilitySavedataFileData icon0FileData;
	PspUtilitySavedataFileData icon1FileData;
	PspUtilitySavedataFileData pic1FileData;
	PspUtilitySavedataFileData snd0FileData;

	unsigned char unknown17[4];
} SceUtilitySavedataParam;



static u32 utilityDialogState = SCE_UTILITY_STATUS_SHUTDOWN;



void __UtilityInit()
{
	utilityDialogState = SCE_UTILITY_STATUS_SHUTDOWN;
}


void __UtilityInitStart()
{
	utilityDialogState = SCE_UTILITY_STATUS_INITIALIZE;
}

void __UtilityShutdownStart()
{
	utilityDialogState = SCE_UTILITY_STATUS_SHUTDOWN;
}

void __UtilityUpdate()
{
	if (utilityDialogState == SCE_UTILITY_STATUS_INITIALIZE)
	{
		utilityDialogState = SCE_UTILITY_STATUS_RUNNING;
	}
	else if (utilityDialogState == SCE_UTILITY_STATUS_RUNNING)
	{
		utilityDialogState = SCE_UTILITY_STATUS_FINISHED;
	}
	else if (utilityDialogState == SCE_UTILITY_STATUS_FINISHED)
	{
		utilityDialogState = SCE_UTILITY_STATUS_SHUTDOWN;
	}
}

u32 __UtilityGetStatus()
{
	u32 ret = utilityDialogState;
	if (utilityDialogState == SCE_UTILITY_STATUS_SHUTDOWN)
		utilityDialogState = SCE_UTILITY_STATUS_NONE;
	if (utilityDialogState == SCE_UTILITY_STATUS_INITIALIZE)
		utilityDialogState = SCE_UTILITY_STATUS_RUNNING;
	return ret;
}


void sceUtilitySavedataInitStart()
{
	SceUtilitySavedataParam *param = (SceUtilitySavedataParam*)Memory::GetPointer(PARAM(0));

	DEBUG_LOG(HLE,"sceUtilitySavedataInitStart(%08x)", PARAM(0));
	DEBUG_LOG(HLE,"Mode: %i", param->mode);
	if (param->mode == 0) //load
	{
		DEBUG_LOG(HLE, "Loading. Title: %s Save: %s File: %s", param->gameName, param->saveName, param->fileName);
		RETURN(SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA);
	}
	else 
	{
		//save
		DEBUG_LOG(HLE, "Saving. Title: %s Save: %s File: %s", param->gameName, param->saveName, param->fileName);
		RETURN(SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_MS);
	}

	__UtilityInitStart();

	// Returning 0 here breaks Bust a Move Deluxe! But should be the right thing to do...
	// The fix is probably to fully implement sceUtility so that it actually works.
	// RETURN(0);
}

void sceUtilitySavedataShutdownStart()
{
	DEBUG_LOG(HLE,"sceUtilitySavedataShutdownStart()");
	__UtilityShutdownStart();
	RETURN(0);
}

void sceUtilitySavedataGetStatus()
{
	u32 retval = __UtilityGetStatus();
	DEBUG_LOG(HLE,"%i=sceUtilitySavedataGetStatus()", retval);
	RETURN(retval);
}

void sceUtilitySavedataUpdate()
{
	ERROR_LOG(HLE,"UNIMPL sceUtilitySavedataUpdate()");
	//draw savedata UI here
	__UtilityUpdate();
	RETURN(0);
}
	

#define PSP_AV_MODULE_AVCODEC					 0
#define PSP_AV_MODULE_SASCORE					 1
#define PSP_AV_MODULE_ATRAC3PLUS				2 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MPEGBASE					3 // Requires PSP_AV_MODULE_AVCODEC loading first
#define PSP_AV_MODULE_MP3							 4
#define PSP_AV_MODULE_VAUDIO						5
#define PSP_AV_MODULE_AAC							 6
#define PSP_AV_MODULE_G729							7

void sceUtilityLoadAvModule()
{
	DEBUG_LOG(HLE,"sceUtilityLoadAvModule(%i)", PARAM(0));
	RETURN(0);
	__KernelReSchedule("utilityloadavmodule");
}

void sceUtilityLoadModule()
{
	DEBUG_LOG(HLE,"sceUtilityLoadModule(%i)", PARAM(0));
	RETURN(0);
	__KernelReSchedule("utilityloadmodule");
}

typedef struct
{
	unsigned int size;	/** Size of the structure */
	int language;		/** Language */
	int buttonSwap;		/** Set to 1 for X/O button swap */
	int graphicsThread;	/** Graphics thread priority */
	int accessThread;	/** Access/fileio thread priority (SceJobThread) */
	int fontThread;		/** Font thread priority (ScePafThread) */
	int soundThread;	/** Sound thread priority */
	int result;			/** Result */
	int reserved[4];	/** Set to 0 */

} pspUtilityDialogCommon;

struct pspMessageDialog
{
	pspUtilityDialogCommon common;
	int result;
	int type;
	u32 errorNum;
	char string[512];
	u32 options;
	u32 buttonPressed;	// 0=?, 1=Yes, 2=No, 3=Back
};

void sceUtilityMsgDialogInitStart()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityMsgDialogInitStart(%i)", PARAM(0));
	pspMessageDialog *dlg = (pspMessageDialog *)Memory::GetPointer(PARAM(0));
	if (dlg->type == 0) // number
	{
		INFO_LOG(HLE, "MsgDialog: %08x", dlg->errorNum);
	}
	else
	{
		INFO_LOG(HLE, "MsgDialog: %s", dlg->string);
	}
	__UtilityInitStart();
}

void sceUtilityMsgDialogShutdownStart()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityMsgDialogShutdownStart(%i)", PARAM(0));
	__UtilityShutdownStart();
	RETURN(0);
}

void sceUtilityMsgDialogUpdate()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityMsgDialogUpdate(%i)", PARAM(0));
	__UtilityUpdate();
	RETURN(0);
}

void sceUtilityMsgDialogGetStatus()
{
	DEBUG_LOG(HLE,"sceUtilityMsgDialogGetStatus()");
	RETURN(__UtilityGetStatus());
}

void sceUtilityOskInitStart()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityOskInitStart(%i)", PARAM(0));
	__UtilityInitStart();
}

void sceUtilityOskShutdownStart()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityOskShutdownStart(%i)", PARAM(0));
	__UtilityShutdownStart();
	RETURN(0);
}

void sceUtilityOskUpdate()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityOskUpdate(%i)", PARAM(0));
	__UtilityUpdate();
	RETURN(0);
}

void sceUtilityOskGetStatus()
{
	DEBUG_LOG(HLE,"sceUtilityOskGetStatus()");
	RETURN(__UtilityGetStatus());
}


void sceUtilityNetconfInitStart()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityNetconfInitStart(%i)", PARAM(0));
	__UtilityInitStart();
}

void sceUtilityNetconfShutdownStart()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityNetconfShutdownStart(%i)", PARAM(0));
	__UtilityShutdownStart();
	RETURN(0);
}

void sceUtilityNetconfUpdate()
{
	DEBUG_LOG(HLE,"FAKE sceUtilityNetconfUpdate(%i)", PARAM(0));
	__UtilityUpdate();
	RETURN(0);
}

void sceUtilityNetconfGetStatus()
{
	DEBUG_LOG(HLE,"sceUtilityNetconfGetStatus()");
	RETURN(__UtilityGetStatus());
}

#define PSP_SYSTEMPARAM_ID_STRING_NICKNAME	1
#define PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL	2
#define PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE	3
#define PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT	4
#define PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT	5
//Timezone offset from UTC in minutes, (EST = -300 = -5 * 60)
#define PSP_SYSTEMPARAM_ID_INT_TIMEZONE		6
#define PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS	7
#define PSP_SYSTEMPARAM_ID_INT_LANGUAGE		8
/**
 * #9 seems to be Region or maybe X/O button swap.
 * It doesn't exist on JAP v1.0
 * is 1 on NA v1.5s
 * is 0 on JAP v1.5s
 * is read-only
 */
#define PSP_SYSTEMPARAM_ID_INT_UNKNOWN		9

/**
 * Return values for the SystemParam functions
 */
#define PSP_SYSTEMPARAM_RETVAL_OK	0
#define PSP_SYSTEMPARAM_RETVAL_FAIL	0x80110103

/**
 * Valid values for PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL
 */
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC 0
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_1		1
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_6		6
#define PSP_SYSTEMPARAM_ADHOC_CHANNEL_11	11

/**
 * Valid values for PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE
 */
#define PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF	0
#define PSP_SYSTEMPARAM_WLAN_POWERSAVE_ON	1

/**
 * Valid values for PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT
 */
#define PSP_SYSTEMPARAM_DATE_FORMAT_YYYYMMDD	0
#define PSP_SYSTEMPARAM_DATE_FORMAT_MMDDYYYY	1
#define PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY	2

/**
 * Valid values for PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT
 */
#define PSP_SYSTEMPARAM_TIME_FORMAT_24HR	0
#define PSP_SYSTEMPARAM_TIME_FORMAT_12HR	1

/**
 * Valid values for PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS
 */
#define PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_STD	0
#define PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING	1

/**
 * Valid values for PSP_SYSTEMPARAM_ID_INT_LANGUAGE
 */
#define PSP_SYSTEMPARAM_LANGUAGE_JAPANESE	0
#define PSP_SYSTEMPARAM_LANGUAGE_ENGLISH	1
#define PSP_SYSTEMPARAM_LANGUAGE_FRENCH		2
#define PSP_SYSTEMPARAM_LANGUAGE_SPANISH	3
#define PSP_SYSTEMPARAM_LANGUAGE_GERMAN		4
#define PSP_SYSTEMPARAM_LANGUAGE_ITALIAN	5
#define PSP_SYSTEMPARAM_LANGUAGE_DUTCH		6
#define PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE	7
#define PSP_SYSTEMPARAM_LANGUAGE_KOREAN		8




u32 sceUtilityGetSystemParamString(u32 id, u32 destaddr, u32 unknownparam)
{
	//DEBUG_LOG(HLE,"sceUtilityGetSystemParamString(%i, %08x, %i)", id,destaddr,unknownparam);
	char *buf = (char *)Memory::GetPointer(destaddr);
	switch (id) {
		case PSP_SYSTEMPARAM_ID_STRING_NICKNAME:
			strcpy(buf, "shadow");
			break;

		default:
			return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	return 0;
}

u32 sceUtilityGetSystemParamInt(u32 id, u32 destaddr)
{
	DEBUG_LOG(HLE,"sceUtilityGetSystemParamInt(%i, %08x)", id,destaddr);
	u32 *outPtr = (u32*)Memory::GetPointer(destaddr);
	switch (id) {
		case PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL:
			*outPtr = PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC;
			break;
		case PSP_SYSTEMPARAM_ID_INT_WLAN_POWERSAVE:
			*outPtr = PSP_SYSTEMPARAM_WLAN_POWERSAVE_OFF;
			break;
		case PSP_SYSTEMPARAM_ID_INT_DATE_FORMAT:
			*outPtr = PSP_SYSTEMPARAM_DATE_FORMAT_DDMMYYYY;
			break;
		case PSP_SYSTEMPARAM_ID_INT_TIME_FORMAT:
			*outPtr = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
			break;
		case PSP_SYSTEMPARAM_ID_INT_TIMEZONE:
			*outPtr = 60;
			break;
		case PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS:
			*outPtr = PSP_SYSTEMPARAM_TIME_FORMAT_24HR;
			break;
		case PSP_SYSTEMPARAM_ID_INT_LANGUAGE:
			*outPtr = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
			break;
		case PSP_SYSTEMPARAM_ID_INT_UNKNOWN:
			*outPtr = 1;
			break;
		default:
			return PSP_SYSTEMPARAM_RETVAL_FAIL;
	}

	return 0;
}

u32 sceUtilityLoadNetModule(u32 module)
{
	DEBUG_LOG(HLE,"FAKE: sceUtilityLoadNetModule(%i)", module);
	return 0;
}

const HLEFunction sceUtility[] = 
{
	{0x1579a159, &WrapU_U<sceUtilityLoadNetModule>, "sceUtilityLoadNetModule"},
	{0xf88155f6, sceUtilityNetconfShutdownStart, "sceUtilityNetconfShutdownStart"},
	{0x4db1e739, sceUtilityNetconfInitStart, "sceUtilityNetconfInitStart"},
	{0x91e70e35, sceUtilityNetconfUpdate, "sceUtilityNetconfUpdate"},
	{0x6332aa39, sceUtilityNetconfGetStatus, "sceUtilityNetconfGetStatus"},

	{0x67af3428, sceUtilityMsgDialogShutdownStart, "sceUtilityMsgDialogShutdownStart"},	
	{0x2ad8e239, sceUtilityMsgDialogInitStart, "sceUtilityMsgDialogInitStart"},			
	{0x95fc253b, sceUtilityMsgDialogUpdate, "sceUtilityMsgDialogUpdate"},				 
	{0x9a1c91d7, sceUtilityMsgDialogGetStatus, "sceUtilityMsgDialogGetStatus"},			

	{0x9790b33c, sceUtilitySavedataShutdownStart, "sceUtilitySavedataShutdownStart"},	 
	{0x50c4cd57, sceUtilitySavedataInitStart, "sceUtilitySavedataInitStart"},			 
	{0xd4b95ffb, sceUtilitySavedataUpdate, "sceUtilitySavedataUpdate"},					
	{0x8874dbe0, sceUtilitySavedataGetStatus, "sceUtilitySavedataGetStatus"},

	{0x3dfaeba9, sceUtilityOskShutdownStart, "sceUtilityOskShutdownStart"}, 
	{0xf6269b82, sceUtilityOskInitStart, "sceUtilityOskInitStart"}, 
	{0x4b85c861, sceUtilityOskUpdate, "sceUtilityOskUpdate"}, 
	{0xf3f76017, sceUtilityOskGetStatus, "sceUtilityOskGetStatus"}, 

	{0x41e30674, 0, "sceUtilitySetSystemParamString"},
	{0x34b78343, &WrapU_UUU<sceUtilityGetSystemParamString>, "sceUtilityGetSystemParamString"}, 
	{0xA5DA2406, &WrapU_UU<sceUtilityGetSystemParamInt>, "sceUtilityGetSystemParamInt"},
	{0xc492f751, 0, "sceUtilityGameSharingInitStart"}, 
	{0xefc6f80f, 0, "sceUtilityGameSharingShutdownStart"}, 
	{0x7853182d, 0, "sceUtilityGameSharingUpdate"}, 
	{0x946963f3, 0, "sceUtilityGameSharingGetStatus"}, 
	{0x2995d020, 0, "sceUtility_2995d020"}, 
	{0xb62a4061, 0, "sceUtility_b62a4061"}, 
	{0xed0fad38, 0, "sceUtility_ed0fad38"}, 
	{0x88bc7406, 0, "sceUtility_88bc7406"}, 
	{0x45c18506, 0, "sceUtilitySetSystemParamInt"}, 
	{0x5eee6548, 0, "sceUtilityCheckNetParam"}, 
	{0x434d4b3a, 0, "sceUtilityGetNetParam"}, 
	{0x64d50c56, 0, "sceUtilityUnloadNetModule"}, 
	{0x4928bd96, 0, "sceUtilityMsgDialogAbort"}, 
	{0xbda7d894, 0, "sceUtilityHtmlViewerGetStatus"}, 
	{0xcdc3aa41, 0, "sceUtilityHtmlViewerInitStart"}, 
	{0xf5ce1134, 0, "sceUtilityHtmlViewerShutdownStart"}, 
	{0x05afb9e4, 0, "sceUtilityHtmlViewerUpdate"}, 
	{0xc629af26, sceUtilityLoadAvModule, "sceUtilityLoadAvModule"}, 
	{0xf7d8d092, 0, "sceUtilityUnloadAvModule"},
	{0x2a2b3de0, sceUtilityLoadModule, "sceUtilityLoadModule"},
	{0xe49bfe92, 0, "sceUtilityUnloadModule"},
	{0x0251B134, 0, "sceUtilityScreenshotInitStart"},
	{0xF9E0008C, 0, "sceUtilityScreenshotShutdownStart"},
	{0xAB083EA9, 0, "sceUtilityScreenshotUpdate"},
	{0xD81957B7, 0, "sceUtilityScreenshotGetStatus"},
	{0x86A03A27, 0, "sceUtilityScreenshotContStart"},

	{0x0D5BC6D2, 0, "sceUtilityLoadUsbModule"},
	{0xF64910F0, 0, "sceUtilityUnloadUsbModule"},

	{0x24AC31EB, 0, "sceUtilityGamedataInstallInitStart"},
  {0x32E32DCB, 0, "sceUtilityGamedataInstallShutdownStart"},
  {0x4AECD179, 0, "sceUtilityGamedataInstallUpdate"},
  {0xB57E95D9, 0, "sceUtilityGamedataInstallGetStatus"},
};

void Register_sceUtility()
{
	RegisterModule("sceUtility", ARRAY_SIZE(sceUtility), sceUtility);
}
