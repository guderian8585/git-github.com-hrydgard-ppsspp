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

#include "../HLE/HLE.h"
#include "MIPS.h"
#include "MIPSDis.h"
#include "MIPSTables.h"
#include "MIPSDebugInterface.h"

#include "JitCommon/JitCommon.h"

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define RN(i) currentDebugMIPS->GetRegName(0,i)
#define FN(i) currentDebugMIPS->GetRegName(1,i)
//#define VN(i) currentMIPS->GetRegName(2,i)

u32 disPC;

namespace MIPSDis
{
	void Dis_Generic(u32 op, char *out)
	{
		sprintf(out, "%s\t --- unknown ---", MIPSGetName(op));
	}

	void Dis_mxc1(u32 op, char *out)
	{
		int fs = _FS;
		int rt = _RT;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s",name,RN(rt),FN(fs));
	}

	void Dis_FPU3op(u32 op, char *out)
	{
		int ft = _FT;
		int fs = _FS;
		int fd = _FD;;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s, %s",name,FN(fd),FN(fs),FN(ft));
	}

	void Dis_FPU2op(u32 op, char *out)
	{
		int fs = _FS;
		int fd = _FD;;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s",name,FN(fd),FN(fs));
	}

	void Dis_FPULS(u32 op, char *out)
	{
		int offset = (signed short)(op&0xFFFF);
		int ft = _FT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %d(%s)",name,FN(ft),offset,RN(rs));
	}
	void Dis_FPUComp(u32 op, char *out)
	{
		int fs = _FS;
		int ft = _FT;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s",name,FN(fs),FN(ft));
	}

	void Dis_FPUBranch(u32 op, char *out)
	{
		u32 off = disPC;
		int imm = (signed short)(op&0xFFFF)<<2;
		off += imm + 4;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t->$%08x",name,off);
	}

	void Dis_RelBranch(u32 op, char *out)
	{
		u32 off = disPC;
		int imm = (signed short)(op&0xFFFF)<<2;
		int rs = _RS;
		off += imm + 4;

		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, ->$%08x",name,RN(rs),off);
	}

	void Dis_Syscall(u32 op, char *out)
	{
		u32 callno = (op>>6) & 0xFFFFF; //20 bits
		int funcnum = callno & 0xFFF;
		int modulenum = (callno & 0xFF000) >> 12;
		sprintf(out, "syscall\t	%s",/*PSPHLE::GetModuleName(modulenum),*/GetFuncName(modulenum, funcnum));
	}

	void Dis_ToHiloTransfer(u32 op, char *out)
	{
		int rs = _RS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s",name,RN(rs));
	}
	void Dis_FromHiloTransfer(u32 op, char *out)
	{
		int rd = _RD;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s",name,RN(rd));
	}

	void Dis_RelBranch2(u32 op, char *out)
	{
		u32 off = disPC;
		int imm = (signed short)(op&0xFFFF)<<2;
		int rt = _RT;
		int rs = _RS;
		off += imm + 4;

		const char *name = MIPSGetName(op);
		int o = op>>26;
		if (o==4 && rs == rt)//beq
			sprintf(out,"b\t->$%08x",off);
		else if (o==20 && rs == rt)//beql
			sprintf(out,"bl\t->$%08x",off);
		else
			sprintf(out, "%s\t%s, %s, ->$%08x",name,RN(rt),RN(rs),off);
	}

	void Dis_IType(u32 op, char *out)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s, %i",name,RN(rt),RN(rs),imm);
	}
	void Dis_ori(u32 op, char *out)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		if (rs == 0)
			sprintf(out, "li\t%s, %i",RN(rt),imm);
		else
			sprintf(out, "%s\t%s, %s, %i",name,RN(rt),RN(rs),imm);
	}

	void Dis_IType1(u32 op, char *out)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %i",name,RN(rt),imm);
	}

	void Dis_addi(u32 op, char *out)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		if (rs == 0)
			sprintf(out, "li\t%s, %i",RN(rt),imm);
		else
			Dis_IType(op,out);
	}

	void Dis_ITypeMem(u32 op, char *out)
	{
		int imm = (signed short)(op&0xFFFF);
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %i(%s)",name,RN(rt),imm,RN(rs));
	}

	void Dis_RType2(u32 op, char *out)
	{
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s",name,RN(rd),RN(rs));
	}

	void Dis_RType3(u32 op, char *out)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s, %s",name,RN(rd),RN(rs),RN(rt));
	}

	void Dis_addu(u32 op, char *out)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		if (rs==0 && rt==0)
			sprintf(out,"li\t%s, 0",RN(rd));
		else if (rs == 0)
			sprintf(out,"mov\t%s, %s",RN(rd),RN(rt));
		else if (rt == 0)
			sprintf(out,"mov\t%s, %s",RN(rd),RN(rs));
		else
			sprintf(out, "%s\t%s, %s, %s",name,RN(rd),RN(rs),RN(rt));
	}

	void Dis_ShiftType(u32 op, char *out)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		int sa = (op>>6)	& 0x1F;
		const char *name = MIPSGetName(op);
		if (((op & 0x3f) == 2) && rs == 1)
			name = "rotr";
		if (((op & 0x3f) == 6) && sa == 1)
			name = "rotrv";
		sprintf(out, "%s\t%s, %s, %d",name,RN(rd),RN(rt),sa);
	}

	void Dis_VarShiftType(u32 op, char *out)
	{
		int rt = _RT;
		int rs = _RS;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s, %s",name,RN(rd),RN(rt),RN(rs));
	}


	void Dis_MulDivType(u32 op, char *out)
	{
		int rt = _RT;
		int rs = _RS;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t%s, %s",name,RN(rs),RN(rt));
	}


	void Dis_Special3(u32 op, char *out)
	{
		int rs = _RS;
		int Rt = _RT;
		int pos = _POS;
		const char *name = MIPSGetName(op);

		switch (op & 0x3f)
		{
		case 0x0: //ext
			{
				int size = _SIZE + 1;
				sprintf(out,"%s\t%s, %s, %i, %i",name,RN(Rt),RN(rs),pos,size);
			}
			break;
		case 0x4: // ins
			{
				int size = (_SIZE + 1) - pos;
				sprintf(out,"%s\t%s, %s, %i, %i",name,RN(Rt),RN(rs),pos,size);
			}
			break;
		}
	}

	void Dis_JumpType(u32 op, char *out)
	{
		u32 off = ((op & 0x3FFFFFF) << 2);
		u32 addr = (disPC & 0xF0000000) | off;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t->$%08x",name,addr);
	}
	void Dis_JumpRegType(u32 op, char *out)
	{
		int rs = (op>>21)&0x1f;
		const char *name = MIPSGetName(op);
		sprintf(out, "%s\t->%s",name,RN(rs));

	}

	void Dis_Allegrex(u32 op, char *out)
	{
		int rt = _RT;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		sprintf(out,"%s\t%s,%s",name,RN(rd),RN(rt));
	}

	void Dis_Allegrex2(u32 op, char *out)
	{
		int rt = _RT;
		int rd = _RD;
		const char *name = MIPSGetName(op);
		sprintf(out,"%s\t%s,%s",name,RN(rd),RN(rt));
	}

	void Dis_Emuhack(u32 op, char *out)
	{
		//const char *name = MIPSGetName(op);
		//sprintf(out,"%s\t-",name);
		out[0]='*';
		out[1]=0;
		// MIPSDisAsm(MIPSComp::GetOriginalOp(op), currentDebugMIPS->GetPC(), out+1);
	}


}
