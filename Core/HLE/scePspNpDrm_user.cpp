#include "HLE.h"

int sceNpDrmSetLicenseeKey()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmSetLicenseeKey");
	return 0;
}

int sceNpDrmClearLicenseeKey()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmClearLicenseeKey");
	return 0;
}

int sceNpDrmRenameCheck()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmRenameCheck");
	return 0;
}

int sceNpDrmEdataSetupKey()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmEdataSetupKey");
	return 0;
}

int sceNpDrmEdataGetDataSize()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmEdataGetDataSize");
	return 0;
}

int sceNpDrmOpen()
{
	ERROR_LOG(HLE, "UNIMPL sceNpDrmOpen");
	return 0;
}

int sceKernelLoadModuleNpDrm()
{
	ERROR_LOG(HLE, "UNIMPL sceKernelLoadModuleNpDrm");
	return 0;
}

int sceKernelLoadExecNpDrm()
{
	ERROR_LOG(HLE, "UNIMPL sceKernelLoadExecNpDrm");
	return 0;
}

const HLEFunction sceNpDrm[] =
{ 
	{0xA1336091, 0, "sceNpDrmSetLicenseeKey"},
	{0x9B745542, 0, "sceNpDrmClearLicenseeKey"},
	{0x275987D1, 0, "sceNpDrmRenameCheck"},
	{0x08d98894, 0, "sceNpDrmEdataSetupKey"},
	{0x219EF5CC, 0, "sceNpDrmEdataGetDataSize"},
	{0x2BAA4294, 0, "sceNpDrmOpen"},
	{0xC618D0B1, 0, "sceKernelLoadModuleNpDrm"},
	{0xAA5FC85B, 0, "sceKernelLoadExecNpDrm"},
};
