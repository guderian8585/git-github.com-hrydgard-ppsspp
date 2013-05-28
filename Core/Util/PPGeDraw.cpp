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

#include "Core/Util/PPGeDraw.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceGe.h"
#include "Core/MemMap.h"
#include "image/zim_load.h"
#include "gfx/texture_atlas.h"
#include "gfx/gl_common.h"
#include "util/text/utf8.h"
#include "MathUtil.h"
#include "Core/System.h"

static u32 atlasPtr;
static int atlasWidth;
static int atlasHeight;

struct PPGeVertex {
	u16 u, v;
	u32 color;
	float x, y, z;
};

static u32 savedContextPtr;
static u32 savedContextSize = 512 * 4;

// Display list writer
static u32 dlPtr;
static u32 dlWritePtr;
static u32 dlSize = 0x10000; // should be enough for a frame of gui...

static u32 dataPtr;
static u32 dataWritePtr;
static u32 dataSize = 0x10000; // should be enough for a frame of gui...

static u32 palettePtr;
static u32 paletteSize = 2 * 16;


// Vertex collector
static u32 vertexStart;
static u32 vertexCount;

//only 0xFFFFFF of data is used
static void WriteCmd(u8 cmd, u32 data) {
	Memory::Write_U32((cmd << 24) | (data & 0xFFFFFF), dlWritePtr);
	dlWritePtr += 4;
}

static void WriteCmdAddrWithBase(u8 cmd, u32 addr) {
	WriteCmd(GE_CMD_BASE, (addr >> 8) & 0xFF0000);
	WriteCmd(cmd, addr & 0xFFFFFF);
}

/*
static void WriteCmdFloat(u8 cmd, float f) {
	union {
		float fl;
		u32 u;
	} conv;
	conv.fl = f;
	WriteCmd(cmd, conv.u >> 8);
}*/

static void BeginVertexData() {
	vertexCount = 0;
	vertexStart = dataWritePtr;
}

static void Vertex(float x, float y, float u, float v, int tw, int th, u32 color = 0xFFFFFFFF)
{
	PPGeVertex vtx;
	vtx.x = x - 0.5f; vtx.y = y - 0.5f; vtx.z = 0;
	vtx.u = u * tw - 0.5f; vtx.v = v * th - 0.5f;
	vtx.color = color;
	Memory::WriteStruct(dataWritePtr, &vtx);
	vertexCount++;
	dataWritePtr += sizeof(vtx);
}

static void EndVertexDataAndDraw(int prim) {
	WriteCmdAddrWithBase(GE_CMD_VADDR, vertexStart);
	WriteCmd(GE_CMD_PRIM, (prim << 16) | vertexCount);
}

static u32 __PPGeDoAlloc(u32 &size, bool fromTop, const char *name) {
	u32 ptr = kernelMemory.Alloc(size, fromTop, name);
	// Didn't get it.
	if (ptr == (u32)-1)
		return 0;
	return ptr;
}

void __PPGeInit()
{
	if (PSP_CoreParameter().gpuCore == GPU_NULL) {
		// Let's just not bother.
		dlPtr = 0;
		NOTICE_LOG(HLE, "Not initializing PPGe - GPU is NullGpu");
		return;
	}
	u8 *imageData;
	int width;
	int height;
	int flags;
	if (!LoadZIM("ppge_atlas.zim", &width, &height, &flags, &imageData)) {
		PanicAlert("Failed to load ppge_atlas.zim.\n\nPlace it in the directory \"assets\" under your PPSSPP directory.");
		ERROR_LOG(HLE, "PPGe init failed - no atlas texture. PPGe stuff will not be drawn.");
		return;
	}

	u32 atlasSize = height * width / 2;  // it's a 4-bit paletted texture in ram
	atlasWidth = width;
	atlasHeight = height;
	dlPtr = __PPGeDoAlloc(dlSize, false, "PPGe Display List");
	dataPtr = __PPGeDoAlloc(dataSize, false, "PPGe Vertex Data");
	savedContextPtr = __PPGeDoAlloc(savedContextSize, false, "PPGe Saved Context");
	atlasPtr = __PPGeDoAlloc(atlasSize, false, "PPGe Atlas Texture");
	palettePtr = __PPGeDoAlloc(paletteSize, false, "PPGe Texture Palette");

	// Generate 16-greyscale palette. All PPGe graphics are greyscale so we can use a tiny paletted texture.
	u16 *palette = (u16 *)Memory::GetPointer(palettePtr);
	for (int i = 0; i < 16; i++) {
		int val = i;
		palette[i] = (val << 12) | 0xFFF;
	}

	u16 *imagePtr = (u16 *)imageData;
	u8 *ramPtr = (u8 *)Memory::GetPointer(atlasPtr);

	// Palettize to 4-bit, the easy way.
	for (int i = 0; i < width * height / 2; i++) {
		u16 c1 = imagePtr[i*2];
		u16 c2 = imagePtr[i*2+1];
		int a1 = c1 & 0xF;
		int a2 = c2 & 0xF;
		u8 cval = (a2 << 4) | a1;
		ramPtr[i] = cval;
	}
	
	free(imageData);

	DEBUG_LOG(HLE, "PPGe drawing library initialized. DL: %08x Data: %08x Atlas: %08x (%i) Ctx: %08x",
		dlPtr, dataPtr, atlasPtr, atlasSize, savedContextPtr);
}

