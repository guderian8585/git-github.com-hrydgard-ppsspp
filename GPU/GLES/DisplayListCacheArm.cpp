// Copyright (c) 2014- PPSSPP Project.

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

#include "Core/Config.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/DisplayListCache.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/GLES/ShaderManager.h"

#define DISABLE { return Jit_Generic(op); }
#define CONDITIONAL_DISABLE
//#define CONDITIONAL_DISABLE { return Jit_Generic(op); }

using namespace ArmGen;

void DisplayListCache::Initialize() {
	AllocCodeSpace(1024 * 1024 * 4);
	BKPT(0);
	BKPT(0);

	for (int i = 0; i < 256; ++i) {
		cmds_[i] = &DisplayListCache::Jit_Generic;
	}

	cmds_[GE_CMD_NOP] = &DisplayListCache::Jit_Nop;
	cmds_[GE_CMD_VADDR] = &DisplayListCache::Jit_Vaddr;
	cmds_[GE_CMD_IADDR] = &DisplayListCache::Jit_Iaddr;
	cmds_[GE_CMD_PRIM] = &DisplayListCache::Jit_Prim;
	cmds_[GE_CMD_VERTEXTYPE] = &DisplayListCache::Jit_VertexType;
}

void DisplayListCache::DoExecuteOp(GLES_GPU *g, u32 op, u32 diff) {
	g->ExecuteOpInternal(op, diff);
}

void DisplayListCache::DoFlush(TransformDrawEngine *t) {
	t->DoFlush();
}

static const ARMReg pcReg = R4;
static const ARMReg opReg = R5;
static const ARMReg diffReg = R6;
static const ARMReg gstateReg = R7;
static const ARMReg pcAddrReg = R8;
static const ARMReg pcParamReg = R0;
static const ARMReg gpuAddrReg = R10;
static const ARMReg membaseReg = R11;

JittedDisplayListEntry DisplayListCache::Compile(u32 &pc, int &downcount) {
	if (GetSpaceLeft() < 0x10000) {
		ClearCodeSpace();
		jitted_.clear();
	}

	const u8 *start = this->GetCodePtr();

	PUSH(8, R4, R5, R6, R7, R8, R10, R11, R_LR);

	LDR(pcReg, pcParamReg, 0);
	MOVP2R(membaseReg, Memory::base);
	MOV(pcAddrReg, pcParamReg);
	MOVP2R(gstateReg, gstate.cmdmem);
	MOVP2R(gpuAddrReg, gpu_);
	ADD(pcReg, pcReg, membaseReg);

	std::vector<FixupBranch> fixups;

	int ops = 0;
	while (downcount > 0) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + pc);
		const u32 cmd = op >> 24;

		if ((ops % 32) == 0) {
			PLD(pcReg, 128);
		}
		++ops;

		LDR(diffReg, gstateReg, cmd * 4);
		LDR(opReg, pcReg, 0);
		EOR(diffReg, diffReg, opReg);

		CMPI2R(diffReg, 0x01000000, R1);
		fixups.push_back(B_CC(CC_HS));

		(this->*cmds_[cmd])(op);

		ADD(pcReg, pcReg, 4);
		pc += 4;
		downcount--;

		if (GetSpaceLeft() < 0x200) {
			break;
		}
	}
	JitStorePC();

	MOV(R0, 0);
	MOVP2R(R1, &downcount);
	MOVI2R(R2, downcount);
	STR(R2, R1);

	POP(8, R4, R5, R6, R7, R8, R10, R11, R_PC);

	for (auto it = fixups.begin(), end = fixups.end(); it != end; ++it) {
		SetJumpTarget(*it);
	}
	JitStorePC();

	// TODO: Temporary for testing.
	MOV(R0, diffReg);
	MOVP2R(R1, &downcount);
	MOV(R2, 0);
	STR(R2, R1);

	POP(8, R4, R5, R6, R7, R8, R10, R11, R_PC);

	return (JittedDisplayListEntry)start;
}

void DisplayListCache::JitLoadPC() {
	LDR(pcReg, pcAddrReg, 0);
	ADD(pcReg, pcReg, membaseReg);
}

void DisplayListCache::JitStorePC() {
	SUB(R1, pcReg, membaseReg);
	STR(R1, pcAddrReg, 0);
}

inline void DisplayListCache::JitFlush(u32 diff, bool onChange, bool exec) {
	if (exec) {
		gpu_->transformDraw_.Flush();
	}

	if (onChange) {
		CMP(diffReg, 0);
		SetCC(CC_NEQ);
	}

	MOVP2R(R0, &gpu_->transformDraw_);
	LDR(R1, R0, offsetof(TransformDrawEngine, numDrawCalls));
	CMP(R1, 0);
	SetCC(CC_NEQ);
	QuickCallFunction(R3, &DoFlush);
	SetCC(CC_AL);
}

