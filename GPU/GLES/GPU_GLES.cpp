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

#include "base/logging.h"
#include "profiler/profiler.h"

#include "Common/ChunkFile.h"
#include "Common/GraphicsContext.h"

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMapHelpers.h"
#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/FramebufferCommon.h"

#include "GPU/GLES/GLStateCache.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/GPU_GLES.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/TextureCache.h"

#include "Core/MIPS/MIPS.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"

#ifdef _WIN32
#include "Windows/GPU/WindowsGLContext.h"
#endif

enum {
	FLAG_FLUSHBEFORE = 1,
	FLAG_FLUSHBEFOREONCHANGE = 2,
	FLAG_EXECUTE = 4,  // needs to actually be executed. unused for now.
	FLAG_EXECUTEONCHANGE = 8,
	FLAG_ANY_EXECUTE = 4 | 8,
	FLAG_READS_PC = 16,
	FLAG_WRITES_PC = 32,
	FLAG_DIRTYONCHANGE = 64,
};

struct CommandTableEntry {
	u8 cmd;
	u8 flags;
	u32 dirtyUniform;
	GPU_GLES::CmdFunc func;
};

// This table gets crunched into a faster form by init.
// TODO: Share this table between the backends. Will have to make another indirection for the function pointers though..
static const CommandTableEntry commandTable[] = {
	// Changes that dirty the framebuffer
	{GE_CMD_FRAMEBUFPTR, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_FramebufType},
	{GE_CMD_FRAMEBUFWIDTH, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_FramebufType},
	{GE_CMD_FRAMEBUFPIXFORMAT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_FramebufType},
	{GE_CMD_ZBUFPTR, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_ZBUFWIDTH, FLAG_FLUSHBEFOREONCHANGE},

	// Changes that dirty uniforms
	{GE_CMD_FOGCOLOR, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_FOGCOLOR, &GPU_GLES::Execute_FogColor},
	{GE_CMD_FOG1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_FOGCOEF, &GPU_GLES::Execute_FogCoef},
	{GE_CMD_FOG2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_FOGCOEF, &GPU_GLES::Execute_FogCoef},

	// Should these maybe flush?
	{GE_CMD_MINZ, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHRANGE},
	{GE_CMD_MAXZ, FLAG_FLUSHBEFOREONCHANGE, DIRTY_DEPTHRANGE},

	// Changes that dirty texture scaling.
	{GE_CMD_TEXMAPMODE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_UVSCALEOFFSET, &GPU_GLES::Execute_TexMapMode},
	{GE_CMD_TEXSCALEU, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_UVSCALEOFFSET, &GPU_GLES::Execute_TexScaleU},
	{GE_CMD_TEXSCALEV, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_UVSCALEOFFSET, &GPU_GLES::Execute_TexScaleV},
	{GE_CMD_TEXOFFSETU, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_UVSCALEOFFSET, &GPU_GLES::Execute_TexOffsetU},
	{GE_CMD_TEXOFFSETV, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_UVSCALEOFFSET, &GPU_GLES::Execute_TexOffsetV},

	// Changes that dirty the current texture. Really should be possible to avoid executing these if we compile
	// by adding some more flags.
	{GE_CMD_TEXSIZE0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPU_GLES::Execute_TexSize0},
	{GE_CMD_TEXSIZE1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXSIZE2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXSIZE3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXSIZE4, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXSIZE5, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXSIZE6, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXSIZE7, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexSizeN},
	{GE_CMD_TEXFORMAT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexFormat},
	{GE_CMD_TEXLEVEL, FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexLevel},
	{GE_CMD_TEXADDR0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddr0},
	{GE_CMD_TEXADDR1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXADDR2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXADDR3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXADDR4, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXADDR5, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXADDR6, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXADDR7, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexAddrN},
	{GE_CMD_TEXBUFWIDTH0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufw0},
	{GE_CMD_TEXBUFWIDTH1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	{GE_CMD_TEXBUFWIDTH2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	{GE_CMD_TEXBUFWIDTH3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	{GE_CMD_TEXBUFWIDTH4, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	{GE_CMD_TEXBUFWIDTH5, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	{GE_CMD_TEXBUFWIDTH6, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	{GE_CMD_TEXBUFWIDTH7, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexBufwN},
	// These must flush on change, so that LoadClut doesn't have to always flush.
	{GE_CMD_CLUTADDR, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_CLUTADDRUPPER, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_CLUTFORMAT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_ClutFormat},

	// These affect the fragment shader so need flushing.
	{GE_CMD_CLEARMODE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_TEXTUREMAPENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_FOGENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_TEXMODE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexParamType},
	{GE_CMD_TEXSHADELS, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_SHADEMODE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_TEXFUNC, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_COLORTEST, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_ALPHATESTENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_COLORTESTENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_COLORTESTMASK, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_ALPHACOLORMASK, &GPU_GLES::Execute_ColorTestMask},

	// These change the vertex shader so need flushing.
	{GE_CMD_REVERSENORMAL, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTINGENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTENABLE0, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTENABLE1, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTENABLE2, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTENABLE3, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTTYPE0, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTTYPE1, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTTYPE2, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LIGHTTYPE3, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_MATERIALUPDATE, FLAG_FLUSHBEFOREONCHANGE},

	// This changes both shaders so need flushing.
	{GE_CMD_LIGHTMODE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_TEXFILTER, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexParamType},
	{GE_CMD_TEXWRAP, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_TexParamType},

	// Uniform changes
	{GE_CMD_ALPHATEST, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_ALPHACOLORREF | DIRTY_ALPHACOLORMASK, &GPU_GLES::Execute_AlphaTest},
	{GE_CMD_COLORREF, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_ColorRef},
	{GE_CMD_TEXENVCOLOR, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_TEXENV, &GPU_GLES::Execute_TexEnvColor},

	// Simple render state changes. Handled in StateMapping.cpp.
	{GE_CMD_OFFSETX, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_OFFSETY, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_CULL, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_CULLFACEENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_DITHERENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_STENCILOP, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_STENCILTEST, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_STENCILREPLACEVALUE, &GPU_GLES::Execute_StencilTest},
	{GE_CMD_STENCILTESTENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_ALPHABLENDENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_BLENDMODE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_BLENDFIXEDA, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_BLENDFIXEDB, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_MASKRGB, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_MASKALPHA, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_ZTEST, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_ZTESTENABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_ZWRITEDISABLE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LOGICOP, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_LOGICOPENABLE, FLAG_FLUSHBEFOREONCHANGE},

	// Can probably ignore this one as we don't support AA lines.
	{GE_CMD_ANTIALIASENABLE, FLAG_FLUSHBEFOREONCHANGE},
	
	// Morph weights. TODO: Remove precomputation?
	{GE_CMD_MORPHWEIGHT0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT4, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT5, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT6, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},
	{GE_CMD_MORPHWEIGHT7, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE},

	// Control spline/bezier patches. Don't really require flushing as such, but meh.
	{GE_CMD_PATCHDIVISION, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_PATCHPRIMITIVE, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_PATCHFACING, FLAG_FLUSHBEFOREONCHANGE},
	{GE_CMD_PATCHCULLENABLE, FLAG_FLUSHBEFOREONCHANGE},

	// Viewport.
	{GE_CMD_VIEWPORTXSCALE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_ViewportType},
	{GE_CMD_VIEWPORTYSCALE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_ViewportType},
	{GE_CMD_VIEWPORTXCENTER, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_ViewportType},
	{GE_CMD_VIEWPORTYCENTER, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_ViewportType},
	{GE_CMD_VIEWPORTZSCALE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_DEPTHRANGE, &GPU_GLES::Execute_ViewportZType},
	{GE_CMD_VIEWPORTZCENTER, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_DEPTHRANGE, &GPU_GLES::Execute_ViewportZType},
	{GE_CMD_CLIPENABLE, FLAG_FLUSHBEFOREONCHANGE},

	// Region
	{GE_CMD_REGION1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_Region},
	{GE_CMD_REGION2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_Region},

	// Scissor
	{GE_CMD_SCISSOR1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_Scissor},
	{GE_CMD_SCISSOR2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_Scissor},

	// These dirty various vertex shader uniforms. Could embed information about that in this table and call dirtyuniform directly, hm...
	{GE_CMD_AMBIENTCOLOR, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_AMBIENT, &GPU_GLES::Execute_Ambient},
	{GE_CMD_AMBIENTALPHA, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_AMBIENT, &GPU_GLES::Execute_Ambient},
	{GE_CMD_MATERIALDIFFUSE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_MATDIFFUSE, &GPU_GLES::Execute_MaterialDiffuse},
	{GE_CMD_MATERIALEMISSIVE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_MATEMISSIVE, &GPU_GLES::Execute_MaterialEmissive},
	{GE_CMD_MATERIALAMBIENT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_MATAMBIENTALPHA, &GPU_GLES::Execute_MaterialAmbient},
	{GE_CMD_MATERIALALPHA, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_MATAMBIENTALPHA, &GPU_GLES::Execute_MaterialAmbient},
	{GE_CMD_MATERIALSPECULAR, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_MATSPECULAR, &GPU_GLES::Execute_MaterialSpecular},
	{GE_CMD_MATERIALSPECULARCOEF, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_MATSPECULAR, &GPU_GLES::Execute_MaterialSpecular},

	// These dirty uniforms, which could be table-ized to avoid execute.
	{GE_CMD_LX0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LY0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LZ0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LX1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LY1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LZ1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LX2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LY2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LZ2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LX3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LY3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LZ3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},

	{GE_CMD_LDX0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LDY0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LDZ0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LDX1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LDY1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LDZ1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LDX2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LDY2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LDZ2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LDX3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LDY3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LDZ3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},

	{GE_CMD_LKA0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LKB0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LKC0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LKA1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LKB1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LKC1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LKA2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LKB2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LKC2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LKA3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LKB3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LKC3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},

	{GE_CMD_LKS0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LKS1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LKS2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LKS3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},

	{GE_CMD_LKO0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LKO1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LKO2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LKO3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},

	{GE_CMD_LAC0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LDC0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LSC0, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT0, &GPU_GLES::Execute_Light0Param},
	{GE_CMD_LAC1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LDC1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LSC1, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT1, &GPU_GLES::Execute_Light1Param},
	{GE_CMD_LAC2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LDC2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LSC2, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT2, &GPU_GLES::Execute_Light2Param},
	{GE_CMD_LAC3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LDC3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},
	{GE_CMD_LSC3, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, DIRTY_LIGHT3, &GPU_GLES::Execute_Light3Param},

	// Ignored commands
	{GE_CMD_TEXFLUSH, 0},
	{GE_CMD_TEXLODSLOPE, 0},
	{GE_CMD_TEXSYNC, 0},

	// These are just nop or part of other later commands.
	{GE_CMD_NOP, 0},
	{GE_CMD_BASE, 0},
	{GE_CMD_TRANSFERSRC, 0},
	{GE_CMD_TRANSFERSRCW, 0},
	{GE_CMD_TRANSFERDST, 0},
	{GE_CMD_TRANSFERDSTW, 0},
	{GE_CMD_TRANSFERSRCPOS, 0},
	{GE_CMD_TRANSFERDSTPOS, 0},
	{GE_CMD_TRANSFERSIZE, 0},

	// From Common. No flushing but definitely need execute.
	{GE_CMD_OFFSETADDR, FLAG_EXECUTE, 0, &GPUCommon::Execute_OffsetAddr},
	{GE_CMD_ORIGIN, FLAG_EXECUTE | FLAG_READS_PC, 0, &GPUCommon::Execute_Origin},  // Really?
	{GE_CMD_PRIM, FLAG_EXECUTE, 0, &GPU_GLES::Execute_Prim},
	{GE_CMD_JUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Jump},
	{GE_CMD_CALL, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Call},
	{GE_CMD_RET, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_Ret},
	{GE_CMD_END, FLAG_FLUSHBEFORE | FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_End},  // Flush?
	{GE_CMD_VADDR, FLAG_EXECUTE, 0, &GPU_GLES::Execute_Vaddr},
	{GE_CMD_IADDR, FLAG_EXECUTE, 0, &GPU_GLES::Execute_Iaddr},
	{GE_CMD_BJUMP, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPUCommon::Execute_BJump},  // EXECUTE
	{GE_CMD_BOUNDINGBOX, FLAG_EXECUTE, 0, &GPU_GLES::Execute_BoundingBox}, // + FLUSHBEFORE when we implement... or not, do we need to?

	// Changing the vertex type requires us to flush.
	{GE_CMD_VERTEXTYPE, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTEONCHANGE, 0, &GPU_GLES::Execute_VertexType},

	{GE_CMD_BEZIER, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPU_GLES::Execute_Bezier},
	{GE_CMD_SPLINE, FLAG_FLUSHBEFORE | FLAG_EXECUTE, 0, &GPU_GLES::Execute_Spline},

	// These two are actually processed in CMD_END.
	{GE_CMD_SIGNAL, FLAG_FLUSHBEFORE},
	{GE_CMD_FINISH, FLAG_FLUSHBEFORE},

	// Changes that trigger data copies. Only flushing on change for LOADCLUT must be a bit of a hack...
	{GE_CMD_LOADCLUT, FLAG_FLUSHBEFOREONCHANGE | FLAG_EXECUTE, 0, &GPU_GLES::Execute_LoadClut},
	{GE_CMD_TRANSFERSTART, FLAG_FLUSHBEFORE | FLAG_EXECUTE | FLAG_READS_PC, 0, &GPU_GLES::Execute_BlockTransferStart},

	// We don't use the dither table.
	{GE_CMD_DITH0},
	{GE_CMD_DITH1},
	{GE_CMD_DITH2},
	{GE_CMD_DITH3},

	// These handle their own flushing.
	{GE_CMD_WORLDMATRIXNUMBER, FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPU_GLES::Execute_WorldMtxNum},
	{GE_CMD_WORLDMATRIXDATA,   FLAG_EXECUTE, 0, &GPU_GLES::Execute_WorldMtxData},
	{GE_CMD_VIEWMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPU_GLES::Execute_ViewMtxNum},
	{GE_CMD_VIEWMATRIXDATA,    FLAG_EXECUTE, 0, &GPU_GLES::Execute_ViewMtxData},
	{GE_CMD_PROJMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPU_GLES::Execute_ProjMtxNum},
	{GE_CMD_PROJMATRIXDATA,    FLAG_EXECUTE, 0, &GPU_GLES::Execute_ProjMtxData},
	{GE_CMD_TGENMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPU_GLES::Execute_TgenMtxNum},
	{GE_CMD_TGENMATRIXDATA,    FLAG_EXECUTE, 0, &GPU_GLES::Execute_TgenMtxData},
	{GE_CMD_BONEMATRIXNUMBER,  FLAG_EXECUTE | FLAG_READS_PC | FLAG_WRITES_PC, 0, &GPU_GLES::Execute_BoneMtxNum},
	{GE_CMD_BONEMATRIXDATA,    FLAG_EXECUTE, 0, &GPU_GLES::Execute_BoneMtxData},

	// Vertex Screen/Texture/Color
	{GE_CMD_VSCX, FLAG_EXECUTE},
	{GE_CMD_VSCY, FLAG_EXECUTE},
	{GE_CMD_VSCZ, FLAG_EXECUTE},
	{GE_CMD_VTCS, FLAG_EXECUTE},
	{GE_CMD_VTCT, FLAG_EXECUTE},
	{GE_CMD_VTCQ, FLAG_EXECUTE},
	{GE_CMD_VCV, FLAG_EXECUTE},
	{GE_CMD_VAP, FLAG_EXECUTE},
	{GE_CMD_VFC, FLAG_EXECUTE},
	{GE_CMD_VSCV, FLAG_EXECUTE},

	// "Missing" commands (gaps in the sequence)
	{GE_CMD_UNKNOWN_03, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_0D, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_11, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_29, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_34, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_35, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_39, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_4E, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_4F, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_52, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_59, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_5A, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_B6, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_B7, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_D1, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_ED, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_EF, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FA, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FB, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FC, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FD, FLAG_EXECUTE},
	{GE_CMD_UNKNOWN_FE, FLAG_EXECUTE},
	// Appears to be debugging related or something?  Hit a lot in GoW.
	{GE_CMD_UNKNOWN_FF, 0},
};

GPU_GLES::CommandInfo GPU_GLES::cmdInfo_[256];

GPU_GLES::GPU_GLES(GraphicsContext *ctx)
: gfxCtx_(ctx) {
	UpdateVsyncInterval(true);
	CheckGPUFeatures();

	shaderManager_ = new ShaderManager();
	framebufferManagerGL_ = new FramebufferManager();
	framebufferManager_ = framebufferManagerGL_;
	textureCacheGL_ = new TextureCache();
	textureCache_ = textureCacheGL_;

	drawEngine_.SetShaderManager(shaderManager_);
	drawEngine_.SetTextureCache(textureCacheGL_);
	drawEngine_.SetFramebufferManager(framebufferManagerGL_);
	drawEngine_.SetFragmentTestCache(&fragmentTestCache_);
	framebufferManagerGL_->Init();
	framebufferManagerGL_->SetTextureCache(textureCacheGL_);
	framebufferManagerGL_->SetShaderManager(shaderManager_);
	framebufferManagerGL_->SetTransformDrawEngine(&drawEngine_);
	textureCacheGL_->SetFramebufferManager(framebufferManagerGL_);
	textureCacheGL_->SetDepalShaderCache(&depalShaderCache_);
	textureCacheGL_->SetShaderManager(shaderManager_);
	textureCacheGL_->SetTransformDrawEngine(&drawEngine_);
	fragmentTestCache_.SetTextureCache(textureCacheGL_);

	// Sanity check gstate
	if ((int *)&gstate.transferstart - (int *)&gstate != 0xEA) {
		ERROR_LOG(G3D, "gstate has drifted out of sync!");
	}

	// Sanity check cmdInfo_ table - no dupes please
	std::set<u8> dupeCheck;
	memset(cmdInfo_, 0, sizeof(cmdInfo_));
	for (size_t i = 0; i < ARRAY_SIZE(commandTable); i++) {
		const u8 cmd = commandTable[i].cmd;
		if (dupeCheck.find(cmd) != dupeCheck.end()) {
			ERROR_LOG(G3D, "Command table Dupe: %02x (%i)", (int)cmd, (int)cmd);
		} else {
			dupeCheck.insert(cmd);
		}
		cmdInfo_[cmd].flags |= commandTable[i].flags;
		cmdInfo_[cmd].func = commandTable[i].func;
		if (!cmdInfo_[cmd].func) {
			cmdInfo_[cmd].func = &GPU_GLES::Execute_Generic;
		}
	}
	// Find commands missing from the table.
	for (int i = 0; i < 0xEF; i++) {
		if (dupeCheck.find((u8)i) == dupeCheck.end()) {
			ERROR_LOG(G3D, "Command missing from table: %02x (%i)", i, i);
		}
	}

	// No need to flush before the tex scale/offset commands if we are baking
	// the tex scale/offset into the vertices anyway.

	UpdateCmdInfo();

	BuildReportingInfo();
	// Update again after init to be sure of any silly driver problems.
	UpdateVsyncInterval(true);

	// Some of our defaults are different from hw defaults, let's assert them.
	// We restore each frame anyway, but here is convenient for tests.
	glstate.Restore();
	drawEngine_.RestoreVAO();
	textureCacheGL_->NotifyConfigChanged();

	// Load shader cache.
	std::string discID = g_paramSFO.GetValueString("DISC_ID");
	if (discID.size()) {
		File::CreateFullPath(GetSysDirectory(DIRECTORY_APP_CACHE));
		shaderCachePath_ = GetSysDirectory(DIRECTORY_APP_CACHE) + "/" + g_paramSFO.GetValueString("DISC_ID") + ".glshadercache";
		shaderManager_->LoadAndPrecompile(shaderCachePath_);
	}
}

GPU_GLES::~GPU_GLES() {
	framebufferManagerGL_->DestroyAllFBOs(true);
	shaderManager_->ClearCache(true);
	depalShaderCache_.Clear();
	fragmentTestCache_.Clear();
	if (!shaderCachePath_.empty()) {
		shaderManager_->Save(shaderCachePath_);
	}
	delete shaderManager_;
	shaderManager_ = nullptr;

#ifdef _WIN32
	gfxCtx_->SwapInterval(0);
#endif
}

// Take the raw GL extension and versioning data and turn into feature flags.
void GPU_GLES::CheckGPUFeatures() {
	u32 features = 0;
	if (gl_extensions.ARB_blend_func_extended || gl_extensions.EXT_blend_func_extended) {
		if (gl_extensions.gpuVendor == GPU_VENDOR_INTEL || !gl_extensions.VersionGEThan(3, 0, 0)) {
			// Don't use this extension to off on sub 3.0 OpenGL versions as it does not seem reliable
			// Also on Intel, see https://github.com/hrydgard/ppsspp/issues/4867
		} else {
#ifdef __ANDROID__
			// This appears to be broken on nVidia Shield TV.
			if (gl_extensions.gpuVendor != GPU_VENDOR_NVIDIA) {
				features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
			}
#else
			features |= GPU_SUPPORTS_DUALSOURCE_BLEND;
#endif
		}
	}

	if (gl_extensions.IsGLES) {
		if (gl_extensions.GLES3)
			features |= GPU_SUPPORTS_GLSL_ES_300;
	} else {
		if (gl_extensions.VersionGEThan(3, 3, 0))
			features |= GPU_SUPPORTS_GLSL_330;
	}

	if (gl_extensions.EXT_shader_framebuffer_fetch || gl_extensions.NV_shader_framebuffer_fetch || gl_extensions.ARM_shader_framebuffer_fetch) {
		// This has caused problems in the past.  Let's only enable on GLES3.
		if (features & GPU_SUPPORTS_GLSL_ES_300) {
			features |= GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH;
		}
	}
	
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.EXT_framebuffer_object || gl_extensions.IsGLES) {
		features |= GPU_SUPPORTS_FBO;
	}
	if (gl_extensions.ARB_framebuffer_object || gl_extensions.GLES3) {
		features |= GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT;
	}
	if (gl_extensions.NV_framebuffer_blit) {
		features |= GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT;
	}
	if (gl_extensions.ARB_vertex_array_object && gl_extensions.IsCoreContext) {
		features |= GPU_SUPPORTS_VAO;
	}

	bool useCPU = false;
	if (!gl_extensions.IsGLES) {
		// Urrgh, we don't even define FB_READFBOMEMORY_CPU on mobile
#ifndef USING_GLES2
		useCPU = g_Config.iRenderingMode == FB_READFBOMEMORY_CPU;
#endif
		// Some cards or drivers seem to always dither when downloading a framebuffer to 16-bit.
		// This causes glitches in games that expect the exact values.
		// It has not been experienced on NVIDIA cards, so those are left using the GPU (which is faster.)
		if (g_Config.iRenderingMode == FB_BUFFERED_MODE) {
			if (gl_extensions.gpuVendor != GPU_VENDOR_NVIDIA || gl_extensions.ver[0] < 3) {
				useCPU = true;
			}
		}
	} else {
		useCPU = true;
	}

	if (useCPU)
		features |= GPU_PREFER_CPU_DOWNLOAD;

	if ((gl_extensions.gpuVendor == GPU_VENDOR_NVIDIA) || (gl_extensions.gpuVendor == GPU_VENDOR_AMD))
		features |= GPU_PREFER_REVERSE_COLOR_ORDER;

	if (gl_extensions.OES_texture_npot)
		features |= GPU_SUPPORTS_OES_TEXTURE_NPOT;

	if (gl_extensions.EXT_unpack_subimage || !gl_extensions.IsGLES)
		features |= GPU_SUPPORTS_UNPACK_SUBIMAGE;

	if (gl_extensions.EXT_blend_minmax || gl_extensions.GLES3)
		features |= GPU_SUPPORTS_BLEND_MINMAX;

	if (gl_extensions.OES_copy_image || gl_extensions.NV_copy_image || gl_extensions.EXT_copy_image || gl_extensions.ARB_copy_image)
		features |= GPU_SUPPORTS_ANY_COPY_IMAGE;

	if (!gl_extensions.IsGLES)
		features |= GPU_SUPPORTS_LOGIC_OP;

	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		features |= GPU_SUPPORTS_TEXTURE_LOD_CONTROL;

	features |= GPU_SUPPORTS_ANISOTROPY;

	// If we already have a 16-bit depth buffer, we don't need to round.
	if (fbo_standard_z_depth() > 16) {
		if (!g_Config.bHighQualityDepth && (features & GPU_SUPPORTS_ACCURATE_DEPTH) != 0) {
			features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
		} else if (PSP_CoreParameter().compat.flags().PixelDepthRounding) {
			if (!gl_extensions.IsGLES || gl_extensions.GLES3) {
				// Use fragment rounding on desktop and GLES3, most accurate.
				features |= GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
			} else if (fbo_standard_z_depth() == 24 && (features & GPU_SUPPORTS_ACCURATE_DEPTH) != 0) {
				// Here we can simulate a 16 bit depth buffer by scaling.
				// Note that the depth buffer is fixed point, not floating, so dividing by 256 is pretty good.
				features |= GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
			} else {
				// At least do vertex rounding if nothing else.
				features |= GPU_ROUND_DEPTH_TO_16BIT;
			}
		} else if (PSP_CoreParameter().compat.flags().VertexDepthRounding) {
			features |= GPU_ROUND_DEPTH_TO_16BIT;
		}
	}

	// The Phantasy Star hack :(
	if (PSP_CoreParameter().compat.flags().DepthRangeHack && (features & GPU_SUPPORTS_ACCURATE_DEPTH) == 0) {
		features |= GPU_USE_DEPTH_RANGE_HACK;
	}

#ifdef MOBILE_DEVICE
	// Arguably, we should turn off GPU_IS_MOBILE on like modern Tegras, etc.
	features |= GPU_IS_MOBILE;
#endif

	gstate_c.featureFlags = features;
}

// Let's avoid passing nulls into snprintf().
static const char *GetGLStringAlways(GLenum name) {
	const GLubyte *value = glGetString(name);
	if (!value)
		return "?";
	return (const char *)value;
}

// Needs to be called on GPU thread, not reporting thread.
void GPU_GLES::BuildReportingInfo() {
	const char *glVendor = GetGLStringAlways(GL_VENDOR);
	const char *glRenderer = GetGLStringAlways(GL_RENDERER);
	const char *glVersion = GetGLStringAlways(GL_VERSION);
	const char *glSlVersion = GetGLStringAlways(GL_SHADING_LANGUAGE_VERSION);
	const char *glExtensions = nullptr;

	if (gl_extensions.VersionGEThan(3, 0)) {
		glExtensions = g_all_gl_extensions.c_str();
	} else {
		glExtensions = GetGLStringAlways(GL_EXTENSIONS);
	}

	char temp[16384];
	snprintf(temp, sizeof(temp), "%s (%s %s), %s (extensions: %s)", glVersion, glVendor, glRenderer, glSlVersion, glExtensions);
	reportingPrimaryInfo_ = glVendor;
	reportingFullInfo_ = temp;

	Reporting::UpdateConfig();
}

void GPU_GLES::DeviceLost() {
	ILOG("GPU_GLES: DeviceLost");
	// Should only be executed on the GL thread.

	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	// TransformDraw has registered as a GfxResourceHolder.
	shaderManager_->ClearCache(false);
	textureCacheGL_->Clear(false);
	fragmentTestCache_.Clear(false);
	depalShaderCache_.Clear();
	framebufferManagerGL_->DeviceLost();
}

void GPU_GLES::DeviceRestore() {
	ILOG("GPU_GLES: DeviceRestore");

	UpdateVsyncInterval(true);
}

void GPU_GLES::ReinitializeInternal() {
	textureCacheGL_->Clear(true);
	depalShaderCache_.Clear();
	framebufferManagerGL_->DestroyAllFBOs(true);
	framebufferManagerGL_->Resized();
}

void GPU_GLES::InitClearInternal() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
}

void GPU_GLES::DumpNextFrame() {
	dumpNextFrame_ = true;
}

void GPU_GLES::BeginFrame() {
	ScheduleEvent(GPU_EVENT_BEGIN_FRAME);
}

inline void GPU_GLES::UpdateVsyncInterval(bool force) {
#ifdef _WIN32
	int desiredVSyncInterval = g_Config.bVSync ? 1 : 0;
	if (PSP_CoreParameter().unthrottle) {
		desiredVSyncInterval = 0;
	}
	if (PSP_CoreParameter().fpsLimit == 1) {
		// For an alternative speed that is a clean factor of 60, the user probably still wants vsync.
		if (g_Config.iFpsLimit == 0 || (g_Config.iFpsLimit != 15 && g_Config.iFpsLimit != 30 && g_Config.iFpsLimit != 60)) {
			desiredVSyncInterval = 0;
		}
	}

	if (desiredVSyncInterval != lastVsync_ || force) {
		// Disabled EXT_swap_control_tear for now, it never seems to settle at the correct timing
		// so it just keeps tearing. Not what I hoped for...
		//if (gl_extensions.EXT_swap_control_tear) {
		//	// See http://developer.download.nvidia.com/opengl/specs/WGL_EXT_swap_control_tear.txt
		//	glstate.SetVSyncInterval(-desiredVSyncInterval);
		//} else {
		gfxCtx_->SwapInterval(desiredVSyncInterval);
		//}
		lastVsync_ = desiredVSyncInterval;
	}
#endif
}

void GPU_GLES::UpdateCmdInfo() {
	if (g_Config.bSoftwareSkinning) {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags &= ~FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPU_GLES::Execute_VertexTypeSkinning;
	} else {
		cmdInfo_[GE_CMD_VERTEXTYPE].flags |= FLAG_FLUSHBEFOREONCHANGE;
		cmdInfo_[GE_CMD_VERTEXTYPE].func = &GPU_GLES::Execute_VertexType;
	}
}

void GPU_GLES::ReapplyGfxStateInternal() {
	drawEngine_.RestoreVAO();
	glstate.Restore();
	GPUCommon::ReapplyGfxStateInternal();
}

void GPU_GLES::BeginFrameInternal() {
	if (resized_) {
		CheckGPUFeatures();
		UpdateCmdInfo();
		drawEngine_.Resized();
		textureCacheGL_->NotifyConfigChanged();
	}
	UpdateVsyncInterval(resized_);
	resized_ = false;

	textureCacheGL_->StartFrame();
	drawEngine_.DecimateTrackedVertexArrays();
	drawEngine_.DecimateBuffers();
	depalShaderCache_.Decimate();
	fragmentTestCache_.Decimate();

	if (dumpNextFrame_) {
		NOTICE_LOG(G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}

	// Save the cache from time to time. TODO: How often?
	if (!shaderCachePath_.empty() && (gpuStats.numFlips & 1023) == 0) {
		shaderManager_->Save(shaderCachePath_);
	}

	shaderManager_->DirtyShader();

	// Not sure if this is really needed.
	shaderManager_->DirtyUniform(DIRTY_ALL);

	framebufferManagerGL_->BeginFrame();
}

void GPU_GLES::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	host->GPUNotifyDisplay(framebuf, stride, format);
	framebufferManagerGL_->SetDisplayFramebuffer(framebuf, stride, format);
}

bool GPU_GLES::FramebufferDirty() {
	if (ThreadEnabled()) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	VirtualFramebuffer *vfb = framebufferManagerGL_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->dirtyAfterDisplay;
		vfb->dirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

bool GPU_GLES::FramebufferReallyDirty() {
	if (ThreadEnabled()) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	VirtualFramebuffer *vfb = framebufferManagerGL_->GetDisplayVFB();
	if (vfb) {
		bool dirty = vfb->reallyDirtyAfterDisplay;
		vfb->reallyDirtyAfterDisplay = false;
		return dirty;
	}
	return true;
}

void GPU_GLES::CopyDisplayToOutputInternal() {
	// Flush anything left over.
	framebufferManagerGL_->RebindFramebuffer();
	drawEngine_.Flush();

	shaderManager_->DirtyLastShader();

	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	framebufferManagerGL_->CopyDisplayToOutput();
	framebufferManagerGL_->EndFrame();

	// If buffered, discard the depth buffer of the backbuffer. Don't even know if we need one.
#if 0
#ifdef USING_GLES2
	if (gl_extensions.EXT_discard_framebuffer && g_Config.iRenderingMode != 0) {
		GLenum attachments[] = {GL_DEPTH_EXT, GL_STENCIL_EXT};
		glDiscardFramebufferEXT(GL_FRAMEBUFFER, 2, attachments);
	}
#endif
#endif

	gstate_c.textureChanged = TEXCHANGE_UPDATED;
}

// Maybe should write this in ASM...
void GPU_GLES::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("gpuloop");
	const CommandInfo *cmdInfo = cmdInfo_;
	int dc = downcount;
	for (; dc > 0; --dc) {
		// We know that display list PCs have the upper nibble == 0 - no need to mask the pointer
		const u32 op = *(const u32 *)(Memory::base + list.pc);
		const u32 cmd = op >> 24;
		const CommandInfo info = cmdInfo[cmd];
		const u8 cmdFlags = info.flags;      // If we stashed the cmdFlags in the top bits of the cmdmem, we could get away with one table lookup instead of two
		const u32 diff = op ^ gstate.cmdmem[cmd];
		// Inlined CheckFlushOp here to get rid of the dumpThisFrame_ check.
		if ((cmdFlags & FLAG_FLUSHBEFORE) || (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE))) {
			drawEngine_.Flush();
		}
		gstate.cmdmem[cmd] = op;  // TODO: no need to write if diff==0...
		if ((cmdFlags & FLAG_EXECUTE) || (diff && (cmdFlags & FLAG_EXECUTEONCHANGE))) {
			downcount = dc;
			(this->*info.func)(op, diff);
			dc = downcount;
		}
		list.pc += 4;
	}
	downcount = 0;
}

void GPU_GLES::FinishDeferred() {
	// This finishes reading any vertex data that is pending.
	drawEngine_.FinishDeferred();
}

inline void GPU_GLES::CheckFlushOp(int cmd, u32 diff) {
	const u8 cmdFlags = cmdInfo_[cmd].flags;
	if ((cmdFlags & FLAG_FLUSHBEFORE) || (diff && (cmdFlags & FLAG_FLUSHBEFOREONCHANGE))) {
		if (dumpThisFrame_) {
			NOTICE_LOG(G3D, "================ FLUSH ================");
		}
		drawEngine_.Flush();
	}
}

void GPU_GLES::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}

void GPU_GLES::ExecuteOp(u32 op, u32 diff) {
	const u8 cmd = op >> 24;
	const CommandInfo info = cmdInfo_[cmd];
	const u8 cmdFlags = info.flags;
	if ((cmdFlags & FLAG_EXECUTE) || (diff && (cmdFlags & FLAG_EXECUTEONCHANGE))) {
		(this->*info.func)(op, diff);
	}
}

void GPU_GLES::Execute_Vaddr(u32 op, u32 diff) {
	gstate_c.vertexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void GPU_GLES::Execute_Iaddr(u32 op, u32 diff) {
	gstate_c.indexAddr = gstate_c.getRelativeAddress(op & 0x00FFFFFF);
}

void GPU_GLES::Execute_Prim(u32 op, u32 diff) {
	// This drives all drawing. All other state we just buffer up, then we apply it only
	// when it's time to draw. As most PSP games set state redundantly ALL THE TIME, this is a huge optimization.

	u32 data = op & 0xFFFFFF;
	u32 count = data & 0xFFFF;
	// Upper bits are ignored.
	GEPrimitiveType prim = static_cast<GEPrimitiveType>((data >> 16) & 7);

	if (count == 0)
		return;

	// Discard AA lines as we can't do anything that makes sense with these anyway. The SW plugin might, though.

	if (gstate.isAntiAliasEnabled()) {
		// Discard AA lines in DOA
		if (prim == GE_PRIM_LINE_STRIP)
			return;
		// Discard AA lines in Summon Night 5
		if ((prim == GE_PRIM_LINES) && gstate.isSkinningEnabled())
			return;
	}

	// This also makes skipping drawing very effective.
	framebufferManagerGL_->SetRenderFrameBuffer(gstate_c.framebufChanged, gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
		drawEngine_.SetupVertexDecoder(gstate.vertType);
		// Rough estimate, not sure what's correct.
		int vertexCost = drawEngine_.EstimatePerVertexCost();
		cyclesExecuted += vertexCost * count;
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *verts = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *inds = 0;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		inds = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

#ifndef MOBILE_DEVICE
	if (prim > GE_PRIM_RECTANGLES) {
		ERROR_LOG_REPORT_ONCE(reportPrim, G3D, "Unexpected prim type: %d", prim);
	}
#endif

	int bytesRead = 0;
	drawEngine_.SubmitPrim(verts, inds, prim, count, gstate.vertType, &bytesRead);

	int vertexCost = drawEngine_.EstimatePerVertexCost();
	gpuStats.vertexGPUCycles += vertexCost * count;
	cyclesExecuted += vertexCost * count;

	// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
	// Some games rely on this, they don't bother reloading VADDR and IADDR.
	// The VADDR/IADDR registers are NOT updated.
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPU_GLES::Execute_VertexType(u32 op, u32 diff) {
	if (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK)) {
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
	}
}

void GPU_GLES::Execute_VertexTypeSkinning(u32 op, u32 diff) {
	// Don't flush when weight count changes, unless morph is enabled.
	if ((diff & ~GE_VTYPE_WEIGHTCOUNT_MASK) || (op & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
		// Restore and flush
		gstate.vertType ^= diff;
		Flush();
		gstate.vertType ^= diff;
		if (diff & (GE_VTYPE_TC_MASK | GE_VTYPE_THROUGH_MASK))
			shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		// In this case, we may be doing weights and morphs.
		// Update any bone matrix uniforms so it uses them correctly.
		if ((op & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
			shaderManager_->DirtyUniform(gstate_c.deferredVertTypeDirty);
			gstate_c.deferredVertTypeDirty = 0;
		}
	}
}

void GPU_GLES::Execute_Bezier(u32 op, u32 diff) {
	// This also make skipping drawing very effective.
	framebufferManagerGL_->SetRenderFrameBuffer(gstate_c.framebufChanged, gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) {
		DEBUG_LOG_REPORT(G3D, "Bezier + morph: %i", (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT);
	}
	if (vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(G3D, "Bezier + skinning: %i", vertTypeGetNumBoneWeights(gstate.vertType));
	}

	GEPatchPrimType patchPrim = gstate.getPatchPrimitiveType();
	int bz_ucount = op & 0xFF;
	int bz_vcount = (op >> 8) & 0xFF;
	bool computeNormals = gstate.isLightingEnabled();
	bool patchFacing = gstate.patchfacing & 1;

	if (g_Config.bHardwareTessellation && g_Config.bHardwareTransform && !g_Config.bSoftwareRendering) {
		gstate_c.bezier = true;
		if (gstate_c.bezier_count_u != bz_ucount) {
			shaderManager_->DirtyUniform(DIRTY_BEZIERCOUNTU);
			gstate_c.bezier_count_u = bz_ucount;
		}
	}

	int bytesRead = 0;
	drawEngine_.SubmitBezier(control_points, indices, gstate.getPatchDivisionU(), gstate.getPatchDivisionV(), bz_ucount, bz_vcount, patchPrim, computeNormals, patchFacing, gstate.vertType, &bytesRead);
	
	gstate_c.bezier = false;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = bz_ucount * bz_vcount;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPU_GLES::Execute_Spline(u32 op, u32 diff) {
	// This also make skipping drawing very effective.
	framebufferManagerGL_->SetRenderFrameBuffer(gstate_c.framebufChanged, gstate_c.skipDrawReason);
	if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
		// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
		return;
	}

	if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
		ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
		return;
	}

	void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
	void *indices = NULL;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
			ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
			return;
		}
		indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
	}

	if (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) {
		DEBUG_LOG_REPORT(G3D, "Spline + morph: %i", (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT);
	}
	if (vertTypeIsSkinningEnabled(gstate.vertType)) {
		DEBUG_LOG_REPORT(G3D, "Spline + skinning: %i", vertTypeGetNumBoneWeights(gstate.vertType));
	}

	int sp_ucount = op & 0xFF;
	int sp_vcount = (op >> 8) & 0xFF;
	int sp_utype = (op >> 16) & 0x3;
	int sp_vtype = (op >> 18) & 0x3;
	GEPatchPrimType patchPrim = gstate.getPatchPrimitiveType();
	bool computeNormals = gstate.isLightingEnabled();
	bool patchFacing = gstate.patchfacing & 1;
	u32 vertType = gstate.vertType;

	if (g_Config.bHardwareTessellation && g_Config.bHardwareTransform && !g_Config.bSoftwareRendering) {
		gstate_c.spline = true;
		if (gstate_c.spline_count_u != sp_ucount) {
			shaderManager_->DirtyUniform(DIRTY_SPLINECOUNTU);
			gstate_c.spline_count_u = sp_ucount;
		}
		if (gstate_c.spline_count_v != sp_vcount) {
			shaderManager_->DirtyUniform(DIRTY_SPLINECOUNTV);
			gstate_c.spline_count_v = sp_vcount;
		}
		if (gstate_c.spline_type_u != sp_utype) {
			shaderManager_->DirtyUniform(DIRTY_SPLINETYPEU);
			gstate_c.spline_type_u = sp_utype;
		}
		if (gstate_c.spline_type_v != sp_vtype) {
			shaderManager_->DirtyUniform(DIRTY_SPLINETYPEV);
			gstate_c.spline_type_v = sp_vtype;
		}
	}

	int bytesRead = 0;
	drawEngine_.SubmitSpline(control_points, indices, gstate.getPatchDivisionU(), gstate.getPatchDivisionV(), sp_ucount, sp_vcount, sp_utype, sp_vtype, patchPrim, computeNormals, patchFacing, vertType, &bytesRead);
	
	gstate_c.spline = false;

	// After drawing, we advance pointers - see SubmitPrim which does the same.
	int count = sp_ucount * sp_vcount;
	AdvanceVerts(gstate.vertType, count, bytesRead);
}

void GPU_GLES::Execute_Region(u32 op, u32 diff) {
	gstate_c.framebufChanged = true;
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_Scissor(u32 op, u32 diff) {
	gstate_c.framebufChanged = true;
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_FramebufType(u32 op, u32 diff) {
	gstate_c.framebufChanged = true;
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_ViewportType(u32 op, u32 diff) {
	gstate_c.framebufChanged = true;
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_ViewportZType(u32 op, u32 diff) {
	gstate_c.framebufChanged = true;
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	shaderManager_->DirtyUniform(DIRTY_DEPTHRANGE);
}

void GPU_GLES::Execute_TexScaleU(u32 op, u32 diff) {
	gstate_c.uv.uScale = getFloat24(op);
	shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
}

void GPU_GLES::Execute_TexScaleV(u32 op, u32 diff) {
	gstate_c.uv.vScale = getFloat24(op);
	shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
}

void GPU_GLES::Execute_TexOffsetU(u32 op, u32 diff) {
	gstate_c.uv.uOff = getFloat24(op);
	shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
}

void GPU_GLES::Execute_TexOffsetV(u32 op, u32 diff) {
	gstate_c.uv.vOff = getFloat24(op);
	shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
}

void GPU_GLES::Execute_TexAddr0(u32 op, u32 diff) {
	gstate_c.textureChanged = TEXCHANGE_UPDATED;
	shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
}

void GPU_GLES::Execute_TexAddrN(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_TexBufw0(u32 op, u32 diff) {
	gstate_c.textureChanged = TEXCHANGE_UPDATED;
}

void GPU_GLES::Execute_TexBufwN(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_TexSize0(u32 op, u32 diff) {
	// Render to texture may have overridden the width/height.
	// Don't reset it unless the size is different / the texture has changed.
	if (diff || gstate_c.textureChanged != TEXCHANGE_UNCHANGED) {
		gstate_c.curTextureWidth = gstate.getTextureWidth(0);
		gstate_c.curTextureHeight = gstate.getTextureHeight(0);
		shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
		// We will need to reset the texture now.
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	}
}

void GPU_GLES::Execute_TexSizeN(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_TexFormat(u32 op, u32 diff) {
	gstate_c.textureChanged = TEXCHANGE_UPDATED;
}

void GPU_GLES::Execute_TexMapMode(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_UVSCALEOFFSET);
}

void GPU_GLES::Execute_TexParamType(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_TexEnvColor(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_TEXENV);
}

void GPU_GLES::Execute_TexLevel(u32 op, u32 diff) {
	// I had hoped that this would let us avoid excessively flushing in Gran Turismo, but not so,
	// as the game switches rapidly between modes 0 and 1.
	/*
	if (gstate.getTexLevelMode() == GE_TEXLEVEL_MODE_CONST) {
		gstate.texlevel ^= diff;
		Flush();
		gstate.texlevel ^= diff;
	}
	*/
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
}

void GPU_GLES::Execute_LoadClut(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	textureCacheGL_->LoadClut(gstate.getClutAddress(), gstate.getClutLoadBytes());
	// This could be used to "dirty" textures with clut.
}

void GPU_GLES::Execute_ClutFormat(u32 op, u32 diff) {
	gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	// This could be used to "dirty" textures with clut.
}

void GPU_GLES::Execute_Ambient(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_AMBIENT);
}

void GPU_GLES::Execute_MaterialDiffuse(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_MATDIFFUSE);
}

void GPU_GLES::Execute_MaterialEmissive(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_MATEMISSIVE);
}

void GPU_GLES::Execute_MaterialAmbient(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_MATAMBIENTALPHA);
}

void GPU_GLES::Execute_MaterialSpecular(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_MATSPECULAR);
}

void GPU_GLES::Execute_Light0Param(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_LIGHT0);
}

void GPU_GLES::Execute_Light1Param(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_LIGHT1);
}

void GPU_GLES::Execute_Light2Param(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_LIGHT2);
}

void GPU_GLES::Execute_Light3Param(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_LIGHT3);
}

void GPU_GLES::Execute_FogColor(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_FOGCOLOR);
}

void GPU_GLES::Execute_FogCoef(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_FOGCOEF);
}

