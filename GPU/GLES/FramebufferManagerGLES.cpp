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

#include <set>
#include <algorithm>

#include "profiler/profiler.h"
#include "gfx/gl_common.h"
#include "gfx/gl_debug_log.h"
#include "gfx_es2/glsl_program.h"
#include "thin3d/thin3d.h"

#include "base/timeutil.h"
#include "math/lin/matrix4x4.h"

#include "Common/ColorConv.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "GPU/Common/PostShader.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Debugger/Stepping.h"
#include "ext/native/gfx/GLStateCache.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"

static const char tex_fs[] =
	"#if __VERSION__ >= 130\n"
	"#define varying in\n"
	"#define texture2D texture\n"
	"#define gl_FragColor fragColor0\n"
	"out vec4 fragColor0;\n"
	"#endif\n"
#ifdef USING_GLES2
	"precision mediump float;\n"
#endif
	"uniform sampler2D sampler0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(sampler0, v_texcoord0);\n"
	"}\n";

static const char basic_vs[] =
	"#if __VERSION__ >= 130\n"
	"#define attribute in\n"
	"#define varying out\n"
	"#endif\n"
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  v_texcoord0 = a_texcoord0;\n"
	"  gl_Position = a_position;\n"
	"}\n";

void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format);

void FramebufferManagerGLES::DisableState() {
	glstate.blend.disable();
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	glstate.scissorTest.disable();
	glstate.stencilTest.disable();
#if !defined(USING_GLES2)
	glstate.colorLogicOp.disable();
#endif
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glstate.stencilMask.set(0xFF);

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
}

void FramebufferManagerGLES::CompileDraw2DProgram() {
	if (!draw2dprogram_) {
		std::string errorString;
		static std::string vs_code, fs_code;
		vs_code = ApplyGLSLPrelude(basic_vs, GL_VERTEX_SHADER);
		fs_code = ApplyGLSLPrelude(tex_fs, GL_FRAGMENT_SHADER);
		draw2dprogram_ = glsl_create_source(vs_code.c_str(), fs_code.c_str(), &errorString);
		if (!draw2dprogram_) {
			ERROR_LOG_REPORT(G3D, "Failed to compile draw2dprogram! This shouldn't happen.\n%s", errorString.c_str());
		} else {
			glsl_bind(draw2dprogram_);
			glUniform1i(draw2dprogram_->sampler0, 0);
		}
		CompilePostShader();
	}
}

void FramebufferManagerGLES::CompilePostShader() {
	SetNumExtraFBOs(0);
	const ShaderInfo *shaderInfo = 0;
	if (g_Config.sPostShaderName != "Off") {
		ReloadAllPostShaderInfo();
		shaderInfo = GetPostShaderInfo(g_Config.sPostShaderName);
	}

	if (shaderInfo) {
		std::string errorString;
		postShaderAtOutputResolution_ = shaderInfo->outputResolution;
		postShaderProgram_ = glsl_create(shaderInfo->vertexShaderFile.c_str(), shaderInfo->fragmentShaderFile.c_str(), &errorString);
		if (!postShaderProgram_) {
			// DO NOT turn this into a report, as it will pollute our logs with all kinds of
			// user shader experiments.
			ERROR_LOG(FRAMEBUF, "Failed to build post-processing program from %s and %s!\n%s", shaderInfo->vertexShaderFile.c_str(), shaderInfo->fragmentShaderFile.c_str(), errorString.c_str());
			// let's show the first line of the error string as an OSM.
			std::set<std::string> blacklistedLines;
			// These aren't useful to show, skip to the first interesting line.
			blacklistedLines.insert("Fragment shader failed to compile with the following errors:");
			blacklistedLines.insert("Vertex shader failed to compile with the following errors:");
			blacklistedLines.insert("Compile failed.");
			blacklistedLines.insert("");

			std::string firstLine;
			size_t start = 0;
			for (size_t i = 0; i < errorString.size(); i++) {
				if (errorString[i] == '\n') {
					firstLine = errorString.substr(start, i - start);
					if (blacklistedLines.find(firstLine) == blacklistedLines.end()) {
						break;
					}
					start = i + 1;
					firstLine.clear();
				}
			}
			if (!firstLine.empty()) {
				host->NotifyUserMessage("Post-shader error: " + firstLine + "...", 10.0f, 0xFF3090FF);
			} else {
				host->NotifyUserMessage("Post-shader error, see log for details", 10.0f, 0xFF3090FF);
			}
			usePostShader_ = false;
		} else {
			glsl_bind(postShaderProgram_);
			glUniform1i(postShaderProgram_->sampler0, 0);
			SetNumExtraFBOs(1);
			deltaLoc_ = glsl_uniform_loc(postShaderProgram_, "u_texelDelta");
			pixelDeltaLoc_ = glsl_uniform_loc(postShaderProgram_, "u_pixelDelta");
			timeLoc_ = glsl_uniform_loc(postShaderProgram_, "u_time");
			videoLoc_ = glsl_uniform_loc(postShaderProgram_, "u_video");
			usePostShader_ = true;
		}
	} else {
		postShaderProgram_ = nullptr;
		usePostShader_ = false;
	}
	glsl_unbind();
}

