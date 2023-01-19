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

#include <string>
#include <cstring>
#include <cstdio>
#include "Core/MIPS/MIPS.h"
#include "Core/Debugger/DebugInterface.h"

class MIPSDebugInterface : public DebugInterface
{
	MIPSState *cpu;
public:
	MIPSDebugInterface(MIPSState *_cpu) { cpu = _cpu; }
	const char *disasm(unsigned int address, unsigned int align) override;
	int getInstructionSize(int instruction) override { return 4; }
	bool isAlive() override;
	bool isBreakpoint(unsigned int address) override;
	void setBreakpoint(unsigned int address) override;
	void clearBreakpoint(unsigned int address) override;
	void clearAllBreakpoints() override;
	void toggleBreakpoint(unsigned int address) override;
	unsigned int readMemory(unsigned int address) override;
	unsigned int getPC() override { return cpu->pc; }
	void setPC(unsigned int address) override { cpu->pc = address; }
	void step() override {}
	void runToBreakpoint() override;
	int getColor(unsigned int address) override;
	std::string getDescription(unsigned int address) override;
	bool initExpression(const char* exp, PostfixExpression& dest) override;
	bool parseExpression(PostfixExpression& exp, u32& dest) override;

	//overridden functions
	const char *GetName() override;
	u32 GetGPR32Value(int reg) override { return cpu->r[reg]; }
	u32 GetPC() override { return cpu->pc; }
	u32 GetLR() override { return cpu->r[MIPS_REG_RA]; }
	void SetPC(u32 _pc) override { cpu->pc = _pc; }

	const char *GetCategoryName(int cat) override {
		static const char *const names[3] = { "GPR", "FPU", "VFPU" };
		return names[cat];
	}
	int GetNumCategories() override { return 3; }
	int GetNumRegsInCategory(int cat) override {
		static int r[3] = { 32, 32, 128 };
		return r[cat];
	}
	const char *GetRegName(int cat, int index) override;

	void PrintRegValue(int cat, int index, char *out) override {
		switch (cat) {
		case 0: sprintf(out, "%08X", cpu->r[index]); break;
		case 1: sprintf(out, "%f", cpu->f[index]); break;
		case 2: sprintf(out, "N/A"); break;
		}
	}

	u32 GetHi() override {
		return cpu->hi;
	}

	u32 GetLo() override {
		return cpu->lo;
	}
	
	void SetHi(u32 val) override {
		cpu->hi = val;
	}

	void SetLo(u32 val) override {
		cpu->lo = val;
	}

	u32 GetRegValue(int cat, int index) override {
		uint32_t temp;
		switch (cat) {
		case 0:
			return cpu->r[index];

		case 1:
			memcpy(&temp, &cpu->f[index], sizeof(uint32_t));
			return temp;

		case 2:
			memcpy(&temp, &cpu->v[voffset[index]], sizeof(uint32_t));
			return temp;

		default:
			return 0;
		}
	}

	void SetRegValue(int cat, int index, u32 value) override {
		float valuef;
		switch (cat) {
		case 0:
			if (index != 0)
				cpu->r[index] = value;
			break;

		case 1:
			memcpy(&valuef, &value, sizeof(float));
			cpu->f[index] = valuef;
			break;

		case 2:
			memcpy(&valuef, &value, sizeof(float));
			cpu->v[voffset[index]] = valuef;
			break;

		default:
			break;
		}
	}
};
