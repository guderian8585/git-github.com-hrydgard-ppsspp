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

// TODO: Remove the Windows-specific code, FILE is fine there too.

#include <map>
#include <string>

#include "../Core/FileSystems/FileSystem.h"

#ifdef _WIN32
typedef void * HANDLE;
#endif


class DirectoryFileSystem : public IFileSystem
{
	struct OpenFileEntry
	{
#ifdef _WIN32
		HANDLE hFile;
#else
		FILE *hFile;
#endif
	};

	typedef std::map<u32,OpenFileEntry> EntryMap;
	EntryMap entries;
	std::string basePath;
	IHandleAllocator *hAlloc;


  // In case of Windows: Translate slashes, etc.
	std::string GetLocalPath(std::string localpath);

public:
	DirectoryFileSystem(IHandleAllocator *_hAlloc, std::string _basePath);
	std::vector<PSPFileInfo> GetDirListing(std::string path);
	u32      OpenFile(std::string filename, FileAccess access);
	void     CloseFile(u32 handle);
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size);
	size_t   WriteFile(u32 handle, const u8 *pointer, s64 size);
	size_t   SeekFile(u32 handle, s32 position, FileMove type);
	PSPFileInfo GetFileInfo(std::string filename);
	bool     OwnsHandle(u32 handle);
	bool MkDir(const std::string &dirname);
	bool RmDir(const std::string &dirname);
	bool RenameFile(const std::string &from, const std::string &to);
	bool DeleteFile(const std::string &filename);
};
 