void __PPGeDoState(PointerWrap &p)
{
	p.Do(atlasPtr);
	p.Do(atlasWidth);
	p.Do(atlasHeight);
	p.Do(palettePtr);

	p.Do(savedContextPtr);
	p.Do(savedContextSize);

	p.Do(dlPtr);
	p.Do(dlWritePtr);
	p.Do(dlSize);

	p.Do(dataPtr);
	p.Do(dataWritePtr);
	p.Do(dataSize);

	p.Do(vertexStart);
	p.Do(vertexCount);

	p.DoMarker("PPGeDraw");
}

void __PPGeShutdown()
{
	if (atlasPtr)
		kernelMemory.Free(atlasPtr);
	if (dataPtr)
		kernelMemory.Free(dataPtr);
	if (dlPtr)
		kernelMemory.Free(dlPtr);
	if (savedContextPtr)
		kernelMemory.Free(savedContextPtr);
	if (palettePtr)
		kernelMemory.Free(palettePtr);

	atlasPtr = 0;
	dataPtr = 0;
	dlPtr = 0;
	savedContextPtr = 0;
}

void PPGeBegin()
{
	if (!dlPtr)
		return;

	// Reset write pointers to start of command and data buffers.
	dlWritePtr = dlPtr;
	dataWritePtr = dataPtr;

	// Set up the correct states for UI drawing
	WriteCmd(GE_CMD_OFFSETADDR, 0);
	WriteCmd(GE_CMD_ALPHABLENDENABLE, 1);
	WriteCmd(GE_CMD_BLENDMODE, 2 | (3 << 4));
	WriteCmd(GE_CMD_ALPHATESTENABLE, 0);
	WriteCmd(GE_CMD_COLORTESTENABLE, 0); 
	WriteCmd(GE_CMD_ZTESTENABLE, 0);
	WriteCmd(GE_CMD_LIGHTINGENABLE, 0);
	WriteCmd(GE_CMD_FOGENABLE, 0);
	WriteCmd(GE_CMD_STENCILTESTENABLE, 0);
	WriteCmd(GE_CMD_CULLFACEENABLE, 0);
	WriteCmd(GE_CMD_CLEARMODE, 0);  // Normal mode
	WriteCmd(GE_CMD_MASKRGB, 0);
	WriteCmd(GE_CMD_MASKALPHA, 0);

	PPGeSetDefaultTexture();

	WriteCmd(GE_CMD_SCISSOR1, (0 << 10) | 0);
	WriteCmd(GE_CMD_SCISSOR2, (1023 << 10) | 1023);
	WriteCmd(GE_CMD_MINZ, 0);
	WriteCmd(GE_CMD_MAXZ, 0xFFFF);

	// Through mode, so we don't have to bother with matrices
	WriteCmd(GE_CMD_VERTEXTYPE, GE_VTYPE_TC_16BIT | GE_VTYPE_COL_8888 | GE_VTYPE_POS_FLOAT | GE_VTYPE_THROUGH);
}

void PPGeEnd()
{
	if (!dlPtr)
		return;

	WriteCmd(GE_CMD_FINISH, 0);
	WriteCmd(GE_CMD_END, 0);

	if (dataWritePtr > dataPtr) {
		sceGeBreak(0);
		sceGeSaveContext(savedContextPtr);
		gpu->EnableInterrupts(false);

		// We actually drew something
		u32 list = sceGeListEnQueueHead(dlPtr, dlWritePtr, -1, 0);
		DEBUG_LOG(HLE, "PPGe enqueued display list %i", list);
		gpu->EnableInterrupts(true);
		sceGeContinue();
		sceGeRestoreContext(savedContextPtr);
	}
}