void GPU_GLES::Execute_ColorTestMask(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_ALPHACOLORMASK);
}

void GPU_GLES::Execute_AlphaTest(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_ALPHACOLORREF);
	shaderManager_->DirtyUniform(DIRTY_ALPHACOLORMASK);
}

void GPU_GLES::Execute_StencilTest(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_STENCILREPLACEVALUE);
}

void GPU_GLES::Execute_ColorRef(u32 op, u32 diff) {
	shaderManager_->DirtyUniform(DIRTY_ALPHACOLORREF);
}

void GPU_GLES::Execute_WorldMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_WORLDMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.worldMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	while ((src[i] >> 24) == GE_CMD_WORLDMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_WORLDMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPU_GLES::Execute_WorldMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.worldmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.worldMatrix)[num]) {
		Flush();
		((u32 *)gstate.worldMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_WORLDMATRIX);
	}
	num++;
	gstate.worldmtxnum = (GE_CMD_WORLDMATRIXNUMBER << 24) | (num & 0xF);
}

void GPU_GLES::Execute_ViewMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_VIEWMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.viewMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	while ((src[i] >> 24) == GE_CMD_VIEWMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_VIEWMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPU_GLES::Execute_ViewMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.viewmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.viewMatrix)[num]) {
		Flush();
		((u32 *)gstate.viewMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_VIEWMATRIX);
	}
	num++;
	gstate.viewmtxnum = (GE_CMD_VIEWMATRIXNUMBER << 24) | (num & 0xF);
}

