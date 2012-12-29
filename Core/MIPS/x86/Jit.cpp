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

#include "../../Core.h"
#include "../../CoreTiming.h"
#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSInt.h"
#include "../MIPSTables.h"

#include "RegCache.h"
#include "Jit.h"


extern u32 *pspmainram;

namespace MIPSComp
{

Jit::Jit(MIPSState *mips) : blocks(mips), mips_(mips)
{
	blocks.Init();
	asm_.Init(mips, this);
	gpr.SetEmitter(this);
	fpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);
}

void Jit::FlushAll()
{
	gpr.Flush(FLUSH_ALL);
	fpr.Flush(FLUSH_ALL);
}

void Jit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace();
}

u8 *codeCache;
#define CACHESIZE 16384*1024
void Jit::CompileAt(u32 addr)
{
	u32 op = Memory::Read_Instruction(addr);
	MIPSCompileOp(op);
}

void Jit::Compile(u32 em_address)
{
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull())
	{
		ClearCache();
	}

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink, DoJit(em_address, b));
}

void Jit::RunLoopUntil(u64 globalticks)
{
	// TODO: copy globalticks somewhere
	((void (*)())asm_.enterCode)();
	// NOTICE_LOG(HLE, "Exited jitted code at %i, corestate=%i, dc=%i", CoreTiming::GetTicks() / 1000, (int)coreState, CoreTiming::downcount);
}

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;

	b->normalEntry = GetCodePtr();

	// TODO: this needs work
	MIPSAnalyst::AnalysisResults analysis; // = MIPSAnalyst::Analyze(em_address);

	gpr.Start(mips_, analysis);
	fpr.Start(mips_, analysis);

	int numInstructions = 0;
	while (js.compiling)
	{
		u32 inst = Memory::Read_Instruction(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);

		js.compilerPC += 4;
		numInstructions++;
	}

	b->codeSize = (u32)(GetCodePtr() - b->normalEntry);
	NOP();
	AlignCode4();
	b->originalSize = numInstructions;
	return b->normalEntry;
}

void Jit::Comp_RunBlock(u32 op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(DYNA_REC, "Comp_RunBlock");
}

void Jit::Comp_Generic(u32 op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		MOV(32, M(&mips_->pc), Imm32(js.compilerPC));
		ABI_CallFunctionC((void *)func, op);
	}
}

void Jit::WriteExit(u32 destination, int exit_num)
{
	SUB(32, M(&CoreTiming::downcount), js.downcountAmount > 127 ? Imm32(js.downcountAmount) : Imm8(js.downcountAmount));

	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (jo.enableBlocklink)
	{
		if (block >= 0 && jo.enableBlocklink)
		{
			// It exists! Joy of joy!
			JMP(blocks.GetBlock(block)->checkedEntry, true);
			b->linkStatus[exit_num] = true;
			return;
		}
	}
	// No blocklinking.
	MOV(32, M(&mips_->pc), Imm32(destination));
	JMP(asm_.dispatcher, true);
}

void Jit::WriteExitDestInEAX()
{
	// TODO: Some wasted potential, dispatcher will alwa
	MOV(32, M(&mips_->pc), R(EAX));
	SUB(32, M(&CoreTiming::downcount), js.downcountAmount > 127 ? Imm32(js.downcountAmount) : Imm8(js.downcountAmount));
	JMP(asm_.dispatcher, true);
}

void Jit::WriteSyscallExit()
{
	SUB(32, M(&CoreTiming::downcount), js.downcountAmount > 127 ? Imm32(js.downcountAmount) : Imm8(js.downcountAmount));
	JMP(asm_.dispatcherCheckCoreState, true);
}

} // namespace