void FramebufferManagerGLES::Bind2DShader() {
	glsl_bind(draw2dprogram_);
}

void FramebufferManagerGLES::BindPostShader(const PostShaderUniforms &uniforms) {
	// Make sure we've compiled the shader.
	if (!postShaderProgram_) {
		CompileDraw2DProgram();
	}

	glsl_bind(postShaderProgram_);
	if (deltaLoc_ != -1)
		glUniform2f(deltaLoc_, uniforms.texelDelta[0], uniforms.texelDelta[1]);
	if (pixelDeltaLoc_ != -1)
		glUniform2f(pixelDeltaLoc_, uniforms.pixelDelta[0], uniforms.pixelDelta[1]);
	if (timeLoc_ != -1)
		glUniform4fv(timeLoc_, 1, uniforms.time);
	if (videoLoc_ != -1)
		glUniform1f(videoLoc_, uniforms.video);
}

void FramebufferManagerGLES::DestroyDraw2DProgram() {
	if (draw2dprogram_) {
		glsl_destroy(draw2dprogram_);
		draw2dprogram_ = nullptr;
	}
	if (postShaderProgram_) {
		glsl_destroy(postShaderProgram_);
		postShaderProgram_ = nullptr;
	}
}

FramebufferManagerGLES::FramebufferManagerGLES(Draw::DrawContext *draw) :
	FramebufferManagerCommon(draw),
	drawPixelsTex_(0),
	drawPixelsTexFormat_(GE_FORMAT_INVALID),
	convBuf_(nullptr),
	draw2dprogram_(nullptr),
	postShaderProgram_(nullptr),
	stencilUploadProgram_(nullptr),
	videoLoc_(-1),
	timeLoc_(-1),
	pixelDeltaLoc_(-1),
	deltaLoc_(-1),
	textureCacheGL_(nullptr),
	shaderManagerGL_(nullptr),
	pixelBufObj_(nullptr),
	currentPBO_(0)
{
	needBackBufferYSwap_ = true;
	needGLESRebinds_ = true;
}

void FramebufferManagerGLES::Init() {
	FramebufferManagerCommon::Init();
	// Workaround for upscaling shaders where we force x1 resolution without saving it
	Resized();
	CompileDraw2DProgram();
}

void FramebufferManagerGLES::SetTextureCache(TextureCacheGLES *tc) {
	textureCacheGL_ = tc;
	textureCache_ = tc;
}

void FramebufferManagerGLES::SetShaderManager(ShaderManagerGLES *sm) {
	shaderManagerGL_ = sm;
	shaderManager_ = sm;
}

void FramebufferManagerGLES::SetDrawEngine(DrawEngineGLES *td) {
	drawEngineGL_ = td;
	drawEngine_ = td;
}

FramebufferManagerGLES::~FramebufferManagerGLES() {
	if (drawPixelsTex_)
		glDeleteTextures(1, &drawPixelsTex_);
	DestroyDraw2DProgram();
	if (stencilUploadProgram_) {
		glsl_destroy(stencilUploadProgram_);
	}
	SetNumExtraFBOs(0);

	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		it->second.fbo->Release();
	}

	delete [] pixelBufObj_;
	delete [] convBuf_;
}

