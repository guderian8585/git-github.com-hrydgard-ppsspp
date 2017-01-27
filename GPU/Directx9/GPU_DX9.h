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

#include <list>
#include <deque>

#include "GPU/GPUCommon.h"
#include "GPU/Directx9/FramebufferDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DepalettizeShaderDX9.h"
#include "GPU/Directx9/helper/dx_fbo.h"
#include "GPU/Common/VertexDecoderCommon.h"

namespace DX9 {

class ShaderManagerDX9;
class LinkedShaderDX9;

class GPU_DX9 : public GPUCommon {
public:
	GPU_DX9(GraphicsContext *gfxCtx);
	~GPU_DX9();

	void CheckGPUFeatures();
	void PreExecuteOp(u32 op, u32 diff) override;
	void ExecuteOp(u32 op, u32 diff) override;

	void ReapplyGfxStateInternal() override;
	void SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) override;
	void BeginFrame() override;
	void GetStats(char *buffer, size_t bufsize) override;
	void ClearCacheNextFrame() override;
	void DeviceLost() override;  // Only happens on Android. Drop all textures and shaders.
	void DeviceRestore() override;

	void DumpNextFrame() override;
	void DoState(PointerWrap &p) override;

	void ClearShaderCache() override;
	bool DecodeTexture(u8 *dest, const GPUgstate &state) override {
		return textureCacheDX9_->DecodeTexture(dest, state);
	}
	bool FramebufferDirty() override;
	bool FramebufferReallyDirty() override;

	void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) override {
		primaryInfo = reportingPrimaryInfo_;
		fullInfo = reportingFullInfo_;
	}
	std::vector<FramebufferInfo> GetFramebufferList() override;

	bool GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes = -1) override;
	bool GetCurrentDepthbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentStencilbuffer(GPUDebugBuffer &buffer) override;
	bool GetCurrentTexture(GPUDebugBuffer &buffer, int level) override;
	bool GetCurrentClut(GPUDebugBuffer &buffer) override;
	static bool GetOutputFramebuffer(GPUDebugBuffer &buffer);
	bool GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices) override;

	typedef void (GPU_DX9::*CmdFunc)(u32 op, u32 diff);
	struct CommandInfo {
		uint64_t flags;
		GPU_DX9::CmdFunc func;
	};

	void Execute_Vaddr(u32 op, u32 diff);
	void Execute_Iaddr(u32 op, u32 diff);
	void Execute_Prim(u32 op, u32 diff);
	void Execute_Bezier(u32 op, u32 diff);
	void Execute_Spline(u32 op, u32 diff);
	void Execute_VertexType(u32 op, u32 diff);
	void Execute_VertexTypeSkinning(u32 op, u32 diff);
	void Execute_TexScaleU(u32 op, u32 diff);
	void Execute_TexScaleV(u32 op, u32 diff);
	void Execute_TexOffsetU(u32 op, u32 diff);
	void Execute_TexOffsetV(u32 op, u32 diff);
	void Execute_TexSize0(u32 op, u32 diff);
	void Execute_LoadClut(u32 op, u32 diff);

	// Using string because it's generic - makes no assumptions on the size of the shader IDs of this backend.
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType shader) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType shader, DebugShaderStringType stringType) override;

protected:
	void FastRunLoop(DisplayList &list) override;
	void FinishDeferred() override;

private:
	void UpdateCmdInfo();

	void Flush() {
		drawEngine_.Flush();
	}
	// void ApplyDrawState(int prim);
	void CheckFlushOp(int cmd, u32 diff);
	void BuildReportingInfo();

	void InitClearInternal() override;
	void BeginFrameInternal() override;
	void CopyDisplayToOutputInternal() override;

	FramebufferManagerDX9 *framebufferManagerDX9_;
	TextureCacheDX9 *textureCacheDX9_;
	DepalShaderCacheDX9 depalShaderCache_;
	DrawEngineDX9 drawEngine_;
	ShaderManagerDX9 *shaderManagerDX9_;

	static CommandInfo cmdInfo_[256];

	int lastVsync_;

	std::string reportingPrimaryInfo_;
	std::string reportingFullInfo_;

	GraphicsContext *gfxCtx_;
};

}  // namespace DX9

typedef DX9::GPU_DX9 DIRECTX9_GPU;
