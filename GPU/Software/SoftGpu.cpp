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

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "Common/ColorConv.h"
#include "Common/GraphicsContext.h"
#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "profiler/profiler.h"
#include "thin3d/thin3d.h"

#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Common/FramebufferCommon.h"

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;
FormatBuffer fb;
FormatBuffer depthbuf;
u32 clut[4096];

static Draw::SamplerState *samplerNearest = nullptr;
static Draw::SamplerState *samplerLinear = nullptr;
static Draw::Buffer *vdata = nullptr;
static Draw::Buffer *idata = nullptr;

SoftGPU::SoftGPU(GraphicsContext *gfxCtx, Draw::DrawContext *_thin3D)
	: gfxCtx_(gfxCtx), thin3d(_thin3D)
{
	using namespace Draw;
	fbTex = thin3d->CreateTexture(LINEAR2D, DataFormat::R8G8B8A8_UNORM, 480, 272, 1, 1);

	InputLayoutDesc desc = {
		{
			{ 24, false },
		},
		{
			{ 0, SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
			{ 0, SEM_COLOR0, DataFormat::R32G32B32_FLOAT, 20 },
		},
	};

	ShaderModule *vshader = thin3d->GetVshaderPreset(VS_TEXTURE_COLOR_2D);

	vdata = thin3d->CreateBuffer(24 * 4, BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);
	idata = thin3d->CreateBuffer(sizeof(int) * 6, BufferUsageFlag::DYNAMIC | BufferUsageFlag::INDEXDATA);

	InputLayout *inputLayout = thin3d->CreateInputLayout(desc);
	DepthStencilState *depth = thin3d->CreateDepthStencilState({ false, false, Comparison::LESS });
	BlendState *blendstateOff = thin3d->CreateBlendState({ false, 0xF });
	RasterState *rasterNoCull = thin3d->CreateRasterState({});

	samplerNearest = thin3d->CreateSamplerState({ TextureFilter::NEAREST, TextureFilter::NEAREST, TextureFilter::NEAREST });
	samplerLinear = thin3d->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR });

	PipelineDesc pipelineDesc{
		Primitive::TRIANGLE_LIST,
		{ thin3d->GetVshaderPreset(VS_TEXTURE_COLOR_2D), thin3d->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
		inputLayout, depth, blendstateOff, rasterNoCull
	};
	texColor = thin3d->CreateGraphicsPipeline(pipelineDesc);

	fb.data = Memory::GetPointer(0x44000000); // TODO: correct default address?
	depthbuf.data = Memory::GetPointer(0x44000000); // TODO: correct default address?

	framebufferDirty_ = true;
	// TODO: Is there a default?
	displayFramebuf_ = 0;
	displayStride_ = 512;
	displayFormat_ = GE_FORMAT_8888;
}

void SoftGPU::DeviceLost() {
	// Handled by thin3d.
}

void SoftGPU::DeviceRestore() {
	// Handled by thin3d.
}

SoftGPU::~SoftGPU() {
	texColor->Release();
	texColor = nullptr;

	fbTex->Release();
	fbTex = nullptr;

	vdata->Release();
	vdata = nullptr;
	idata->Release();
	idata = nullptr;
	samplerNearest->Release();
	samplerNearest = nullptr;
	samplerLinear->Release();
	samplerLinear = nullptr;
}

void SoftGPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	// Seems like this can point into RAM, but should be VRAM if not in RAM.
	displayFramebuf_ = (framebuf & 0xFF000000) == 0 ? 0x44000000 | framebuf : framebuf;
	displayStride_ = stride;
	displayFormat_ = format;
	host->GPUNotifyDisplay(framebuf, stride, format);
}