void FramebufferManagerGLES::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) {
	// Optimization: skip a copy if possible in a common case.
	int texWidth = width;
	if (srcPixelFormat == GE_FORMAT_8888 && width < srcStride) {
		// Don't up the upload requirements too much if subimages are unsupported.
		if (gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE) || width >= 480) {
			texWidth = srcStride;
			u1 *= (float)width / texWidth;
		}
	}

	if (drawPixelsTex_ && (drawPixelsTexFormat_ != srcPixelFormat || drawPixelsTexW_ != texWidth || drawPixelsTexH_ != height)) {
		glDeleteTextures(1, &drawPixelsTex_);
		drawPixelsTex_ = 0;
	}

	if (!drawPixelsTex_) {
		drawPixelsTex_ = textureCacheGL_->AllocTextureName();
		drawPixelsTexW_ = texWidth;
		drawPixelsTexH_ = height;

		// Initialize backbuffer texture for DrawPixels
		glBindTexture(GL_TEXTURE_2D, drawPixelsTex_);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		drawPixelsTexFormat_ = srcPixelFormat;
	} else {
		glBindTexture(GL_TEXTURE_2D, drawPixelsTex_);
	}

	// TODO: We can just change the texture format and flip some bits around instead of this.
	// Could share code with the texture cache perhaps.
	bool useConvBuf = false;
	if (srcPixelFormat != GE_FORMAT_8888 || srcStride != texWidth) {
		useConvBuf = true;
		u32 neededSize = texWidth * height * 4;
		if (!convBuf_ || convBufSize_ < neededSize) {
			delete [] convBuf_;
			convBuf_ = new u8[neededSize];
			convBufSize_ = neededSize;
		}
		for (int y = 0; y < height; y++) {
			switch (srcPixelFormat) {
			case GE_FORMAT_565:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * texWidth * y;
					ConvertRGBA565ToRGBA8888((u32 *)dst, src, width);
				}
				break;

			case GE_FORMAT_5551:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * texWidth * y;
					ConvertRGBA5551ToRGBA8888((u32 *)dst, src, width);
				}
				break;

			case GE_FORMAT_4444:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * texWidth * y;
					ConvertRGBA4444ToRGBA8888((u32 *)dst, src, width);
				}
				break;

			case GE_FORMAT_8888:
				{
					const u8 *src = srcPixels + srcStride * 4 * y;
					u8 *dst = convBuf_ + 4 * texWidth * y;
					memcpy(dst, src, 4 * width);
				}
				break;

			case GE_FORMAT_INVALID:
				_dbg_assert_msg_(G3D, false, "Invalid pixelFormat passed to DrawPixels().");
				break;
			}
		}
	}

	// Try to skip uploading the unnecessary parts.
	if (gstate_c.Supports(GPU_SUPPORTS_UNPACK_SUBIMAGE) && width != texWidth) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, texWidth);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, useConvBuf ? convBuf_ : srcPixels);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texWidth, height, GL_RGBA, GL_UNSIGNED_BYTE, useConvBuf ? convBuf_ : srcPixels);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::SetViewport2D(int x, int y, int w, int h) {
	glstate.viewport.set(x, y, w, h);
}

// x, y, w, h are relative coordinates against destW/destH, which is not very intuitive.
// TODO: This could totally use fbo_blit in many cases.
void FramebufferManagerGLES::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
	float texCoords[8] = {
		u0,v0,
		u1,v0,
		u1,v1,
		u0,v1,
	};

	static const GLushort indices[4] = {0,1,3,2};

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		float temp[8];
		int rotation = 0;
		// Vertical and Vertical180 needed swapping after we changed the coordinate system.
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 4; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 6; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 2; break;
		}
		for (int i = 0; i < 8; i++) {
			temp[i] = texCoords[(i + rotation) & 7];
		}
		memcpy(texCoords, temp, sizeof(temp));
	}

	float pos[12] = {
		x,y,0,
		x+w,y,0,
		x+w,y+h,0,
		x,y+h,0
	};

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	for (int i = 0; i < 4; i++) {
		pos[i * 3] = pos[i * 3] * invDestW - 1.0f;
		pos[i * 3 + 1] = pos[i * 3 + 1] * invDestH - 1.0f;
	}

	// Upscaling postshaders doesn't look well with linear
	if (flags & DRAWTEX_LINEAR) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	const GLSLProgram *program = glsl_get_program();
	if (!program) {
		ERROR_LOG(FRAMEBUF, "Trying to DrawActiveTexture() without a program");
		return;
	}

	glEnableVertexAttribArray(program->a_position);
	glEnableVertexAttribArray(program->a_texcoord0);
	if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
		drawEngineGL_->BindBuffer(pos, sizeof(pos), texCoords, sizeof(texCoords));
		drawEngineGL_->BindElementBuffer(indices, sizeof(indices));
		glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
		glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (void *)sizeof(pos));
		glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
	} else {
		glstate.arrayBuffer.unbind();
		glstate.elementArrayBuffer.unbind();
		glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
		glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
		glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, indices);
	}
	glDisableVertexAttribArray(program->a_position);
	glDisableVertexAttribArray(program->a_texcoord0);
}

