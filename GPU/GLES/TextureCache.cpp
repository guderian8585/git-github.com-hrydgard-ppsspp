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

#include <map>

#include "../../Core/MemMap.h"
#include "../ge_constants.h"
#include "../GPUState.h"
#include "TextureCache.h"
#include "../Core/Config.h"

// If a texture hasn't been seen for 200 frames, get rid of it.
#define TEXTURE_KILL_AGE 200

// TODO: Speed up by switching to ReadUnchecked*.

struct TexCacheEntry
{
	u32 addr;
	u32 hash;
	int frameCounter;
	u32 format;
	u32 clutaddr;
	u32 clutformat;
	u32 cluthash;
	int dim;
	GLuint texture;
	int invalidHint;
	u32 fullhash;
};

typedef std::map<u64, TexCacheEntry> TexCache;
static TexCache cache;

u32 *tmpTexBuf32;
u16 *tmpTexBuf16;

u32 *tmpTexBufRearrange;

u32 *clutBuf32;
u16 *clutBuf16;

void TextureCache_Init()
{
	// TODO: Switch to aligned allocations for alignment. AllocateMemoryPages would do the trick.
	tmpTexBuf32 = new u32[1024 * 512];
	tmpTexBuf16 = new u16[1024 * 512];
	tmpTexBufRearrange = new u32[1024 * 512];
	clutBuf32 = new u32[4096];
	clutBuf16 = new u16[4096];
}

void TextureCache_Shutdown()
{
	delete [] tmpTexBuf32;
	tmpTexBuf32 = 0;
	delete [] tmpTexBuf16;
	tmpTexBuf16 = 0;
	delete [] tmpTexBufRearrange;
	tmpTexBufRearrange = 0;
	delete [] clutBuf32;
	delete [] clutBuf16;
}

void TextureCache_Clear(bool delete_them)
{
	if (delete_them)
	{
		for (TexCache::iterator iter = cache.begin(); iter != cache.end(); ++iter)
		{
			DEBUG_LOG(G3D, "Deleting texture %i", iter->second.texture);
			glDeleteTextures(1, &iter->second.texture);
		}
	}
	if (cache.size()) {
		INFO_LOG(G3D, "Texture cached cleared from %i textures", (int)cache.size());
		cache.clear();
	}
}

// Removes old textures.
void TextureCache_Decimate()
{
	for (TexCache::iterator iter = cache.begin(); iter != cache.end(); )
	{
		if (iter->second.frameCounter + TEXTURE_KILL_AGE < gpuStats.numFrames)
		{
			glDeleteTextures(1, &iter->second.texture);
			cache.erase(iter++);
		}
		else
			++iter;
	}
}

void TextureCache_Invalidate(u32 addr, int size, bool force)
{
	u32 addr_end = addr + size;

	for (TexCache::iterator iter = cache.begin(); iter != cache.end(); )
	{
		// Clear if either the addr or clutaddr is in the range.
		bool invalidate = iter->second.addr >= addr && iter->second.addr < addr_end;
		invalidate |= iter->second.clutaddr >= addr && iter->second.clutaddr < addr_end;

		if (invalidate)
		{
			if (force)
			{
				glDeleteTextures(1, &iter->second.texture);
				cache.erase(iter++);
			}
			else
			{
				iter->second.invalidHint++;
				++iter;
			}
		}
		else
			++iter;
	}
}

void TextureCache_InvalidateAll(bool force)
{
	if (force)
		TextureCache_Clear(true);
	else
		TextureCache_Invalidate(0, 0xFFFFFFFF, force);
}

int TextureCache_NumLoadedTextures() 
{
	return cache.size();
}


u32 GetClutAddr(u32 clutEntrySize)
{
	return ((gstate.clutaddr & 0xFFFFFF) | ((gstate.clutaddrupper << 8) & 0x0F000000)) + ((gstate.clutformat >> 16) & 0x1f) * clutEntrySize;
}

u32 GetClutIndex(u32 index)
{
	return ((((gstate.clutformat >> 16) & 0x1f) + index) >> ((gstate.clutformat >> 2) & 0x1f)) & ((gstate.clutformat >> 8) & 0xff);
}

