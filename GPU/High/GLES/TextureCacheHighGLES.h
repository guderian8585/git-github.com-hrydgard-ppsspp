// Copyright (c) 2015- PPSSPP Project.

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

#include <map>

#include "gfx_es2/fbo.h"
#include "gfx_es2/gpu_features.h"

#include "Globals.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/TextureScaler.h"
#include "GPU/High/Command.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManager;
class DepalShaderCache;

namespace HighGpu {

class ShaderManagerGLES;

inline bool UseBGRA8888() {
	// TODO: Other platforms?  May depend on vendor which is faster?
#ifdef _WIN32
	return gl_extensions.EXT_bgra;
#endif
	return false;
}

inline int GetDimWidth(int dim) {
	return 1 << (dim & 0xF);
}
inline int GetDimHeight(int dim) {
	return 1 << ((dim >> 8) & 0xF);
}

// Wow this is starting to grow big. Soon need to start looking at resizing it.
// Must stay a POD.
struct TexCacheEntry {
	// After marking STATUS_UNRELIABLE, if it stays the same this many frames we'll trust it again.
	const static int FRAMES_REGAIN_TRUST = 1000;

	enum Status {
		STATUS_HASHING = 0x00,
		STATUS_RELIABLE = 0x01,        // Don't bother rehashing.
		STATUS_UNRELIABLE = 0x02,      // Always recheck hash.
		STATUS_MASK = 0x03,

		STATUS_ALPHA_UNKNOWN = 0x04,
		STATUS_ALPHA_FULL = 0x00,      // Has no alpha channel, or always full alpha.
		STATUS_ALPHA_SIMPLE = 0x08,    // Like above, but also has 0 alpha (e.g. 5551.)
		STATUS_ALPHA_MASK = 0x0c,

		STATUS_CHANGE_FREQUENT = 0x10, // Changes often (less than 15 frames in between.)
		STATUS_CLUT_RECHECK = 0x20,    // Another texture with same addr had a hashfail.
		STATUS_DEPALETTIZE = 0x40,     // Needs to go through a depalettize pass.
		STATUS_TO_SCALE = 0x80,        // Pending texture scaling in a later frame.
	};

	// Status, but int so we can zero initialize.
	int status;
	u32 addr;
	u32 hash;
	VirtualFramebuffer *framebuffer;  // if null, not sourced from an FBO.
	u32 sizeInRAM;
	int lastFrame;
	int numFrames;
	int numInvalidated;
	u32 framesUntilNextFullHash;
	u8 format;
	u16 dim;
	u16 bufw;
	u32 texture;  //GLuint
	int invalidHint;
	u32 fullhash;
	u32 cluthash;
	int maxLevel;
	float lodBias;

	// Cache the current filter settings so we can avoid setting it again.
	// (Old school OpenGL madness where filter settings are attached to each texture).
	// TODO: Add proper sampler support where available (GLES3+, GL3.0+)
	u8 magFilt;
	u8 minFilt;
	bool sClamp;
	bool tClamp;

	Status GetHashStatus() {
		return Status(status & STATUS_MASK);
	}
	void SetHashStatus(Status newStatus) {
		status = (status & ~STATUS_MASK) | newStatus;
	}
	Status GetAlphaStatus() {
		return Status(status & STATUS_ALPHA_MASK);
	}
	void SetAlphaStatus(Status newStatus) {
		status = (status & ~STATUS_ALPHA_MASK) | newStatus;
	}
	void SetAlphaStatus(Status newStatus, int level) {
		// For non-level zero, only set more restrictive.
		if (newStatus == STATUS_ALPHA_UNKNOWN || level == 0) {
			SetAlphaStatus(newStatus);
		} else if (newStatus == STATUS_ALPHA_SIMPLE && GetAlphaStatus() == STATUS_ALPHA_FULL) {
			SetAlphaStatus(STATUS_ALPHA_SIMPLE);
		}
	}
	bool Matches(u16 dim2, u8 format2, int maxLevel2);
	int GetWidth() const { return GetDimWidth(dim); }
	int GetHeight() const { return GetDimHeight(dim); }
};

class TextureCacheGLES : public TextureCacheCommon {
public:
	TextureCacheGLES();
	~TextureCacheGLES();

