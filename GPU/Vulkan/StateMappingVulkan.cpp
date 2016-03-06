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

#define VK_PROTOTYPES
#include "ext/vulkan/vulkan.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
//#include "GPU/Vulkan/StateMappingVulkan.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
//#include "GPU/Vulkan/PixelShaderGeneratorVulkan.h"

// These tables all fit into u8s.
static const VkBlendFactor vkBlendFactorLookup[(size_t)BlendFactor::COUNT] = {
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
	VK_BLEND_FACTOR_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	VK_BLEND_FACTOR_SRC1_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
	VK_BLEND_FACTOR_MAX_ENUM,
};

static const VkBlendOp vkBlendEqLookup[(size_t)BlendEq::COUNT] = {
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX,
};

static const VkCullModeFlagBits cullingMode[] = {
	VK_CULL_MODE_BACK_BIT,
	VK_CULL_MODE_FRONT_BIT,
};

static const VkCompareOp compareOps[] = {
	VK_COMPARE_OP_NEVER,
	VK_COMPARE_OP_ALWAYS,
	VK_COMPARE_OP_EQUAL,
	VK_COMPARE_OP_NOT_EQUAL,
	VK_COMPARE_OP_LESS,
	VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER,
	VK_COMPARE_OP_GREATER_OR_EQUAL,
};

static const VkStencilOp stencilOps[] = {
	VK_STENCIL_OP_KEEP,
	VK_STENCIL_OP_ZERO,
	VK_STENCIL_OP_REPLACE,
	VK_STENCIL_OP_INVERT,
	VK_STENCIL_OP_INCREMENT_AND_CLAMP,
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,
	VK_STENCIL_OP_KEEP, // reserved
	VK_STENCIL_OP_KEEP, // reserved
};

const VkPrimitiveTopology primToVulkan[8] = {
	VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
	VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  // Vulkan doesn't do quads. We could do strips with restart-index though. We could also do RECT primitives in the geometry shader.
};


bool ApplyShaderBlending() {
	return false;
}

void ResetShaderBlending() {
	//
}

