// Copyright (c) 2013- PPSSPP Project.

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

#include "Common/Data/Convert/ColorConv.h"
#include "GPU/GPUState.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/SoftGpu.h"

using namespace Math3D;

namespace Rasterizer {

void Init() {
}

void Shutdown() {
}

bool DescribeCodePtr(const u8 *ptr, std::string &name) {
	return false;
}

static inline u8 GetPixelStencil(GEBufferFormat fmt, int x, int y) {
	if (fmt == GE_FORMAT_565) {
		// Always treated as 0 for comparison purposes.
		return 0;
	} else if (fmt == GE_FORMAT_5551) {
		return ((fb.Get16(x, y, gstate.FrameBufStride()) & 0x8000) != 0) ? 0xFF : 0;
	} else if (fmt == GE_FORMAT_4444) {
		return Convert4To8(fb.Get16(x, y, gstate.FrameBufStride()) >> 12);
	} else {
		return fb.Get32(x, y, gstate.FrameBufStride()) >> 24;
	}
}

static inline void SetPixelStencil(GEBufferFormat fmt, int x, int y, u8 value) {
	if (fmt == GE_FORMAT_565) {
		// Do nothing
	} else if (fmt == GE_FORMAT_5551) {
		if ((gstate.getStencilWriteMask() & 0x80) == 0) {
			u16 pixel = fb.Get16(x, y, gstate.FrameBufStride()) & ~0x8000;
			pixel |= (value & 0x80) << 8;
			fb.Set16(x, y, gstate.FrameBufStride(), pixel);
		}
	} else if (fmt == GE_FORMAT_4444) {
		const u16 write_mask = (gstate.getStencilWriteMask() << 8) | 0x0FFF;
		u16 pixel = fb.Get16(x, y, gstate.FrameBufStride()) & write_mask;
		pixel |= ((u16)value << 8) & ~write_mask;
		fb.Set16(x, y, gstate.FrameBufStride(), pixel);
	} else {
		const u32 write_mask = (gstate.getStencilWriteMask() << 24) | 0x00FFFFFF;
		u32 pixel = fb.Get32(x, y, gstate.FrameBufStride()) & write_mask;
		pixel |= ((u32)value << 24) & ~write_mask;
		fb.Set32(x, y, gstate.FrameBufStride(), pixel);
	}
}

static inline u16 GetPixelDepth(int x, int y) {
	return depthbuf.Get16(x, y, gstate.DepthBufStride());
}

static inline void SetPixelDepth(int x, int y, u16 value) {
	depthbuf.Set16(x, y, gstate.DepthBufStride(), value);
}

// NOTE: These likely aren't endian safe
static inline u32 GetPixelColor(GEBufferFormat fmt, int x, int y) {
	switch (fmt) {
	case GE_FORMAT_565:
		// A should be zero for the purposes of alpha blending.
		return RGB565ToRGBA8888(fb.Get16(x, y, gstate.FrameBufStride())) & 0x00FFFFFF;

	case GE_FORMAT_5551:
		return RGBA5551ToRGBA8888(fb.Get16(x, y, gstate.FrameBufStride()));

	case GE_FORMAT_4444:
		return RGBA4444ToRGBA8888(fb.Get16(x, y, gstate.FrameBufStride()));

	case GE_FORMAT_8888:
		return fb.Get32(x, y, gstate.FrameBufStride());

	default:
		return 0;
	}
}

static inline void SetPixelColor(GEBufferFormat fmt, int x, int y, u32 value) {
	switch (fmt) {
	case GE_FORMAT_565:
		fb.Set16(x, y, gstate.FrameBufStride(), RGBA8888ToRGB565(value));
		break;

	case GE_FORMAT_5551:
		fb.Set16(x, y, gstate.FrameBufStride(), RGBA8888ToRGBA5551(value));
		break;

	case GE_FORMAT_4444:
		fb.Set16(x, y, gstate.FrameBufStride(), RGBA8888ToRGBA4444(value));
		break;

	case GE_FORMAT_8888:
		fb.Set32(x, y, gstate.FrameBufStride(), value);
		break;

	default:
		break;
	}
}

static inline bool AlphaTestPassed(const PixelFuncID &pixelID, int alpha) {
	const u8 ref = pixelID.alphaTestRef;
	if (pixelID.hasAlphaTestMask)
		alpha &= gstate.getAlphaTestMask();

	switch (pixelID.AlphaTestFunc()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
		return (alpha == ref);

	case GE_COMP_NOTEQUAL:
		return (alpha != ref);

	case GE_COMP_LESS:
		return (alpha < ref);

	case GE_COMP_LEQUAL:
		return (alpha <= ref);

	case GE_COMP_GREATER:
		return (alpha > ref);

	case GE_COMP_GEQUAL:
		return (alpha >= ref);
	}
	return true;
}

static inline bool ColorTestPassed(const Vec3<int> &color) {
	const u32 mask = gstate.getColorTestMask();
	const u32 c = color.ToRGB() & mask;
	const u32 ref = gstate.getColorTestRef() & mask;
	switch (gstate.getColorTestFunction()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
		return c == ref;

	case GE_COMP_NOTEQUAL:
		return c != ref;
	}
	return true;
}

static inline bool StencilTestPassed(const PixelFuncID &pixelID, u8 stencil) {
	if (pixelID.hasStencilTestMask)
		stencil &= gstate.getStencilTestMask();
	u8 ref = pixelID.stencilTestRef;
	switch (pixelID.StencilTestFunc()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
		return ref == stencil;

	case GE_COMP_NOTEQUAL:
		return ref != stencil;

	case GE_COMP_LESS:
		return ref < stencil;

	case GE_COMP_LEQUAL:
		return ref <= stencil;

	case GE_COMP_GREATER:
		return ref > stencil;

	case GE_COMP_GEQUAL:
		return ref >= stencil;
	}
	return true;
}

static inline u8 ApplyStencilOp(GEBufferFormat fmt, GEStencilOp op, u8 old_stencil) {
	switch (op) {
	case GE_STENCILOP_KEEP:
		return old_stencil;

	case GE_STENCILOP_ZERO:
		return 0;

	case GE_STENCILOP_REPLACE:
		return gstate.getStencilTestRef();

	case GE_STENCILOP_INVERT:
		return ~old_stencil;

	case GE_STENCILOP_INCR:
		switch (fmt) {
		case GE_FORMAT_8888:
			if (old_stencil != 0xFF) {
				return old_stencil + 1;
			}
			return old_stencil;
		case GE_FORMAT_5551:
			return 0xFF;
		case GE_FORMAT_4444:
			if (old_stencil < 0xF0) {
				return old_stencil + 0x10;
			}
			return old_stencil;
		default:
			return old_stencil;
		}
		break;

	case GE_STENCILOP_DECR:
		switch (fmt) {
		case GE_FORMAT_4444:
			if (old_stencil >= 0x10)
				return old_stencil - 0x10;
			break;
		case GE_FORMAT_5551:
			return 0;
		default:
			if (old_stencil != 0)
				return old_stencil - 1;
			return old_stencil;
		}
		break;
	}

	return old_stencil;
}

static inline bool DepthTestPassed(GEComparison func, int x, int y, u16 z) {
	u16 reference_z = GetPixelDepth(x, y);

	switch (func) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
		return (z == reference_z);

	case GE_COMP_NOTEQUAL:
		return (z != reference_z);

	case GE_COMP_LESS:
		return (z < reference_z);

	case GE_COMP_LEQUAL:
		return (z <= reference_z);

	case GE_COMP_GREATER:
		return (z > reference_z);

	case GE_COMP_GEQUAL:
		return (z >= reference_z);

	default:
		return 0;
	}
}