static const AtlasChar *PPGeGetChar(const AtlasFont &atlasfont, unsigned int cval)
{
	const AtlasChar *c = atlasfont.getChar(cval);
	if (c == NULL) {
		// Try to use a replacement character, these come from the below table.
		// http://unicode.org/cldr/charts/supplemental/character_fallback_substitutions.html
		switch (cval) {
		case 0x00A0: // NO-BREAK SPACE
		case 0x2000: // EN QUAD
		case 0x2001: // EM QUAD
		case 0x2002: // EN SPACE
		case 0x2003: // EM SPACE
		case 0x2004: // THREE-PER-EM SPACE
		case 0x2005: // FOUR-PER-EM SPACE
		case 0x2006: // SIX-PER-EM SPACE
		case 0x2007: // FIGURE SPACE
		case 0x2008: // PUNCTUATION SPACE
		case 0x2009: // THIN SPACE
		case 0x200A: // HAIR SPACE
		case 0x202F: // NARROW NO-BREAK SPACE
		case 0x205F: // MEDIUM MATHEMATICAL
		case 0x3000: // IDEOGRAPHIC SPACE
			c = atlasfont.getChar(0x0020);
			break;

		default:
			c = atlasfont.getChar(0xFFFD);
			break;
		}
		if (c == NULL)
			c = atlasfont.getChar('?');
	}
	return c;
}

static void PPGeMeasureText(const char *text, float scale, float *w, float *h) {
	const AtlasFont &atlasfont = *ppge_atlas.fonts[0];
	unsigned int cval;
	float wacc = 0;
	float maxw = 0;
	int lines = 1;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		cval = utf.next();
		if (cval == '\n') {
			if (wacc > maxw) maxw = wacc;
			wacc = 0;
			lines++;
		}
		const AtlasChar *c = PPGeGetChar(atlasfont, cval);
		if (c) {
			wacc += c->wx * scale;
		}
	}
	if (wacc > maxw) maxw = wacc;
	if (w) *w = maxw;
	if (h) *h = atlasfont.height * scale * lines;
}

static void PPGeDoAlign(int flags, float *x, float *y, float *w, float *h) {
	if (flags & PPGE_ALIGN_HCENTER) *x -= *w / 2;
	if (flags & PPGE_ALIGN_RIGHT) *x -= *w;
	if (flags & PPGE_ALIGN_VCENTER) *y -= *h / 2;
	if (flags & PPGE_ALIGN_BOTTOM) *y -= *h;
}

// Draws some text using the one font we have.
// Mostly stolen from DrawBuffer.
void PPGeDrawText(const char *text, float x, float y, int align, float scale, u32 color) {
	if (!dlPtr)
		return;
	const AtlasFont &atlasfont = *ppge_atlas.fonts[0];
	unsigned int cval;
	float w, h;
	PPGeMeasureText(text, scale, &w, &h);
	if (align) {
		PPGeDoAlign(align, &x, &y, &w, &h);
	}
	BeginVertexData();
	y += atlasfont.ascend*scale;
	float sx = x;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		cval = utf.next();
		if (cval == '\n') {
			// This is not correct when centering or right-justifying, need to set x depending on line width (tricky)
			y += atlasfont.height * scale;
			x = sx;
			continue;
		}
		const AtlasChar *ch = PPGeGetChar(atlasfont, cval);
		if (ch) {
			const AtlasChar &c = *ch;
			float cx1 = x + c.ox * scale;
			float cy1 = y + c.oy * scale;
			float cx2 = x + (c.ox + c.pw) * scale;
			float cy2 = y + (c.oy + c.ph) * scale;
			Vertex(cx1, cy1, c.sx, c.sy, atlasWidth, atlasHeight, color);
			Vertex(cx2, cy2, c.ex, c.ey, atlasWidth, atlasHeight, color);
			x += c.wx * scale;
		}
	}
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

static float NextWordWidth(UTF8 utf, const AtlasFont &atlasfont, float scale) {
	float w = 0.0;
	bool finished = false;
	while (!utf.end() && !finished) {
		u32 cval = utf.next();
		const AtlasChar *ch = PPGeGetChar(atlasfont, cval);
		if (!ch) {
			continue;
		}

		switch (cval) {
		// TODO: This list of punctuation is very incomplete.
		case ',':
		case '.':
		case ':':
		case '!':
		case ')':
		case '?':
		case 0x3001: // IDEOGRAPHIC COMMA
		case 0x3002: // IDEOGRAPHIC FULL STOP
		case 0x06D4: // ARABIC FULL STOP
		case 0xFF01: // FULLWIDTH EXCLAMATION MARK
		case 0xFF09: // FULLWIDTH RIGHT PARENTHESIS
		case 0xFF1F: // FULLWIDTH QUESTION MARK
			// Count this character (punctuation is so clingy), but then we're done.
			w += ch->wx * scale;
			finished = true;
			break;

		case ' ':
		case '\t':
		case '\r':
		case '\n':
		case 0x3000: // IDEOGRAPHIC SPACE
			finished = true;
			break;

		default:
			w += ch->wx * scale;
			break;
		}
	}

	return w;
}

