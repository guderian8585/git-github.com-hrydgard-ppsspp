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

#include "../../../Globals.h"

#include "ArmJitCache.h"
#include "ArmRegCache.h"
#include "ArmAsm.h"

namespace MIPSComp
{

struct ArmJitOptions
{
	ArmJitOptions()
	{
		enableBlocklink = true;
	}

	bool enableBlocklink;
};

struct ArmJitState
{
	u32 compilerPC;
	u32 blockStart;
	bool cancel;
	bool inDelaySlot;
	int downcountAmount;
	bool compiling;	// TODO: get rid of this in favor of using analysis results to determine end of block
	ArmJitBlock *curBlock;
};

class Jit : public ArmGen::ARMXCodeBlock
{
public:
	Jit(MIPSState *mips);

	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(u32 op);

	void RunLoopUntil(u64 globalticks);

	void Compile(u32 em_address);	// Compiles a block at current MIPS PC
	const u8 *DoJit(u32 em_address, ArmJitBlock *b);

	void CompileAt(u32 addr);
	void Comp_RunBlock(u32 op);

	// Ops
	void Comp_ITypeMem(u32 op);

	void Comp_RelBranch(u32 op);
	void Comp_RelBranchRI(u32 op);
	void Comp_FPUBranch(u32 op);
	void Comp_FPULS(u32 op);
	void Comp_Jump(u32 op);
	void Comp_JumpReg(u32 op);
	void Comp_Syscall(u32 op);

	void Comp_IType(u32 op);
	void Comp_RType3(u32 op);
	void Comp_ShiftType(u32 op);
	void Comp_Allegrex(u32 op);
	void Comp_VBranch(u32 op);

	void Comp_FPU3op(u32 op);
	void Comp_FPU2op(u32 op);
	void Comp_mxc1(u32 op);

	ArmJitBlockCache *GetBlockCache() { return &blocks; }

	void ClearCache();
	void ClearCacheAt(u32 em_address);

private:
	void GenerateFixedCode();
	void FlushAll();

	// TODO: Split into two parts, the first part can be shared in branches.
	void DoDownCount();
	void MovFromPC(ARMReg r);
	void MovToPC(ARMReg r);

	void WriteExit(u32 destination, int exit_num);
	void WriteExitDestInR(ARMReg Reg);
	void WriteSyscallExit();

	// Utility compilation functions
	void BranchFPFlag(u32 op, ArmGen::CCFlags cc, bool likely);
	void BranchVFPUFlag(u32 op, ArmGen::CCFlags cc, bool likely);
	void BranchRSZeroComp(u32 op, ArmGen::CCFlags cc, bool andLink, bool likely);
	void BranchRSRTComp(u32 op, ArmGen::CCFlags cc, bool likely);

	// Utilities to reduce duplicated code
	void CompImmLogic(int rs, int rt, u32 uimm, void (ARMXEmitter::*arith)(ARMReg dst, ARMReg src, Operand2 op2), u32 (*eval)(u32 a, u32 b));
	void CompShiftImm(u32 op, ArmGen::ShiftType shiftType);
		/*
	void CompImmLogic(u32 op, void (ARMXEmitter::*arith)(int, const OpArg &, const OpArg &));
	void CompTriArith(u32 op, void (ARMXEmitter::*arith)(int, const OpArg &, const OpArg &));
	void CompShiftImm(u32 op, void (ARMXEmitter::*shift)(int, OpArg, OpArg));
	void CompShiftVar(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg));

	void CompFPTriArith(u32 op, void (XEmitter::*arith)(X64Reg reg, OpArg), bool orderMatters);
	*/

	ArmJitBlockCache blocks;
	ArmJitOptions jo;
	ArmJitState js;

	ArmRegCache gpr;
	// FPURegCache fpr;

	MIPSState *mips_;

public:
	// Code pointers
	const u8 *enterCode;

	const u8 *outerLoop;
	const u8 *dispatcherCheckCoreState;
	const u8 *dispatcher;
	const u8 *dispatcherNoCheck;

	const u8 *breakpointBailout;
};

typedef void (Jit::*MIPSCompileFunc)(u32 opcode);

}	// namespace MIPSComp