void GPU_GLES::Execute_ProjMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_PROJMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.projMatrix + (op & 0xF));
	const int end = 16 - (op & 0xF);
	int i = 0;

	while ((src[i] >> 24) == GE_CMD_PROJMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPU_GLES::Execute_ProjMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.projmtxnum & 0xF;
	u32 newVal = op << 8;
	if (newVal != ((const u32 *)gstate.projMatrix)[num]) {
		Flush();
		((u32 *)gstate.projMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
	num++;
	gstate.projmtxnum = (GE_CMD_PROJMATRIXNUMBER << 24) | (num & 0xF);
}

void GPU_GLES::Execute_TgenMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_TGENMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.tgenMatrix + (op & 0xF));
	const int end = 12 - (op & 0xF);
	int i = 0;

	while ((src[i] >> 24) == GE_CMD_TGENMATRIXDATA) {
		const u32 newVal = src[i] << 8;
		if (dst[i] != newVal) {
			Flush();
			dst[i] = newVal;
			shaderManager_->DirtyUniform(DIRTY_TEXMATRIX);
		}
		if (++i >= end) {
			break;
		}
	}

	const int count = i;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | ((op + count) & 0xF);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPU_GLES::Execute_TgenMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.texmtxnum & 0xF;
	u32 newVal = op << 8;
	if (num < 12 && newVal != ((const u32 *)gstate.tgenMatrix)[num]) {
		Flush();
		((u32 *)gstate.tgenMatrix)[num] = newVal;
		shaderManager_->DirtyUniform(DIRTY_TEXMATRIX);
	}
	num++;
	gstate.texmtxnum = (GE_CMD_TGENMATRIXNUMBER << 24) | (num & 0xF);
}