void PPGeDrawTextWrapped(const char *text, float x, float y, float wrapWidth, int align, float scale, u32 color) {
	if (!dlPtr)
		return;
	const AtlasFont &atlasfont = *ppge_atlas.fonts[0];
	unsigned int cval;
	float w, h;
	// TODO: Could ideally try to handle center, right align better.
	PPGeMeasureText(text, scale, &w, &h);
	if (align && w < wrapWidth) {
		PPGeDoAlign(align, &x, &y, &w, &h);
	}
	BeginVertexData();
	y += atlasfont.ascend*scale;
	float sx = x;
	bool skipWrap = false;
	const float wrapCutoff = wrapWidth * 0.8f;
	UTF8 utf(text);
	while (true) {
		if (utf.end())
			break;
		cval = utf.next();
		if (cval == '\n') {
			// This is not correct when centering or right-justifying, need to set x depending on line width (tricky)
			y += atlasfont.height * scale;
			x = sx;
			skipWrap = false;
			continue;
		}
		const AtlasChar *ch = PPGeGetChar(atlasfont, cval);
		if (ch) {
			const AtlasChar &c = *ch;
			float cx1 = x + c.ox * scale;
			float cy1 = y + c.oy * scale;
			float cx2 = x + (c.ox + c.pw) * scale;
			float cy2 = y + (c.oy + c.ph) * scale;
			Vertex(cx1, cy1, c.sx, c.sy, atlasWidth, atlasHeight, color);
			Vertex(cx2, cy2, c.ex, c.ey, atlasWidth, atlasHeight, color);
			x += c.wx * scale;

			float nextWidth = NextWordWidth(utf, atlasfont, scale);
			// This word is too long, and we're not near the end of the line.
			if (nextWidth > wrapCutoff && wrapWidth + sx - x > wrapCutoff) {
				skipWrap = true;
			}
			// Pretend the word is only a single character long.
			if (skipWrap) {
				nextWidth = c.wx * scale;
			}

			if (x + nextWidth > wrapWidth) {
				// This is not correct when centering or right-justifying, need to set x depending on line width (tricky)
				y += atlasfont.height * scale;
				x = sx;
				skipWrap = false;
			}
		}
	}
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

// Draws a "4-patch" for button-like things that can be resized
void PPGeDraw4Patch(int atlasImage, float x, float y, float w, float h, u32 color)
{
	if (!dlPtr)
		return;
	const AtlasImage &img = ppge_images[atlasImage];
	float borderx = img.w / 20;
	float bordery = img.h / 20;
	float u1 = img.u1, uhalf = (img.u1 + img.u2) / 2, u2 = img.u2;
	float v1 = img.v1, vhalf = (img.v1 + img.v2) / 2, v2 = img.v2;
	float xmid1 = x + borderx;
	float xmid2 = x + w - borderx;
	float ymid1 = y + bordery;
	float ymid2 = y + h - bordery;
	float x2 = x + w;
	float y2 = y + h;
	BeginVertexData();
	// Top row
	Vertex(x, y, u1, v1, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, y, uhalf, v1, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, y, uhalf, v1, atlasWidth, atlasHeight, color);
	Vertex(x2, ymid1, u2, vhalf, atlasWidth, atlasHeight, color);
	// Middle row
	Vertex(x, ymid1, u1, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid1, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(x2, ymid2, u2, v2, atlasWidth, atlasHeight, color);
	// Bottom row
	Vertex(x, ymid2, u1, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid1, y2, uhalf, v2, atlasWidth, atlasHeight, color);
	Vertex(xmid1, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(xmid2, y2, uhalf, v2, atlasWidth, atlasHeight, color);
	Vertex(xmid2, ymid2, uhalf, vhalf, atlasWidth, atlasHeight, color);
	Vertex(x2, y2, u2, v2, atlasWidth, atlasHeight, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeDrawRect(float x1, float y1, float x2, float y2, u32 color)
{
	if (!dlPtr)
		return;

	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 0);

	BeginVertexData();
	Vertex(x1, y1, 0, 0, 0, 0, color);
	Vertex(x2, y2, 0, 0, 0, 0, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);

	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 1);
}

// Just blits an image to the screen, multiplied with the color.
void PPGeDrawImage(int atlasImage, float x, float y, int align, u32 color)
{
	if (!dlPtr)
		return;

	const AtlasImage &img = ppge_atlas.images[atlasImage];
	float w = img.w;
	float h = img.h;
	BeginVertexData();
	Vertex(x, y, img.u1, img.v1, atlasWidth, atlasHeight, color);
	Vertex(x + w, y + h, img.u2, img.v2, atlasWidth, atlasHeight, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeDrawImage(int atlasImage, float x, float y, float w, float h, int align, u32 color)
{
	if (!dlPtr)
		return;

	const AtlasImage &img = ppge_atlas.images[atlasImage];
	BeginVertexData();
	Vertex(x, y, img.u1, img.v1, atlasWidth, atlasHeight, color);
	Vertex(x + w, y + h, img.u2, img.v2, atlasWidth, atlasHeight, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeDrawImage(float x, float y, float w, float h, float u1, float v1, float u2, float v2, int tw, int th, u32 color)
{
	if (!dlPtr)
		return;
	BeginVertexData();
	Vertex(x, y, u1, v1, tw, th, color);
	Vertex(x + w, y + h, u2, v2, tw, th, color);
	EndVertexDataAndDraw(GE_PRIM_RECTANGLES);
}

void PPGeSetDefaultTexture()
{
	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 1);
	int wp2 = GetPow2(atlasWidth);
	int hp2 = GetPow2(atlasHeight);
	WriteCmd(GE_CMD_CLUTADDR, palettePtr & 0xFFFFF0);
	WriteCmd(GE_CMD_CLUTADDRUPPER, (palettePtr & 0xFF000000) >> 8);
	WriteCmd(GE_CMD_CLUTFORMAT, 0x00FF02);
	WriteCmd(GE_CMD_LOADCLUT, 2);
	WriteCmd(GE_CMD_TEXSIZE0, wp2 | (hp2 << 8));
	WriteCmd(GE_CMD_TEXMAPMODE, 0 | (1 << 8));
	WriteCmd(GE_CMD_TEXMODE, 0);
	WriteCmd(GE_CMD_TEXFORMAT, GE_TFMT_CLUT4);  // 4-bit CLUT
	WriteCmd(GE_CMD_TEXFILTER, (1 << 8) | 1);   // mag = LINEAR min = LINEAR
	WriteCmd(GE_CMD_TEXWRAP, (1 << 8) | 1);  // clamp texture wrapping
	WriteCmd(GE_CMD_TEXFUNC, (0 << 16) | (1 << 8) | 0);  // RGBA texture reads, modulate, no color doubling
	WriteCmd(GE_CMD_TEXADDR0, atlasPtr & 0xFFFFF0);
	WriteCmd(GE_CMD_TEXBUFWIDTH0, atlasWidth | ((atlasPtr & 0xFF000000) >> 8));
	WriteCmd(GE_CMD_TEXFLUSH, 0);
}

void PPGeSetTexture(u32 dataAddr, int width, int height)
{
	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 1);
	int wp2 = GetPow2(width);
	int hp2 = GetPow2(height);
	WriteCmd(GE_CMD_TEXSIZE0, wp2 | (hp2 << 8));
	WriteCmd(GE_CMD_TEXMAPMODE, 0 | (1 << 8));
	WriteCmd(GE_CMD_TEXMODE, 0);
	WriteCmd(GE_CMD_TEXFORMAT, GE_TFMT_8888);  // 4444
	WriteCmd(GE_CMD_TEXFILTER, (1 << 8) | 1);   // mag = LINEAR min = LINEAR
	WriteCmd(GE_CMD_TEXWRAP, (1 << 8) | 1);  // clamp texture wrapping
	WriteCmd(GE_CMD_TEXFUNC, (0 << 16) | (1 << 8) | 0);  // RGBA texture reads, modulate, no color doubling
	WriteCmd(GE_CMD_TEXADDR0, dataAddr & 0xFFFFF0);
	WriteCmd(GE_CMD_TEXBUFWIDTH0, width | ((dataAddr & 0xFF000000) >> 8));
	WriteCmd(GE_CMD_TEXFLUSH, 0);
}

void PPGeDisableTexture()
{
	WriteCmd(GE_CMD_TEXTUREMAPENABLE, 0);
}

