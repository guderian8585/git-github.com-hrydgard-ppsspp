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


// Alpha/stencil is a convoluted mess. Some good comments are here:
// https://github.com/hrydgard/ppsspp/issues/3768


#include "StateMapping.h"
#include "profiler/profiler.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/GLES/GLStateCache.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/FragmentShaderGenerator.h"

static const GLushort glBlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	GL_ZERO,
	GL_ONE,
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_CONSTANT_COLOR,
	GL_ONE_MINUS_CONSTANT_COLOR,
	GL_CONSTANT_ALPHA,
	GL_ONE_MINUS_CONSTANT_ALPHA,
#if !defined(USING_GLES2)   // TODO: Remove when we have better headers
	GL_SRC1_ALPHA,
	GL_ONE_MINUS_SRC1_ALPHA,
#elif !defined(IOS)
	GL_SRC1_ALPHA_EXT,
	GL_ONE_MINUS_SRC1_ALPHA_EXT,
#else
	GL_INVALID_ENUM,
	GL_INVALID_ENUM,
#endif
	GL_INVALID_ENUM,
};

static const GLushort glBlendEqLookup[(size_t)BlendEq::COUNT] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
	GL_MIN,
	GL_MAX,
};

static const GLushort cullingMode[] = {
	GL_FRONT,
	GL_BACK,
};

static const GLushort ztests[] = {
	GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, 
	GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL,
};

static const GLushort stencilOps[] = {
	GL_KEEP,
	GL_ZERO,
	GL_REPLACE,
	GL_INVERT,
	GL_INCR,
	GL_DECR,
	GL_KEEP, // reserved
	GL_KEEP, // reserved
};

#if !defined(USING_GLES2)
static const GLushort logicOps[] = {
	GL_CLEAR,
	GL_AND,
	GL_AND_REVERSE,
	GL_COPY,
	GL_AND_INVERTED,
	GL_NOOP,
	GL_XOR,
	GL_OR,
	GL_NOR,
	GL_EQUIV,
	GL_INVERT,
	GL_OR_REVERSE,
	GL_COPY_INVERTED,
	GL_OR_INVERTED,
	GL_NAND,
	GL_SET,
};
#endif

bool TransformDrawEngine::ApplyShaderBlending() {
	if (gstate_c.featureFlags & GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH) {
		return true;
	}

	static const int MAX_REASONABLE_BLITS_PER_FRAME = 24;

	static int lastFrameBlit = -1;
	static int blitsThisFrame = 0;
	if (lastFrameBlit != gpuStats.numFlips) {
		if (blitsThisFrame > MAX_REASONABLE_BLITS_PER_FRAME) {
			WARN_LOG_REPORT_ONCE(blendingBlit, G3D, "Lots of blits needed for obscure blending: %d per frame, blend %d/%d/%d", blitsThisFrame, gstate.getBlendFuncA(), gstate.getBlendFuncB(), gstate.getBlendEq());
		}
		blitsThisFrame = 0;
		lastFrameBlit = gpuStats.numFlips;
	}
	++blitsThisFrame;
	if (blitsThisFrame > MAX_REASONABLE_BLITS_PER_FRAME * 2) {
		WARN_LOG_ONCE(blendingBlit2, G3D, "Skipping additional blits needed for obscure blending: %d per frame, blend %d/%d/%d", blitsThisFrame, gstate.getBlendFuncA(), gstate.getBlendFuncB(), gstate.getBlendEq());
		return false;
	}

	fboTexNeedBind_ = true;

	shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
	return true;
}

inline void TransformDrawEngine::ResetShaderBlending() {
	if (fboTexBound_) {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		fboTexBound_ = false;
	}
}

