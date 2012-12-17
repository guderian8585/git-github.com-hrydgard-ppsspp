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

#include <set>
#include "MetaFileSystem.h"

bool applyPathStringToComponentsVector(std::vector<std::string> &vector, const std::string &pathString)
{
	size_t len = pathString.length();
	size_t start = 0;

	while (start < len)
	{
		size_t i = pathString.find('/', start);
		if (i == std::string::npos)
			i = len;

		if (i > start)
		{
			std::string component = pathString.substr(start, i - start);
			if (component != ".")
			{
				if (component == "..")
				{
					if (vector.size() != 0)
					{
						vector.pop_back();
					}
					else
					{
						//	what does the real PSP do for "/../filename"?
						WARN_LOG(HLE, "RealPath: .. as first path component: \"%s\"", pathString.c_str());
					}
				}
				else
				{
					vector.push_back(component);
				}
			}
		}

		start = i + 1;
	}

	return true;
}

/*
 * Changes relative paths to absolute, removes ".", "..", and trailing "/"
 * babel (and possibly other games) use "/directoryThatDoesNotExist/../directoryThatExists/filename"
 */
bool RealPath(const std::string &currentDirectory, const std::string &inPath, std::string &outPath)
{
	size_t inLen = inPath.length();
	if (inLen == 0)
	{
		ERROR_LOG(HLE, "RealPath: inPath is empty");
		return false;
	}

	size_t inColon = inPath.find(':');
	if (inColon + 1 == inLen)
	{
		WARN_LOG(HLE, "RealPath: inPath is all prefix and no path: \"%s\"", inPath.c_str());

		outPath = inPath;
		return true;
	}

	std::string curDirPrefix;
	size_t curDirColon = std::string::npos, curDirLen = currentDirectory.length();
	if (curDirLen != 0)
	{
		curDirColon = currentDirectory.find(':');
		
		if (curDirColon == std::string::npos)
		{
			DEBUG_LOG(HLE, "RealPath: currentDirectory has no prefix: \"%s\"", currentDirectory.c_str());
		}
		else
		{
			if (curDirColon + 1 == curDirLen)
				DEBUG_LOG(HLE, "RealPath: currentDirectory is all prefix and no path: \"%s\"", currentDirectory.c_str());
			
			curDirPrefix = currentDirectory.substr(0, curDirColon + 1);
		}
	}

	std::string inPrefix, inAfter;

	if (inColon == std::string::npos)
	{
		inPrefix = curDirPrefix;
		inAfter = inPath;
	}
	else
	{
		inPrefix = inPath.substr(0, inColon + 1);
		inAfter = inPath.substr(inColon + 1);
	}

	std::vector<std::string> cmpnts;  // path components
	size_t capacityGuess = inPath.length();

	if ((inAfter[0] != '/'))
	{
		if (curDirLen == 0)
		{
			ERROR_LOG(HLE, "RealPath: inPath \"%s\" is relative, but current directory is empty", inPath.c_str());
			return false;
		}
		
		if (curDirColon == std::string::npos || curDirPrefix.length() == 0)
		{
			ERROR_LOG(HLE, "RealPath: inPath \"%s\" is relative, but current directory \"%s\" has no prefix", inPath.c_str(), currentDirectory.c_str());
			return false;
		}
		
		if (inPrefix != curDirPrefix)
			WARN_LOG(HLE, "RealPath: inPath \"%s\" is relative, but specifies a different prefix than current directory \"%s\"", inPath.c_str(), currentDirectory.c_str());
		
		if (curDirColon + 1 == curDirLen)
		{
			ERROR_LOG(HLE, "RealPath: inPath \"%s\" is relative, but current directory \"%s\" is all prefix and no path. Using \"/\" as path for current directory.", inPath.c_str(), currentDirectory.c_str());
		}
		else
		{
			const std::string curDirAfter = currentDirectory.substr(curDirColon + 1);
			if (! applyPathStringToComponentsVector(cmpnts, curDirAfter) )
			{
				ERROR_LOG(HLE,"RealPath: currentDirectory is not a valid path: \"%s\"", currentDirectory.c_str());
				return false;
			}
		}

		capacityGuess += currentDirectory.length();
	}

	if (! applyPathStringToComponentsVector(cmpnts, inAfter) )
	{
		DEBUG_LOG(HLE, "RealPath: inPath is not a valid path: \"%s\"", inPath.c_str());
		return false;
	}

	outPath.clear();
	outPath.reserve(capacityGuess);

	outPath.append(inPrefix);

	size_t numCmpnts = cmpnts.size();
	for (size_t i = 0; i < numCmpnts; i++)
	{
		outPath.append(1, '/');
		outPath.append(cmpnts[i]);
	}

	return true;
}

