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


// Optimization ideas:
//
// It's common to see sequences of stores writing or reading to a contiguous set of
// addresses in function prologues/epilogues:
//  sw s5, 104(sp)
//  sw s4, 100(sp)
//  sw s3, 96(sp)
//  sw s2, 92(sp)
//  sw s1, 88(sp)
//  sw s0, 84(sp)
//  sw ra, 108(sp)
//  mov s4, a0
//  mov s3, a1
//  ...
// Such sequences could easily be detected and turned into nice contiguous
// sequences of ARM stores instead of the current 3 instructions per sw/lw.
//
// Also, if we kept track of the likely register content of a cached register,
// (pointer or data), we could avoid many BIC instructions.


#include "../../MemMap.h"
#include "../MIPSAnalyst.h"
#include "../../Config.h"
#include "ArmJit.h"
#include "ArmRegCache.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define DISABLE Comp_Generic(op); return;

namespace MIPSComp
{
	
	void Jit::SetR0ToEffectiveAddress(int rs, s16 offset) {
		Operand2 op2;
		if (offset) {
			bool negated;
			if (TryMakeOperand2_AllowNegation(offset, op2, &negated)) {
				if (!negated)
					ADD(R0, gpr.R(rs), op2);
				else
					SUB(R0, gpr.R(rs), op2);
			} else {
				// Try to avoid using MOVT
				if (offset < 0) {
					MOVI2R(R0, (u32)(-offset));
					SUB(R0, gpr.R(rs), R0);
				} else {
					MOVI2R(R0, (u32)offset);
					ADD(R0, gpr.R(rs), R0);
				}
			}
			BIC(R0, R0, Operand2(0xC0, 4));   // &= 0x3FFFFFFF
		} else {
			BIC(R0, gpr.R(rs), Operand2(0xC0, 4));   // &= 0x3FFFFFFF
		}
	}
	
	void Jit::Comp_ITypeMem(u32 op)
	{
		int offset = (signed short)(op&0xFFFF);
		bool load = false;
		int rt = _RT;
		int rs = _RS;
		int o = op>>26;
		if (((op >> 29) & 1) == 0 && rt == 0) {
			// Don't load anything into $zr
			return;
		}
		switch (o)
		{
		case 32: //lb
		case 33: //lh
		case 35: //lw
		case 36: //lbu
		case 37: //lhu
			load = true;
		case 40: //sb
		case 41: //sh
		case 43: //sw
			if (g_Config.bFastMemory) {
				if (gpr.IsImm(rs)) {
					// We can compute the full address at compile time. Kickass.
					u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
					// Must be OK even if rs == rt since we have the value from imm already.
					gpr.MapReg(rt, load ? MAP_NOINIT | MAP_DIRTY : 0);
					MOVI2R(R0, addr);
				} else {
					load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);
					SetR0ToEffectiveAddress(rs, offset);
				}
				switch (o)
				{
				// Load
				case 35: LDR  (gpr.R(rt), R11, R0, true, true); break;
				case 37: LDRH (gpr.R(rt), R11, R0, true, true); break;
				case 33: LDRSH(gpr.R(rt), R11, R0, true, true); break;
				case 36: LDRB (gpr.R(rt), R11, R0, true, true); break;
				case 32: LDRSB(gpr.R(rt), R11, R0, true, true); break;
				// Store
				case 43: STR  (R0, gpr.R(rt), R11, true, true); break;
				case 41: STRH (R0, gpr.R(rt), R11, true, true); break;
				case 40: STRB (R0, gpr.R(rt), R11, true, true); break;
				}
			} else {
				Comp_Generic(op);
				return;
			}
			break;
			/*
		case 34: //lwl
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);
				//R(rt) = ( u32(R(rt)) & (0x00ffffff >> shift) ) | ( mem << (24 - shift) );
			}
			break;

		case 38: //lwr
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);

				//R(rt) = ( u32(rt) & (0xffffff00 << (24 - shift)) ) | ( mem	>> shift );
			}
			break;
 
		case 42: //swl
			{
				Crash();
				//u32 shift = (addr & 3) << 3;
				//u32 mem = ReadMem32(addr & 0xfffffffc);
				//WriteMem32((addr & 0xfffffffc),	( ( u32(R(rt)) >>	(24 - shift) ) ) |
				//	(	mem & (0xffffff00 << shift) ));
			}
			break;
		case 46: //swr
			{
				Crash();
				//	u32 shift = (addr & 3) << 3;
			//	u32 mem = ReadMem32(addr & 0xfffffffc);
//
//				WriteMem32((addr & 0xfffffffc), ( ( u32(R(rt)) << shift ) |
//					(mem	& (0x00ffffff >> (24 - shift)) ) ) );
			}
			break;*/
		default:
			Comp_Generic(op);
			return ;
		}

	}
}
