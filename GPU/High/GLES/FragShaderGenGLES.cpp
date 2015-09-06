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

#if !defined(USING_GLES2)
// SDL 1.2 on Apple does not have support for OpenGL 3 and hence needs
// special treatment in the shader generator.
#if defined(__APPLE__)
#define FORCE_OPENGL_2_0
#endif
#endif

#include <cstdio>

#include "base/logging.h"
#include "gfx_es2/gpu_features.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "GPU/High/GLES/FragShaderGenGLES.h"
#include "GPU/High/Command.h"
#include "GPU/High/GLES/ShaderManagerHighGLES.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/ge_constants.h"

#define WRITE p+=sprintf

namespace HighGpu {

// #define DEBUG_SHADER

// Dest factors where it's safe to eliminate the alpha test under certain conditions
const bool safeDestFactors[16] = {
	true, // GE_DSTBLEND_SRCCOLOR,
	true, // GE_DSTBLEND_INVSRCCOLOR,
	false, // GE_DSTBLEND_SRCALPHA,
	true, // GE_DSTBLEND_INVSRCALPHA,
	true, // GE_DSTBLEND_DSTALPHA,
	true, // GE_DSTBLEND_INVDSTALPHA,
	false, // GE_DSTBLEND_DOUBLESRCALPHA,
	false, // GE_DSTBLEND_DOUBLEINVSRCALPHA,
	true, // GE_DSTBLEND_DOUBLEDSTALPHA,
	true, // GE_DSTBLEND_DOUBLEINVDSTALPHA,
	true, //GE_DSTBLEND_FIXB,
};

bool IsAlphaTestTriviallyTrue(u32 enabled, const BlendState *blend, const FragmentState *frag, bool vertexFullAlpha, bool textureFullAlpha) {
	switch (blend->alphaTestFunc) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_GEQUAL:
		if (vertexFullAlpha && (textureFullAlpha || !frag->useTextureAlpha))
			return true;  // If alpha is full, it doesn't matter what the ref value is.
		return blend->alphaTestRef == 0;

	// Non-zero check. If we have no depth testing (and thus no depth writing), and an alpha func that will result in no change if zero alpha, get rid of the alpha test.
	// Speeds up Lumines by a LOT on PowerVR.
	case GE_COMP_NOTEQUAL:
		if (blend->alphaTestRef == 255) {
			// Likely to be rare. Let's just skip the vertexFullAlpha optimization here instead of adding
			// complicated code to discard the draw or whatnot.
			return false;
		}
		// Fallthrough on purpose

	case GE_COMP_GREATER:
		{
#if 0
			// Easy way to check the values in the debugger without ruining && early-out
			bool doTextureAlpha = gstate.isTextureAlphaUsed();
			bool stencilTest = gstate.isStencilTestEnabled();
			bool depthTest = gstate.isDepthTestEnabled();
			GEComparison depthTestFunc = gstate.getDepthTestFunction();
			int alphaRef = gstate.getAlphaTestRef();
			int blendA = gstate.getBlendFuncA();
			bool blendEnabled = gstate.isAlphaBlendEnabled();
			int blendB = gstate.getBlendFuncA();
#endif
			return (vertexFullAlpha && (textureFullAlpha || !frag->useTextureAlpha)) || (
					(!(enabled & ENABLE_STENCIL_TEST) &&
					!(enabled & ENABLE_DEPTH_TEST) &&
					blend->alphaTestRef == 0 &&
					(enabled & ENABLE_ALPHA_TEST) &&
					blend->blendSrc == GE_SRCBLEND_SRCALPHA &&
					safeDestFactors[blend->blendDst]));
		}

	case GE_COMP_LEQUAL:
		return blend->alphaTestRef == 255;

	case GE_COMP_EQUAL:
	case GE_COMP_LESS:
		return false;

	default:
		return false;
	}
}

bool IsAlphaTestAgainstZero(const BlendState *blendState) {
	return blendState->alphaTestRef == 0 && blendState->alphaTestMask == 0xFF;
}

bool IsColorTestAgainstZero(const BlendState *blendState) {
	return blendState->colorTestRef == 0 && blendState->colorTestMask == 0xFFFFFF;
}

const bool nonAlphaSrcFactors[16] = {
	true,  // GE_SRCBLEND_DSTCOLOR,
	true,  // GE_SRCBLEND_INVDSTCOLOR,
	false, // GE_SRCBLEND_SRCALPHA,
	false, // GE_SRCBLEND_INVSRCALPHA,
	true,  // GE_SRCBLEND_DSTALPHA,
	true,  // GE_SRCBLEND_INVDSTALPHA,
	false, // GE_SRCBLEND_DOUBLESRCALPHA,
	false, // GE_SRCBLEND_DOUBLEINVSRCALPHA,
	true,  // GE_SRCBLEND_DOUBLEDSTALPHA,
	true,  // GE_SRCBLEND_DOUBLEINVDSTALPHA,
	true,  // GE_SRCBLEND_FIXA,
};

const bool nonAlphaDestFactors[16] = {
	true,  // GE_DSTBLEND_SRCCOLOR,
	true,  // GE_DSTBLEND_INVSRCCOLOR,
	false, // GE_DSTBLEND_SRCALPHA,
	false, // GE_DSTBLEND_INVSRCALPHA,
	true,  // GE_DSTBLEND_DSTALPHA,
	true,  // GE_DSTBLEND_INVDSTALPHA,
	false, // GE_DSTBLEND_DOUBLESRCALPHA,
	false, // GE_DSTBLEND_DOUBLEINVSRCALPHA,
	true,  // GE_DSTBLEND_DOUBLEDSTALPHA,
	true,  // GE_DSTBLEND_DOUBLEINVDSTALPHA,
	true,  // GE_DSTBLEND_FIXB,
};