void FramebufferManagerGLES::RebindFramebuffer() {
	FramebufferManagerCommon::RebindFramebuffer();
	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE)
		glstate.viewport.restore();
}

void FramebufferManagerGLES::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
	if (!useBufferedRendering_ || !vfb->fbo) {
		return;
	}

	// Technically, we should at this point re-interpret the bytes of the old format to the new.
	// That might get tricky, and could cause unnecessary slowness in some games.
	// For now, we just clear alpha/stencil from 565, which fixes shadow issues in Kingdom Hearts.
	// (it uses 565 to write zeros to the buffer, than 4444 to actually render the shadow.)
	//
	// The best way to do this may ultimately be to create a new FBO (combine with any resize?)
	// and blit with a shader to that, then replace the FBO on vfb.  Stencil would still be complex
	// to exactly reproduce in 4444 and 8888 formats.

	if (old == GE_FORMAT_565) {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
	} else {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
	}

	RebindFramebuffer();
}

void FramebufferManagerGLES::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	if (g_Config.bDisableSlowFramebufEffects) {
		return;
	}

	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = src->width == dst->width && src->height == dst->height;

	// Note: we don't use CopyFramebufferImage here, because it would copy depth AND stencil.  See #9740.
	if (matchingDepthBuffer && matchingSize) {
		int w = std::min(src->renderWidth, dst->renderWidth);
		int h = std::min(src->renderHeight, dst->renderHeight);

		if (gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT | GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT)) {
			// Let's only do this if not clearing depth.
			glstate.scissorTest.force(false);
			draw_->BlitFramebuffer(src->fbo, 0, 0, w, h, dst->fbo, 0, 0, w, h, Draw::FB_DEPTH_BIT, Draw::FB_BLIT_NEAREST);
			// WARNING: If we set dst->depthUpdated here, our optimization above would be pointless.
			glstate.scissorTest.restore();
		}
	}
}

void FramebufferManagerGLES::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		glBindTexture(GL_TEXTURE_2D, 0);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = (flags & BINDFBCOLOR_MAY_COPY) == 0;
	if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
		skipCopy = true;
	}
	if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == gstate.getFrameBufRawAddress()) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		Draw::Framebuffer *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (Draw::FBColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;

			CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags);
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
	} else {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
	}
}

void FramebufferManagerGLES::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
	PROFILE_THIS_SCOPE("gpu-readback");
	if (sync) {
		// flush async just in case when we go for synchronous update
		// Doesn't actually pack when sent a null argument.
		PackFramebufferAsync_(nullptr);
	}

	if (vfb) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
		OptimizeDownloadRange(vfb, x, y, w, h);
		BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

		// PackFramebufferSync_() - Synchronous pixel data transfer using glReadPixels
		// PackFramebufferAsync_() - Asynchronous pixel data transfer using glReadPixels with PBOs

		if (gl_extensions.IsGLES) {
			PackFramebufferSync_(nvfb, x, y, w, h);
		} else {
			// TODO: Can we fall back to sync without these?
			if (gl_extensions.ARB_pixel_buffer_object && gstate_c.Supports(GPU_SUPPORTS_OES_TEXTURE_NPOT)) {
				if (!sync) {
					PackFramebufferAsync_(nvfb);
				} else {
					PackFramebufferSync_(nvfb, x, y, w, h);
				}
			}
		}

		textureCacheGL_->ForgetLastTexture();
		RebindFramebuffer();
	}
}

void FramebufferManagerGLES::DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) {
	PROFILE_THIS_SCOPE("gpu-readback");
	// Flush async just in case.
	PackFramebufferAsync_(nullptr);
	FramebufferManagerCommon::DownloadFramebufferForClut(fb_address, loadBytes);
}