void GPU_GLES::Execute_BoneMtxNum(u32 op, u32 diff) {
	// This is almost always followed by GE_CMD_BONEMATRIXDATA.
	const u32_le *src = (const u32_le *)Memory::GetPointerUnchecked(currentList->pc + 4);
	u32 *dst = (u32 *)(gstate.boneMatrix + (op & 0x7F));
	const int end = 12 * 8 - (op & 0x7F);
	int i = 0;

	// If we can't use software skinning, we have to flush and dirty.
	if (!g_Config.bSoftwareSkinning || (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
		while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
			const u32 newVal = src[i] << 8;
			if (dst[i] != newVal) {
				Flush();
				dst[i] = newVal;
			}
			if (++i >= end) {
				break;
			}
		}

		const int numPlusCount = (op & 0x7F) + i;
		for (int num = op & 0x7F; num < numPlusCount; num += 12) {
			shaderManager_->DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
		}
	} else {
		while ((src[i] >> 24) == GE_CMD_BONEMATRIXDATA) {
			dst[i] = src[i] << 8;
			if (++i >= end) {
				break;
			}
		}

		const int numPlusCount = (op & 0x7F) + i;
		for (int num = op & 0x7F; num < numPlusCount; num += 12) {
			gstate_c.deferredVertTypeDirty |= DIRTY_BONEMATRIX0 << (num / 12);
		}
	}

	const int count = i;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | ((op + count) & 0x7F);

	// Skip over the loaded data, it's done now.
	UpdatePC(currentList->pc, currentList->pc + count * 4);
	currentList->pc += count * 4;
}