ReplaceAlphaType ReplaceAlphaWithStencil(ReplaceBlendType replaceBlend, u32 enabled, const BlendState *blend) {
	if (!(enabled & ENABLE_STENCIL_TEST)) {
		return REPLACE_ALPHA_NO;
	}

	if (replaceBlend != REPLACE_BLEND_NO && replaceBlend != REPLACE_BLEND_COPY_FBO) {
		if (nonAlphaSrcFactors[blend->blendSrc] && nonAlphaDestFactors[blend->blendDst]) {
			return REPLACE_ALPHA_YES;
		} else {
			if (gl_extensions.ARB_blend_func_extended) {
				return REPLACE_ALPHA_DUALSOURCE;
			} else {
				return REPLACE_ALPHA_NO;
			}
		}
	}

	return REPLACE_ALPHA_YES;
}

StencilValueType ReplaceAlphaWithStencilType(const FramebufState *fb, const DepthStencilState *ds) {
	switch (fb->colorFormat) {
	case GE_FORMAT_565:
		// There's never a stencil value.  Maybe the right alpha is 1?
		return STENCIL_VALUE_ONE;

	case GE_FORMAT_5551:
		switch (ds->stencilOpZPass) {
		// Technically, this should only ever use zero/one.
		case GE_STENCILOP_REPLACE:
			return (ds->stencilRef & 0x80) != 0 ? STENCIL_VALUE_ONE : STENCIL_VALUE_ZERO;

		// Decrementing always zeros, since there's only one bit.
		case GE_STENCILOP_DECR:
		case GE_STENCILOP_ZERO:
			return STENCIL_VALUE_ZERO;

		// Incrementing always fills, since there's only one bit.
		case GE_STENCILOP_INCR:
			return STENCIL_VALUE_ONE;

		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_INVERT;

		case GE_STENCILOP_KEEP:
			return STENCIL_VALUE_KEEP;
		}
		break;

	case GE_FORMAT_4444:
	case GE_FORMAT_8888:
	case GE_FORMAT_INVALID:
		switch (ds->stencilOpZPass) {
		case GE_STENCILOP_REPLACE:
			return STENCIL_VALUE_UNIFORM;

		case GE_STENCILOP_ZERO:
			return STENCIL_VALUE_ZERO;

		case GE_STENCILOP_DECR:
			return fb->colorFormat == GE_FORMAT_4444 ? STENCIL_VALUE_DECR_4 : STENCIL_VALUE_DECR_8;

		case GE_STENCILOP_INCR:
			return fb->colorFormat == GE_FORMAT_4444 ? STENCIL_VALUE_INCR_4 : STENCIL_VALUE_INCR_8;

		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_INVERT;

		case GE_STENCILOP_KEEP:
			return STENCIL_VALUE_KEEP;
		}
		break;
	}

	return STENCIL_VALUE_KEEP;
}

bool IsColorTestTriviallyTrue(const BlendState *blendState) {
	switch (blendState->colorTestFunc) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
	case GE_COMP_NOTEQUAL:
		return false;
	default:
		return false;
	}
}

ReplaceBlendType ReplaceBlendWithShader(bool allowShaderBlend, u32 enabled, const BlendState *blend) {
	if (!(enabled & ENABLE_BLEND)) {
		return REPLACE_BLEND_NO;
	}

	GEBlendMode eq = blend->getBlendEq();
	// Let's get the non-factor modes out of the way first.
	switch (eq) {
	case GE_BLENDMODE_ABSDIFF:
		return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

	case GE_BLENDMODE_MIN:
	case GE_BLENDMODE_MAX:
		if (gl_extensions.EXT_blend_minmax || gl_extensions.GLES3) {
			return REPLACE_BLEND_STANDARD;
		} else {
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;
		}

	default:
		break;
	}

	GEBlendSrcFactor funcA = blend->getBlendSrc();
	GEBlendDstFactor funcB = blend->getBlendDst();

	switch (funcA) {
	case GE_SRCBLEND_DOUBLESRCALPHA:
	case GE_SRCBLEND_DOUBLEINVSRCALPHA:
		// 2x alpha in the source function and not in the dest = source color doubling.
		// Even dest alpha is safe, since we're moving the * 2.0 into the src color.
		switch (funcB) {
		case GE_DSTBLEND_SRCCOLOR:
		case GE_DSTBLEND_INVSRCCOLOR:
			// Can't double, we need the source color to be correct.
			return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			// We can't technically do this correctly (due to clamping) without reading the dst color.
			// Using a copy isn't accurate either, though, when there's overlap.
			if (gl_extensions.ANY_shader_framebuffer_fetch)
				return !allowShaderBlend ? REPLACE_BLEND_PRE_SRC_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
			return REPLACE_BLEND_PRE_SRC_2X_ALPHA;

		default:
			// TODO: Could use vertexFullAlpha, but it's not calculated yet.
			return REPLACE_BLEND_PRE_SRC;
		}

	case GE_SRCBLEND_DOUBLEDSTALPHA:
	case GE_SRCBLEND_DOUBLEINVDSTALPHA:
		switch (funcB) {
		case GE_DSTBLEND_SRCCOLOR:
		case GE_DSTBLEND_INVSRCCOLOR:
			// Can't double, we need the source color to be correct.
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			return !allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;

		default:
			// We can't technically do this correctly (due to clamping) without reading the dst alpha.
			return !allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;
		}

	case GE_SRCBLEND_FIXA:
		switch (funcB) {
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			// Can't safely double alpha, will clamp.
			return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_FIXB:
			if (blend->blendfixa == 0xFFFFFF && blend->blendfixb == 0x000000) {
				// Some games specify this.  Some cards may prefer blending off entirely.
				return REPLACE_BLEND_NO;
			} else if (blend->blendfixa == 0xFFFFFF || blend->blendfixa == 0x000000 ||
								 blend->blendfixb == 0xFFFFFF || blend->blendfixb == 0x000000) {
				return REPLACE_BLEND_STANDARD;
			} else {
				return REPLACE_BLEND_PRE_SRC;
			}

		default:
			return REPLACE_BLEND_STANDARD;
		}

	default:
		switch (funcB) {
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			if (funcA == GE_SRCBLEND_SRCALPHA || funcA == GE_SRCBLEND_INVSRCALPHA) {
				// Can't safely double alpha, will clamp.  However, a copy may easily be worse due to overlap.
				if (gl_extensions.ANY_shader_framebuffer_fetch)
					return !allowShaderBlend ? REPLACE_BLEND_PRE_SRC_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
				return REPLACE_BLEND_PRE_SRC_2X_ALPHA;
			} else {
				// This means dst alpha/color is used in the src factor.
				// Unfortunately, copying here causes overlap problems in Silent Hill games (it seems?)
				// We will just hope that doubling alpha for the dst factor will not clamp too badly.
				if (gl_extensions.ANY_shader_framebuffer_fetch)
					return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
				return REPLACE_BLEND_2X_ALPHA;
			}

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		default:
			return REPLACE_BLEND_STANDARD;
		}
	}
}