u16 *ReadClut16()
{
	u32 clutNumEntries = (gstate.loadclut & 0x3f) * 16;
	u32 clutAddr = GetClutAddr(2);
	for (u32 i = ((gstate.clutformat >> 16) & 0x1f); i < clutNumEntries; i++)
		clutBuf16[i] = Memory::Read_U16(clutAddr + i * 2);
	return clutBuf16;
}

u32 *ReadClut32()
{
	u32 clutNumEntries = (gstate.loadclut & 0x3f) * 8;
	u32 clutAddr = GetClutAddr(4);
	for (u32 i = ((gstate.clutformat >> 16) & 0x1f); i < clutNumEntries; i++)
		clutBuf32[i] = Memory::Read_U32(clutAddr + i * 4);
	return clutBuf32;
}

void *UnswizzleFromMem(u32 texaddr, u32 bytesPerPixel, u32 level)
{
	u32 addr = texaddr;
	u32 rowWidth = (bytesPerPixel > 0) ? ((gstate.texbufwidth[level] & 0x3FF) * bytesPerPixel) : ((gstate.texbufwidth[level] & 0x3FF) / 2);
	u32 pitch = rowWidth / 4;
	u32 bxc = rowWidth / 16;
	u32 byc = ((1 << ((gstate.texsize[level] >> 8) & 0xf)) + 7) / 8;
	if (byc == 0)
		byc = 1;

	u32 ydest = 0;

	u32 by;
	for (by = 0; by < byc; by++)
	{
		if (rowWidth >= 16)
		{
			u32 xdest = ydest;
			u32 bx;
			for (bx = 0; bx < bxc; bx++)
			{
				u32 dest = xdest;
				u8 n;
				for (n = 0; n < 8; n++)
				{
					u32 k;
					for (k = 0; k < 4; k++) {
						tmpTexBuf32[dest + k] = Memory::Read_U32(addr);
						addr += 4;
					}
					dest += pitch;
				}
				xdest += 4;
			}
			ydest += (rowWidth * 8) / 4;
		}
		else if (rowWidth == 8)
		{
			u32 n;
			for (n = 0; n < 8; n++, ydest += 2)
			{
				tmpTexBuf32[ydest + 0] = Memory::Read_U32(addr + 0);
				tmpTexBuf32[ydest + 1] = Memory::Read_U32(addr + 4);
				addr += 16; // skip two u32
			}
		}
		else if (rowWidth == 4)
		{
			u32 n;
			for (n = 0; n < 8; n++, ydest++) {
				tmpTexBuf32[ydest] = Memory::Read_U32(addr);
				addr += 16;
			}
		}
		else if (rowWidth == 2)
		{
			u32 n;
			for (n = 0; n < 4; n++, ydest++)
			{
				u16 n1 = Memory::Read_U32(addr +  0) & 0xffff;
				u16 n2 = Memory::Read_U32(addr + 16) & 0xffff;
				tmpTexBuf32[ydest] = (u32)n1 | ((u32)n2 << 16);
				addr += 32;
			}
		}
		else if (rowWidth == 1)
		{
			u32 n;
			for (n = 0; n < 2; n++, ydest++)
			{
				u8 n1 = Memory::Read_U32(addr +  0) & 0xf;
				u8 n2 = Memory::Read_U32(addr + 16) & 0xf;
				u8 n3 = Memory::Read_U32(addr + 32) & 0xf;
				u8 n4 = Memory::Read_U32(addr + 48) & 0xf;

				tmpTexBuf32[ydest] = (u32)n1 | ((u32)n2 << 8) | ((u32)n3 << 16) | ((u32)n4 << 24);
			}
		}
	}
	return tmpTexBuf32;
}