void GPU_GLES::Execute_BoneMtxData(u32 op, u32 diff) {
	// Note: it's uncommon to get here now, see above.
	int num = gstate.boneMatrixNumber & 0x7F;
	u32 newVal = op << 8;
	if (num < 96 && newVal != ((const u32 *)gstate.boneMatrix)[num]) {
		// Bone matrices should NOT flush when software skinning is enabled!
		if (!g_Config.bSoftwareSkinning || (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
			Flush();
			shaderManager_->DirtyUniform(DIRTY_BONEMATRIX0 << (num / 12));
		} else {
			gstate_c.deferredVertTypeDirty |= DIRTY_BONEMATRIX0 << (num / 12);
		}
		((u32 *)gstate.boneMatrix)[num] = newVal;
	}
	num++;
	gstate.boneMatrixNumber = (GE_CMD_BONEMATRIXNUMBER << 24) | (num & 0x7F);
}

void GPU_GLES::Execute_BlockTransferStart(u32 op, u32 diff) {
	// TODO: Here we should check if the transfer overlaps a framebuffer or any textures,
	// and take appropriate action. This is a block transfer between RAM and VRAM, or vice versa.
	// Can we skip this on SkipDraw?
	DoBlockTransfer(gstate_c.skipDrawReason);

	// Fixes Gran Turismo's funky text issue, since it overwrites the current texture.
	gstate_c.textureChanged = TEXCHANGE_UPDATED;
}

void GPU_GLES::Execute_Generic(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_BASE:
		break;

	case GE_CMD_VADDR:
		Execute_Vaddr(op, diff);
		break;

	case GE_CMD_IADDR:
		Execute_Iaddr(op, diff);
		break;

	case GE_CMD_PRIM:
		Execute_Prim(op, diff);
		break;

	// The arrow and other rotary items in Puzbob are bezier patches, strangely enough.
	case GE_CMD_BEZIER:
		Execute_Bezier(op, diff);
		break;

	case GE_CMD_SPLINE:
		Execute_Spline(op, diff);
		break;

	case GE_CMD_BOUNDINGBOX:
		Execute_BoundingBox(op, diff);
		break;

	case GE_CMD_VERTEXTYPE:
		Execute_VertexType(op, diff);
		break;

	case GE_CMD_REGION1:
	case GE_CMD_REGION2:
		Execute_Region(op, diff);
		break;

	case GE_CMD_CLIPENABLE:
		//we always clip, this is opengl
		break;

	case GE_CMD_CULLFACEENABLE:
	case GE_CMD_CULL:
		break;

	case GE_CMD_TEXTUREMAPENABLE:
		// Don't need to dirty the texture here, already dirtied at list start/etc.
		break;

	case GE_CMD_LIGHTINGENABLE:
		break;

	case GE_CMD_FOGCOLOR:
		Execute_FogColor(op, diff);
		break;

	case GE_CMD_FOG1:
	case GE_CMD_FOG2:
		Execute_FogCoef(op, diff);
		break;

	case GE_CMD_FOGENABLE:
		break;

	case GE_CMD_DITHERENABLE:
		break;

	case GE_CMD_OFFSETX:
		break;

	case GE_CMD_OFFSETY:
		break;

	case GE_CMD_TEXSCALEU:
		Execute_TexScaleU(op, diff);
		break;

	case GE_CMD_TEXSCALEV:
		Execute_TexScaleV(op, diff);
		break;

	case GE_CMD_TEXOFFSETU:
		Execute_TexOffsetU(op, diff);
		break;

	case GE_CMD_TEXOFFSETV:
		Execute_TexOffsetV(op, diff);
		break;

	case GE_CMD_SCISSOR1:
	case GE_CMD_SCISSOR2:
		Execute_Scissor(op, diff);
		break;

		///
	case GE_CMD_MINZ:
	case GE_CMD_MAXZ:
		break;

	case GE_CMD_FRAMEBUFPTR:
	case GE_CMD_FRAMEBUFWIDTH:
	case GE_CMD_FRAMEBUFPIXFORMAT:
		Execute_FramebufType(op, diff);
		break;

	case GE_CMD_TEXADDR0:
		Execute_TexAddr0(op, diff);
		break;

	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		Execute_TexAddrN(op, diff);
		break;

	case GE_CMD_TEXBUFWIDTH0:
		Execute_TexBufw0(op, diff);
		break;

	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		Execute_TexBufwN(op, diff);
		break;

	case GE_CMD_CLUTFORMAT:
		Execute_ClutFormat(op, diff);
		break;

	case GE_CMD_CLUTADDR:
	case GE_CMD_CLUTADDRUPPER:
		// Hm, LOADCLUT actually changes the CLUT so no need to dirty here.
		break;

	case GE_CMD_LOADCLUT:
		Execute_LoadClut(op, diff);
		break;

	case GE_CMD_TEXMAPMODE:
		Execute_TexMapMode(op, diff);
		break;

	case GE_CMD_TEXSHADELS:
		break;

	case GE_CMD_TRANSFERSRC:
	case GE_CMD_TRANSFERSRCW:
	case GE_CMD_TRANSFERDST:
	case GE_CMD_TRANSFERDSTW:
	case GE_CMD_TRANSFERSRCPOS:
	case GE_CMD_TRANSFERDSTPOS:
		break;

	case GE_CMD_TRANSFERSIZE:
		break;

	case GE_CMD_TRANSFERSTART:  // Orphis calls this TRXKICK
		Execute_BlockTransferStart(op, diff);
		break;

	case GE_CMD_TEXSIZE0:
		Execute_TexSize0(op, diff);
		break;

	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		Execute_TexSizeN(op, diff);
		break;

	case GE_CMD_ZBUFPTR:
	case GE_CMD_ZBUFWIDTH:
		break;

	case GE_CMD_AMBIENTCOLOR:
	case GE_CMD_AMBIENTALPHA:
		Execute_Ambient(op, diff);
		break;

	case GE_CMD_MATERIALDIFFUSE:
		Execute_MaterialDiffuse(op, diff);
		break;

	case GE_CMD_MATERIALEMISSIVE:
		Execute_MaterialEmissive(op, diff);
		break;

	case GE_CMD_MATERIALAMBIENT:
	case GE_CMD_MATERIALALPHA:
		Execute_MaterialAmbient(op, diff);
		break;

	case GE_CMD_MATERIALSPECULAR:
	case GE_CMD_MATERIALSPECULARCOEF:
		Execute_MaterialSpecular(op, diff);
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		break;

	case GE_CMD_LX0:case GE_CMD_LY0:case GE_CMD_LZ0:
	case GE_CMD_LDX0:case GE_CMD_LDY0:case GE_CMD_LDZ0:
	case GE_CMD_LKA0:case GE_CMD_LKB0:case GE_CMD_LKC0:
	case GE_CMD_LKS0:  // spot coef ("conv")
	case GE_CMD_LKO0: // light angle ("cutoff")
	case GE_CMD_LAC0:
	case GE_CMD_LDC0:
	case GE_CMD_LSC0:
		Execute_Light0Param(op, diff);
		break;
	case GE_CMD_LX1:case GE_CMD_LY1:case GE_CMD_LZ1:
	case GE_CMD_LDX1:case GE_CMD_LDY1:case GE_CMD_LDZ1:
	case GE_CMD_LKA1:case GE_CMD_LKB1:case GE_CMD_LKC1:
	case GE_CMD_LKS1:
	case GE_CMD_LKO1:
	case GE_CMD_LAC1:
	case GE_CMD_LDC1:
	case GE_CMD_LSC1:
		Execute_Light1Param(op, diff);
		break;
	case GE_CMD_LX2:case GE_CMD_LY2:case GE_CMD_LZ2:
	case GE_CMD_LDX2:case GE_CMD_LDY2:case GE_CMD_LDZ2:
	case GE_CMD_LKA2:case GE_CMD_LKB2:case GE_CMD_LKC2:
	case GE_CMD_LKS2:
	case GE_CMD_LKO2:
	case GE_CMD_LAC2:
	case GE_CMD_LDC2:
	case GE_CMD_LSC2:
		Execute_Light2Param(op, diff);
		break;
	case GE_CMD_LX3:case GE_CMD_LY3:case GE_CMD_LZ3:
	case GE_CMD_LDX3:case GE_CMD_LDY3:case GE_CMD_LDZ3:
	case GE_CMD_LKA3:case GE_CMD_LKB3:case GE_CMD_LKC3:
	case GE_CMD_LKS3:
	case GE_CMD_LKO3:
	case GE_CMD_LAC3:
	case GE_CMD_LDC3:
	case GE_CMD_LSC3:
		Execute_Light3Param(op, diff);
		break;

	case GE_CMD_VIEWPORTXSCALE:
	case GE_CMD_VIEWPORTYSCALE:
	case GE_CMD_VIEWPORTXCENTER:
	case GE_CMD_VIEWPORTYCENTER:
	case GE_CMD_VIEWPORTZSCALE:
	case GE_CMD_VIEWPORTZCENTER:
		Execute_ViewportType(op, diff);
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		break;

	case GE_CMD_SHADEMODE:
		break;

	case GE_CMD_PATCHDIVISION:
	case GE_CMD_PATCHPRIMITIVE:
	case GE_CMD_PATCHFACING:
		break;


	case GE_CMD_MATERIALUPDATE:
		break;

	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
		break;

	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
	case GE_CMD_BLENDMODE:
		break;

	case GE_CMD_BLENDFIXEDA:
	case GE_CMD_BLENDFIXEDB:
		break;

	case GE_CMD_ALPHATESTENABLE:
	case GE_CMD_COLORTESTENABLE:
		// They are done in the fragment shader.
		break;

	case GE_CMD_COLORTEST:
		break;

	case GE_CMD_COLORTESTMASK:
		Execute_ColorTestMask(op, diff);
		break;

	case GE_CMD_ALPHATEST:
		Execute_AlphaTest(op, diff);
		break;

	case GE_CMD_COLORREF:
		Execute_ColorRef(op, diff);
		break;

	case GE_CMD_TEXENVCOLOR:
		Execute_TexEnvColor(op, diff);
		break;

	case GE_CMD_TEXFUNC:
	case GE_CMD_TEXFLUSH:
		break;

	case GE_CMD_TEXFORMAT:
		Execute_TexFormat(op, diff);
		break;

	case GE_CMD_TEXMODE:
	case GE_CMD_TEXFILTER:
	case GE_CMD_TEXWRAP:
		Execute_TexParamType(op, diff);
		break;

	//////////////////////////////////////////////////////////////////
	//	DEPTH TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
	case GE_CMD_ZTEST:
	case GE_CMD_ZWRITEDISABLE:
		break;

	case GE_CMD_MORPHWEIGHT0:
	case GE_CMD_MORPHWEIGHT1:
	case GE_CMD_MORPHWEIGHT2:
	case GE_CMD_MORPHWEIGHT3:
	case GE_CMD_MORPHWEIGHT4:
	case GE_CMD_MORPHWEIGHT5:
	case GE_CMD_MORPHWEIGHT6:
	case GE_CMD_MORPHWEIGHT7:
		gstate_c.morphWeights[cmd - GE_CMD_MORPHWEIGHT0] = getFloat24(data);
		break;

	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		Execute_WorldMtxNum(op, diff);
		break;

	case GE_CMD_WORLDMATRIXDATA:
		Execute_WorldMtxData(op, diff);
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		Execute_ViewMtxNum(op, diff);
		break;

	case GE_CMD_VIEWMATRIXDATA:
		Execute_ViewMtxData(op, diff);
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		Execute_ProjMtxNum(op, diff);
		break;

	case GE_CMD_PROJMATRIXDATA:
		Execute_ProjMtxData(op, diff);
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		Execute_TgenMtxNum(op, diff);
		break;

	case GE_CMD_TGENMATRIXDATA:
		Execute_TgenMtxData(op, diff);
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		Execute_BoneMtxNum(op, diff);
		break;

	case GE_CMD_BONEMATRIXDATA:
		Execute_BoneMtxData(op, diff);
		break;

#ifndef MOBILE_DEVICE
	case GE_CMD_ANTIALIASENABLE:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(antiAlias, G3D, "Unsupported antialias enabled: %06x", data);
		break;

	case GE_CMD_TEXLODSLOPE:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(texLodSlope, G3D, "Unsupported texture lod slope: %06x", data);
		break;
#endif

	case GE_CMD_TEXLEVEL:
		Execute_TexLevel(op, diff);
		break;

	//////////////////////////////////////////////////////////////////
	//	STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_STENCILTEST:
		Execute_StencilTest(op, diff);
		break;

	case GE_CMD_STENCILTESTENABLE:
	case GE_CMD_STENCILOP:
		break;

	case GE_CMD_MASKRGB:
	case GE_CMD_MASKALPHA:
		break;

	case GE_CMD_REVERSENORMAL:
		break;

	case GE_CMD_VSCX:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vscx, G3D, "Unsupported Vertex Screen Coordinate X : %06x", data);
		break;

	case GE_CMD_VSCY:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vscy, G3D, "Unsupported Vertex Screen Coordinate Y : %06x", data);
		break;

	case GE_CMD_VSCZ:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vscz, G3D, "Unsupported Vertex Screen Coordinate Z : %06x", data);
		break;

	case GE_CMD_VTCS:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vtcs, G3D, "Unsupported Vertex Texture Coordinate S : %06x", data);
		break;

	case GE_CMD_VTCT:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vtct, G3D, "Unsupported Vertex Texture Coordinate T : %06x", data);
		break;

	case GE_CMD_VTCQ:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vtcq, G3D, "Unsupported Vertex Texture Coordinate Q : %06x", data);
		break;

	case GE_CMD_VCV:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vcv, G3D, "Unsupported Vertex Color Value : %06x", data);
		break;

	case GE_CMD_VAP:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vap, G3D, "Unsupported Vertex Alpha and Primitive : %06x", data);
		break;

	case GE_CMD_VFC:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vfc, G3D, "Unsupported Vertex Fog Coefficient : %06x", data);
		break;

	case GE_CMD_VSCV:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(vscv, G3D, "Unsupported Vertex Secondary Color Value : %06x", data);
		break;


	case GE_CMD_UNKNOWN_03: 
	case GE_CMD_UNKNOWN_0D:
	case GE_CMD_UNKNOWN_11:
	case GE_CMD_UNKNOWN_29:
	case GE_CMD_UNKNOWN_34:
	case GE_CMD_UNKNOWN_35:
	case GE_CMD_UNKNOWN_39:
	case GE_CMD_UNKNOWN_4E:
	case GE_CMD_UNKNOWN_4F:
	case GE_CMD_UNKNOWN_52:
	case GE_CMD_UNKNOWN_59:
	case GE_CMD_UNKNOWN_5A:
	case GE_CMD_UNKNOWN_B6:
	case GE_CMD_UNKNOWN_B7:
	case GE_CMD_UNKNOWN_D1:
	case GE_CMD_UNKNOWN_ED:
	case GE_CMD_UNKNOWN_EF:
	case GE_CMD_UNKNOWN_FA:
	case GE_CMD_UNKNOWN_FB:
	case GE_CMD_UNKNOWN_FC:
	case GE_CMD_UNKNOWN_FD:
	case GE_CMD_UNKNOWN_FE:
		if (data != 0)
			WARN_LOG_REPORT_ONCE(unknowncmd, G3D, "Unknown GE command : %08x ", op);
		break;
	case GE_CMD_UNKNOWN_FF:
		// This is hit in quite a few games, supposedly it is a no-op.
		// Might be used for debugging or something?
		break;
		
	default:
		GPUCommon::ExecuteOp(op, diff);
		break;
	}
}