IFileSystem *MetaFileSystem::GetHandleOwner(u32 handle)
{
	for (size_t i = 0; i < fileSystems.size(); i++)
	{
		if (fileSystems[i].system->OwnsHandle(handle))
			return fileSystems[i].system; //got it!
	}
	//none found?
	return 0;
}

bool MetaFileSystem::MapFilePath(std::string inpath, std::string &outpath, IFileSystem **system)
{
	//TODO: implement current directory per thread (NOT per drive)
	
	//DEBUG_LOG(HLE, "MapFilePath: starting with \"%s\"", inpath.c_str());

	if ( RealPath(currentDirectory, inpath, inpath) )
	{
		for (size_t i = 0; i < fileSystems.size(); i++)
		{
			size_t prefLen = fileSystems[i].prefix.size();
			if (fileSystems[i].prefix == inpath.substr(0, prefLen))
			{
				outpath = inpath.substr(prefLen);
				*system = fileSystems[i].system;

				DEBUG_LOG(HLE, "MapFilePath: mapped to prefix: \"%s\", path: \"%s\"", fileSystems[i].prefix.c_str(), outpath.c_str());

				return true;
			}
		}
	}

	DEBUG_LOG(HLE, "MapFilePath: failed, returning false");

	return false;
}

void MetaFileSystem::Mount(std::string prefix, IFileSystem *system)
{
	System x;
	x.prefix=prefix;
	x.system=system;
	fileSystems.push_back(x);
}

void MetaFileSystem::UnmountAll()
{
	current = 6;

	// Ownership is a bit convoluted. Let's just delete everything once.

	std::set<IFileSystem *> toDelete;
	for (size_t i = 0; i < fileSystems.size(); i++) {
		toDelete.insert(fileSystems[i].system);
	}

	for (auto iter = toDelete.begin(); iter != toDelete.end(); ++iter)
	{
		delete *iter;
	}

	fileSystems.clear();
	currentDirectory = "";
}

u32 MetaFileSystem::OpenFile(std::string filename, FileAccess access)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->OpenFile(of, access);
	}
	else
	{
		return 0;
	}
}

PSPFileInfo MetaFileSystem::GetFileInfo(std::string filename)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->GetFileInfo(of);
	}
	else
	{
		PSPFileInfo bogus; // TODO
		return bogus; 
	}
}

//TODO: Not sure where this should live. Seems a bit wrong putting it in common
bool stringEndsWith (std::string const &fullString, std::string const &ending)
{
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

std::vector<PSPFileInfo> MetaFileSystem::GetDirListing(std::string path)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(path, of, &system))
	{
		return system->GetDirListing(of);
	}
	else
	{
		std::vector<PSPFileInfo> empty;
		return empty;
	}
}

bool MetaFileSystem::MkDir(const std::string &dirname)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(dirname, of, &system))
	{
		return system->MkDir(of);
	}
	else
	{
		return false;
	}
}

bool MetaFileSystem::RmDir(const std::string &dirname)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(dirname, of, &system))
	{
		return system->RmDir(of);
	}
	else
	{
		return false;
	}
}

bool MetaFileSystem::RenameFile(const std::string &from, const std::string &to)
{
	std::string of;
	std::string rf;
	IFileSystem *system;
	if (MapFilePath(from, of, &system) && MapFilePath(to, rf, &system))
	{
		return system->RenameFile(of, rf);
	}
	else
	{
		return false;
	}
}

bool MetaFileSystem::DeleteFile(const std::string &filename)
{
	std::string of;
	IFileSystem *system;
	if (MapFilePath(filename, of, &system))
	{
		return system->DeleteFile(of);
	}
	else
	{
		return false;
	}
}

void MetaFileSystem::CloseFile(u32 handle)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		sys->CloseFile(handle);
}

size_t MetaFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->ReadFile(handle,pointer,size);
	else
		return 0;
}

size_t MetaFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->WriteFile(handle,pointer,size);
	else
		return 0;
}

size_t MetaFileSystem::SeekFile(u32 handle, s32 position, FileMove type)
{
	IFileSystem *sys = GetHandleOwner(handle);
	if (sys)
		return sys->SeekFile(handle,position,type);
	else
		return 0;
}

