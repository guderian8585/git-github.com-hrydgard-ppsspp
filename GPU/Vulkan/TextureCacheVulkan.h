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

#include "Globals.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/Vulkan/TextureScalerVulkan.h"
#include "GPU/Common/TextureCacheCommon.h"

struct VirtualFramebuffer;
class FramebufferManagerVulkan;
class DepalShaderCacheVulkan;
class ShaderManagerVulkan;
class DrawEngineVulkan;

class VulkanContext;
struct SamplerCacheKey;

class TextureCacheVulkan : public TextureCacheCommon {
public:
	TextureCacheVulkan(VulkanContext *vulkan);
	~TextureCacheVulkan();

	void SetTexture(VkCommandBuffer cmd, VkImageView &imageView);
	virtual bool SetOffsetTexture(u32 offset) override;

	void Clear(bool delete_them);
	void StartFrame();
	void Invalidate(u32 addr, int size, GPUInvalidationType type);
	void InvalidateAll(GPUInvalidationType type);
	void ClearNextFrame();

	void SetFramebufferManager(FramebufferManagerVulkan *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetDepalShaderCache(DepalShaderCacheVulkan *dpCache) {
		depalShaderCache_ = dpCache;
	}
	void SetShaderManager(ShaderManagerVulkan *sm) {
		shaderManager_ = sm;
	}
	void SetTransformDrawEngine(DrawEngineVulkan *td) {
		transformDraw_ = td;
	}

	size_t NumLoadedTextures() const {
		return cache.size();
	}

	void ForgetLastTexture() {
		lastBoundTexture = nullptr;
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
	}

	void ApplyTexture(VkImageView &imageView, VkSampler &sampler);

	bool DecodeTexture(u8 *dest, const GPUgstate &state) {
		return false;
	}

	void DownloadFramebufferForClut(u32 clutAddr, u32 bytes) override {

	}

private:
	void Decimate();  // Run this once per frame to get rid of old textures.
	void DeleteTexture(TexCache::iterator it);
	void *ReadIndexedTex(int level, const u8 *texptr, int bytesPerIndex, VkFormat dstFmt, int bufw);
	void UpdateSamplingParams(TexCacheEntry &entry, bool force);
	void LoadTextureLevel(TexCacheEntry &entry, int level, bool replaceImages, int scaleFactor, VkFormat dstFmt);
	VkFormat GetDestFormat(GETextureFormat format, GEPaletteFormat clutFormat) const;
	void *DecodeTextureLevel(GETextureFormat format, GEPaletteFormat clutformat, int level, u32 &texByteAlign, VkFormat dstFmt, int scaleFactor, int *bufw = 0);
	TexCacheEntry::Status CheckAlpha(const u32 *pixelData, VkFormat dstFmt, int stride, int w, int h);
	template <typename T>
	const T *GetCurrentClut();
	u32 GetCurrentClutHash();
	void UpdateCurrentClut(GEPaletteFormat clutFormat, u32 clutBase, bool clutIndexIsSimple);
	bool AttachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer, u32 texaddrOffset = 0) override;
	void DetachFramebuffer(TexCacheEntry *entry, u32 address, VirtualFramebuffer *framebuffer) override;
	void SetTextureFramebuffer(TexCacheEntry *entry, VirtualFramebuffer *framebuffer);
	void ApplyTextureFramebuffer(VkCommandBuffer cmd, TexCacheEntry *entry, VirtualFramebuffer *framebuffer, VkImageView &image, VkSampler &sampler);
	void SetFramebufferSamplingParams(u16 bufferWidth, u16 bufferHeight, SamplerCacheKey &key);

	VulkanContext *vulkan_;

	TexCache secondCache;
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

	TextureScalerVulkan scaler;

	u32 *clutBuf_;
	u32 clutHash_;
	// True if the clut is just alpha values in the same order (RGBA4444-bit only.)
	bool clutAlphaLinear_;
	u16 clutAlphaLinearColor_;

	CachedTextureVulkan *lastBoundTexture;
	float maxAnisotropyLevel;

	int decimationCounter_;
	int texelsScaledThisFrame_;
	int timesInvalidatedAllThisFrame_;

	FramebufferManagerVulkan *framebufferManager_;
	DepalShaderCacheVulkan *depalShaderCache_;
	ShaderManagerVulkan *shaderManager_;
	DrawEngineVulkan *transformDraw_;
};

VkFormat getClutDestFormatVulkan(GEPaletteFormat format);