void GPU_GLES::FastLoadBoneMatrix(u32 target) {
	const int num = gstate.boneMatrixNumber & 0x7F;
	const int mtxNum = num / 12;
	uint32_t uniformsToDirty = DIRTY_BONEMATRIX0 << mtxNum;
	if ((num - 12 * mtxNum) != 0) {
		uniformsToDirty |= DIRTY_BONEMATRIX0 << ((mtxNum + 1) & 7);
	}

	if (!g_Config.bSoftwareSkinning || (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) != 0) {
		Flush();
		shaderManager_->DirtyUniform(uniformsToDirty);
	} else {
		gstate_c.deferredVertTypeDirty |= uniformsToDirty;
	}
	gstate.FastLoadBoneMatrix(target);
}

void GPU_GLES::GetStats(char *buffer, size_t bufsize) {
	float vertexAverageCycles = gpuStats.numVertsSubmitted > 0 ? (float)gpuStats.vertexGPUCycles / (float)gpuStats.numVertsSubmitted : 0.0f;
	snprintf(buffer, bufsize - 1,
		"DL processing time: %0.2f ms\n"
		"Draw calls: %i, flushes %i\n"
		"Cached Draw calls: %i\n"
		"Num Tracked Vertex Arrays: %i\n"
		"GPU cycles executed: %d (%f per vertex)\n"
		"Commands per call level: %i %i %i %i\n"
		"Vertices submitted: %i\n"
		"Cached, Uncached Vertices Drawn: %i, %i\n"
		"FBOs active: %i\n"
		"Textures active: %i, decoded: %i  invalidated: %i\n"
		"Vertex, Fragment, Programs loaded: %i, %i, %i\n",
		gpuStats.msProcessingDisplayLists * 1000.0f,
		gpuStats.numDrawCalls,
		gpuStats.numFlushes,
		gpuStats.numCachedDrawCalls,
		gpuStats.numTrackedVertexArrays,
		gpuStats.vertexGPUCycles + gpuStats.otherGPUCycles,
		vertexAverageCycles,
		gpuStats.gpuCommandsAtCallLevel[0], gpuStats.gpuCommandsAtCallLevel[1], gpuStats.gpuCommandsAtCallLevel[2], gpuStats.gpuCommandsAtCallLevel[3],
		gpuStats.numVertsSubmitted,
		gpuStats.numCachedVertsDrawn,
		gpuStats.numUncachedVertsDrawn,
		(int)framebufferManagerGL_->NumVFBs(),
		(int)textureCacheGL_->NumLoadedTextures(),
		gpuStats.numTexturesDecoded,
		gpuStats.numTextureInvalidations,
		shaderManager_->NumVertexShaders(),
		shaderManager_->NumFragmentShaders(),
		shaderManager_->NumPrograms());
}