enum LogicOpReplaceType {
	LOGICOPTYPE_NORMAL,
	LOGICOPTYPE_ONE,
	LOGICOPTYPE_INVERT,
};

static inline LogicOpReplaceType ReplaceLogicOpType(u32 enabled, const BlendState *blend) {
#if defined(USING_GLES2)
	if (enabled & ENABLE_LOGIC_OP) {
		switch (blend->logicOp) {
		case GE_LOGIC_COPY_INVERTED:
		case GE_LOGIC_AND_INVERTED:
		case GE_LOGIC_OR_INVERTED:
		case GE_LOGIC_NOR:
		case GE_LOGIC_NAND:
		case GE_LOGIC_EQUIV:
			return LOGICOPTYPE_INVERT;
		case GE_LOGIC_INVERTED:
			return LOGICOPTYPE_ONE;
		case GE_LOGIC_SET:
			return LOGICOPTYPE_ONE;
		default:
			return LOGICOPTYPE_NORMAL;
		}
	}
#endif
	return LOGICOPTYPE_NORMAL;
}

// Local
enum {
	BIT_CLEARMODE = 0,
	BIT_DO_TEXTURE = 1,
	BIT_TEXFUNC = 2,  //3
	BIT_TEXALPHA = 5,
	BIT_FLIP_TEXTURE = 6,
	BIT_SHADER_TEX_CLAMP = 7,
	BIT_CLAMP_S = 8,
	BIT_CLAMP_T = 9,
	BIT_TEXTURE_AT_OFFSET = 10,
	BIT_LMODE = 11,
	BIT_ALPHA_TEST = 12,
	BIT_ALPHA_TEST_FUNC = 13,
	BIT_ALPHA_AGAINST_ZERO = 16,
	BIT_COLOR_TEST = 17,
	BIT_COLOR_TEST_FUNC = 18,
	BIT_COLOR_AGAINST_ZERO = 20,
	BIT_ENABLE_FOG = 21,
	BIT_DO_TEXTURE_PROJ = 22,
	BIT_COLOR_DOUBLE = 23,
	BIT_STENCIL_TO_ALPHA = 24,
	BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE = 26,
	BIT_REPLACE_LOGIC_OP_TYPE = 30,
	BIT_REPLACE_BLEND = 32,
	BIT_BLENDEQ = 35,
	BIT_BLENDFUNC_A = 38,
	BIT_BLENDFUNC_B = 42,
	BIT_FLATSHADE = 46,
};

// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(ShaderID *id_out, u32 enabled, u32 vertType,
		const FramebufState *fb, const RasterState *raster, const FragmentState *frag,
		const TexScaleState *ts, const BlendState *blend, const DepthStencilState *ds,
		bool allowShaderBlend, bool flipTexture, bool vertexFullAlpha, bool textureFullAlpha) {
	ShaderID id;
	if (raster->clearMode) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id.SetBit(BIT_CLEARMODE);
	} else {
		bool isModeThrough = (vertType & GE_VTYPE_THROUGH_MASK) != 0;
		bool lmode = frag->isUsingSecondaryColor() && (enabled & ENABLE_LIGHTS) && !isModeThrough;
		bool enableFog = (enabled & ENABLE_FOG) && !isModeThrough;
		bool enableAlphaTest = (enabled & ENABLE_ALPHA_TEST) && !IsAlphaTestTriviallyTrue(enabled, blend, frag, vertexFullAlpha, textureFullAlpha) && !g_Config.bDisableAlphaTest;
		bool enableColorTest = (enabled & ENABLE_COLOR_TEST) && !IsColorTestTriviallyTrue(blend);
		bool enableColorDoubling = frag->colorDouble && (enabled & ENABLE_TEXTURE);
		bool doTextureProjection = ts->getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
		bool doTextureAlpha = frag->isTextureAlphaUsed();
		bool doFlatShading = raster->shadeMode == GE_SHADE_FLAT;

		ReplaceBlendType replaceBlend = ReplaceBlendWithShader(allowShaderBlend, enabled, blend);

		// All texfuncs except replace are the same for RGB as for RGBA with full alpha.
		if (textureFullAlpha && frag->getTextureFunction() != GE_TEXFUNC_REPLACE)
			doTextureAlpha = false;

		if (enabled & ENABLE_TEXTURE) {
			id.SetBit(BIT_DO_TEXTURE);
			id.SetBits(BIT_TEXFUNC, 3, frag->texFunc);
			id.SetBit(BIT_TEXALPHA, doTextureAlpha & 1); // rgb or rgba
			id.SetBit(BIT_FLIP_TEXTURE, flipTexture);
			// TODO
#if 0
			if (gstate_c.needShaderTexClamp) {
				bool textureAtOffset = gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;
				// 3 bits total.
				id.SetBit(BIT_SHADER_TEX_CLAMP);
				id.SetBit(BIT_CLAMP_S, gstate.isTexCoordClampedS());
				id.SetBit(BIT_CLAMP_T, gstate.isTexCoordClampedT());
				id.SetBit(BIT_TEXTURE_AT_OFFSET, textureAtOffset);
			}
#endif
		}

		id.SetBit(BIT_LMODE, lmode);
#if !defined(DX9_USE_HW_ALPHA_TEST)
		if (enableAlphaTest) {
			// 5 bits total.
			id.SetBit(BIT_ALPHA_TEST);
			id.SetBits(BIT_ALPHA_TEST_FUNC, 3, blend->alphaTestFunc);
			id.SetBit(BIT_ALPHA_AGAINST_ZERO, IsAlphaTestAgainstZero(blend));
		}
#endif
		if (enableColorTest) {
			// 4 bits total.
			id.SetBit(BIT_COLOR_TEST);
			id.SetBits(BIT_COLOR_TEST_FUNC, 2, blend->colorTestFunc);
			id.SetBit(BIT_COLOR_AGAINST_ZERO, IsColorTestAgainstZero(blend));
		}

		id.SetBit(BIT_ENABLE_FOG, enableFog);
		id.SetBit(BIT_DO_TEXTURE_PROJ, doTextureProjection);
		id.SetBit(BIT_COLOR_DOUBLE, enableColorDoubling);

		// 2 bits
		ReplaceAlphaType stencilToAlpha = ReplaceAlphaWithStencil(replaceBlend, enabled, blend);
		id.SetBits(BIT_STENCIL_TO_ALPHA, 2, stencilToAlpha);

		if (stencilToAlpha != REPLACE_ALPHA_NO) {
			// 4 bits
			id.SetBits(BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4, ReplaceAlphaWithStencilType(fb, ds));
		}

		if (enableAlphaTest)
			gpuStats.numAlphaTestedDraws++;
		else
			gpuStats.numNonAlphaTestedDraws++;

		// 2 bits.
		id.SetBits(BIT_REPLACE_LOGIC_OP_TYPE, 2, ReplaceLogicOpType(enabled, blend));

		// 3 bits.
		id.SetBits(BIT_REPLACE_BLEND, 3, replaceBlend);
		if (replaceBlend > REPLACE_BLEND_STANDARD) {
			// 11 bits total.
			id.SetBits(BIT_BLENDEQ, 3, blend->blendEq);
			id.SetBits(BIT_BLENDFUNC_A, 4, blend->blendSrc);
			id.SetBits(BIT_BLENDFUNC_B, 4, blend->blendDst);
		}
		id.SetBit(BIT_FLATSHADE, doFlatShading & 1);
	}

	*id_out = id;
}