static inline u32 ApplyLogicOp(GELogicOp op, u32 old_color, u32 new_color) {
	// All of the operations here intentionally preserve alpha/stencil.
	switch (op) {
	case GE_LOGIC_CLEAR:
		new_color &= 0xFF000000;
		break;

	case GE_LOGIC_AND:
		new_color = new_color & (old_color | 0xFF000000);
		break;

	case GE_LOGIC_AND_REVERSE:
		new_color = new_color & (~old_color | 0xFF000000);
		break;

	case GE_LOGIC_COPY:
		// No change to new_color.
		break;

	case GE_LOGIC_AND_INVERTED:
		new_color = (~new_color & (old_color & 0x00FFFFFF)) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_NOOP:
		new_color = (old_color & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_XOR:
		new_color = new_color ^ (old_color & 0x00FFFFFF);
		break;

	case GE_LOGIC_OR:
		new_color = new_color | (old_color & 0x00FFFFFF);
		break;

	case GE_LOGIC_NOR:
		new_color = (~(new_color | old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_EQUIV:
		new_color = (~(new_color ^ old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_INVERTED:
		new_color = (~old_color & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_OR_REVERSE:
		new_color = new_color | (~old_color & 0x00FFFFFF);
		break;

	case GE_LOGIC_COPY_INVERTED:
		new_color = (~new_color & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_OR_INVERTED:
		new_color = ((~new_color | old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_NAND:
		new_color = (~(new_color & old_color) & 0x00FFFFFF) | (new_color & 0xFF000000);
		break;

	case GE_LOGIC_SET:
		new_color |= 0x00FFFFFF;
		break;
	}

	return new_color;
}

template <bool clearMode, GEBufferFormat fbFormat>
inline void DrawSinglePixel(int x, int y, int z, int fog, const Vec4<int> &color_in, const PixelFuncID &pixelID) {
	Vec4<int> prim_color = color_in.Clamp(0, 255);
	// Depth range test - applied in clear mode, if not through mode.
	if (pixelID.applyDepthRange)
		if (z < gstate.getDepthRangeMin() || z > gstate.getDepthRangeMax())
			return;

	if (pixelID.AlphaTestFunc() != GE_COMP_ALWAYS && !clearMode)
		if (!AlphaTestPassed(pixelID, prim_color.a()))
			return;

	// Fog is applied prior to color test.
	if (pixelID.applyFog && !clearMode) {
		Vec3<int> fogColor = Vec3<int>::FromRGB(gstate.fogcolor);
		fogColor = (prim_color.rgb() * fog + fogColor * (255 - fog)) / 255;
		prim_color.r() = fogColor.r();
		prim_color.g() = fogColor.g();
		prim_color.b() = fogColor.b();
	}

	if (pixelID.colorTest && !clearMode)
		if (!ColorTestPassed(prim_color.rgb()))
			return;

	// In clear mode, it uses the alpha color as stencil.
	u8 stencil = clearMode ? prim_color.a() : GetPixelStencil(fbFormat, x, y);
	if (clearMode) {
		if (pixelID.DepthClear())
			SetPixelDepth(x, y, z);
	} else if (pixelID.stencilTest) {
		if (!StencilTestPassed(pixelID, stencil)) {
			stencil = ApplyStencilOp(fbFormat, GEStencilOp(pixelID.sFail), stencil);
			SetPixelStencil(fbFormat, x, y, stencil);
			return;
		}

		// Also apply depth at the same time.  If disabled, same as passing.
		if (pixelID.DepthTestFunc() != GE_COMP_ALWAYS && !DepthTestPassed(pixelID.DepthTestFunc(), x, y, z)) {
			stencil = ApplyStencilOp(fbFormat, GEStencilOp(pixelID.zFail), stencil);
			SetPixelStencil(fbFormat, x, y, stencil);
			return;
		}

		stencil = ApplyStencilOp(fbFormat, GEStencilOp(pixelID.zPass), stencil);
	} else {
		if (pixelID.DepthTestFunc() != GE_COMP_ALWAYS && !DepthTestPassed(pixelID.DepthTestFunc(), x, y, z)) {
			return;
		}
	}

	if (pixelID.depthWrite && !clearMode)
		SetPixelDepth(x, y, z);

	const u32 old_color = GetPixelColor(fbFormat, x, y);
	u32 new_color;

	// Dithering happens before the logic op and regardless of framebuffer format or clear mode.
	// We do it while alpha blending because it happens before clamping.
	if (pixelID.alphaBlend && !clearMode) {
		const Vec4<int> dst = Vec4<int>::FromRGBA(old_color);
		Vec3<int> blended = AlphaBlendingResult(pixelID, prim_color, dst);
		if (pixelID.dithering) {
			blended += Vec3<int>::AssignToAll(gstate.getDitherValue(x, y));
		}

		// ToRGB() always automatically clamps.
		new_color = blended.ToRGB();
		new_color |= stencil << 24;
	} else {
		if (pixelID.dithering) {
			// We'll discard alpha anyway.
			prim_color += Vec4<int>::AssignToAll(gstate.getDitherValue(x, y));
		}

#if defined(_M_SSE)
		new_color = Vec3<int>(prim_color.ivec).ToRGB();
		new_color |= stencil << 24;
#else
		new_color = Vec4<int>(prim_color.r(), prim_color.g(), prim_color.b(), stencil).ToRGBA();
#endif
	}

	// Logic ops are applied after blending (if blending is enabled.)
	if (pixelID.applyLogicOp && !clearMode) {
		// Logic ops don't affect stencil, which happens inside ApplyLogicOp.
		new_color = ApplyLogicOp(gstate.getLogicOp(), old_color, new_color);
	}

	if (clearMode) {
		new_color = (new_color & ~gstate.getClearModeColorMask()) | (old_color & gstate.getClearModeColorMask());
	}
	new_color = (new_color & ~gstate.getColorMask()) | (old_color & gstate.getColorMask());

	SetPixelColor(fbFormat, x, y, new_color);
}

SingleFunc GetSingleFunc(const PixelFuncID &id) {
	if (id.clearMode) {
		switch (id.fbFormat) {
		case GE_FORMAT_565:
			return &DrawSinglePixel<true, GE_FORMAT_565>;
		case GE_FORMAT_5551:
			return &DrawSinglePixel<true, GE_FORMAT_5551>;
		case GE_FORMAT_4444:
			return &DrawSinglePixel<true, GE_FORMAT_4444>;
		case GE_FORMAT_8888:
			return &DrawSinglePixel<true, GE_FORMAT_8888>;
		}
	}
	switch (id.fbFormat) {
	case GE_FORMAT_565:
		return &DrawSinglePixel<false, GE_FORMAT_565>;
	case GE_FORMAT_5551:
		return &DrawSinglePixel<false, GE_FORMAT_5551>;
	case GE_FORMAT_4444:
		return &DrawSinglePixel<false, GE_FORMAT_4444>;
	case GE_FORMAT_8888:
		return &DrawSinglePixel<false, GE_FORMAT_8888>;
	}
	_assert_(false);
	return nullptr;
}

};