void GPU_GLES::ClearCacheNextFrame() {
	textureCacheGL_->ClearNextFrame();
}

void GPU_GLES::ClearShaderCache() {
	shaderManager_->ClearCache(true);
}

void GPU_GLES::CleanupBeforeUI() {
	// Clear any enabled vertex arrays.
	shaderManager_->DirtyLastShader();
	glstate.arrayBuffer.bind(0);
	glstate.elementArrayBuffer.bind(0);
}

std::vector<FramebufferInfo> GPU_GLES::GetFramebufferList() {
	return framebufferManagerGL_->GetFramebufferList();
}

void GPU_GLES::DoState(PointerWrap &p) {
	GPUCommon::DoState(p);

	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCacheGL_->Clear(true);
		depalShaderCache_.Clear();
		drawEngine_.ClearTrackedVertexArrays();

		gstate_c.textureChanged = TEXCHANGE_UPDATED;
		framebufferManagerGL_->DestroyAllFBOs(true);
		shaderManager_->ClearCache(true);
	}
}

bool GPU_GLES::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) {
	u32 fb_address = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.getFrameBufRawAddress() : framebufferManagerGL_->DisplayFramebufAddr();
	int fb_stride = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.FrameBufStride() : framebufferManagerGL_->DisplayFramebufStride();
	GEBufferFormat format = type == GPU_DBG_FRAMEBUF_RENDER ? gstate.FrameBufFormat() : framebufferManagerGL_->DisplayFramebufFormat();
	return framebufferManagerGL_->GetFramebuffer(fb_address, fb_stride, format, buffer, maxRes);
}