void TransformDrawEngine::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.textureChanged != TEXCHANGE_UNCHANGED && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.textureChanged = TEXCHANGE_UNCHANGED;
		if (gstate_c.needShaderTexClamp) {
			// We will rarely need to set this, so let's do it every time on use rather than in runloop.
			// Most of the time non-framebuffer textures will be used which can be clamped themselves.
			shaderManager_->DirtyUniform(DIRTY_TEXCLAMP);
		}
	}

	// Start profiling here to skip SetTexture which is already accounted for
	PROFILE_THIS_SCOPE("applydrawstate");

	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	gstate_c.allowShaderBlend = !g_Config.bDisableSlowFramebufEffects;

	// Do the large chunks of state conversion. We might be able to hide these two behind a dirty-flag each,
	// to avoid recomputing heavy stuff unnecessarily every draw call.
	GenericBlendState blendState;
	ConvertBlendState(blendState, gstate_c.allowShaderBlend);
	ViewportAndScissor vpAndScissor;
	ConvertViewportAndScissor(useBufferedRendering,
		framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
		framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
		vpAndScissor);

	if (blendState.applyShaderBlending) {
		if (ApplyShaderBlending()) {
			// We may still want to do something about stencil -> alpha.
			ApplyStencilReplaceAndLogicOp(blendState.replaceAlphaWithStencil, blendState);
		} else {
			// Until next time, force it off.
			ResetShaderBlending();
			gstate_c.allowShaderBlend = false;
		}
	} else if (blendState.resetShaderBlending) {
		ResetShaderBlending();
	}

	if (blendState.enabled) {
		glstate.blend.enable();
		glstate.blendEquationSeparate.set(glBlendEqLookup[(size_t)blendState.eqColor], glBlendEqLookup[(size_t)blendState.eqAlpha]);
		glstate.blendFuncSeparate.set(
			glBlendFactorLookup[(size_t)blendState.srcColor], glBlendFactorLookup[(size_t)blendState.dstColor],
			glBlendFactorLookup[(size_t)blendState.srcAlpha], glBlendFactorLookup[(size_t)blendState.dstAlpha]);
		if (blendState.dirtyShaderBlend) {
			shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
		}
		if (blendState.useBlendColor) {
			uint32_t color = blendState.blendColor;
			const float col[4] = {
				(float)((color & 0xFF) >> 0) * (1.0f / 255.0f),
				(float)((color & 0xFF00) >> 8) * (1.0f / 255.0f),
				(float)((color & 0xFF0000) >> 16) * (1.0f / 255.0f),
				(float)((color & 0xFF000000) >> 24) * (1.0f / 255.0f),
			};
			glstate.blendColor.set(col);
		}
	} else {
		glstate.blend.disable();
	}

	bool alwaysDepthWrite = g_Config.bAlwaysDepthWrite;
	bool enableStencilTest = !g_Config.bDisableStencilTest;

	// Dither
	if (gstate.isDitherEnabled()) {
		glstate.dither.enable();
		glstate.dither.set(GL_TRUE);
	} else {
		glstate.dither.disable();
	}

	if (gstate.isModeClear()) {
#ifndef USING_GLES2
		if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
			// Logic Ops
			glstate.colorLogicOp.disable();
		}
#endif
		// Culling
		glstate.cullFace.disable();

		// Depth Test
		glstate.depthTest.enable();
		glstate.depthFunc.set(GL_ALWAYS);
		glstate.depthWrite.set(gstate.isClearModeDepthMask() || alwaysDepthWrite ? GL_TRUE : GL_FALSE);
		if (gstate.isClearModeDepthMask() || alwaysDepthWrite) {
			framebufferManager_->SetDepthUpdated();
		}

		// Color Test
		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		glstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);

		// Stencil Test
		if (alphaMask && enableStencilTest) {
			glstate.stencilTest.enable();
			glstate.stencilOp.set(GL_REPLACE, GL_REPLACE, GL_REPLACE);
			// TODO: In clear mode, the stencil value is set to the alpha value of the vertex.
			// A normal clear will be 2 points, the second point has the color.
			// We should set "ref" to that value instead of 0.
			// In case of clear rectangles, we set it again once we know what the color is.
			glstate.stencilFunc.set(GL_ALWAYS, 255, 0xFF);
			glstate.stencilMask.set(0xFF);
		} else {
			glstate.stencilTest.disable();
		}
	} else {
#ifndef USING_GLES2
		if (gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP)) {
			// TODO: Make this dynamic
			// Logic Ops
			if (gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY) {
				glstate.colorLogicOp.enable();
				glstate.logicOp.set(logicOps[gstate.getLogicOp()]);
			} else {
				glstate.colorLogicOp.disable();
			}
		}
#endif
		// Set cull
		bool cullEnabled = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		if (cullEnabled) {
			glstate.cullFace.enable();
			glstate.cullFaceMode.set(cullingMode[gstate.getCullMode() ^ !useBufferedRendering]);
		} else {
			glstate.cullFace.disable();
		}

		// Depth Test
		if (gstate.isDepthTestEnabled()) {
			glstate.depthTest.enable();
			glstate.depthFunc.set(ztests[gstate.getDepthTestFunction()]);
			glstate.depthWrite.set(gstate.isDepthWriteEnabled() || alwaysDepthWrite ? GL_TRUE : GL_FALSE);
			if (gstate.isDepthWriteEnabled() || alwaysDepthWrite) {
				framebufferManager_->SetDepthUpdated();
			}
		} else {
			glstate.depthTest.disable();
		}

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;

