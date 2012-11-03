
#pragma once

#include "../../Globals.h"

#include <vector>
#include <list>
#include <cstring>


// Generic allocator thingy
// Allocates blocks from a range

class BlockAllocator
{
	struct Block
	{
		Block(u32 _start, u32 _size, bool _taken) : start(_start), size(_size), taken(_taken)
		{
			strcpy(tag, "Empty");
		}
		void SetTag(const char *_tag) {
			if (_tag)
				strcpy(tag, _tag);
			else
				strcpy(tag, "---");
		}
		u32 start;
		u32 size;
		bool taken;
		char tag[16];
	};

	std::list<Block> blocks;

	Block *GetBlockFromAddress(u32 addr);
	std::list<Block>::iterator GetBlockIterFromAddress(u32 addr);

public:
	BlockAllocator();
	~BlockAllocator();

	void Init(u32 _rangeStart, u32 _rangeSize);
	void Shutdown();

	void ListBlocks();

  // WARNING: size can be modified upwards!
	u32 Alloc(u32 &size, bool fromEnd = false, const char *tag = 0);
	u32 AllocAt(u32 position, u32 size, const char *tag = 0);

	void Free(u32 position);
	bool IsBlockFree(u32 position) {
		Block *b = GetBlockFromAddress(position);
		if (b)
			return !b->taken;
		else
			return false;
	}

	void MergeFreeBlocks();

	u32 GetBlockStartFromAddress(u32 addr);
	u32 GetBlockSizeFromAddress(u32 addr);
	u32 GetLargestFreeBlockSize();
	u32 GetTotalFreeBytes();
};