// Copies RGBA8 data from RAM to the currently bound render target.
void SoftGPU::CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight) {
	using namespace Draw;

	if (!thin3d)
		return;
	float dstwidth = (float)PSP_CoreParameter().pixelWidth;
	float dstheight = (float)PSP_CoreParameter().pixelHeight;

	Viewport viewport = {0.0f, 0.0f, dstwidth, dstheight, 0.0f, 1.0f};
	thin3d->SetViewports(1, &viewport);
	SamplerState *sampler;
	if (g_Config.iBufFilter == SCALE_NEAREST) {
		sampler = samplerNearest;
	} else {
		sampler = samplerLinear;
	}
	thin3d->BindSamplerStates(0, 1, &sampler);
	thin3d->SetScissorRect(0, 0, dstwidth, dstheight);

	float u0 = 0.0f;
	float u1;
	if (!Memory::IsValidAddress(displayFramebuf_)) {
		u8 data[] = {0, 0, 0, 0};
		fbTex->SetImageData(0, 0, 0, 1, 1, 1, 0, 4, data);
		u1 = 1.0f;
	} else if (displayFormat_ == GE_FORMAT_8888) {
		u8 *data = Memory::GetPointer(displayFramebuf_);
		fbTex->SetImageData(0, 0, 0, displayStride_, srcheight, 1, 0, displayStride_ * 4, data);
		u1 = (float)srcwidth / displayStride_;
	} else {
		// TODO: This should probably be converted in a shader instead..
		fbTexBuffer.resize(srcwidth * srcheight);
		FormatBuffer displayBuffer;
		displayBuffer.data = Memory::GetPointer(displayFramebuf_);
		for (int y = 0; y < srcheight; ++y) {
			u32 *buf_line = &fbTexBuffer[y * srcwidth];
			const u16 *fb_line = &displayBuffer.as16[y * displayStride_];

			switch (displayFormat_) {
			case GE_FORMAT_565:
				ConvertRGBA565ToRGBA8888(buf_line, fb_line, srcwidth);
				break;

			case GE_FORMAT_5551:
				ConvertRGBA5551ToRGBA8888(buf_line, fb_line, srcwidth);
				break;

			case GE_FORMAT_4444:
				ConvertRGBA4444ToRGBA8888(buf_line, fb_line, srcwidth);
				break;

			default:
				ERROR_LOG_REPORT(G3D, "Software: Unexpected framebuffer format: %d", displayFormat_);
			}
		}

		fbTex->SetImageData(0, 0, 0, srcwidth, srcheight, 1, 0, srcwidth * 4, (const uint8_t *)&fbTexBuffer[0]);
		u1 = 1.0f;
	}
	fbTex->Finalize(0);

	float x, y, w, h;
	CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, dstwidth, dstheight, ROTATION_LOCKED_HORIZONTAL);

	if (GetGPUBackend() == GPUBackend::DIRECT3D9) {
		x += 0.5f;
		y += 0.5f;
	}

	x /= 0.5f * dstwidth;
	y /= 0.5f * dstheight;
	w /= 0.5f * dstwidth;
	h /= 0.5f * dstheight;
	float x2 = x + w;
	float y2 = y + h;
	x -= 1.0f;
	y -= 1.0f;
	x2 -= 1.0f;
	y2 -= 1.0f;

	struct Vertex {
		float x, y, z;
		float u, v;
		uint32_t rgba;
	};

	float v0 = 1.0f;
	float v1 = 0.0f;

	if (GetGPUBackend() == GPUBackend::VULKAN) {
		std::swap(v0, v1);
	}

	const Vertex verts[4] = {
		{x, y, 0,    u0, v0,  0xFFFFFFFF}, // TL
		{x, y2, 0,   u0, v1,  0xFFFFFFFF}, // BL
		{x2, y2, 0,  u1, v1,  0xFFFFFFFF}, // BR
		{x2, y, 0,   u1, v0,  0xFFFFFFFF}, // TR
	};
	vdata->SetData((const uint8_t *)verts, sizeof(verts));

	int indexes[] = {0, 1, 2, 0, 2, 3};
	idata->SetData((const uint8_t *)indexes, sizeof(indexes));

	thin3d->BindTexture(0, fbTex);

	static const float identity4x4[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	};

	texColor->SetMatrix4x4("WorldViewProj", identity4x4);
	thin3d->BindPipeline(texColor);
	thin3d->DrawIndexed(vdata, idata, 6, 0);
}

void SoftGPU::CopyDisplayToOutput()
{
	ScheduleEvent(GPU_EVENT_COPY_DISPLAY_TO_OUTPUT);
}

void SoftGPU::CopyDisplayToOutputInternal()
{
	// The display always shows 480x272.
	CopyToCurrentFboFromDisplayRam(FB_WIDTH, FB_HEIGHT);
	framebufferDirty_ = false;
}