// Missing: Z depth range
void GenerateFragmentShader(ShaderID id, char *buffer) {
	char *p = buffer;

	// In GLSL ES 3.0, you use "in" variables instead of varying.

	bool glslES30 = false;
	const char *varying = "varying";
	const char *fragColor0 = "gl_FragColor";
	const char *texture = "texture2D";
	const char *texelFetch = NULL;
	bool highpFog = false;
	bool highpTexcoord = false;
	bool bitwiseOps = false;
	const char *lastFragData = nullptr;

#if defined(USING_GLES2)
	// Let's wait until we have a real use for this.
	// ES doesn't support dual source alpha :(
	if (gl_extensions.GLES3) {
		WRITE(p, "#version 300 es\n");  // GLSL ES 3.0
		fragColor0 = "fragColor0";
		texture = "texture";
		glslES30 = true;
		bitwiseOps = true;
		texelFetch = "texelFetch";
	} else {
		WRITE(p, "#version 100\n");  // GLSL ES 1.0
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
			bitwiseOps = true;
			texelFetch = "texelFetch2D";
		}
	}

	// PowerVR needs highp to do the fog in MHU correctly.
	// Others don't, and some can't handle highp in the fragment shader.
	highpFog = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? true : false;
	highpTexcoord = highpFog;

	if (gl_extensions.EXT_shader_framebuffer_fetch) {
		WRITE(p, "#extension GL_EXT_shader_framebuffer_fetch : require\n");
		lastFragData = "gl_LastFragData[0]";
	} else if (gl_extensions.NV_shader_framebuffer_fetch) {
		// GL_NV_shader_framebuffer_fetch is available on mobile platform and ES 2.0 only but not on desktop.
		WRITE(p, "#extension GL_NV_shader_framebuffer_fetch : require\n");
		lastFragData = "gl_LastFragData[0]";
	} else if (gl_extensions.ARM_shader_framebuffer_fetch) {
		WRITE(p, "#extension GL_ARM_shader_framebuffer_fetch : require\n");
		lastFragData = "gl_LastFragColorARM";
	}

	WRITE(p, "precision lowp float;\n");

#elif !defined(FORCE_OPENGL_2_0)
	if (gl_extensions.VersionGEThan(3, 3, 0)) {
		fragColor0 = "fragColor0";
		texture = "texture";
		glslES30 = true;
		bitwiseOps = true;
		texelFetch = "texelFetch";
		WRITE(p, "#version 330\n");
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	} else if (gl_extensions.VersionGEThan(3, 0, 0)) {
		fragColor0 = "fragColor0";
		bitwiseOps = true;
		texelFetch = "texelFetch";
		WRITE(p, "#version 130\n");
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
		}
		// Remove lowp/mediump in non-mobile non-glsl 3 implementations
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	} else {
		WRITE(p, "#version 110\n");
		if (gl_extensions.EXT_gpu_shader4) {
			WRITE(p, "#extension GL_EXT_gpu_shader4 : enable\n");
			bitwiseOps = true;
			texelFetch = "texelFetch2D";
		}
		// Remove lowp/mediump in non-mobile non-glsl 3 implementations
		WRITE(p, "#define lowp\n");
		WRITE(p, "#define mediump\n");
		WRITE(p, "#define highp\n");
	}
#else
	// Need to remove lowp/mediump for Mac
	WRITE(p, "#define lowp\n");
	WRITE(p, "#define mediump\n");
	WRITE(p, "#define highp\n");