void *readIndexedTex(u32 level, u32 texaddr, u32 bytesPerIndex)
{
	u32 length = (gstate.texbufwidth[level] & 0x3FF) * (1 << ((gstate.texsize[level] >> 8) & 0xf));
	void *buf = NULL;

	switch ((gstate.clutformat & 3))
	{
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		{
		u16 *clut = ReadClut16();
		if (!(gstate.texmode & 1))
		{
			u32 i;
			switch (bytesPerIndex)
			{
			case 1:
				for (i = 0; i < length; i++) {
					u8 index = Memory::Read_U8(texaddr + i);
					tmpTexBuf16[i] = clut[GetClutIndex(index)];
				}
				break;

			case 2:
				for (i = 0; i < length; i++) {
					u16 index = Memory::Read_U16(texaddr + i * 2);
					tmpTexBuf16[i] = clut[GetClutIndex(index)];
				}
				break;

			case 4:
				for (i = 0; i < length; i++) {
					u32 index = Memory::Read_U32(texaddr + i * 4);
					tmpTexBuf16[i] = clut[GetClutIndex(index)];
				}
				break;
			}
		}
		else
		{
			u32 i, j;
			UnswizzleFromMem(texaddr, bytesPerIndex, level);
			switch (bytesPerIndex)
			{
			case 1:
				for (i = 0, j = 0; i < length; i += 4, j++)
				{
					u32 n = tmpTexBuf32[j];
					u32 k;
					for (k = 0; k < 4; k++) {
						u8 index = (n >> (k * 8)) & 0xff;
						tmpTexBuf16[i + k] = clut[GetClutIndex(index)];
					}
				}
				break;

			case 2:
				for (i = 0, j = 0; i < length; i += 2, j++)
				{
					u32 n = tmpTexBuf32[j];
					tmpTexBuf16[i + 0] = clut[GetClutIndex(n & 0xffff)];
					tmpTexBuf16[i + 1] = clut[GetClutIndex(n >> 16)];
				}
				break;

			case 4:
				for (i = 0; i < length; i++) {
					u32 n = tmpTexBuf32[i];
					tmpTexBuf16[i] = clut[GetClutIndex(n)];
				}
				break;
			}
		}
		buf = tmpTexBuf16;
		}
		break;

	case GE_CMODE_32BIT_ABGR8888:
		{
		u32 *clut = ReadClut32();
		if (!(gstate.texmode & 1))
		{
			u32 i;
			switch (bytesPerIndex)
			{
			case 1:
				for (i = 0; i < length; i++) {
					u8 index = Memory::Read_U8(texaddr + i);
					tmpTexBuf32[i] = clut[GetClutIndex(index)];
				}
				break;

			case 2:
				for (i = 0; i < length; i++) {
					u16 index = Memory::Read_U16(texaddr + i * 2);
					tmpTexBuf32[i] = clut[GetClutIndex(index)];
				}
				break;

			case 4:
				for (i = 0; i < length; i++) {
					u32 index = Memory::Read_U32(texaddr + i * 4);
					tmpTexBuf32[i] = clut[GetClutIndex(index)];
				}
				break;
			}
		}
		else
		{
			u32 j;
			s32 i;
			UnswizzleFromMem(texaddr, bytesPerIndex, level);
			switch (bytesPerIndex)
			{
			case 1:
				for (i = length - 4, j = (length / 4) - 1; i >= 0; i -= 4, j--)
				{
					u32 n = tmpTexBuf32[j];
					u32 k;
					for (k = 0; k < 4; k++) {
						u32 index = (n >> (k * 8)) & 0xff;
						tmpTexBuf32[i + k] = clut[GetClutIndex(index)];
					}
				}
				break;

			case 2:
				for (i = length - 2, j = (length / 2) - 1; i >= 0; i -= 2, j--)
				{
					u32 n = tmpTexBuf32[j];
					tmpTexBuf32[i + 0] = clut[GetClutIndex(n & 0xffff)];
					tmpTexBuf32[i + 1] = clut[GetClutIndex(n >> 16)];
				}
				break;

			case 4:
				for (i = 0; (u32)i < length; i++) {
					u32 n = tmpTexBuf32[i];
					tmpTexBuf32[i] = clut[GetClutIndex(n)];
				}
				break;
			}
		}
		buf = tmpTexBuf32;
		}
		break;

	default:
		ERROR_LOG(G3D, "Unhandled clut texture mode %d!!!", (gstate.clutformat & 3));
		break;
	}

	return buf;
}

GLenum getClutDestFormat(GEPaletteFormat format)
{
	switch (format)
	{
	case GE_CMODE_16BIT_ABGR4444:
		return GL_UNSIGNED_SHORT_4_4_4_4;
	case GE_CMODE_16BIT_ABGR5551:
		return GL_UNSIGNED_SHORT_5_5_5_1;
	case GE_CMODE_16BIT_BGR5650:
		return GL_UNSIGNED_SHORT_5_6_5;
	case GE_CMODE_32BIT_ABGR8888:
		return GL_UNSIGNED_BYTE;
	}
	return 0;
}

