///

#include "Core/Reporting.h"
#include "Core/CoreParameter.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/Config.h"
#include "Common/ChunkFile.h"

#include "GPU/GPUState.h"

// Reuse some components of the old GPU emulation
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/Framebuffer.h"

#include "GPU/High/HighGpu.h"
#include "GPU/High/GLES/HighGLES.h"
#include "GPU/High/GLES/TextureCacheHighGLES.h"
#include "GPU/High/GLES/VertShaderGenGLES.h"
#include "GPU/High/GLES/FragShaderGenGLES.h"
#include "GPU/High/GLES/ShaderManagerHighGLES.h"
#include "GPU/High/Command.h"
#include "GPU/Common/IndexGenerator.h"

#include "native/gfx_es2/gl_state.h"

namespace HighGpu {

enum {
	VTXDEC_BUFSIZE = 1024 * 1024 * 4,
	VTXDEC_IBUFSIZE = 1024 * 1024,
};

HighGpu_GLES::HighGpu_GLES() : resized_(false), dumpNextFrame_(false), dumpThisFrame_(false) {
	shaderManager_ = new ShaderManagerGLES();
	framebufferManager_ = new FramebufferManager();
	textureCache_ = new TextureCacheGLES();

	framebufferManager_->Init();
	framebufferManager_->SetTextureCache(textureCache_);

	vertexDecodeBuf_ = new u8[VTXDEC_BUFSIZE];
	memset(vertexDecodeBuf_, 0, VTXDEC_BUFSIZE);
	indexDecodeBuf_ = new u8[VTXDEC_IBUFSIZE];
	memset(indexDecodeBuf_, 0, VTXDEC_IBUFSIZE);
	/*
	framebufferManager_->SetShaderManager(shaderManager_);
	framebufferManager_->SetTransformDrawEngine(&transformDraw_);
	textureCache_.SetFramebufferManager(&framebufferManager_);
	textureCache_.SetDepalShaderCache(&depalShaderCache_);
	textureCache_.SetShaderManager(shaderManager_);
	fragmentTestCache_.SetTextureCache(&textureCache_);
	*/
	BuildReportingInfo();
}

HighGpu_GLES::~HighGpu_GLES() {
	framebufferManager_->DestroyAllFBOs();
	shaderManager_->ClearCache(true);
	depalShaderCache_.Clear();
	fragmentTestCache_.Clear();
	delete shaderManager_;
	delete framebufferManager_;
	delete [] vertexDecodeBuf_;
	delete [] indexDecodeBuf_;
	glstate.SetVSyncInterval(0);
}

void HighGpu_GLES::Execute(CommandPacket *packet) {
	PrintCommandPacket(packet);

	// We do things in multiple passes, in order to maximize CPU cache efficiency.
	//
	// First, we translate all state objects that need translation, create textures, and so on.
	for (int i = 0; i < packet->numTexture; i++) {
		// textures[i] = textureCache->GetTexture(packet->texture[i]);
	}

	// Then, we do a first pass through the draw commands, and identify ranges we would like to deal with.
	// Block transfers get separated to their own ranges. We generate and set up shaders.
	for (int i = 0; i < packet->numCommands; i++) {

	}

	// Now for each range we could look it up in a cache, to see if we have decoded it before.

	//
	// First, we decode all the vertex data, and build bone matrix sets.
	//
	// Then, we create all framebuffers and textures (or check that they exist).
	// We also look up shaders.
	//
	// Only then do we submit any draw calls, but what we submit will be very efficient.
	//
	// Note that this architecture means that there's a lot here that can be parallelized
	// as each draw call is completely independent from each other, up until the last step
	// where we actually go through and submit the draw calls. Candidates for parallelization
	// are probably texture and vertex decoding.
	//
	// Pass 1: Decode all the textures. This is done first so that the GL driver can
	// upload them in the background (if it's that advanced) while we are decoding vertex
	// data and preparing the draw calls.

	int start = 0;
	int end = packet->numCommands;
	// Local mirror arrays. Should possible put it all in a struct instead and attach it
	// using the userdata pointer.
	TexCacheEntry *tex[ARRAY_SIZE(packet->texture)];

	for (int i = 0; i < packet->numTexture; i++) {
		const TextureState *texState = packet->texture[i];
		const ClutState *clut = texState->clutStateIndex == INVALID_STATE ? nullptr : packet->clut[texState->clutStateIndex];
		tex[i] = textureCache_->GetTexture(texState, clut);
	}

	short primCount[8] = {0};
	short idxCount[4] = {0};
	for (int i = start; i < end; i++) {
		const Command *cmd = &packet->commands[i];
		if (cmd->type != CMD_DRAWPRIM) {
			continue;
		}
		primCount[cmd->draw.prim]++;
		idxCount[(cmd->draw.vtxformat & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT]++;
		if (!(cmd->draw.enabled & ENABLE_TEXTURE))
			continue;
	}

	// bool softwareTransform = primCount[GE_PRIM_RECTANGLES] > 0;

	// Pass 2: Allocate a buffer and decode all the vertex data into it.
	for (int i = start; i < end; i++) {
		const Command *cmd = &packet->commands[i];
		if (cmd->type != CMD_DRAWPRIM)
			continue;

		// VertexDecoder *dec = GetVertexDecoder(cmd->draw.vtxformat);
		if (cmd->draw.vtxformat & GE_VTYPE_IDX_NONE) {
			// Decode it all.
			// dec->DecodeVerts(
		}
	}

	// As shaders depend on the decoded data (vertexFullAlpha, textureFullAlpha) we simply
	// fetch the shaders after that work is done.

	ShaderID fragShaderIds[256];
	ShaderID vertShaderIds[256];

	LinkedShader *linkedShaders[256];

	// TODO: Reuse work wherever possible.
	for (int i = start; i < end; i++) {
		const Command *cmd = &packet->commands[i];
		u32 enabled = cmd->draw.enabled;
		// TODO: Fix the lights
		const LightState *ls[4] = {nullptr, nullptr, nullptr, nullptr};
		const LightState **lss = ls;
		bool useHWTransform = cmd->draw.prim != GE_PRIM_RECTANGLES;
		// TODO: Fix these four:
		bool allowShaderBlend = false;
		bool flipTexture = false;
		bool vertexFullAlpha = false;
		bool textureFullAlpha = false;
		ComputeVertexShaderID(&vertShaderIds[i], enabled, cmd->draw.vtxformat,
			packet->raster[cmd->draw.raster],
			cmd->draw.texScale == INVALID_STATE ? nullptr : packet->texScale[cmd->draw.texScale],
			cmd->draw.lightGlobal == INVALID_STATE ? nullptr : packet->lightGlobal[cmd->draw.lightGlobal],
			lss,
			false,
			useHWTransform);
		ComputeFragmentShaderID(&fragShaderIds[i], enabled, cmd->draw.vtxformat,
			packet->framebuf[cmd->draw.framebuf],
			packet->raster[cmd->draw.raster],
			packet->fragment[cmd->draw.fragment],
			cmd->draw.texScale == INVALID_STATE ? nullptr : packet->texScale[cmd->draw.texScale],
			cmd->draw.blend == INVALID_STATE ? nullptr : packet->blend[cmd->draw.blend],
			cmd->draw.depthStencil == INVALID_STATE ? nullptr : packet->depthStencil[cmd->draw.depthStencil],
			allowShaderBlend, flipTexture, vertexFullAlpha, textureFullAlpha);
	}

	for (int i = start; i < end; i++) {
		linkedShaders[i] = shaderManager_->GetLinkedShader(packet->commands[i].draw.vtxformat, vertShaderIds[i], fragShaderIds[i]);
	}

	// Set up the vertex pointers.

	// Pass 3: Fetch shaders, perform the draws.
	int curFramebuf = -1;
	LinkedShader *last = nullptr;

	for (int i = start; i < end; i++) {
		const Command *cmd = &packet->commands[i];
		if (curFramebuf != cmd->draw.framebuf) {
			ApplyFramebuffer(packet, cmd);
			curFramebuf = cmd->draw.framebuf;
		}
		if (linkedShaders[i] != last) {
			// linkedShaders[i]->Bind();
			last = linkedShaders[i];
		}

		// glDraw...
	}
}

void HighGpu_GLES::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	framebufferManager_->SetDisplayFramebuffer(framebuf, stride, format);
}

VertexDecoder *HighGpu_GLES::GetVertexDecoder(u32 vtype) {
	auto iter = decoderMap_.find(vtype);
	if (iter != decoderMap_.end())
		return iter->second;
	VertexDecoder *dec = new VertexDecoder();
	dec->SetVertexType(vtype, decOptions_, decJitCache_);
	decoderMap_[vtype] = dec;
	return dec;
}

// So much data to fetch to make the heuristic happy...
void HighGpu_GLES::ApplyFramebuffer(const CommandPacket *packet, const Command *cmd) {
	FramebufferHeuristicParams fb;
	const FramebufState *fbState = packet->framebuf[cmd->draw.framebuf];
	const RasterState *raster = packet->raster[cmd->draw.raster];
	fb.fb_address = fbState->colorPtr;
	fb.fb_stride = fbState->colorStride;
	fb.fb_addr = fb.fb_address;
	fb.z_address = fbState->depthPtr;
	fb.z_stride = fbState->depthStride;
	fb.fmt = (GEBufferFormat)(fbState->colorFormat);
	// We always load the viewport. It's too important for fbo heuristics to leave out,
	// even when not strictly needed.
	ViewportState *vs = packet->viewport[cmd->draw.viewport];
	fb.viewportWidth = 2.0 * vs->xScale;
	fb.viewportHeight = 2.0 * vs->yScale;
	fb.regionWidth = vs->regionX2 - vs->regionX1 + 1;
	fb.regionHeight = vs->regionY2 - vs->regionY1 + 1;
	fb.scissorWidth = raster->scissorX2 - raster->scissorX1 + 1;
	fb.scissorHeight = raster->scissorY2 - raster->scissorY1 + 1;
	fb.isDrawing = true;  // TODO
	fb.isClearingDepth = false;  // TODO
	fb.isWritingDepth = true;
	fb.isModeThrough = true;

	framebufferManager_->DoSetRenderFrameBuffer(fb, gstate_c.skipDrawReason);
}

// Let's avoid passing nulls into snprintf().
static const char *GetGLStringAlways(GLenum name) {
	const GLubyte *value = glGetString(name);
	if (!value)
		return "?";
	return (const char *)value;
}

// Needs to be called on GPU thread, not reporting thread.
void HighGpu_GLES::BuildReportingInfo() {
	const char *glVendor = GetGLStringAlways(GL_VENDOR);
	const char *glRenderer = GetGLStringAlways(GL_RENDERER);
	const char *glVersion = GetGLStringAlways(GL_VERSION);
	const char *glSlVersion = GetGLStringAlways(GL_SHADING_LANGUAGE_VERSION);
	const char *glExtensions = GetGLStringAlways(GL_EXTENSIONS);

	char temp[16384];
	snprintf(temp, sizeof(temp), "%s (%s %s), %s (extensions: %s)", glVersion, glVendor, glRenderer, glSlVersion, glExtensions);
	reportingPrimaryInfo_ = glVendor;
	reportingFullInfo_ = temp;

	Reporting::UpdateConfig();
}

void HighGpu_GLES::GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) {
	primaryInfo = reportingPrimaryInfo_;
	fullInfo = reportingFullInfo_;
}