bool FramebufferManagerGLES::CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// When updating VRAM, it need to be exact format.
	if (!gstate_c.Supports(GPU_PREFER_CPU_DOWNLOAD)) {
		switch (nvfb->format) {
		case GE_FORMAT_4444:
			nvfb->colorDepth = Draw::FBO_4444;
			break;
		case GE_FORMAT_5551:
			nvfb->colorDepth = Draw::FBO_5551;
			break;
		case GE_FORMAT_565:
			nvfb->colorDepth = Draw::FBO_565;
			break;
		case GE_FORMAT_8888:
		default:
			nvfb->colorDepth = Draw::FBO_8888;
			break;
		}
	}

	nvfb->fbo = draw_->CreateFramebuffer({ nvfb->width, nvfb->height, 1, 1, false, (Draw::FBColorDepth)nvfb->colorDepth });
	if (!nvfb->fbo) {
		ERROR_LOG(FRAMEBUF, "Error creating GL FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
		return false;
	}
	return true;
}

void FramebufferManagerGLES::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	_assert_msg_(G3D, nvfb->fbo, "Expecting a valid nvfb in UpdateDownloadTempBuffer");

	// Discard the previous contents of this buffer where possible.
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
		draw_->BindFramebufferAsRenderTarget(nvfb->fbo, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
		GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_STENCIL_ATTACHMENT, GL_DEPTH_ATTACHMENT };
		glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
	} else if (gl_extensions.IsGLES) {
		draw_->BindFramebufferAsRenderTarget(nvfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		if (useBufferedRendering_)
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
		return;
	}

	bool useBlit = gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT | GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT);
	bool useNV = useBlit && !gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT);

	float srcXFactor = useBlit ? (float)src->renderWidth / (float)src->bufferWidth : 1.0f;
	float srcYFactor = useBlit ? (float)src->renderHeight / (float)src->bufferHeight : 1.0f;
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = useBlit ? (float)dst->renderWidth / (float)dst->bufferWidth : 1.0f;
	float dstYFactor = useBlit ? (float)dst->renderHeight / (float)dst->bufferHeight : 1.0f;
	const int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
	if (dstBpp != bpp && bpp != 0) {
		dstXFactor = (dstXFactor * bpp) / dstBpp;
	}
	int dstX1 = dstX * dstXFactor;
	int dstX2 = (dstX + w) * dstXFactor;
	int dstY1 = dstY * dstYFactor;
	int dstY2 = (dstY + h) * dstYFactor;

	if (src == dst && srcX == dstX && srcY == dstY) {
		// Let's just skip a copy where the destination is equal to the source.
		WARN_LOG_REPORT_ONCE(blitSame, G3D, "Skipped blit with equal dst and src");
		return;
	}

	if (gstate_c.Supports(GPU_SUPPORTS_ANY_COPY_IMAGE)) {
		// glBlitFramebuffer can clip, but glCopyImageSubData is more restricted.
		// In case the src goes outside, we just skip the optimization in that case.
		const bool sameSize = dstX2 - dstX1 == srcX2 - srcX1 && dstY2 - dstY1 == srcY2 - srcY1;
		const bool sameDepth = dst->colorDepth == src->colorDepth;
		const bool srcInsideBounds = srcX2 <= src->renderWidth && srcY2 <= src->renderHeight;
		const bool dstInsideBounds = dstX2 <= dst->renderWidth && dstY2 <= dst->renderHeight;
		const bool xOverlap = src == dst && srcX2 > dstX1 && srcX1 < dstX2;
		const bool yOverlap = src == dst && srcY2 > dstY1 && srcY1 < dstY2;
		if (sameSize && sameDepth && srcInsideBounds && dstInsideBounds && !(xOverlap && yOverlap)) {
			draw_->CopyFramebufferImage(src->fbo, 0, srcX1, srcY1, 0, dst->fbo, 0, dstX1, dstY1, 0, dstX2 - dstX1, dstY2 - dstY1, 1, Draw::FB_COLOR_BIT);
			CHECK_GL_ERROR_IF_DEBUG();
			return;
		}
	}

	glstate.scissorTest.force(false);
	if (useBlit) {
		draw_->BlitFramebuffer(src->fbo, srcX1, srcY1, srcX2, srcY2, dst->fbo, dstX1, dstY1, dstX2, dstY2, Draw::FB_COLOR_BIT, Draw::FB_BLIT_NEAREST);
	} else {
		draw_->BindFramebufferAsRenderTarget(dst->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP });
		draw_->BindFramebufferAsTexture(src->fbo, 0, Draw::FB_COLOR_BIT, 0);

		// Make sure our 2D drawing program is ready. Compiles only if not already compiled.
		CompileDraw2DProgram();

		glstate.viewport.force(0, 0, dst->renderWidth, dst->renderHeight);
		glstate.blend.force(false);
		glstate.cullFace.force(false);
		glstate.depthTest.force(false);
		glstate.stencilTest.force(false);
