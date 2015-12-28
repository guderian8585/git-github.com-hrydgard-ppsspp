// Copyright (c) 2015- PPSSPP Project.

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

#include <string.h>
#include "base/timeutil.h"
#include "thread/thread.h"
#include "thread/threadutil.h"
#include "Core/FileLoaders/RamCachingFileLoader.h"

#include "Common/Log.h"

// Takes ownership of backend.
RamCachingFileLoader::RamCachingFileLoader(FileLoader *backend)
	: filesize_(0), filepos_(0), backend_(backend), exists_(-1), isDirectory_(-1), aheadThread_(false) {
	filesize_ = backend->FileSize();
	if (filesize_ > 0) {
		InitCache();
	}
}

RamCachingFileLoader::~RamCachingFileLoader() {
	if (filesize_ > 0) {
		ShutdownCache();
	}
	// Takes ownership.
	delete backend_;
}

bool RamCachingFileLoader::Exists() {
	if (exists_ == -1) {
		lock_guard guard(backendMutex_);
		exists_ = backend_->Exists() ? 1 : 0;
	}
	return exists_ == 1;
}

bool RamCachingFileLoader::IsDirectory() {
	if (isDirectory_ == -1) {
		lock_guard guard(backendMutex_);
		isDirectory_ = backend_->IsDirectory() ? 1 : 0;
	}
	return isDirectory_ == 1;
}

s64 RamCachingFileLoader::FileSize() {
	return filesize_;
}

std::string RamCachingFileLoader::Path() const {
	lock_guard guard(backendMutex_);
	return backend_->Path();
}

void RamCachingFileLoader::Seek(s64 absolutePos) {
	filepos_ = absolutePos;
}

size_t RamCachingFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data) {
	size_t readSize = 0;
	if (cache_ == nullptr) {
		lock_guard guard(backendMutex_);
		readSize = backend_->ReadAt(absolutePos, bytes, data);
	} else {
		readSize = ReadFromCache(absolutePos, bytes, data);
		// While in case the cache size is too small for the entire read.
		while (readSize < bytes) {
			SaveIntoCache(absolutePos + readSize, bytes - readSize);
			readSize += ReadFromCache(absolutePos + readSize, bytes - readSize, (u8 *)data + readSize);
		}
	}

	StartReadAhead(absolutePos + readSize);

	filepos_ = absolutePos + readSize;
	return readSize;
}

void RamCachingFileLoader::InitCache() {
	cache_ = (u8 *)malloc(filesize_);
	if (cache_ == nullptr) {
		return;
	}

	lock_guard guard(blocksMutex_);
	u32 blockCount = (u32)((filesize_ + BLOCK_SIZE - 1) >> BLOCK_SHIFT);
	aheadRemaining_ = blockCount;
	blocks_.resize(blockCount);
}

void RamCachingFileLoader::ShutdownCache() {
	{
		lock_guard guard(blocksMutex_);
		// Try to have the thread stop.
		aheadRemaining_ = 0;
	}

	// We can't delete while the thread is running, so have to wait.
	// This should only happen from the menu.
	while (aheadThread_) {
		sleep_ms(1);
	}

	lock_guard guard(blocksMutex_);
	blocks_.clear();
	if (cache_ != nullptr) {
		free(cache_);
		cache_ = nullptr;
	}
}

size_t RamCachingFileLoader::ReadFromCache(s64 pos, size_t bytes, void *data) {
	s64 cacheStartPos = pos >> BLOCK_SHIFT;
	s64 cacheEndPos = (pos + bytes - 1) >> BLOCK_SHIFT;
	if ((size_t)cacheEndPos >= blocks_.size()) {
		cacheEndPos = blocks_.size() - 1;
	}

	size_t readSize = 0;
	size_t offset = (size_t)(pos - (cacheStartPos << BLOCK_SHIFT));
	u8 *p = (u8 *)data;

	lock_guard guard(blocksMutex_);
	for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
		if (blocks_[i] == 0) {
			return readSize;
		}

		size_t toRead = std::min(bytes - readSize, (size_t)BLOCK_SIZE - offset);
		s64 cachePos = (i << BLOCK_SHIFT) + offset;
		memcpy(p + readSize, &cache_[cachePos], toRead);
		readSize += toRead;

		// Don't need an offset after the first read.
		offset = 0;
	}
	return readSize;
}

void RamCachingFileLoader::SaveIntoCache(s64 pos, size_t bytes) {
	s64 cacheStartPos = pos >> BLOCK_SHIFT;
	s64 cacheEndPos = (pos + bytes - 1) >> BLOCK_SHIFT;
	if ((size_t)cacheEndPos >= blocks_.size()) {
		cacheEndPos = blocks_.size() - 1;
	}

	size_t blocksToRead = 0;
	{
		lock_guard guard(blocksMutex_);
		for (s64 i = cacheStartPos; i <= cacheEndPos; ++i) {
			if (blocks_[i] == 0) {
				++blocksToRead;
				if (blocksToRead >= MAX_BLOCKS_PER_READ) {
					break;
				}
			}
		}
	}

	backendMutex_.lock();
	s64 cacheFilePos = cacheStartPos << BLOCK_SHIFT;
	backend_->ReadAt(cacheFilePos, blocksToRead << BLOCK_SHIFT, &cache_[cacheFilePos]);
	backendMutex_.unlock();

	{
		lock_guard guard(blocksMutex_);

		// In case they were simultaneously read.
		u32 blocksRead = 0;
		for (size_t i = 0; i < blocksToRead; ++i) {
			if (blocks_[cacheStartPos + i] == 0) {
				blocks_[cacheStartPos + i] = 1;
				++blocksRead;
			}
		}

		if (aheadRemaining_ != 0) {
			aheadRemaining_ -= blocksRead;
		}
	}
}

void RamCachingFileLoader::StartReadAhead(s64 pos) {
	if (cache_ == nullptr) {
		return;
	}

	lock_guard guard(blocksMutex_);
	aheadPos_ = pos;
	if (aheadThread_) {
		// Already going.
		return;
	}

	aheadThread_ = true;
	std::thread th([this] {
		setCurrentThreadName("FileLoaderReadAhead");

		while (aheadRemaining_ != 0) {
			// Where should we look?
			const u32 cacheStartPos = NextAheadBlock();
			if (cacheStartPos == 0xFFFFFFFF) {
				// Must be full.
				break;
			}
			u32 cacheEndPos = cacheStartPos + BLOCK_READAHEAD - 1;
			if (cacheEndPos >= blocks_.size()) {
				cacheEndPos = (u32)blocks_.size() - 1;
			}

			for (u32 i = cacheStartPos; i <= cacheEndPos; ++i) {
				if (blocks_[i] == 0) {
					SaveIntoCache(i << BLOCK_SHIFT, BLOCK_SIZE * BLOCK_READAHEAD);
					break;
				}
			}
		}

		aheadThread_ = false;
	});
	th.detach();
}

u32 RamCachingFileLoader::NextAheadBlock() {
	lock_guard guard(blocksMutex_);

	// If we had an aheadPos_ set, start reading from there and go forward.
	u32 startFrom = (u32)(aheadPos_ >> BLOCK_SHIFT);
	// But next time, start from the beginning again.
	aheadPos_ = 0;

	for (u32 i = startFrom; i < blocks_.size(); ++i) {
		if (blocks_[i] == 0) {
			return i;
		}
	}

	return 0xFFFFFFFF;
}
