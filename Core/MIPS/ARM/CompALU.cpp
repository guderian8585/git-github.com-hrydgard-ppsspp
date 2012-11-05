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

#include "Jit.h"
#include "RegCache.h"
#include <ArmEmitter.h>

using namespace MIPSAnalyst;
#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _SA ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define OLDD Comp_Generic(op); return;

namespace MIPSComp
{
	/*
	void Jit::CompImmLogic(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &))
	{
		u32 uimm = (u16)(op & 0xFFFF);
		int rt = _RT;
		int rs = _RS;
		gpr.Lock(rt, rs);
		gpr.BindToRegister(rt, rt == rs, true);
		if (rt != rs)
			MOV(32, gpr.R(rt), gpr.R(rs));
		(this->*arith)(32, gpr.R(rt), Imm32(uimm));
		gpr.UnlockAll();

	}
	*/

	void Jit::Comp_IType(u32 op)
	{
		OLDD
			/*
		s32 simm = (s16)(op & 0xFFFF);
		u32 uimm = (u16)(op & 0xFFFF);

		int rt = _RT;
		int rs = _RS;

		switch (op >> 26) 
		{
		case 8:	// same as addiu?
		case 9:	//R(rt) = R(rs) + simm; break;	//addiu
			{
				if (gpr.R(rs).IsImm())
				{
					gpr.SetImmediate32(rt, gpr.R(rs).GetImmValue() + simm);
					break;
				}

				gpr.Lock(rt, rs);
				if (rs != 0)
				{
					gpr.BindToRegister(rt, rt == rs, true);
					if (rt != rs)
						MOV(32, gpr.R(rt), gpr.R(rs));
					if (simm != 0)
						ADD(32, gpr.R(rt), Imm32((u32)(s32)simm));
					// TODO: Can also do LEA if both operands happen to be in registers.
				}
				else
				{
					gpr.SetImmediate32(rt, simm);
				}
				gpr.UnlockAll();
			}
			break;

		case 10: // R(rt) = (s32)R(rs) < simm; break; //slti
			gpr.Lock(rt, rs);
			gpr.BindToRegister(rt, rt == rs, true);
			XOR(32, R(EAX), R(EAX));
			CMP(32, gpr.R(rs), Imm32(simm));
			SETcc(CC_L, R(EAX));
			MOV(32, gpr.R(rt), R(EAX));
			gpr.UnlockAll();
			break;

		case 11: // R(rt) = R(rs) < uimm; break; //sltiu
			gpr.Lock(rt, rs);
			gpr.BindToRegister(rt, rt == rs, true);
			XOR(32, R(EAX), R(EAX));
			CMP(32, gpr.R(rs), Imm32((u32)simm));
			SETcc(CC_B, R(EAX));
			MOV(32, gpr.R(rt), R(EAX));
			gpr.UnlockAll();
			break;

		case 12: CompImmLogic(op, &XEmitter::AND); break;
		case 13: CompImmLogic(op, &XEmitter::OR); break;
		case 14: CompImmLogic(op, &XEmitter::XOR); break;

		case 15: //R(rt) = uimm << 16;	 break; //lui
			gpr.SetImmediate32(rt, uimm << 16);
			break;

		default:
			Comp_Generic(op);
			break;
		}*/

	}

	//rd = rs X rt
	/*
	void Jit::CompTriArith(u32 op, void (XEmitter::*arith)(int, const OpArg &, const OpArg &))
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;

		gpr.Lock(rt, rs, rd);
		MOV(32, R(EAX), gpr.R(rs));
		MOV(32, R(EBX), gpr.R(rt));
		gpr.BindToRegister(rd, true, true);
		(this->*arith)(32, R(EAX), R(EBX));
		MOV(32, gpr.R(rd), R(EAX));
		gpr.UnlockAll();
	}
	*/