void HighGpu_GLES::UpdateStats() {
	// gpuStats.numVertexShaders = shaderManager_->NumVertexShaders();
	// gpuStats.numFragmentShaders = shaderManager_->NumFragmentShaders();
	// gpuStats.numShaders = shaderManager_->NumPrograms();
	// gpuStats.numTextures = (int)textureCache_->NumLoadedTextures();
	// gpuStats.numFBOs = (int)framebufferManager_->NumVFBs();
}

void HighGpu_GLES::DeviceLost() {
	// TODO: Figure out sync. Which thread does this call come on?

	// Simply drop all caches and textures.
	// FBOs appear to survive? Or no?
	// TransformDraw has registered as a GfxResourceHolder.
	shaderManager_->ClearCache(false);
	textureCache_->Clear(false);
	fragmentTestCache_.Clear(false);
	depalShaderCache_.Clear();
	framebufferManager_->DeviceLost();

	UpdateVsyncInterval(true);
}

void HighGpu_GLES::BeginFrameInternal() {
	UpdateVsyncInterval(resized_);
	resized_ = false;

	textureCache_->StartFrame();
	depalShaderCache_.Decimate();

	if (dumpNextFrame_) {
		NOTICE_LOG(G3D, "DUMPING THIS FRAME");
		dumpThisFrame_ = true;
		dumpNextFrame_ = false;
	} else if (dumpThisFrame_) {
		dumpThisFrame_ = false;
	}
	shaderManager_->DirtyShader();
	framebufferManager_->BeginFrame();
}