void DisplayListCache::JitDirtyUniform(u32 what, bool exec) {
	if (exec) {
		gpu_->shaderManager_->DirtyUniform(what);
	}

	MOVP2R(R0, &gpu_->shaderManager_->globalDirty_);
	LDR(R1, R0, 0);
	ORI2R(R1, R1, what, R_IP);
	STR(R1, R0, 0);
}

void DisplayListCache::Jit_Generic(u32 op) {
	const u32 cmd = op >> 24;
	const u32 diff = op ^ gstate.cmdmem[cmd];
	const u8 cmdFlags = gpu_->commandFlags_[cmd];

	if (cmdFlags & FLAG_FLUSHBEFORE) {
		JitFlush();
	} else if (cmdFlags & FLAG_FLUSHBEFOREONCHANGE) {
		JitFlush(diff, true, diff != 0);
	}

	gstate.cmdmem[cmd] = op;
	STR(opReg, gstateReg, cmd * 4);

	if (cmdFlags & FLAG_ANY_EXECUTE) {
		FixupBranch changedSkip;
		if (!(cmdFlags & FLAG_EXECUTE)) {
			CMP(diffReg, 0);
			changedSkip = B_CC(CC_EQ);
		}
		if (cmdFlags & FLAG_READS_PC) {
			JitStorePC();
		}
		gpu_->ExecuteOp(op, diff);
		MOV(R0, gpuAddrReg);
		MOV(R1, opReg);
		MOV(R2, diffReg);
		QuickCallFunction(R3, &DoExecuteOp);
		if (cmdFlags & FLAG_WRITES_PC) {
			JitLoadPC();
		}
		if (!(cmdFlags & FLAG_EXECUTE)) {
			SetJumpTarget(changedSkip);
		}
	}
}

void DisplayListCache::Jit_Nop(u32 op) {
	CONDITIONAL_DISABLE;
	// Do nothing.
}

void DisplayListCache::Jit_Vaddr(u32 op) {
	CONDITIONAL_DISABLE;

	gstate_c.vertexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);

	// TODO: Update cmdmem also?

	LDR(R1, gstateReg, offsetof(GPUgstate, base));
	ANDI2R(R2, opReg, 0x00FFFFFF, R2);
	ANDI2R(R1, R1, 0x000F0000, R2);
	ORR(R1, R2, Operand2(R1, ST_LSL, 8));

	MOVP2R(R3, &gstate_c);
	LDR(R2, R3, offsetof(GPUStateCache, offsetAddr));
	ADD(R1, R1, R2);
	ANDI2R(R1, R1, 0x0FFFFFFF, R2);
	STR(R1, R3, offsetof(GPUStateCache, vertexAddr));
}

void DisplayListCache::Jit_Iaddr(u32 op) {
	CONDITIONAL_DISABLE;

	gstate_c.indexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);

	// TODO: Update cmdmem also?

	LDR(R1, gstateReg, offsetof(GPUgstate, base));
	ANDI2R(R2, opReg, 0x00FFFFFF, R2);
	ANDI2R(R1, R1, 0x000F0000, R2);
	ORR(R1, R2, Operand2(R1, ST_LSL, 8));

	MOVP2R(R3, &gstate_c);
	LDR(R2, R3, offsetof(GPUStateCache, offsetAddr));
	ADD(R1, R1, R2);
	ANDI2R(R1, R1, 0x0FFFFFFF, R2);
	STR(R1, R3, offsetof(GPUStateCache, indexAddr));
}

void DisplayListCache::Jit_VertexType(u32 op) {
	CONDITIONAL_DISABLE;

	const u32 cmd = op >> 24;
	const u32 diff = op ^ gstate.cmdmem[cmd];

	if (g_Config.bSoftwareSkinning) {
		TSTI2R(diffReg, ~GE_VTYPE_WEIGHTCOUNT_MASK, R0);
		SetCC(CC_NEQ);
		TSTI2R(opReg, GE_VTYPE_MORPHCOUNT_MASK, R0);
		FixupBranch skip = B_CC(CC_NEQ);
		SetCC(CC_AL);

		JitFlush(0, false, (diff & ~GE_VTYPE_WEIGHTCOUNT_MASK) != 0 || (op & GE_VTYPE_MORPHCOUNT_MASK) != 0);

		SetJumpTarget(skip);
	} else {
		JitFlush(diff, true, diff != 0);
	}

	gstate.cmdmem[cmd] = op;
	STR(opReg, gstateReg, cmd * 4);

	TSTI2R(diffReg, GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK, R0);
	SetCC(CC_NEQ);
	JitDirtyUniform(DIRTY_UVSCALEOFFSET, (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK)) != 0);
	SetCC(CC_AL);
}

void DisplayListCache::Jit_Prim(u32 op) {
	DISABLE;
}
