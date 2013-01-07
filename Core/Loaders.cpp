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

#include "file/file_util.h"

#include "MIPS/MIPS.h"
#include "MIPS/MIPSCodeUtils.h"

#include "HLE/HLE.h"
#include "HLE/sceKernelModule.h"
#include "PSPLoaders.h"
#include "MemMap.h"
#include "Loaders.h"
#include "System.h"

// TODO : improve, look in the file more
EmuFileType Identify_File(const char *filename)
{
	//then: easy bulletproof IDs.
	FILE *f = fopen(filename, "rb");
	if (!f)
	{
		//File does not exists
		return FILETYPE_ERROR;
	}
	u32 id;
	fread(&id,4,1,f);
	fclose(f);
	if (id == 'FLE\x7F')
	{
		if (strstr(filename,".plf") || strstr(filename,"BOOT.BIN") || strstr(filename,".elf") || strstr(filename,".prx") )
		{
			return FILETYPE_PSP_ELF;
		}
		else
			return FILETYPE_PSP_ELF;
	}
	else if (id == 'PBP\x00')
	{
		return FILETYPE_PSP_PBP;
	}
	else
	{
		if (strstr(filename,".pbp") || strstr(filename,".PBP"))
		{
			return FILETYPE_PSP_PBP;
		}
		else if (strstr(filename,".iso") || strstr(filename,".ISO"))
		{
			return FILETYPE_PSP_ISO;
		}
		else if (strstr(filename,".cso") || strstr(filename,".CSO"))
		{
			return FILETYPE_PSP_ISO;
		}
		else if (strstr(filename,".bin") || strstr(filename,".BIN"))
		{
			return FILETYPE_UNKNOWN_BIN;
		}
	}
	return FILETYPE_UNKNOWN;
}

bool LoadFile(const char *filename, std::string *error_string)
{
	INFO_LOG(LOADER,"Identifying file...");
	switch (Identify_File(filename))
	{
	case FILETYPE_PSP_PBP:
	case FILETYPE_PSP_ELF:
		{
			INFO_LOG(LOADER,"File is an ELF!");
			std::string path = getDir(filename);
			// If loading from memstick...
			size_t pos = path.find("/PSP/GAME/");
			if (pos != std::string::npos)
				pspFileSystem.SetCurrentDirectory("ms0:" + path.substr(pos));
			return Load_PSP_ELF_PBP(filename, error_string);
		}
	case FILETYPE_PSP_ISO:
		pspFileSystem.SetCurrentDirectory("disc0:/PSP_GAME/USRDIR");
		return Load_PSP_ISO(filename, error_string);
	case FILETYPE_ERROR:
		ERROR_LOG(LOADER, "Could not file");
		*error_string = "Error reading file";
		break;
	case FILETYPE_UNKNOWN_BIN:
	case FILETYPE_UNKNOWN_ELF:
	case FILETYPE_UNKNOWN:
	default:
		ERROR_LOG(LOADER, "Failed to identify file");
		break;
	}
	return false;
}