#endif

	if (glslES30) {
		varying = "in";
	}

	bool isModeClear = id.Bit(BIT_CLEARMODE);
	bool lmode = id.Bit(BIT_LMODE);
	bool doTexture = id.Bit(BIT_DO_TEXTURE);
	bool enableFog = id.Bit(BIT_ENABLE_FOG);
	bool enableAlphaTest = id.Bit(BIT_ALPHA_TEST);
	bool alphaTestAgainstZero = id.Bit(BIT_ALPHA_AGAINST_ZERO);
	bool enableColorTest = id.Bit(BIT_COLOR_TEST);
	bool colorTestAgainstZero = id.Bit(BIT_COLOR_AGAINST_ZERO);
	bool enableColorDoubling = id.Bit(BIT_COLOR_DOUBLE);
	bool doTextureProjection = id.Bit(BIT_DO_TEXTURE_PROJ);
	bool doTextureAlpha = id.Bit(BIT_TEXALPHA);
	bool doFlatShading = id.Bit(BIT_FLATSHADE);
	bool flipTexture = id.Bit(BIT_FLIP_TEXTURE);

	GEComparison alphaTestFunc = (GEComparison)id.Bits(BIT_ALPHA_TEST_FUNC, 3);
	GEComparison colorTestFunc = (GEComparison)id.Bits(BIT_COLOR_TEST_FUNC, 2);
	bool needShaderTexClamp = false;  // gstate_c.needShaderTexClamp

	GETexFunc texFunc = (GETexFunc)id.Bits(BIT_TEXFUNC, 3);
	bool textureAtOffset = false;  // gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;

	ReplaceBlendType replaceBlend = static_cast<ReplaceBlendType>(id.Bits(BIT_REPLACE_BLEND, 3));
	ReplaceAlphaType stencilToAlpha = static_cast<ReplaceAlphaType>(id.Bits(BIT_STENCIL_TO_ALPHA, 4));

	const char *shading = "";
	if (glslES30)
		shading = doFlatShading ? "flat" : "";

	if (doTexture)
		WRITE(p, "uniform sampler2D tex;\n");

	if (!isModeClear && replaceBlend > REPLACE_BLEND_STANDARD) {
		if (!gl_extensions.ANY_shader_framebuffer_fetch && replaceBlend == REPLACE_BLEND_COPY_FBO) {
			if (!texelFetch) {
				WRITE(p, "uniform vec2 u_fbotexSize;\n");
			}
			WRITE(p, "uniform sampler2D fbotex;\n");
		}
		int replaceBlendA = id.Bits(BIT_BLENDFUNC_A, 4);
		int replaceBlendB = id.Bits(BIT_BLENDFUNC_B, 4);
		if (replaceBlendA == GE_SRCBLEND_FIXA) {
			WRITE(p, "uniform vec3 u_blendFixA;\n");
		}
		if (replaceBlendB == GE_DSTBLEND_FIXB) {
			WRITE(p, "uniform vec3 u_blendFixB;\n");
		}
	}

	if (id.Bit(BIT_SHADER_TEX_CLAMP) && doTexture) {
		WRITE(p, "uniform vec4 u_texclamp;\n");
		if (id.Bit(BIT_TEXTURE_AT_OFFSET)) {
			WRITE(p, "uniform vec2 u_texclampoff;\n");
		}
	}

	if (enableAlphaTest || enableColorTest) {
		if (g_Config.bFragmentTestCache) {
			WRITE(p, "uniform sampler2D testtex;\n");
		} else {
			WRITE(p, "uniform vec4 u_alphacolorref;\n");
			if (bitwiseOps && ((enableColorTest && !colorTestAgainstZero) || (enableAlphaTest && !alphaTestAgainstZero))) {
				WRITE(p, "uniform ivec4 u_alphacolormask;\n");
			}
		}
	}

	StencilValueType replaceAlphaWithStencilType = (StencilValueType)id.Bits(BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);
	if (stencilToAlpha && replaceAlphaWithStencilType == STENCIL_VALUE_UNIFORM) {
		WRITE(p, "uniform float u_stencilReplaceValue;\n");
	}
	if (doTexture && texFunc == GE_TEXFUNC_BLEND)
		WRITE(p, "uniform vec3 u_texenv;\n");

	WRITE(p, "%s %s vec4 v_color0;\n", shading, varying);
	if (lmode)
		WRITE(p, "%s %s vec3 v_color1;\n", shading, varying);
	if (enableFog) {
		WRITE(p, "uniform vec3 u_fogcolor;\n");
		WRITE(p, "%s %s float v_fogdepth;\n", varying, highpFog ? "highp" : "mediump");
	}
	if (doTexture) {
		if (doTextureProjection)
			WRITE(p, "%s %s vec3 v_texcoord;\n", varying, highpTexcoord ? "highp" : "mediump");
		else
			WRITE(p, "%s %s vec2 v_texcoord;\n", varying, highpTexcoord ? "highp" : "mediump");
	}

	if (!g_Config.bFragmentTestCache) {
		if (enableAlphaTest && !alphaTestAgainstZero) {
			if (bitwiseOps) {
				WRITE(p, "int roundAndScaleTo255i(in float x) { return int(floor(x * 255.0 + 0.5)); }\n");
			} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
				WRITE(p, "float roundTo255thf(in mediump float x) { mediump float y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
			} else {
				WRITE(p, "float roundAndScaleTo255f(in float x) { return floor(x * 255.0 + 0.5); }\n");
			}
		}
		if (enableColorTest && !colorTestAgainstZero) {
			if (bitwiseOps) {
				WRITE(p, "ivec3 roundAndScaleTo255iv(in vec3 x) { return ivec3(floor(x * 255.0 + 0.5)); }\n");
			} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
				WRITE(p, "vec3 roundTo255thv(in vec3 x) { vec3 y = x + (0.5/255.0); return y - fract(y * 255.0) * (1.0 / 255.0); }\n");
			} else {
				WRITE(p, "vec3 roundAndScaleTo255v(in vec3 x) { return floor(x * 255.0 + 0.5); }\n");
			}
		}
	}

	if (stencilToAlpha == REPLACE_ALPHA_DUALSOURCE) {
		WRITE(p, "out vec4 fragColor0;\n");
		WRITE(p, "out vec4 fragColor1;\n");
	} else if (!strcmp(fragColor0, "fragColor0")) {
		WRITE(p, "out vec4 fragColor0;\n");
	}

	// PowerVR needs a custom modulo function. For some reason, this has far higher precision than the builtin one.
	if ((gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) && needShaderTexClamp) {
		WRITE(p, "float mymod(float a, float b) { return a - b * floor(a / b); }\n");
	}

	WRITE(p, "void main() {\n");

	if (isModeClear) {
		// Clear mode does not allow any fancy shading.
		WRITE(p, "  vec4 v = v_color0;\n");
	} else {
		const char *secondary = "";
		// Secondary color for specular on top of texture
		if (lmode) {
			WRITE(p, "  vec4 s = vec4(v_color1, 0.0);\n");
			secondary = " + s";
		} else {
			secondary = "";
		}

		if (doTexture) {
			const char *texcoord = "v_texcoord";
			// TODO: Not sure the right way to do this for projection.
			// This path destroys resolution on older PowerVR no matter what I do, so we disable it on SGX 540 and lesser, and live with the consequences.
			if (needShaderTexClamp && !(gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_TERRIBLE)) {
				// We may be clamping inside a larger surface (tex = 64x64, buffer=480x272).
				// We may also be wrapping in such a surface, or either one in a too-small surface.
				// Obviously, clamping to a smaller surface won't work.  But better to clamp to something.
				std::string ucoord = "v_texcoord.x";
				std::string vcoord = "v_texcoord.y";
				if (doTextureProjection) {
					ucoord += " / v_texcoord.z";
					vcoord = "(v_texcoord.y / v_texcoord.z)";
					// Vertex texcoords are NOT flipped when projecting despite gstate_c.flipTexture.
				} else if (flipTexture) {
					vcoord = "1.0 - " + vcoord;
				}

				std::string modulo = (gl_extensions.bugs & BUG_PVR_SHADER_PRECISION_BAD) ? "mymod" : "mod";

				if (id.Bit(BIT_CLAMP_S)) {
					ucoord = "clamp(" + ucoord + ", u_texclamp.z, u_texclamp.x - u_texclamp.z)";
				} else {
					ucoord = modulo + "(" + ucoord + ", u_texclamp.x)";
				}
				if (id.Bit(BIT_CLAMP_T)) {
					vcoord = "clamp(" + vcoord + ", u_texclamp.w, u_texclamp.y - u_texclamp.w)";
				} else {
					vcoord = modulo + "(" + vcoord + ", u_texclamp.y)";
				}
				if (textureAtOffset) {
					ucoord = "(" + ucoord + " + u_texclampoff.x)";
					vcoord = "(" + vcoord + " + u_texclampoff.y)";
				}

				if (flipTexture) {
					vcoord = "1.0 - " + vcoord;
				}

				WRITE(p, "  vec2 fixedcoord = vec2(%s, %s);\n", ucoord.c_str(), vcoord.c_str());
				texcoord = "fixedcoord";
				// We already projected it.
				doTextureProjection = false;
			} else if (doTextureProjection && flipTexture) {
				// Since we need to flip v, we project manually.
				WRITE(p, "  vec2 fixedcoord = vec2(v_texcoord.x / v_texcoord.z, 1.0 - (v_texcoord.y / v_texcoord.z));\n");
				texcoord = "fixedcoord";
				doTextureProjection = false;
			}

			if (doTextureProjection) {
				WRITE(p, "  vec4 t = %sProj(tex, %s);\n", texture, texcoord);
			} else {
				WRITE(p, "  vec4 t = %s(tex, %s);\n", texture, texcoord);
			}
			WRITE(p, "  vec4 p = v_color0;\n");

			if (doTextureAlpha) { // texfmt == RGBA
				switch (texFunc) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = p * t%s;\n", secondary);
					break;

				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, t.rgb, t.a), p.a)%s;\n", secondary);
					break;

				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, u_texenv.rgb, t.rgb), p.a * t.a)%s;\n", secondary);
					break;

				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = t%s;\n", secondary);
					break;

				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3:
					WRITE(p, "  vec4 v = vec4(p.rgb + t.rgb, p.a * t.a)%s;\n", secondary);
					break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}
			} else { // texfmt == RGB
				switch (texFunc) {
				case GE_TEXFUNC_MODULATE:
					WRITE(p, "  vec4 v = vec4(t.rgb * p.rgb, p.a)%s;\n", secondary);
					break;

				case GE_TEXFUNC_DECAL:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary);
					break;

				case GE_TEXFUNC_BLEND:
					WRITE(p, "  vec4 v = vec4(mix(p.rgb, u_texenv.rgb, t.rgb), p.a)%s;\n", secondary);
					break;

				case GE_TEXFUNC_REPLACE:
					WRITE(p, "  vec4 v = vec4(t.rgb, p.a)%s;\n", secondary);
					break;

				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3:
					WRITE(p, "  vec4 v = vec4(p.rgb + t.rgb, p.a)%s;\n", secondary); break;
				default:
					WRITE(p, "  vec4 v = p;\n"); break;
				}
			}
		} else {
			// No texture mapping
			WRITE(p, "  vec4 v = v_color0 %s;\n", secondary);
		}

		// Texture access is at half texels [0.5/256, 255.5/256], but colors are normalized [0, 255].
		// So we have to scale to account for the difference.
		std::string alphaTestXCoord = "0";
		if (g_Config.bFragmentTestCache) {
			if (enableColorTest && !colorTestAgainstZero) {
				WRITE(p, "  vec4 vScale256 = v * %f + %f;\n", 255.0 / 256.0, 0.5 / 256.0);
				alphaTestXCoord = "vScale256.a";
			} else if (enableAlphaTest && !alphaTestAgainstZero) {
				char temp[64];
				snprintf(temp, sizeof(temp), "v.a * %f + %f", 255.0 / 256.0, 0.5 / 256.0);
				alphaTestXCoord = temp;
			}
		}

		if (enableAlphaTest) {
			if (alphaTestAgainstZero) {
				// When testing against 0 (extremely common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (alphaTestFunc == GE_COMP_NOTEQUAL || alphaTestFunc == GE_COMP_GREATER) {
					WRITE(p, "  if (v.a < 0.002) discard;\n");
				} else if (alphaTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.  Happens sometimes, actually...
					WRITE(p, "  if (v.a > 0.002) discard;\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  discard;\n");
				}
			} else if (g_Config.bFragmentTestCache) {
				WRITE(p, "  float aResult = %s(testtex, vec2(%s, 0)).a;\n", texture, alphaTestXCoord.c_str());
				WRITE(p, "  if (aResult < 0.5) discard;\n");
			} else {
				const char *alphaTestFuncs[] = { "#", "#", " != ", " == ", " >= ", " > ", " <= ", " < " };
				if (alphaTestFuncs[alphaTestFunc][0] != '#') {
					if (bitwiseOps) {
						WRITE(p, "  if ((roundAndScaleTo255i(v.a) & u_alphacolormask.a) %s int(u_alphacolorref.a)) discard;\n", alphaTestFuncs[alphaTestFunc]);
					} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
						// Work around bad PVR driver problem where equality check + discard just doesn't work.
						if (alphaTestFunc != GE_COMP_NOTEQUAL) {
							WRITE(p, "  if (roundTo255thf(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
						}
					} else {
						WRITE(p, "  if (roundAndScaleTo255f(v.a) %s u_alphacolorref.a) discard;\n", alphaTestFuncs[alphaTestFunc]);
					}
				} else {
					// This means NEVER.  See above.
					WRITE(p, "  discard;\n");
				}
			}
		}

		if (enableColorTest) {
			if (colorTestAgainstZero) {
				// When testing against 0 (common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (colorTestFunc == GE_COMP_NOTEQUAL) {
					WRITE(p, "  if (v.r < 0.002 && v.g < 0.002 && v.b < 0.002) discard;\n");
				} else if (colorTestFunc != GE_COMP_NEVER) {
					// Anything else is a test for == 0.
					WRITE(p, "  if (v.r > 0.002 || v.g > 0.002 || v.b > 0.002) discard;\n");
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					WRITE(p, "  discard;\n");
				}
			} else if (g_Config.bFragmentTestCache) {
				WRITE(p, "  float rResult = %s(testtex, vec2(vScale256.r, 0)).r;\n", texture);
				WRITE(p, "  float gResult = %s(testtex, vec2(vScale256.g, 0)).g;\n", texture);
				WRITE(p, "  float bResult = %s(testtex, vec2(vScale256.b, 0)).b;\n", texture);
				if (colorTestFunc == GE_COMP_EQUAL) {
					// Equal means all parts must be equal.
					WRITE(p, "  if (rResult < 0.5 || gResult < 0.5 || bResult < 0.5) discard;\n");
				} else {
					// Not equal means any part must be not equal.
					WRITE(p, "  if (rResult < 0.5 && gResult < 0.5 && bResult < 0.5) discard;\n");
				}
			} else {
				const char *colorTestFuncs[] = { "#", "#", " != ", " == " };
				if (colorTestFuncs[colorTestFunc][0] != '#') {
					if (bitwiseOps) {
						// Apparently GLES3 does not support vector bitwise ops.
						WRITE(p, "  ivec3 v_scaled = roundAndScaleTo255iv(v.rgb);\n");
						const char *maskedFragColor = "ivec3(v_scaled.r & u_alphacolormask.r, v_scaled.g & u_alphacolormask.g, v_scaled.b & u_alphacolormask.b)";
						const char *maskedColorRef = "ivec3(int(u_alphacolorref.r) & u_alphacolormask.r, int(u_alphacolorref.g) & u_alphacolormask.g, int(u_alphacolorref.b) & u_alphacolormask.b)";
						WRITE(p, "  if (%s %s %s) discard;\n", maskedFragColor, colorTestFuncs[colorTestFunc], maskedColorRef);
					} else if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
						WRITE(p, "  if (roundTo255thv(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
					} else {
						WRITE(p, "  if (roundAndScaleTo255v(v.rgb) %s u_alphacolorref.rgb) discard;\n", colorTestFuncs[colorTestFunc]);
					}
				} else {
					WRITE(p, "  discard;\n");
				}
			}
		}

		// Color doubling happens after the color test.
		if (enableColorDoubling && replaceBlend == REPLACE_BLEND_2X_SRC) {
			WRITE(p, "  v.rgb = v.rgb * 4.0;\n");
		} else if (enableColorDoubling || replaceBlend == REPLACE_BLEND_2X_SRC) {
			WRITE(p, "  v.rgb = v.rgb * 2.0;\n");
		}

		if (enableFog) {
			WRITE(p, "  float fogCoef = clamp(v_fogdepth, 0.0, 1.0);\n");
			WRITE(p, "  v = mix(vec4(u_fogcolor, v.a), v, fogCoef);\n");
			// WRITE(p, "  v.x = v_depth;\n");
		}

		if (replaceBlend == REPLACE_BLEND_PRE_SRC || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			GEBlendSrcFactor funcA = (GEBlendSrcFactor)id.Bits(BIT_BLENDFUNC_A, 4);
			const char *srcFactor = "ERROR";
			switch (funcA) {
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "vec3(v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "vec3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "ERROR"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "vec3(v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "vec3(1.0 - v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "ERROR"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "ERROR"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			}

			WRITE(p, "  v.rgb = v.rgb * %s;\n", srcFactor);
		}

		if (replaceBlend == REPLACE_BLEND_COPY_FBO) {
			// If we have NV_shader_framebuffer_fetch / EXT_shader_framebuffer_fetch, we skip the blit.
			// We can just read the prev value more directly.
			if (gl_extensions.ANY_shader_framebuffer_fetch) {
				WRITE(p, "  lowp vec4 destColor = %s;\n", lastFragData);
			} else if (!texelFetch) {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, gl_FragCoord.xy * u_fbotexSize.xy);\n", texture);
			} else {
				WRITE(p, "  lowp vec4 destColor = %s(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);\n", texelFetch);
			}

			GEBlendSrcFactor funcA = (GEBlendSrcFactor)id.Bits(BIT_BLENDFUNC_A, 4);
			GEBlendDstFactor funcB = (GEBlendDstFactor)id.Bits(BIT_BLENDFUNC_B, 4);
			GEBlendMode eq = (GEBlendMode)id.Bits(BIT_BLENDEQ, 3);

			const char *srcFactor = "vec3(1.0)";
			const char *dstFactor = "vec3(0.0)";

			switch (funcA)
			{
			case GE_SRCBLEND_DSTCOLOR:          srcFactor = "destColor.rgb"; break;
			case GE_SRCBLEND_INVDSTCOLOR:       srcFactor = "(vec3(1.0) - destColor.rgb)"; break;
			case GE_SRCBLEND_SRCALPHA:          srcFactor = "vec3(v.a)"; break;
			case GE_SRCBLEND_INVSRCALPHA:       srcFactor = "vec3(1.0 - v.a)"; break;
			case GE_SRCBLEND_DSTALPHA:          srcFactor = "vec3(destColor.a)"; break;
			case GE_SRCBLEND_INVDSTALPHA:       srcFactor = "vec3(1.0 - destColor.a)"; break;
			case GE_SRCBLEND_DOUBLESRCALPHA:    srcFactor = "vec3(v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = "vec3(1.0 - v.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEDSTALPHA:    srcFactor = "vec3(destColor.a * 2.0)"; break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = "vec3(1.0 - destColor.a * 2.0)"; break;
			case GE_SRCBLEND_FIXA:              srcFactor = "u_blendFixA"; break;
			}
			switch (funcB)
			{
			case GE_DSTBLEND_SRCCOLOR:          dstFactor = "v.rgb"; break;
			case GE_DSTBLEND_INVSRCCOLOR:       dstFactor = "(vec3(1.0) - v.rgb)"; break;
			case GE_DSTBLEND_SRCALPHA:          dstFactor = "vec3(v.a)"; break;
			case GE_DSTBLEND_INVSRCALPHA:       dstFactor = "vec3(1.0 - v.a)"; break;
			case GE_DSTBLEND_DSTALPHA:          dstFactor = "vec3(destColor.a)"; break;
			case GE_DSTBLEND_INVDSTALPHA:       dstFactor = "vec3(1.0 - destColor.a)"; break;
			case GE_DSTBLEND_DOUBLESRCALPHA:    dstFactor = "vec3(v.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEINVSRCALPHA: dstFactor = "vec3(1.0 - v.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEDSTALPHA:    dstFactor = "vec3(destColor.a * 2.0)"; break;
			case GE_DSTBLEND_DOUBLEINVDSTALPHA: dstFactor = "vec3(1.0 - destColor.a * 2.0)"; break;
			case GE_DSTBLEND_FIXB:              dstFactor = "u_blendFixB"; break;
			}

			switch (eq)
			{
			case GE_BLENDMODE_MUL_AND_ADD:
				WRITE(p, "  v.rgb = v.rgb * %s + destColor.rgb * %s;\n", srcFactor, dstFactor);
				break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT:
				WRITE(p, "  v.rgb = v.rgb * %s - destColor.rgb * %s;\n", srcFactor, dstFactor);
				break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
				WRITE(p, "  v.rgb = destColor.rgb * %s - v.rgb * %s;\n", dstFactor, srcFactor);
				break;
			case GE_BLENDMODE_MIN:
				WRITE(p, "  v.rgb = min(v.rgb, destColor.rgb);\n");
				break;
			case GE_BLENDMODE_MAX:
				WRITE(p, "  v.rgb = max(v.rgb, destColor.rgb);\n");
				break;
			case GE_BLENDMODE_ABSDIFF:
				WRITE(p, "  v.rgb = abs(v.rgb - destColor.rgb);\n");
				break;
			}
		}

		if (replaceBlend == REPLACE_BLEND_2X_ALPHA || replaceBlend == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			WRITE(p, "  v.a = v.a * 2.0;\n");
		}
	}

	std::string replacedAlpha = "0.0";
	char replacedAlphaTemp[64] = "";
	if (stencilToAlpha != REPLACE_ALPHA_NO) {
		switch (replaceAlphaWithStencilType) {
		case STENCIL_VALUE_UNIFORM:
			replacedAlpha = "u_stencilReplaceValue";
			break;

		case STENCIL_VALUE_ZERO:
			replacedAlpha = "0.0";
			break;

		case STENCIL_VALUE_ONE:
		case STENCIL_VALUE_INVERT:
			// In invert, we subtract by one, but we want to output one here.
			replacedAlpha = "1.0";
			break;

		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_DECR_4:
			// We're adding/subtracting, just by the smallest value in 4-bit.
			snprintf(replacedAlphaTemp, sizeof(replacedAlphaTemp), "%f", 1.0 / 15.0);
			replacedAlpha = replacedAlphaTemp;
			break;

		case STENCIL_VALUE_INCR_8:
		case STENCIL_VALUE_DECR_8:
			// We're adding/subtracting, just by the smallest value in 8-bit.
			snprintf(replacedAlphaTemp, sizeof(replacedAlphaTemp), "%f", 1.0 / 255.0);
			replacedAlpha = replacedAlphaTemp;
			break;

		case STENCIL_VALUE_KEEP:
			// Do nothing. We'll mask out the alpha using color mask.
			break;
		}
	}

	switch (stencilToAlpha) {
	case REPLACE_ALPHA_DUALSOURCE:
		WRITE(p, "  fragColor0 = vec4(v.rgb, %s);\n", replacedAlpha.c_str());
		WRITE(p, "  fragColor1 = vec4(0.0, 0.0, 0.0, v.a);\n");
		break;

	case REPLACE_ALPHA_YES:
		WRITE(p, "  %s = vec4(v.rgb, %s);\n", fragColor0, replacedAlpha.c_str());
		break;

	case REPLACE_ALPHA_NO:
		WRITE(p, "  %s = v;\n", fragColor0);
		break;
	}

	LogicOpReplaceType replaceLogicOpType = (LogicOpReplaceType)id.Bits(BIT_REPLACE_LOGIC_OP_TYPE, 2);
	switch (replaceLogicOpType) {
	case LOGICOPTYPE_ONE:
		WRITE(p, "  %s.rgb = vec3(1.0, 1.0, 1.0);\n", fragColor0);
		break;
	case LOGICOPTYPE_INVERT:
		WRITE(p, "  %s.rgb = vec3(1.0, 1.0, 1.0) - %s.rgb;\n", fragColor0, fragColor0);
		break;
	case LOGICOPTYPE_NORMAL:
		break;
	}

#ifdef DEBUG_SHADER
	if (doTexture) {
		WRITE(p, "  %s = texture2D(tex, v_texcoord.xy);\n", fragColor0);
		WRITE(p, "  %s += vec4(0.3,0,0.3,0.3);\n", fragColor0);
	} else {
		WRITE(p, "  %s = vec4(1,0,1,1);\n", fragColor0);
	}
#endif
	WRITE(p, "}\n");
}

}  // namespace