#if !defined(USING_GLES2)
		glstate.colorLogicOp.force(false);
#endif
		glstate.colorMask.force(true, true, true, true);
		glstate.stencilMask.force(0xFF);

		// The first four coordinates are relative to the 6th and 7th arguments of DrawActiveTexture.
		// Should maybe revamp that interface.
		float srcW = src->bufferWidth;
		float srcH = src->bufferHeight;
		glsl_bind(draw2dprogram_);
		DrawActiveTexture(dstX1, dstY1, w * dstXFactor, h, dst->bufferWidth, dst->bufferHeight, srcX1 / srcW, srcY1 / srcH, srcX2 / srcW, srcY2 / srcH, ROTATION_LOCKED_HORIZONTAL, DRAWTEX_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
		textureCacheGL_->ForgetLastTexture();
		glstate.viewport.restore();
		glstate.blend.restore();
		glstate.cullFace.restore();
		glstate.depthTest.restore();
		glstate.stencilTest.restore();
#if !defined(USING_GLES2)
		glstate.colorLogicOp.restore();
#endif
		glstate.colorMask.restore();
		glstate.stencilMask.restore();
	}

	glstate.scissorTest.restore();
	CHECK_GL_ERROR_IF_DEBUG();
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const u32 *src32 = (const u32 *)src;

	if (format == GE_FORMAT_8888) {
		u32 *dst32 = (u32 *)dst;
		if (src == dst) {
			return;
		} else {
			// Here let's assume they don't intersect
			for (u32 y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else {
		// But here it shouldn't matter if they do intersect
		u16 *dst16 = (u16 *)dst;
		switch (format) {
			case GE_FORMAT_565: // BGR 565
				{
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGB565(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				}
				break;
			case GE_FORMAT_5551: // ABGR 1555
				{
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGBA5551(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				}
				break;
			case GE_FORMAT_4444: // ABGR 4444
				{
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGBA4444(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				}
				break;
			case GE_FORMAT_8888:
			case GE_FORMAT_INVALID:
				// Not possible.
				break;
		}
	}
}

void FramebufferManagerGLES::PackFramebufferAsync_(VirtualFramebuffer *vfb) {
	CHECK_GL_ERROR_IF_DEBUG();
	const int MAX_PBO = 2;
	GLubyte *packed = 0;
	bool unbind = false;
	const u8 nextPBO = (currentPBO_ + 1) % MAX_PBO;
	const bool useCPU = gstate_c.Supports(GPU_PREFER_CPU_DOWNLOAD);

	// We'll prepare two PBOs to switch between readying and reading
	if (!pixelBufObj_) {
		if (!vfb) {
			// This call is just to flush the buffers.  We don't have any yet,
			// so there's nothing to do.
			return;
		}

		GLuint pbos[MAX_PBO];
		glGenBuffers(MAX_PBO, pbos);

		pixelBufObj_ = new AsyncPBO[MAX_PBO];
		for (int i = 0; i < MAX_PBO; i++) {
			pixelBufObj_[i].handle = pbos[i];
			pixelBufObj_[i].maxSize = 0;
			pixelBufObj_[i].reading = false;
		}
	}

	// Receive previously requested data from a PBO
	AsyncPBO &pbo = pixelBufObj_[nextPBO];
	if (pbo.reading) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo.handle);
#ifdef USING_GLES2
		// Not on desktop GL 2.x...
		packed = (GLubyte *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, pbo.size, GL_MAP_READ_BIT);
#else
		packed = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
#endif

		if (packed) {
			DEBUG_LOG(FRAMEBUF, "Reading PBO to memory , bufSize = %u, packed = %p, fb_address = %08x, stride = %u, pbo = %u",
			pbo.size, packed, pbo.fb_address, pbo.stride, nextPBO);

			if (useCPU) {
				u8 *dst = Memory::GetPointer(pbo.fb_address);
				ConvertFromRGBA8888(dst, packed, pbo.stride, pbo.stride, pbo.stride, pbo.height, pbo.format);
			} else {
				// We don't need to convert, GPU already did (or should have)
				Memory::MemcpyUnchecked(pbo.fb_address, packed, pbo.size);
			}

			pbo.reading = false;
		}

		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		unbind = true;
	}

	// Order packing/readback of the framebuffer
	if (vfb) {
		bool reverseOrder = gstate_c.Supports(GPU_PREFER_REVERSE_COLOR_ORDER);
		Draw::DataFormat dataFmt = Draw::DataFormat::UNDEFINED;
		switch (vfb->format) {
		case GE_FORMAT_4444:
			dataFmt = (reverseOrder ? Draw::DataFormat::A4R4G4B4_UNORM_PACK16 : Draw::DataFormat::B4G4R4A4_UNORM_PACK16);
			break;
		case GE_FORMAT_5551:
			dataFmt = (reverseOrder ? Draw::DataFormat::A1R5G5B5_UNORM_PACK16 : Draw::DataFormat::B5G5R5A1_UNORM_PACK16);
			break;
		case GE_FORMAT_565:
			dataFmt = (reverseOrder ? Draw::DataFormat::R5G6B5_UNORM_PACK16 : Draw::DataFormat::B5G6R5_UNORM_PACK16);
			break;
		case GE_FORMAT_8888:
			dataFmt = Draw::DataFormat::R8G8B8A8_UNORM;
			break;
		};

		if (useCPU) {
			dataFmt = Draw::DataFormat::R8G8B8A8_UNORM;
		}

		int pixelSize = (int)DataFormatSizeInBytes(dataFmt);
		int align = pixelSize;

		// If using the CPU, we need 4 bytes per pixel always.
		u32 bufSize = vfb->fb_stride * vfb->height * pixelSize;
		u32 fb_address = (0x04000000) | vfb->fb_address;

		if (!vfb->fbo) {
			ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferAsync_: vfb->fbo == 0");
			return;
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelBufObj_[currentPBO_].handle);

		if (pixelBufObj_[currentPBO_].maxSize < bufSize) {
			// We reserve a buffer big enough to fit all those pixels
			glBufferData(GL_PIXEL_PACK_BUFFER, bufSize, NULL, GL_DYNAMIC_READ);
			pixelBufObj_[currentPBO_].maxSize = bufSize;
		}

		// TODO: This is a hack since PBOs have not been implemented in Thin3D yet (and maybe shouldn't? maybe should do this internally?)
		draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, 0, vfb->fb_stride, vfb->height, dataFmt, nullptr, vfb->fb_stride);

		unbind = true;

		pixelBufObj_[currentPBO_].fb_address = fb_address;
		pixelBufObj_[currentPBO_].size = bufSize;
		pixelBufObj_[currentPBO_].stride = vfb->fb_stride;
		pixelBufObj_[currentPBO_].height = vfb->height;
		pixelBufObj_[currentPBO_].format = vfb->format;
		pixelBufObj_[currentPBO_].reading = true;
	}

	currentPBO_ = nextPBO;

	if (unbind) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferSync_: vfb->fbo == 0");
		return;
	}

	int possibleH = std::max(vfb->height - y, 0);
	if (h > possibleH) {
		h = possibleH;
	}

	// Pixel size always 4 here because we always request RGBA8888
	u32 bufSize = vfb->fb_stride * h * 4;
	u32 fb_address = 0x04000000 | vfb->fb_address;

	bool convert = vfb->format != GE_FORMAT_8888;
	const int dstBpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;
	const int packWidth = std::min(vfb->fb_stride, std::min(x + w, (int)vfb->width));

	int dstByteOffset = y * vfb->fb_stride * dstBpp;
	u8 *dst = Memory::GetPointer(fb_address + dstByteOffset);

	u8 *packed = nullptr;
	if (!convert) {
		packed = (u8 *)dst;
	} else {
		// End result may be 16-bit but we are reading 32-bit, so there may not be enough space at fb_address
		if (!convBuf_ || convBufSize_ < bufSize) {
			delete [] convBuf_;
			convBuf_ = new u8[bufSize];
			convBufSize_ = bufSize;
		}
		packed = convBuf_;
	}

	if (packed) {
		DEBUG_LOG(FRAMEBUF, "Reading framebuffer to mem, bufSize = %u, fb_address = %08x", bufSize, fb_address);
		int packW = h == 1 ? packWidth : vfb->fb_stride;  // TODO: What's this about?
		draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, 0, y, packW, h, Draw::DataFormat::R8G8B8A8_UNORM, packed, packW);
		if (convert) {
			ConvertFromRGBA8888(dst, packed, vfb->fb_stride, vfb->fb_stride, packWidth, h, vfb->format);
		}
	}

	// TODO: Move this into Thin3d.
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
#ifdef USING_GLES2
		// GLES3 doesn't support using GL_READ_FRAMEBUFFER here.
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
		const GLenum target = GL_FRAMEBUFFER;
#else
		const GLenum target = GL_READ_FRAMEBUFFER;
#endif
		GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
		glInvalidateFramebuffer(target, 3, attachments);
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::PackDepthbuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackDepthbuffer: vfb->fbo == 0");
		return;
	}

	// Pixel size always 4 here because we always request float
	const u32 bufSize = vfb->z_stride * (h - y) * 4;
	const u32 z_address = (0x04000000) | vfb->z_address;
	const int packWidth = std::min(vfb->z_stride, std::min(x + w, (int)vfb->width));

	if (!convBuf_ || convBufSize_ < bufSize) {
		delete [] convBuf_;
		convBuf_ = new u8[bufSize];
		convBufSize_ = bufSize;
	}

	DEBUG_LOG(FRAMEBUF, "Reading depthbuffer to mem at %08x for vfb=%08x", z_address, vfb->fb_address);

	int packW = h == 1 ? packWidth : vfb->z_stride;
	draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_DEPTH_BIT, 0, y, packW, h, Draw::DataFormat::D32F, convBuf_, packW);

	int dstByteOffset = y * vfb->fb_stride * sizeof(u16);
	u16 *depth = (u16 *)Memory::GetPointer(z_address + dstByteOffset);
	GLfloat *packed = (GLfloat *)convBuf_;

	int totalPixels = h == 1 ? packWidth : vfb->z_stride * h;
	for (int i = 0; i < totalPixels; ++i) {
		float scaled = FromScaledDepth(packed[i]);
		if (scaled <= 0.0f) {
			depth[i] = 0;
		} else if (scaled >= 65535.0f) {
			depth[i] = 65535;
		} else {
			depth[i] = (int)scaled;
		}
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::EndFrame() {
	CHECK_GL_ERROR_IF_DEBUG();

	// We flush to memory last requested framebuffer, if any.
	// Only do this in the read-framebuffer modes.
	if (updateVRAM_)
		PackFramebufferAsync_(nullptr);

	// Let's explicitly invalidate any temp FBOs used during this frame.
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
		for (auto temp : tempFBOs_) {
			if (temp.second.last_frame_used < gpuStats.numFlips) {
				continue;
			}

			draw_->BindFramebufferAsRenderTarget(temp.second.fbo, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
			GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_STENCIL_ATTACHMENT, GL_DEPTH_ATTACHMENT };
			glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
		}
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP , Draw::RPAction::KEEP });
	}
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::DeviceLost() {
	DestroyAllFBOs();
	DestroyDraw2DProgram();
}