u32 texByteAlignMap[] = {2, 2, 2, 4};

// This should not have to be done per texture! OpenGL is silly yo
// TODO: Dirty-check this against the current texture.
void UpdateSamplingParams()
{
	int minFilt = gstate.texfilter & 0x7;
	int magFilt = (gstate.texfilter>>8)&1;
	minFilt &= 1; //no mipmaps yet

	int sClamp = gstate.texwrap & 1;
	int tClamp = (gstate.texwrap>>8) & 1;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	if ( g_Config.bLinearFiltering ) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilt ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilt ? GL_LINEAR : GL_NEAREST);
	}
}


// Convert from PSP bit order to GLES bit order
u16 convert565(u16 c) {
	return (c >> 11) | (c & 0x07E0) | (c << 11);
}

// Convert from PSP bit order to GLES bit order
u16 convert4444(u16 c) {
	return (c >> 12) | ((c >> 4) & 0xF0) | ((c << 4) & 0xF00) | (c << 12);
}

// Convert from PSP bit order to GLES bit order
u16 convert5551(u16 c) {
	return ((c & 0x8000) >> 15) | (c << 1);
}


// All these DXT structs are in the reverse order, as compared to PC.
// On PC, alpha comes before color, and interpolants are before the tile data.

struct DXT1Block
{
	u8 lines[4];
	u16 color1;
	u16 color2;
};

struct DXT3Block
{
	DXT1Block color;
	u16 alphaLines[4];
};

struct DXT5Block 
{
	DXT1Block color;
	u32 alphadata2;
	u16 alphadata1;
	u8 alpha1; u8 alpha2;
};

inline u32 makecol(int r, int g, int b, int a)
{
	return (a << 24)|(r << 16)|(g << 8)|b;
}

// This could probably be done faster by decoding two or four blocks at a time with SSE/NEON.
void decodeDXT1Block(u32 *dst, const DXT1Block *src, int pitch, bool ignore1bitAlpha = false)
{
	// S3TC Decoder
	// Needs more speed and debugging.
	u16 c1 = (src->color1);
	u16 c2 = (src->color2);
	int red1 = Convert5To8(c1 & 0x1F);
	int red2 = Convert5To8(c2 & 0x1F);
	int green1 = Convert6To8((c1 >> 5) & 0x3F);
	int green2 = Convert6To8((c2 >> 5) & 0x3F);
	int blue1 = Convert5To8((c1 >> 11) & 0x1F);
	int blue2 = Convert5To8((c2 >> 11) & 0x1F);
	u32 colors[4];
	colors[0] = makecol(red1, green1, blue1, 255);
	colors[1] = makecol(red2, green2, blue2, 255);
	if (c1 > c2 || ignore1bitAlpha)
	{
		int blue3 = ((blue2 - blue1) >> 1) - ((blue2 - blue1) >> 3);
		int green3 = ((green2 - green1) >> 1) - ((green2 - green1) >> 3);
		int red3 = ((red2 - red1) >> 1) - ((red2 - red1) >> 3);				
		colors[2] = makecol(red1 + red3, green1 + green3, blue1 + blue3, 255);
		colors[3] = makecol(red2 - red3, green2 - green3, blue2 - blue3, 255);
	}
	else
	{
		colors[2] = makecol((red1 + red2 + 1) / 2, // Average
			(green1 + green2 + 1) / 2,
			(blue1 + blue2 + 1) / 2, 255);
		colors[3] = makecol(red2, green2, blue2, 0);	// Color2 but transparent
	}

	for (int y = 0; y < 4; y++)
	{
		int val = src->lines[y];
		for (int x = 0; x < 4; x++)
		{
			dst[x] = colors[val & 3];
			val >>= 2;
		}
		dst += pitch;
	}
}

void decodeDXT3Block(u32 *dst, const DXT3Block *src, int pitch)
{
	decodeDXT1Block(dst, &src->color, pitch, true);
	// Alpha: TODO
}

inline u8 lerp8(const DXT5Block *src, int n) {
	float d = n / 7.0f;
	return (u8)(src->alpha1 + (src->alpha2 - src->alpha1) * d);
}