	// Separates looking the texture up from applying it.
	TexCacheEntry *GetTexture(const TextureState *state, const ClutState *clut);
	void ApplyTexture(TexCacheEntry *tex, const SamplerState *samp);

	virtual bool SetOffsetTexture(u32 offset) override;

	void Clear(bool delete_them);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();
	void LoadClut(u32 clutAddr, u32 loadBytes);

	// FramebufferManager keeps TextureCache updated about what regions of memory
	// are being rendered to. This is barebones so far.
	void NotifyFramebuffer(u32 address, VirtualFramebuffer *framebuffer, FramebufferNotification msg);

	void SetFramebufferManager(FramebufferManager *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetDepalShaderCache(DepalShaderCache *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerGLES *sm) {
		shaderManager_ = sm;
	}

	size_t NumLoadedTextures() const {
		return cache.size();
	}

	void ForgetLastTexture() {
		// TODO: Get rid of all this stateful junk
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	}

	u32 AllocTextureName();

	// Only used by Qt UI?
	bool DecodeTexture(u8 *output, const GPUgstate &state);

private:
	// Can't be unordered_map, we use lower_bound ... although for some reason that compiles on MSVC.
	typedef std::map<u64, TexCacheEntry> TexCache;

	void Decimate();  // Run this once per frame to get rid of old textures.
	void DeleteTexture(TexCache::iterator it);
	void *UnswizzleFromMem(const u8 *texptr, u32 bufw, u32 height, u32 bytesPerPixel);
	void *ReadIndexedTex(const TextureState *texState, int level, const u8 *texptr, int bytesPerIndex, GLuint dstFmt, int bufw);
	void GetSamplingParams(const SamplerState *samp, int &minFilt, int &magFilt, bool &sClamp, bool &tClamp, float &lodBias, int maxLevel);
	void SetFramebufferSamplingParams(const TexCacheEntry *entry, const SamplerState *samp, u16 bufferWidth, u16 bufferHeight);
	void UpdateSamplingParams(TexCacheEntry *entry, const SamplerState *samp, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, const TextureState *texState, int level, bool replaceImages, int scaleFactor, GLenum dstFmt);
	GLenum GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	void *DecodeTextureLevel(const TextureState *texState, GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, GLenum dstFmt, int *bufw = 0);
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, GLenum dstFmt, int stride, int w, int h);
	template <typename T>
	const T *GetCurrentClut();
	u32 GetCurrentClutHash();
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple);
	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0);
	void DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer);
	void SetTextureFramebuffer(const FramebufState *fbState, const TextureState *texState, TexCacheEntry *entry, VirtualFramebuffer *framebuffer);

	TexCacheEntry *GetEntryAt(u32 texaddr);

	TexCache cache;
	TexCache secondCache;
	std::vector<VirtualFramebuffer *> fbCache_;
	std::vector<u32> nameCache_;
	u32 cacheSizeEstimate_;
	u32 secondCacheSizeEstimate_;

	// Separate to keep main texture cache size down.
	struct AttachedFramebufferInfo {
		u32 xOffset;
		u32 yOffset;
	};
	std::map<u32, AttachedFramebufferInfo> fbTexInfo_;
	void AttachFramebufferValid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo);
	void AttachFramebufferInvalid(TexCacheEntry *entry, VirtualFramebuffer *framebuffer, const AttachedFramebufferInfo &fbInfo);

	bool clearCacheNextFrame_;
	bool lowMemoryMode_;

	TextureScalerGL scaler;

	SimpleBuf<u32> tmpTexBuf32;
	SimpleBuf<u16> tmpTexBuf16;

	SimpleBuf<u32> tmpTexBufRearrange;

	u32 clutLastFormat_;
	u32 *clutBufRaw_;
	u32 *clutBufConverted_;
	u32 *clutBuf_;
	u32 clutHash_;
	u32 clutTotalBytes_;
	u32 clutMaxBytes_;
	// True if the clut is just alpha values in the same order (RGBA4444-bit only.)
	bool clutAlphaLinear_;
	u16 clutAlphaLinearColor_;

	float maxAnisotropyLevel;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManager *framebufferManager_;
	DepalShaderCache *depalShaderCache_;
	ShaderManagerGLES *shaderManager_;
};

GLenum getClutDestFormat(GEPaletteFormat format);

}  // namespace HighGpu
