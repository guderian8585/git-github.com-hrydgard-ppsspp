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

#if defined(ANDROID) || defined(BLACKBERRY)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif

#include "ge_constants.h"
#include "GPUState.h"
#include "GLES/ShaderManager.h"
#include "GLES/DisplayListInterpreter.h"

GPUgstate gstate;

void InitGfxState()
{
	memset(&gstate, 0, sizeof(gstate));
	for (int i = 0; i < 256; i++) {
		gstate.cmdmem[i] = i << 24;
	}
	gstate.lightingEnable = 0x17000001;

	static const float identity4x3[12] = 
	{1,0,0,
 	 0,1,0,
 	 0,0,1,
	 0,0,0,};
	static const float identity4x4[16] =
	{1,0,0,0,
	0,1,0,0,
	0,0,1,0,
	0,0,0,1};

	memcpy(gstate.worldMatrix, identity4x3, 12 * sizeof(float));
	memcpy(gstate.viewMatrix, identity4x3, 12 * sizeof(float));
	memcpy(gstate.projMatrix, identity4x4, 16 * sizeof(float));
	memcpy(gstate.tgenMatrix, identity4x3, 12 * sizeof(float));
	for (int i = 0; i < 8; i++) {
		memcpy(gstate.boneMatrix + i * 12, identity4x3, 12 * sizeof(float));
	}
}

// When you have changed state outside the psp gfx core,
// or saved the context and has reloaded it, call this function.
void ReapplyGfxState()
{
	// ShaderManager_DirtyShader();
	// The commands are embedded in the command memory so we can just reexecute the words. Convenient.
	// To be safe we pass 0xFFFFFFF as the diff.
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_ALPHABLENDENABLE], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_ALPHATESTENABLE], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_BLENDMODE], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_ZTEST], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_ZTESTENABLE], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_CULL], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_CULLFACEENABLE], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_SCISSOR1], 0xFFFFFFFF);
	GPU::ExecuteOp(gstate.cmdmem[GE_CMD_SCISSOR2], 0xFFFFFFFF);
}