inline u8 lerp6(const DXT5Block *src, int n) {
	float d = n / 5.0f;
	return (u8)(src->alpha1 + (src->alpha2 - src->alpha1) * d);
}

// The alpha channel is not 100% correct 
void decodeDXT5Block(u32 *dst, const DXT5Block *src, int pitch)
{
	decodeDXT1Block(dst, &src->color, pitch, true);
	u8 alpha[8];

	alpha[0] = src->alpha1;
	alpha[1] = src->alpha2;
	if (alpha[0] > alpha[1]) {
		alpha[2] = lerp8(src, 6);
		alpha[3] = lerp8(src, 5);
		alpha[4] = lerp8(src, 4);
		alpha[5] = lerp8(src, 3);
		alpha[6] = lerp8(src, 2);
		alpha[7] = lerp8(src, 1);
	} else {
		alpha[2] = lerp6(src, 4);
		alpha[3] = lerp6(src, 3);
		alpha[4] = lerp6(src, 2);
		alpha[5] = lerp6(src, 1);
		alpha[6] = 0;
		alpha[7] = 255;
	}

	u64 data = ((u64)src->alphadata1 << 32) | src->alphadata2;

	for (int y = 0; y < 4; y++)
	{
		for (int x = 0; x < 4; x++)
		{
			dst[x] = (dst[x] & 0xFFFFFF) | (alpha[data & 7] << 24);
			data >>= 3;
		}
		dst += pitch;
	}
}

void convertColors(u8 *finalBuf, GLuint dstFmt, int numPixels)
{
	// TODO: All these can be massively sped up with SSE, or even
	// somewhat sped up using "manual simd" in 32 or 64-bit gprs.
	switch (dstFmt) {
	case GL_UNSIGNED_SHORT_4_4_4_4:
		{
			u16 *p = (u16 *)finalBuf;
			for (int i = 0; i < numPixels; i++) {
				u16 c = p[i];
				p[i] = (c >> 12) | ((c >> 4) & 0xF0) | ((c << 4) & 0xF00) | (c << 12);
			}
		}
		break;
	case GL_UNSIGNED_SHORT_5_5_5_1:
		{
			u16 *p = (u16 *)finalBuf;
			for (int i = 0; i < numPixels; i++) {
				u16 c = p[i];
				p[i] = ((c & 0x8000) >> 15) | ((c >> 9) & 0x3E) | ((c << 1) & 0x7C0) | ((c << 11) & 0xF800);
			}
		}
		break;
	case GL_UNSIGNED_SHORT_5_6_5:
		{
			u16 *p = (u16 *)finalBuf;
			for (int i = 0; i < numPixels; i++) {
				u16 c = p[i];
				p[i] = (c >> 11) | (c & 0x07E0) | (c << 11);
			}
		}
		break;
	default:
		{
			// No need to convert RGBA8888, right order already
		}
		break;
	}
}