void SoftGPU::ProcessEvent(GPUEvent ev) {
	switch (ev.type) {
	case GPU_EVENT_COPY_DISPLAY_TO_OUTPUT:
		CopyDisplayToOutputInternal();
		break;

	default:
		GPUCommon::ProcessEvent(ev);
	}
}

void SoftGPU::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("soft_runloop");
	for (; downcount > 0; --downcount) {
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		u32 cmd = op >> 24;

		u32 diff = op ^ gstate.cmdmem[cmd];
		gstate.cmdmem[cmd] = op;
		ExecuteOp(op, diff);

		list.pc += 4;
	}
}

int EstimatePerVertexCost() {
	// TODO: This is transform cost, also account for rasterization cost somehow... although it probably
	// runs in parallel with transform.

	// Also, this is all pure guesswork. If we can find a way to do measurements, that would be great.

	// GTA wants a low value to run smooth, GoW wants a high value (otherwise it thinks things
	// went too fast and starts doing all the work over again).

	int cost = 20;
	if (gstate.isLightingEnabled()) {
		cost += 10;
	}

	for (int i = 0; i < 4; i++) {
		if (gstate.isLightChanEnabled(i))
			cost += 10;
	}
	if (gstate.getUVGenMode() != GE_TEXMAP_TEXTURE_COORDS) {
		cost += 20;
	}
	// TODO: morphcount

	return cost;
}