void FramebufferManagerGLES::DestroyAllFBOs() {
	CHECK_GL_ERROR_IF_DEBUG();
	currentRenderVfb_ = 0;
	displayFramebuf_ = 0;
	prevDisplayFramebuf_ = 0;
	prevPrevDisplayFramebuf_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		INFO_LOG(FRAMEBUF, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();

	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = bvfbs_[i];
		DestroyFramebuf(vfb);
	}
	bvfbs_.clear();

	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		it->second.fbo->Release();
	}
	tempFBOs_.clear();

	DisableState();
	CHECK_GL_ERROR_IF_DEBUG();
}

void FramebufferManagerGLES::Resized() {
	FramebufferManagerCommon::Resized();

	if (UpdateSize()) {
		DestroyAllFBOs();
	}

	DestroyDraw2DProgram();

#ifndef USING_GLES2
	if (g_Config.iInternalResolution == 0) {
		glLineWidth(std::max(1, (int)(renderWidth_ / 480)));
		glPointSize(std::max(1.0f, (float)(renderWidth_ / 480.f)));
	} else {
		glLineWidth(g_Config.iInternalResolution);
		glPointSize((float)g_Config.iInternalResolution);
	}
#endif

	if (!draw2dprogram_) {
		CompileDraw2DProgram();
	}
}

bool FramebufferManagerGLES::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	int w, h;
	draw_->GetFramebufferDimensions(nullptr, &w, &h);
	buffer.Allocate(w, h, GPU_DBG_FORMAT_888_RGB, true);
	draw_->CopyFramebufferToMemorySync(nullptr, Draw::FB_COLOR_BIT, 0, 0, w, h, Draw::DataFormat::R8G8B8_UNORM, buffer.GetData(), w);
	return true;
}