#ifndef MOBILE_DEVICE
		u8 abits = (gstate.pmska >> 0) & 0xFF;
		u8 rbits = (gstate.pmskc >> 0) & 0xFF;
		u8 gbits = (gstate.pmskc >> 8) & 0xFF;
		u8 bbits = (gstate.pmskc >> 16) & 0xFF;
		if ((rbits != 0 && rbits != 0xFF) || (gbits != 0 && gbits != 0xFF) || (bbits != 0 && bbits != 0xFF)) {
			WARN_LOG_REPORT_ONCE(rgbmask, G3D, "Unsupported RGB mask: r=%02x g=%02x b=%02x", rbits, gbits, bbits);
		}
		if (abits != 0 && abits != 0xFF) {
			// The stencil part of the mask is supported.
			WARN_LOG_REPORT_ONCE(amask, G3D, "Unsupported alpha/stencil mask: %02x", abits);
		}
#endif

		// Let's not write to alpha if stencil isn't enabled.
		if (!gstate.isStencilTestEnabled()) {
			amask = false;
		} else {
			// If the stencil type is set to KEEP, we shouldn't write to the stencil/alpha channel.
			if (ReplaceAlphaWithStencilType() == STENCIL_VALUE_KEEP) {
				amask = false;
			}
		}

		glstate.colorMask.set(rmask, gmask, bmask, amask);

		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		// Stencil Test
		if (stencilState.enabled) {
			glstate.stencilTest.enable();
			glstate.stencilFunc.set(ztests[stencilState.testFunc], stencilState.testRef, stencilState.testMask);
			glstate.stencilOp.set(stencilOps[stencilState.sFail], stencilOps[stencilState.zFail], stencilOps[stencilState.zPass]);
			glstate.stencilMask.set(stencilState.writeMask);
		} else {
			glstate.stencilTest.disable();
		}
	}

	if (vpAndScissor.scissorEnable) {
		glstate.scissorTest.enable();
		if (!useBufferedRendering) {
			vpAndScissor.scissorY = PSP_CoreParameter().pixelHeight - vpAndScissor.scissorH - vpAndScissor.scissorY;
		}
		glstate.scissorRect.set(vpAndScissor.scissorX, vpAndScissor.scissorY, vpAndScissor.scissorW, vpAndScissor.scissorH);
	} else {
		glstate.scissorTest.disable();
	}

	if (!useBufferedRendering) {
		vpAndScissor.viewportY = PSP_CoreParameter().pixelHeight - vpAndScissor.viewportH - vpAndScissor.viewportY;
	}
	glstate.viewport.set(vpAndScissor.viewportX, vpAndScissor.viewportY, vpAndScissor.viewportW, vpAndScissor.viewportH);
	glstate.depthRange.set(vpAndScissor.depthRangeMin, vpAndScissor.depthRangeMax);

	if (vpAndScissor.dirtyProj) {
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
	if (vpAndScissor.dirtyDepth) {
		shaderManager_->DirtyUniform(DIRTY_DEPTHRANGE);
	}
}

void TransformDrawEngine::ApplyDrawStateLate() {
	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		if (gstate.isAlphaTestEnabled() || gstate.isColorTestEnabled()) {
			fragmentTestCache_->BindTestTexture(GL_TEXTURE2);
		}

		if (fboTexNeedBind_) {
			// Note that this is positions, not UVs, that we need the copy from.
			framebufferManager_->BindFramebufferColor(GL_TEXTURE1, gstate.getFrameBufRawAddress(), nullptr, BINDFBCOLOR_MAY_COPY);
			framebufferManager_->RebindFramebuffer();

			glActiveTexture(GL_TEXTURE1);
			// If we are rendering at a higher resolution, linear is probably best for the dest color.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glActiveTexture(GL_TEXTURE0);
			fboTexBound_ = true;
			fboTexNeedBind_ = false;
		}

		// Apply the texture after the FBO tex, since it might unbind the texture.
		// TODO: Could use a separate texture unit to be safer?
		textureCache_->ApplyTexture();
	}
}