void PSPSetTexture()
{
	static int lastBoundTexture = -1;

	u32 texaddr = (gstate.texaddr[0] & 0xFFFFF0) | ((gstate.texbufwidth[0]<<8) & 0xFF000000);
	texaddr &= 0xFFFFFFF;

	u8 level = 0;
	u32 format = gstate.texformat & 0xF;
	u32 clutformat = gstate.clutformat & 3;
	u32 clutaddr = GetClutAddr(clutformat == GE_CMODE_32BIT_ABGR8888 ? 4 : 2);

	u8 *texptr = Memory::GetPointer(texaddr);
	u32 texhash = texptr ? *(u32*)texptr : 0;

	u64 cachekey = texaddr ^ texhash;
	cachekey |= (u64) clutaddr << 32;
	TexCache::iterator iter = cache.find(cachekey);
	if (iter != cache.end())
	{
		//Validate the texture here (width, height etc)
		TexCacheEntry &entry = iter->second;

		int dim = gstate.texsize[0] & 0xF0F;
		bool match = true;
		
		//TODO: Check more texture parameters, compute real texture hash
		if (dim != entry.dim || entry.hash != texhash || entry.format != format)
			match = false;

		//TODO: Check more clut parameters, compute clut hash
		if (match && (format >= GE_TFMT_CLUT4 && format <= GE_TFMT_CLUT32) &&
			 (entry.clutformat != clutformat ||
				entry.clutaddr != clutaddr ||
				entry.cluthash != Memory::Read_U32(entry.clutaddr))) 
			match = false;

		// If it's not huge or has been invalidated many times, recheck the whole texture.
		if (entry.invalidHint > 180 || (entry.invalidHint > 15 && dim <= 0x909)) {
			entry.invalidHint = 0;
			int bufw = gstate.texbufwidth[0] & 0x3ff;
			int h = 1 << ((gstate.texsize[0]>>8) & 0xf);

			u32 check = 0;
			for (int i = 0; i < bufw * h; i += 4) {
				check += Memory::Read_U32(texaddr + i);
			}

			if (check != entry.fullhash) {
				match = false;
			}
		}

		if (match) {
			//got one!
			entry.frameCounter = gpuStats.numFrames;
			if (true || entry.texture != lastBoundTexture) {
				glBindTexture(GL_TEXTURE_2D, entry.texture);
				UpdateSamplingParams();
				lastBoundTexture = entry.texture;
			}
			DEBUG_LOG(G3D, "Texture at %08x Found in Cache, applying", texaddr);
			return; //Done!
		} else {
			INFO_LOG(G3D, "Texture different or overwritten, reloading at %08x", texaddr);
			glDeleteTextures(1, &entry.texture);
			cache.erase(iter);
		}
	}
	else
	{
		INFO_LOG(G3D,"No texture in cache, decoding...");
	}

	//we have to decode it

	TexCacheEntry entry = {0};

	entry.addr = texaddr;
	entry.hash = texhash;
	entry.format = format;
	entry.frameCounter = gpuStats.numFrames;

	if(format >= GE_TFMT_CLUT4 && format <= GE_TFMT_CLUT32)
	{
		entry.clutformat = clutformat;
		entry.clutaddr = GetClutAddr(clutformat == GE_CMODE_32BIT_ABGR8888 ? 4 : 2);
		entry.cluthash = Memory::Read_U32(entry.clutaddr);
	}
	else
	{
		entry.clutaddr = 0;
	}

	int bufw = gstate.texbufwidth[0] & 0x3ff;
	
	entry.dim = gstate.texsize[0] & 0xF0F;

	int w = 1 << (gstate.texsize[0] & 0xf);
	int h = 1 << ((gstate.texsize[0]>>8) & 0xf);

	for (int i = 0; i < bufw * h; i += 4)
		entry.fullhash += Memory::Read_U32(texaddr + i);

	gstate_c.curTextureWidth=w;
	gstate_c.curTextureHeight=h;
	GLenum dstFmt = 0;
	u32 texByteAlign = 1;

	void *finalBuf = NULL;

	// TODO: Look into using BGRA for 32-bit textures when the GL_EXT_texture_format_BGRA8888 extension is available, as it's faster than RGBA on some chips.

	// TODO: Actually decode the mipmaps.

	switch (format)
	{
	case GE_TFMT_CLUT4:
		dstFmt = getClutDestFormat((GEPaletteFormat)(gstate.clutformat & 3));

		switch (clutformat)
		{
		case GE_CMODE_16BIT_BGR5650:
		case GE_CMODE_16BIT_ABGR5551:
		case GE_CMODE_16BIT_ABGR4444:
			{
			u16 *clut = ReadClut16();
			u32 clutSharingOff = 0;//gstate.mipmapShareClut ? 0 : level * 16;
			texByteAlign = 2;
			if (!(gstate.texmode & 1))
			{
				u32 addr = texaddr;
				for (int i = 0; i < bufw * h; i += 2)
				{
					u8 index = Memory::Read_U8(addr);
					tmpTexBuf16[i + 0] = clut[GetClutIndex((index >> 0) & 0xf) + clutSharingOff];
					tmpTexBuf16[i + 1] = clut[GetClutIndex((index >> 4) & 0xf) + clutSharingOff];
					addr++;
				}
			}
			else
			{
				UnswizzleFromMem(texaddr, 0, level);
				for (int i = 0, j = 0; i < bufw * h; i += 8, j++)
				{
					u32 n = tmpTexBuf32[j];
					u32 k, index;
					for (k = 0; k < 8; k++) {
						index = (n >> (k * 4)) & 0xf;
						tmpTexBuf16[i + k] = clut[GetClutIndex(index) + clutSharingOff];
					}
				}
			}
			finalBuf = tmpTexBuf16;
			}
			break;

		case GE_CMODE_32BIT_ABGR8888:
			{
			u32 *clut = ReadClut32();
			u32 clutSharingOff = 0;//gstate.mipmapShareClut ? 0 : level * 16;
			if (!(gstate.texmode & 1))
			{
				u32 addr = texaddr;
				for (int i = 0; i < bufw * h; i += 2)
				{
					u8 index = Memory::Read_U8(addr);
					tmpTexBuf32[i + 0] = clut[GetClutIndex((index >> 0) & 0xf) + clutSharingOff];
					tmpTexBuf32[i + 1] = clut[GetClutIndex((index >> 4) & 0xf) + clutSharingOff];
					addr++;
				}
			}
			else
			{
				u32 pixels = bufw * h;
				UnswizzleFromMem(texaddr, 0, level);
				for (int i = pixels - 8, j = (pixels / 8) - 1; i >= 0; i -= 8, j--)
				{
					u32 n = tmpTexBuf32[j];
					for (int k = 0; k < 8; k++) {
						u32 index = (n >> (k * 4)) & 0xf;
						tmpTexBuf32[i + k] = clut[GetClutIndex(index) + clutSharingOff];
					}
				}
			}
			finalBuf = tmpTexBuf32;
			}
			break;

		default:
			ERROR_LOG(G3D, "Unknown CLUT4 texture mode %d", (gstate.clutformat & 3));
			return;
		}
		break;

	case GE_TFMT_CLUT8:
		finalBuf = readIndexedTex(level, texaddr, 1);
		dstFmt = getClutDestFormat((GEPaletteFormat)(gstate.clutformat & 3));
		texByteAlign = texByteAlignMap[(gstate.clutformat & 3)];
		break;

	case GE_TFMT_CLUT16:
		finalBuf = readIndexedTex(level, texaddr, 2);
		dstFmt = getClutDestFormat((GEPaletteFormat)(gstate.clutformat & 3));
		texByteAlign = texByteAlignMap[(gstate.clutformat & 3)];
		break;

	case GE_TFMT_CLUT32:
		finalBuf = readIndexedTex(level, texaddr, 4);
		dstFmt = getClutDestFormat((GEPaletteFormat)(gstate.clutformat & 3));
		texByteAlign = texByteAlignMap[(gstate.clutformat & 3)];
		break;

	case GE_TFMT_4444:
	case GE_TFMT_5551:
	case GE_TFMT_5650:
		if (format == GE_TFMT_4444)
			dstFmt = GL_UNSIGNED_SHORT_4_4_4_4;
		else if (format == GE_TFMT_5551)
			dstFmt = GL_UNSIGNED_SHORT_5_5_5_1;
		else if (format == GE_TFMT_5650)
			dstFmt = GL_UNSIGNED_SHORT_5_6_5;
		texByteAlign = 2;

		if (!(gstate.texmode & 1))
		{
			int len = std::max(bufw, w) * h;
			for (int i = 0; i < len; i++)
				tmpTexBuf16[i] = Memory::Read_U16(texaddr + i * 2);
			finalBuf = tmpTexBuf16;
		}
		else
			finalBuf = UnswizzleFromMem(texaddr, 2, level);
		break;

	case GE_TFMT_8888:
		dstFmt = GL_UNSIGNED_BYTE;
		if (!(gstate.texmode & 1))
		{
			int len = bufw * h;
			for (int i = 0; i < len; i++)
				tmpTexBuf32[i] = Memory::Read_U32(texaddr + i * 4);
			finalBuf = tmpTexBuf32;
		}
		else
			finalBuf = UnswizzleFromMem(texaddr, 4, level);
		break;

	case GE_TFMT_DXT1:
		dstFmt = GL_UNSIGNED_BYTE;
		{
			u32 *dst = tmpTexBuf32;
			DXT1Block *src = (DXT1Block*)texptr;

			for (int y = 0; y < h; y += 4)
			{
				u32 blockIndex = (y / 4) * (bufw / 4);
				for (int x = 0; x < std::min(bufw, w); x += 4)
				{
					decodeDXT1Block(dst + bufw * y + x, src + blockIndex, bufw);
					blockIndex++;
				}
			}
			finalBuf = tmpTexBuf32;
			w = (w + 3) & ~3;
		}
		break;

	case GE_TFMT_DXT3:
		dstFmt = GL_UNSIGNED_BYTE;
		{
			u32 *dst = tmpTexBuf32;
			DXT3Block *src = (DXT3Block*)texptr;

			// Alpha is off
			for (int y = 0; y < h; y += 4)
			{
				u32 blockIndex = (y / 4) * (bufw / 4);
				for (int x = 0; x < std::min(bufw, w); x += 4)
				{
					decodeDXT3Block(dst + bufw * y + x, src + blockIndex, bufw);
					blockIndex++;
				}
			}
			w = (w + 3) & ~3;
			finalBuf = tmpTexBuf32;
		}
		break;

	case GE_TFMT_DXT5:
		ERROR_LOG(G3D, "Unhandled compressed texture, format %i! swizzle=%i", format, gstate.texmode & 1);
		dstFmt = GL_UNSIGNED_BYTE;
		{
			u32 *dst = tmpTexBuf32;
			DXT5Block *src = (DXT5Block*)texptr;

			// Alpha is almost right
			for (int y = 0; y < h; y += 4)
			{
				u32 blockIndex = (y / 4) * (bufw / 4);
				for (int x = 0; x < std::min(bufw, w); x += 4)
				{
					decodeDXT5Block(dst + bufw * y + x, src + blockIndex, bufw);
					blockIndex++;
				}
			}
			w = (w + 3) & ~3;
			finalBuf = tmpTexBuf32;
		}
		break;

	default:
		ERROR_LOG(G3D, "Unknown Texture Format %d!!!", format);
		finalBuf = tmpTexBuf32;
		return;
	}

	if (!finalBuf) {
		ERROR_LOG(G3D, "NO finalbuf! Will crash!");
	}

	convertColors((u8*)finalBuf, dstFmt, bufw * h);

	if (w != bufw) {
		int pixelSize;
		switch (dstFmt) {
		case GL_UNSIGNED_SHORT_4_4_4_4:
		case GL_UNSIGNED_SHORT_5_5_5_1:
		case GL_UNSIGNED_SHORT_5_6_5:
			pixelSize = 2;
			break;
		default:
			pixelSize = 4;
			break;
		}
		// Need to rearrange the buffer to simulate GL_UNPACK_ROW_LENGTH etc.
		int inRowBytes = bufw * pixelSize;
		int outRowBytes = w * pixelSize;
		const u8 *read = (const u8 *)finalBuf;
		u8 *write = 0;
		if (w > bufw) {
			write = (u8 *)tmpTexBufRearrange;
			finalBuf = tmpTexBufRearrange;
		} else {
			write = (u8 *)finalBuf;
		}
		for (int y = 0; y < h; y++) {
			memmove(write, read, outRowBytes);
			read += inRowBytes;
			write += outRowBytes;
		}
	}

	gpuStats.numTexturesDecoded++;
	// Can restore these and remove the above fixup on some platforms.
	//glPixelStorei(GL_UNPACK_ROW_LENGTH, bufw);
	glPixelStorei(GL_UNPACK_ALIGNMENT, texByteAlign);
	//glPixelStorei(GL_PACK_ROW_LENGTH, bufw);
	glPixelStorei(GL_PACK_ALIGNMENT, texByteAlign);

	INFO_LOG(G3D, "Creating texture %i from %08x: %i x %i (stride: %i). fmt: %i", entry.texture, entry.addr, w, h, bufw, entry.format);

	glGenTextures(1, &entry.texture);
	glBindTexture(GL_TEXTURE_2D, entry.texture);
	lastBoundTexture = entry.texture;
	GLuint components = dstFmt == GL_UNSIGNED_SHORT_5_6_5 ? GL_RGB : GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, components, w, h, 0, components, dstFmt, finalBuf);
	// glGenerateMipmap(GL_TEXTURE_2D);
	UpdateSamplingParams();

	//glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	//glPixelStorei(GL_PACK_ROW_LENGTH, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	cache[cachekey] = entry;
}