void SoftGPU::ExecuteOp(u32 op, u32 diff)
{
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_BASE:
		break;

	case GE_CMD_VADDR:
		gstate_c.vertexAddr = gstate_c.getRelativeAddress(data);
		break;

	case GE_CMD_IADDR:
		gstate_c.indexAddr	= gstate_c.getRelativeAddress(data);
		break;

	case GE_CMD_PRIM:
		{
			u32 count = data & 0xFFFF;
			u32 type = data >> 16;

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Software: Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *verts = Memory::GetPointer(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Software: Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			cyclesExecuted += EstimatePerVertexCost() * count;
			int bytesRead;
			TransformUnit::SubmitPrimitive(verts, indices, type, count, gstate.vertType, &bytesRead);
			framebufferDirty_ = true;

			// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
			// Some games rely on this, they don't bother reloading VADDR and IADDR.
			// The VADDR/IADDR registers are NOT updated.
			AdvanceVerts(gstate.vertType, count, bytesRead);
		}
		break;

	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			DEBUG_LOG(G3D,"DL DRAW BEZIER: %i x %i", bz_ucount, bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Software: Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *control_points = Memory::GetPointer(gstate_c.vertexAddr);
			// void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Software: Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				// indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			if (gstate.getPatchPrimitiveType() != GE_PATCHPRIM_TRIANGLES) {
				ERROR_LOG_REPORT(G3D, "Software: Unsupported patch primitive %x", gstate.patchprimitive&3);
				break;
			}

			if (!(gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME)) {
				//TransformUnit::SubmitSpline(control_points, indices, sp_ucount, sp_vcount, sp_utype, sp_vtype, gstate.getPatchPrimitiveType(), gstate.vertType);
			}
			framebufferDirty_ = true;
			DEBUG_LOG(G3D,"DL DRAW SPLINE: %i x %i, %i x %i", sp_ucount, sp_vcount, sp_utype, sp_vtype);
		}
		break;

	case GE_CMD_BOUNDINGBOX:
		if (data != 0)
			DEBUG_LOG(G3D, "Unsupported bounding box: %06x", data);
		// bounding box test. Let's assume the box was within the drawing region.
		currentList->bboxResult = true;
		break;

	case GE_CMD_VERTEXTYPE:
		break;

	case GE_CMD_REGION1:
	case GE_CMD_REGION2:
		break;

	case GE_CMD_CLIPENABLE:
		break;

	case GE_CMD_CULLFACEENABLE:
	case GE_CMD_CULL:
		break;

	case GE_CMD_TEXTUREMAPENABLE: 
		break;

	case GE_CMD_LIGHTINGENABLE:
		break;

	case GE_CMD_FOGCOLOR:
	case GE_CMD_FOG1:
	case GE_CMD_FOG2:
	case GE_CMD_FOGENABLE:
		break;

	case GE_CMD_DITHERENABLE:
		break;

	case GE_CMD_OFFSETX:
		break;

	case GE_CMD_OFFSETY:
		break;

	case GE_CMD_TEXSCALEU:
		gstate_c.uv.uScale = getFloat24(data);
		break;

	case GE_CMD_TEXSCALEV:
		gstate_c.uv.vScale = getFloat24(data);
		break;

	case GE_CMD_TEXOFFSETU:
		gstate_c.uv.uOff = getFloat24(data);
		break;

	case GE_CMD_TEXOFFSETV:
		gstate_c.uv.vOff = getFloat24(data);
		break;

	case GE_CMD_SCISSOR1:
	case GE_CMD_SCISSOR2:
		break;

	case GE_CMD_MINZ:
		break;

	case GE_CMD_FRAMEBUFPTR:
		fb.data = Memory::GetPointer(gstate.getFrameBufAddress());
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		fb.data = Memory::GetPointer(gstate.getFrameBufAddress());
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		break;

	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		break;

	case GE_CMD_CLUTADDR:
	case GE_CMD_CLUTADDRUPPER:
		break;

	case GE_CMD_LOADCLUT:
		{
			u32 clutAddr = gstate.getClutAddress();
			u32 clutTotalBytes = gstate.getClutLoadBytes();

			if (Memory::IsValidAddress(clutAddr)) {
				u32 validSize = Memory::ValidSize(clutAddr, clutTotalBytes);
				Memory::MemcpyUnchecked(clut, clutAddr, validSize);
				if (validSize < clutTotalBytes) {
					// Zero out the parts that were outside valid memory.
					memset((u8 *)clut + validSize, 0x00, clutTotalBytes - validSize);
				}
			} else if (clutAddr != 0) {
				// Some invalid addresses trigger a crash, others fill with zero.  We always fill zero.
				DEBUG_LOG(G3D, "Software: Invalid CLUT address, filling with garbage instead of crashing");
				memset(clut, 0x00, clutTotalBytes);
			}
		}
		break;

	// Don't need to do anything, just state for transferstart.
	case GE_CMD_TRANSFERSRC:
	case GE_CMD_TRANSFERSRCW:
	case GE_CMD_TRANSFERDST:
	case GE_CMD_TRANSFERDSTW:
	case GE_CMD_TRANSFERSRCPOS:
	case GE_CMD_TRANSFERDSTPOS:
	case GE_CMD_TRANSFERSIZE:
		break;

	case GE_CMD_TRANSFERSTART:
		{
			u32 srcBasePtr = gstate.getTransferSrcAddress();
			u32 srcStride = gstate.getTransferSrcStride();

			u32 dstBasePtr = gstate.getTransferDstAddress();
			u32 dstStride = gstate.getTransferDstStride();

			int srcX = gstate.getTransferSrcX();
			int srcY = gstate.getTransferSrcY();

			int dstX = gstate.getTransferDstX();
			int dstY = gstate.getTransferDstY();

			int width = gstate.getTransferWidth();
			int height = gstate.getTransferHeight();

			int bpp = gstate.getTransferBpp();

			DEBUG_LOG(G3D, "Block transfer: %08x/%x -> %08x/%x, %ix%ix%i (%i,%i)->(%i,%i)", srcBasePtr, srcStride, dstBasePtr, dstStride, width, height, bpp, srcX, srcY, dstX, dstY);

			for (int y = 0; y < height; y++) {
				const u8 *src = Memory::GetPointer(srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp);
				u8 *dst = Memory::GetPointer(dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp);
				memcpy(dst, src, width * bpp);
			}

#ifndef MOBILE_DEVICE
			CBreakPoints::ExecMemCheck(srcBasePtr + (srcY * srcStride + srcX) * bpp, false, height * srcStride * bpp, currentMIPS->pc);
			CBreakPoints::ExecMemCheck(dstBasePtr + (srcY * dstStride + srcX) * bpp, true, height * dstStride * bpp, currentMIPS->pc);
#endif

			// TODO: Correct timing appears to be 1.9, but erring a bit low since some of our other timing is inaccurate.
			cyclesExecuted += ((height * width * bpp) * 16) / 10;

			// Could theoretically dirty the framebuffer.
			framebufferDirty_ = true;
			break;
		}

	case GE_CMD_TEXSIZE0:
	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		break;

	case GE_CMD_ZBUFPTR:
		depthbuf.data = Memory::GetPointer(gstate.getDepthBufAddress());
		break;

	case GE_CMD_ZBUFWIDTH:
		depthbuf.data = Memory::GetPointer(gstate.getDepthBufAddress());
		break;

	case GE_CMD_AMBIENTCOLOR:
	case GE_CMD_AMBIENTALPHA:
	case GE_CMD_MATERIALAMBIENT:
	case GE_CMD_MATERIALDIFFUSE:
	case GE_CMD_MATERIALEMISSIVE:
	case GE_CMD_MATERIALSPECULAR:
	case GE_CMD_MATERIALALPHA:
	case GE_CMD_MATERIALSPECULARCOEF:
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		break;

	case GE_CMD_LX0:case GE_CMD_LY0:case GE_CMD_LZ0:
	case GE_CMD_LX1:case GE_CMD_LY1:case GE_CMD_LZ1:
	case GE_CMD_LX2:case GE_CMD_LY2:case GE_CMD_LZ2:
	case GE_CMD_LX3:case GE_CMD_LY3:case GE_CMD_LZ3:
		break;

	case GE_CMD_LDX0:case GE_CMD_LDY0:case GE_CMD_LDZ0:
	case GE_CMD_LDX1:case GE_CMD_LDY1:case GE_CMD_LDZ1:
	case GE_CMD_LDX2:case GE_CMD_LDY2:case GE_CMD_LDZ2:
	case GE_CMD_LDX3:case GE_CMD_LDY3:case GE_CMD_LDZ3:
		break;

	case GE_CMD_LKA0:case GE_CMD_LKB0:case GE_CMD_LKC0:
	case GE_CMD_LKA1:case GE_CMD_LKB1:case GE_CMD_LKC1:
	case GE_CMD_LKA2:case GE_CMD_LKB2:case GE_CMD_LKC2:
	case GE_CMD_LKA3:case GE_CMD_LKB3:case GE_CMD_LKC3:
		break;

	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		break;

	case GE_CMD_VIEWPORTXSCALE:
	case GE_CMD_VIEWPORTYSCALE:
	case GE_CMD_VIEWPORTZSCALE:
	case GE_CMD_VIEWPORTXCENTER:
	case GE_CMD_VIEWPORTYCENTER:
	case GE_CMD_VIEWPORTZCENTER:
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		break;

	case GE_CMD_LIGHTMODE:
		break;

	case GE_CMD_PATCHDIVISION:
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
		break;

	case GE_CMD_BLENDMODE:
		break;

	case GE_CMD_BLENDFIXEDA:
	case GE_CMD_BLENDFIXEDB:
		break;

	case GE_CMD_ALPHATESTENABLE:
	case GE_CMD_ALPHATEST:
		break;

	case GE_CMD_TEXFUNC:
	case GE_CMD_TEXFILTER:
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
	case GE_CMD_STENCILTESTENABLE:
	case GE_CMD_ZTEST:
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
		gstate.worldmtxnum = data & 0xF;
		break;

	case GE_CMD_WORLDMATRIXDATA:
		{
			int num = gstate.worldmtxnum & 0xF;
			if (num < 12) {
				gstate.worldMatrix[num] = getFloat24(data);
			}
			gstate.worldmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		gstate.viewmtxnum = data & 0xF;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		{
			int num = gstate.viewmtxnum & 0xF;
			if (num < 12) {
				gstate.viewMatrix[num] = getFloat24(data);
			}
			gstate.viewmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		gstate.projmtxnum = data & 0xF;
		break;

	case GE_CMD_PROJMATRIXDATA:
		{
			int num = gstate.projmtxnum & 0xF;
			gstate.projMatrix[num] = getFloat24(data);
			gstate.projmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		gstate.texmtxnum = data&0xF;
		break;

	case GE_CMD_TGENMATRIXDATA:
		{
			int num = gstate.texmtxnum & 0xF;
			if (num < 12) {
				gstate.tgenMatrix[num] = getFloat24(data);
			}
			gstate.texmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		gstate.boneMatrixNumber = data & 0x7F;
		break;

	case GE_CMD_BONEMATRIXDATA:
		{
			int num = gstate.boneMatrixNumber & 0x7F;
			if (num < 96) {
				gstate.boneMatrix[num] = getFloat24(data);
			}
			gstate.boneMatrixNumber = (++num) & 0x7F;
		}
		break;

	default:
		GPUCommon::ExecuteOp(op, diff);
		break;
	}
}

void SoftGPU::GetStats(char *buffer, size_t bufsize) {
	snprintf(buffer, bufsize, "SoftGPU: (N/A)");
}

void SoftGPU::InvalidateCache(u32 addr, int size, GPUInvalidationType type)
{
	// Nothing to invalidate.
}

void SoftGPU::NotifyVideoUpload(u32 addr, int size, int width, int format)
{
	// Ignore.
}

bool SoftGPU::PerformMemoryCopy(u32 dest, u32 src, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	// Let's just be safe.
	framebufferDirty_ = true;
	return false;
}

bool SoftGPU::PerformMemorySet(u32 dest, u8 v, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	// Let's just be safe.
	framebufferDirty_ = true;
	return false;
}

bool SoftGPU::PerformMemoryDownload(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool SoftGPU::PerformMemoryUpload(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool SoftGPU::PerformStencilUpload(u32 dest, int size)
{
	return false;
}

bool SoftGPU::FramebufferDirty() {
	if (g_Config.bSeparateCPUThread) {
		// Allow it to process fully before deciding if it's dirty.
		SyncThread();
	}

	if (g_Config.iFrameSkip != 0) {
		bool dirty = framebufferDirty_;
		framebufferDirty_ = false;
		return dirty;
	}
	return true;
}

bool SoftGPU::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes)
{
	int x1 = gstate.getRegionX1();
	int y1 = gstate.getRegionY1();
	int x2 = gstate.getRegionX2() + 1;
	int y2 = gstate.getRegionY2() + 1;
	int stride = gstate.FrameBufStride();
	GEBufferFormat fmt = gstate.FrameBufFormat();

	if (type == GPU_DBG_FRAMEBUF_DISPLAY) {
		x1 = 0;
		y1 = 0;
		x2 = 480;
		y2 = 272;
		stride = displayStride_;
		fmt = displayFormat_;
	}

	buffer.Allocate(x2 - x1, y2 - y1, fmt);

	const int depth = fmt == GE_FORMAT_8888 ? 4 : 2;
	const u8 *src = fb.data + stride * depth * y1;
	u8 *dst = buffer.GetData();
	const int byteWidth = (x2 - x1) * depth;
	for (int y = y1; y < y2; ++y) {
		memcpy(dst, src + x1, byteWidth);
		dst += byteWidth;
		src += stride * depth;
	}
	return true;
}

bool SoftGPU::GetCurrentDepthbuffer(GPUDebugBuffer &buffer)
{
	const int w = gstate.getRegionX2() - gstate.getRegionX1() + 1;
	const int h = gstate.getRegionY2() - gstate.getRegionY1() + 1;
	buffer.Allocate(w, h, GPU_DBG_FORMAT_16BIT);

	const int depth = 2;
	const u8 *src = depthbuf.data + gstate.DepthBufStride() * depth * gstate.getRegionY1();
	u8 *dst = buffer.GetData();
	for (int y = gstate.getRegionY1(); y <= gstate.getRegionY2(); ++y) {
		memcpy(dst, src + gstate.getRegionX1(), (gstate.getRegionX2() + 1) * depth);
		dst += w * depth;
		src += gstate.DepthBufStride() * depth;
	}
	return true;
}

bool SoftGPU::GetCurrentStencilbuffer(GPUDebugBuffer &buffer)
{
	return Rasterizer::GetCurrentStencilbuffer(buffer);
}

bool SoftGPU::GetCurrentTexture(GPUDebugBuffer &buffer, int level)
{
	return Rasterizer::GetCurrentTexture(buffer, level);
}

bool SoftGPU::GetCurrentClut(GPUDebugBuffer &buffer)
{
	const u32 bpp = gstate.getClutPaletteFormat() == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const u32 pixels = 1024 / bpp;

	buffer.Allocate(pixels, 1, (GEBufferFormat)gstate.getClutPaletteFormat());
	memcpy(buffer.GetData(), clut, 1024);
	return true;
}

bool SoftGPU::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices)
{
	return TransformUnit::GetCurrentSimpleVertices(count, vertices, indices);
}