bool HighGpu_GLES::ProcessEvent(GPUEvent ev) {
	switch (ev.type) {
	case GPU_EVENT_INIT_CLEAR:
		InitClearInternal();
		break;

	case GPU_EVENT_BEGIN_FRAME:
		BeginFrameInternal();
		break;

	case GPU_EVENT_COPY_DISPLAY_TO_OUTPUT:
		CopyDisplayToOutputInternal();
		break;

	case GPU_EVENT_INVALIDATE_CACHE:
		InvalidateCacheInternal(ev.invalidate_cache.addr, ev.invalidate_cache.size, ev.invalidate_cache.type);
		break;

	case GPU_EVENT_FB_MEMCPY:
		PerformMemoryCopyInternal(ev.fb_memcpy.dst, ev.fb_memcpy.src, ev.fb_memcpy.size);
		break;

	case GPU_EVENT_FB_MEMSET:
		PerformMemorySetInternal(ev.fb_memset.dst, ev.fb_memset.v, ev.fb_memset.size);
		break;

	case GPU_EVENT_FB_STENCIL_UPLOAD:
		PerformStencilUploadInternal(ev.fb_stencil_upload.dst, ev.fb_stencil_upload.size);
		break;

	case GPU_EVENT_REINITIALIZE:
		ReinitializeInternal();
		break;
	default:
		return false;
	}
	return true;
}