// TODO: Do this more progressively. No need to compute the entire state if the entire state hasn't changed.
// In Vulkan, we simply collect all the state together into a "pipeline key" - we don't actually set any state here
// (the caller is responsible for setting the little dynamic state that is supported, dynState).
void ConvertStateToVulkanKey(FramebufferManagerVulkan &fbManager, int prim, VulkanPipelineRasterStateKey &key, VulkanDynamicState &dynState) {
	// Unfortunately, this isn't implemented yet.
	gstate_c.allowShaderBlend = false;

	// Set blend - unless we need to do it in the shader.
	GenericBlendState blendState;
	ConvertBlendState(blendState, gstate_c.allowShaderBlend);

	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	ViewportAndScissor vpAndScissor;
	ConvertViewportAndScissor(useBufferedRendering,
		fbManager.GetRenderWidth(), fbManager.GetRenderHeight(),
		fbManager.GetTargetBufferWidth(), fbManager.GetTargetBufferHeight(),
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
		key.blendEnable = true;
		key.blendOpColor = vkBlendEqLookup[(size_t)blendState.eqColor];
		key.blendOpAlpha = vkBlendEqLookup[(size_t)blendState.eqAlpha];
		key.srcColor = vkBlendFactorLookup[(size_t)blendState.srcColor];
		key.srcAlpha = vkBlendFactorLookup[(size_t)blendState.srcAlpha];
		key.destColor = vkBlendFactorLookup[(size_t)blendState.dstColor];
		key.destAlpha = vkBlendFactorLookup[(size_t)blendState.dstAlpha];
		if (blendState.dirtyShaderBlend) {
			//shaderManager_->DirtyUniform(DIRTY_SHADERBLEND);
		}
		dynState.useBlendColor = blendState.useBlendColor;
		if (blendState.useBlendColor) {
			dynState.blendColor = blendState.blendColor;
		}
	} else {
		key.blendEnable = false;
		dynState.useBlendColor = false;
	}

	dynState.useStencil = false;

	// Set ColorMask/Stencil/Depth
	if (gstate.isModeClear()) {
		key.cullMode = VK_CULL_MODE_NONE;

		key.depthTestEnable = true;
		key.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		key.depthWriteEnable = gstate.isClearModeDepthMask();
		if (gstate.isClearModeDepthMask()) {
			// framebufferManager_->SetDepthUpdated();
		}

		// Color Test
		bool colorMask = gstate.isClearModeColorMask();
		bool alphaMask = gstate.isClearModeAlphaMask();
		key.colorWriteMask = (colorMask ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_A_BIT) : 0) | (alphaMask ? VK_COLOR_COMPONENT_A_BIT : 0);

		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);

		// Stencil Test
		if (stencilState.enabled) {
			key.stencilTestEnable = true;
			key.stencilCompareOp = compareOps[stencilState.testFunc];
			key.stencilPassOp = stencilOps[stencilState.zPass];
			key.stencilFailOp = stencilOps[stencilState.sFail];
			key.stencilDepthFailOp = stencilOps[stencilState.zFail];
			dynState.useStencil = true;
			dynState.stencilRef = stencilState.testRef;
			dynState.stencilCompareMask = stencilState.testMask;
			dynState.stencilWriteMask = stencilState.writeMask;
		} else {
			key.stencilTestEnable = false;
			dynState.useStencil = false;
		}
	} else {
		// Set cull
		bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		key.cullMode = wantCull ? (gstate.getCullMode() ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE;

		// Depth Test
		if (gstate.isDepthTestEnabled()) {
			key.depthTestEnable = true;
			key.depthCompareOp = compareOps[gstate.getDepthTestFunction()];
			key.depthWriteEnable = gstate.isDepthWriteEnabled();
			if (gstate.isDepthWriteEnabled()) {
				// framebufferManager_->SetDepthUpdated();
			}
		} else {
			key.depthTestEnable = false;
		}

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;

		u8 abits = (gstate.pmska >> 0) & 0xFF;
#ifndef MOBILE_DEVICE
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

		key.colorWriteMask = (rmask ? VK_COLOR_COMPONENT_R_BIT : 0) | (gmask ? VK_COLOR_COMPONENT_G_BIT : 0) | (bmask ? VK_COLOR_COMPONENT_B_BIT : 0) | (amask ? VK_COLOR_COMPONENT_A_BIT : 0);
		
		// Stencil Test
		if (gstate.isStencilTestEnabled()) {
			key.stencilTestEnable = true;
			key.stencilCompareOp = compareOps[gstate.getStencilTestFunction()];
			dynState.stencilRef = gstate.getStencilTestRef();
			dynState.stencilCompareMask = gstate.getStencilTestMask();
			key.stencilFailOp = stencilOps[gstate.getStencilOpSFail()];  // stencil fail
			key.stencilDepthFailOp = stencilOps[gstate.getStencilOpZFail()];  // depth fail
			key.stencilPassOp = stencilOps[gstate.getStencilOpZPass()]; // depth pass
			dynState.stencilWriteMask = ~abits;
		} else {
			key.stencilTestEnable = false;
		}
	}

	key.topology = primToVulkan[prim];

	VkViewport &vp = dynState.viewport;
	vp.x = vpAndScissor.viewportX;
	vp.y = vpAndScissor.viewportY;
	vp.width = vpAndScissor.viewportW;
	vp.height = vpAndScissor.viewportH;
	vp.minDepth = vpAndScissor.depthRangeMin;
	vp.maxDepth = vpAndScissor.depthRangeMax;
	if (vpAndScissor.dirtyProj) {
		// shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}

	VkRect2D &scissor = dynState.scissor;
	scissor.offset.x = vpAndScissor.scissorX;
	scissor.offset.y = vpAndScissor.scissorY;
	scissor.extent.width = vpAndScissor.scissorW;
	scissor.extent.height = vpAndScissor.scissorH;

	float depthMin = vpAndScissor.depthRangeMin;
	float depthMax = vpAndScissor.depthRangeMax;

	if (depthMin < 0.0f) depthMin = 0.0f;
	if (depthMax > 1.0f) depthMax = 1.0f;
	if (vpAndScissor.dirtyProj) {
		// shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
	if (vpAndScissor.dirtyDepth) {
		// shaderManager_->DirtyUniform(DIRTY_DEPTHRANGE);
	}
}

//void DrawEngineVulkan::ApplyDrawStateLate() {
	/*
	// At this point, we know if the vertices are full alpha or not.
	// TODO: Set the nearest/linear here (since we correctly know if alpha/color tests are needed)?
	if (!gstate.isModeClear()) {
		// TODO: Test texture?

		textureCache_->ApplyTexture();

		if (fboTexNeedBind_) {
			// Note that this is positions, not UVs, that we need the copy from.
			framebufferManager_->BindFramebufferColor(1, nullptr, BINDFBCOLOR_MAY_COPY);
			// If we are rendering at a higher resolution, linear is probably best for the dest color.
			pD3Ddevice->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
			pD3Ddevice->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			fboTexBound_ = true;
			fboTexNeedBind_ = false;
		}
	}
	*/
//}