	void Jit::Comp_RType3(u32 op)
	{
		OLDD

		
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		
		gpr.Lock(rd, rs, rt);
		gpr.BindToRegister(rs, true, false);
		gpr.BindToRegister(rt, true, false);
		gpr.BindToRegister(rd, true, true);
		
		switch (op & 63) 
		{
		//case 10: if (!R(rt)) R(rd) = R(rs); break; //movz
		//case 11: if (R(rt)) R(rd) = R(rs); break; //movn
			
		// case 32: //R(rd) = R(rs) + R(rt);		break; //add
		case 33: //R(rd) = R(rs) + R(rt);		break; //addu
			ADD(gpr.RX(rd), gpr.RX(rs), gpr.RX(rt));
			break;
		case 134: //R(rd) = R(rs) - R(rt);		break; //sub
		case 135:
			SUB(gpr.RX(rd), gpr.RX(rs), gpr.RX(rt));
			break;
		case 136: //R(rd) = R(rs) & R(rt);		break; //and
			AND(gpr.RX(rd), gpr.RX(rs), gpr.RX(rt));
			break;
		case 137: //R(rd) = R(rs) | R(rt);		break; //or
			ORR(gpr.RX(rd), gpr.RX(rs), gpr.RX(rt));
			break;
		case 138: //R(rd) = R(rs) ^ R(rt);		break; //xor/eor	
			EOR(gpr.RX(rd), gpr.RX(rs), gpr.RX(rt));
			break;

		case 39: // R(rd) = ~(R(rs) | R(rt)); //nor
			ORR(gpr.RX(rd), gpr.RX(rs), gpr.RX(rt));
			MVN(gpr.RX(rd), gpr.RX(rd));
			break;

		case 42: //R(rd) = (int)R(rs) < (int)R(rt); break; //slt
			CMP(gpr.RX(rs), gpr.RX(rt));
			SetCC(CC_LT);
			ARMABI_MOVI2R(gpr.RX(rd), 1);
			SetCC(CC_GE);
			ARMABI_MOVI2R(gpr.RX(rd), 0);
			SetCC(CC_AL);
			break; 

		case 43: //R(rd) = R(rs) < R(rt);		break; //sltu
			CMP(gpr.RX(rs), gpr.RX(rt));
			SetCC(CC_LO);
			ARMABI_MOVI2R(gpr.RX(rd), 1);
			SetCC(CC_HS);
			ARMABI_MOVI2R(gpr.RX(rd), 0);
			SetCC(CC_AL);
			break;

		// case 44: R(rd) = (R(rs) > R(rt)) ? R(rs) : R(rt); break; //max
		// CMP(a,b); CMOVLT(a,b)

		// case 45: R(rd) = (R(rs) < R(rt)) ? R(rs) : R(rt); break; //min
		// CMP(a,b); CMOVGT(a,b)

		default:
			gpr.UnlockAll();
			Comp_Generic(op);
			break;
		}
		gpr.UnlockAll();
		
	}

	/*

	void Jit::CompShiftImm(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg))
	{
		int rd = _RD;
		int rt = _RT;
		gpr.Lock(rd, rt);
		int sa = _SA;
		gpr.BindToRegister(rd, rd == rt, true);
		if (rd != rt)
			MOV(32, gpr.R(rd), gpr.R(rt));
		(this->*shift)(32, gpr.R(rd), Imm8(sa));
		gpr.UnlockAll();
	}
	*/
	// "over-shifts" work the same as on x86 - only bottom 5 bits are used to get the shift value
	/*
	void Jit::CompShiftVar(u32 op, void (XEmitter::*shift)(int, OpArg, OpArg))
	{
		int rd = _RD;
		int rt = _RT;
		int rs = _RS;
		gpr.FlushLockX(ECX);
		gpr.Lock(rd, rt, rs);
		gpr.BindToRegister(rd, true, true);
		if (rd != rt)
			MOV(32, gpr.R(rd), gpr.R(rt));
		MOV(32, R(ECX), gpr.R(rs));	// Only ECX can be used for variable shifts.
		AND(32, R(ECX), Imm32(0x1f));
		(this->*shift)(32, gpr.R(rd), R(ECX));
		gpr.UnlockAll();
		gpr.UnlockAllX();
	}
*/
	void Jit::Comp_ShiftType(u32 op)
	{
		// WARNIGN : ROTR
		OLDD
		switch (op & 0x3f)
		{
		//case 0: CompShiftImm(op, &ARMXEmitter::SHL); break;
		//case 2: CompShiftImm(op, &XEmitter::SHR); break;	// srl
		//case 3: CompShiftImm(op, &XEmitter::SAR); break;	// sra
		
	 // case 4: CompShiftVar(op, &XEmitter::SHL); break;	// R(rd) = R(rt) << R(rs);				break; //sllv
	//	case 6: CompShiftVar(op, &XEmitter::SHR); break;	// R(rd) = R(rt) >> R(rs);				break; //srlv
	//	case 7: CompShiftVar(op, &XEmitter::SAR); break;	// R(rd) = ((s32)R(rt)) >> R(rs); break; //srav
		
		default:
			Comp_Generic(op);
			//_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
	}

	void Jit::Comp_Allegrex(u32 op)
	{
		OLDD
		int rt = _RT;
		int rd = _RD;
		switch ((op >> 6) & 31)
		{
		case 16: // seb	// R(rd) = (u32)(s32)(s8)(u8)R(rt);
			/*
			gpr.Lock(rd, rt);
			gpr.BindToRegister(rd, true, true);
			MOV(32, R(EAX), gpr.R(rt));	// work around the byte-register addressing problem
			MOVSX(32, 8, gpr.RX(rd), R(EAX));
			gpr.UnlockAll();*/
			break;

		case 24: // seh
			/*
			gpr.Lock(rd, rt);
			gpr.BindToRegister(rd, true, true);
			MOVSX(32, 16, gpr.RX(rd), gpr.R(rt));
			gpr.UnlockAll();*/
			break;

		case 20: //bitrev
		default:
			Comp_Generic(op);
			return;
		}
	}

}