void HighGpu_GLES::ReinitializeInternal() {
	textureCache_->Clear(true);
	depalShaderCache_.Clear();
	framebufferManager_->DestroyAllFBOs();
	framebufferManager_->Resized();
}

void HighGpu_GLES::InitClearInternal() {
	bool useNonBufferedRendering = g_Config.iRenderingMode == FB_NON_BUFFERED_MODE;
	if (useNonBufferedRendering) {
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
}

void HighGpu_GLES::InvalidateCacheInternal(u32 addr, int size, GPUInvalidationType type) {
	if (size > 0)
		textureCache_->Invalidate(addr, size, type);
	else
		textureCache_->InvalidateAll(type);

	if (type != GPU_INVALIDATE_ALL && framebufferManager_->MayIntersectFramebuffer(addr)) {
		// If we're doing block transfers, we shouldn't need this, and it'll only confuse us.
		// Vempire invalidates (with writeback) after drawing, but before blitting.
		if (!g_Config.bBlockTransferGPU || type == GPU_INVALIDATE_SAFE) {
			framebufferManager_->UpdateFromMemory(addr, size, type == GPU_INVALIDATE_SAFE);
		}
	}
}

void HighGpu_GLES::PerformMemoryCopyInternal(u32 dest, u32 src, int size) {
	if (!framebufferManager_->NotifyFramebufferCopy(src, dest, size, false, gstate_c.skipDrawReason)) {
		// We use a little hack for Download/Upload using a VRAM mirror.
		// Since they're identical we don't need to copy.
		if (!Memory::IsVRAMAddress(dest) || (dest ^ 0x00400000) != src) {
			Memory::Memcpy(dest, src, size);
		}
	}
	InvalidateCacheInternal(dest, size, GPU_INVALIDATE_HINT);
}

void HighGpu_GLES::PerformMemorySetInternal(u32 dest, u8 v, int size) {
	if (!framebufferManager_->NotifyFramebufferCopy(dest, dest, size, true, gstate_c.skipDrawReason)) {
		InvalidateCacheInternal(dest, size, GPU_INVALIDATE_HINT);
	}
}

void HighGpu_GLES::PerformStencilUploadInternal(u32 dest, int size) {
	framebufferManager_->NotifyStencilUpload(dest, size);
}

void HighGpu_GLES::CopyDisplayToOutputInternal() {
	// Flush anything left over.

	framebufferManager_->RebindFramebuffer();

	shaderManager_->DirtyLastShader();

	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	framebufferManager_->CopyDisplayToOutput();
	framebufferManager_->EndFrame();

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

void HighGpu_GLES::UpdateVsyncInterval(bool force) {
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
			glstate.SetVSyncInterval(desiredVSyncInterval);
		//}
		lastVsync_ = desiredVSyncInterval;
	}
#endif
}

void HighGpu_GLES::DoState(PointerWrap &p) {
	// TODO: Some of these things may not be necessary.
	// None of these are necessary when saving.
	// In Freeze-Frame mode, we don't want to do any of this.
	if (p.mode == p.MODE_READ && !PSP_CoreParameter().frozen) {
		textureCache_->Clear(true);
		depalShaderCache_.Clear();
		// transformDraw_.ClearTrackedVertexArrays();

		gstate_c.textureChanged = TEXCHANGE_UPDATED;
		framebufferManager_->DestroyAllFBOs();
		shaderManager_->ClearCache(true);
	}
}

}  // namespace HighGpu