bool GPU_GLES::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	u32 z_address = gstate.getDepthBufRawAddress();
	int z_stride = gstate.DepthBufStride();

	return framebufferManagerGL_->GetDepthbuffer(fb_address, fb_stride, z_address, z_stride, buffer);
}

bool GPU_GLES::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	return framebufferManagerGL_->GetStencilbuffer(fb_address, fb_stride, buffer);
}

bool GPU_GLES::GetCurrentTexture(GPUDebugBuffer &buffer, int level) {
	if (!gstate.isTextureMapEnabled()) {
		return false;
	}

#ifndef USING_GLES2
	GPUgstate saved;
	if (level != 0) {
		saved = gstate;

		// The way we set textures is a bit complex.  Let's just override level 0.
		gstate.texsize[0] = gstate.texsize[level];
		gstate.texaddr[0] = gstate.texaddr[level];
		gstate.texbufwidth[0] = gstate.texbufwidth[level];
	}

	textureCacheGL_->SetTexture(true);
	textureCacheGL_->ApplyTexture();
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

	if (level != 0) {
		gstate = saved;
	}

	buffer.Allocate(w, h, GE_FORMAT_8888, false);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
#else
	return false;
#endif
}

bool GPU_GLES::GetCurrentClut(GPUDebugBuffer &buffer) {
	return textureCacheGL_->GetCurrentClutBuffer(buffer);
}

bool GPU_GLES::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	return FramebufferManager::GetOutputFramebuffer(buffer);
}

bool GPU_GLES::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) {
	return drawEngine_.GetCurrentSimpleVertices(count, vertices, indices);
}

bool GPU_GLES::DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (drawEngine_.IsCodePtrVertexDecoder(ptr)) {
		name = "VertexDecoderJit";
		return true;
	}
	return false;
}

std::vector<std::string> GPU_GLES::DebugGetShaderIDs(DebugShaderType type) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderIDs();
	} else {
		return shaderManager_->DebugGetShaderIDs(type);
	}
}

std::string GPU_GLES::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	if (type == SHADER_TYPE_VERTEXLOADER) {
		return drawEngine_.DebugGetVertexLoaderString(id, stringType);
	} else {
		return shaderManager_->DebugGetShaderString(id, type, stringType);
	}
}
