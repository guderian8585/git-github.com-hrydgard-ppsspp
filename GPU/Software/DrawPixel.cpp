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

#include <mutex>
#include "Common/Data/Convert/ColorConv.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/FuncId.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/SoftGpu.h"

using namespace Math3D;

namespace Rasterizer {

std::mutex jitCacheLock;
PixelJitCache *jitCache = nullptr;

void Init() {
	jitCache = new PixelJitCache();
}

void Shutdown() {
	delete jitCache;
	jitCache = nullptr;
}

bool DescribeCodePtr(const u8 *ptr, std::string &name) {
	if (!jitCache->IsInSpace(ptr)) {
		return false;
	}

	name = jitCache->DescribeCodePtr(ptr);
	return true;
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
void SOFTPIXEL_CALL DrawSinglePixel(int x, int y, int z, int fog, SOFTPIXEL_VEC4I color_in, const PixelFuncID &pixelID) {
	Vec4<int> prim_color = Vec4<int>(color_in).Clamp(0, 255);
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
			stencil = ApplyStencilOp(fbFormat, pixelID.SFail(), stencil);
			SetPixelStencil(fbFormat, x, y, stencil);
			return;
		}

		// Also apply depth at the same time.  If disabled, same as passing.
		if (pixelID.DepthTestFunc() != GE_COMP_ALWAYS && !DepthTestPassed(pixelID.DepthTestFunc(), x, y, z)) {
			stencil = ApplyStencilOp(fbFormat, pixelID.ZFail(), stencil);
			SetPixelStencil(fbFormat, x, y, stencil);
			return;
		}

		stencil = ApplyStencilOp(fbFormat, pixelID.ZPass(), stencil);
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
	SingleFunc jitted = jitCache->GetSingle(id);
	if (jitted) {
		return jitted;
	}

	return jitCache->GenericSingle(id);
}

SingleFunc PixelJitCache::GenericSingle(const PixelFuncID &id) {
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

PixelJitCache::PixelJitCache()
#if PPSSPP_ARCH(ARM64)
	: fp(this)
#endif
{
	// 256k should be plenty of space for plenty of variations.
	AllocCodeSpace(1024 * 64 * 4);

	// Add some random code to "help" MSVC's buggy disassembler :(
#if defined(_WIN32) && (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !PPSSPP_PLATFORM(UWP)
	using namespace Gen;
	for (int i = 0; i < 100; i++) {
		MOV(32, R(EAX), R(EBX));
		RET();
	}
#elif PPSSPP_ARCH(ARM)
	BKPT(0);
	BKPT(0);
#endif
}

void PixelJitCache::Clear() {
	ClearCodeSpace(0);
	cache_.clear();
	addresses_.clear();
}

std::string PixelJitCache::DescribeCodePtr(const u8 *ptr) {
	ptrdiff_t dist = 0x7FFFFFFF;
	PixelFuncID found{};
	for (const auto &it : addresses_) {
		ptrdiff_t it_dist = ptr - it.second;
		if (it_dist >= 0 && it_dist < dist) {
			found = it.first;
			dist = it_dist;
		}
	}

	return DescribePixelFuncID(found);
}

SingleFunc PixelJitCache::GetSingle(const PixelFuncID &id) {
	std::lock_guard<std::mutex> guard(jitCacheLock);

	auto it = cache_.find(id);
	if (it != cache_.end()) {
		return it->second;
	}

	// x64 is typically 200-500 bytes, but let's be safe.
	if (GetSpaceLeft() < 65536) {
		Clear();
	}

#if PPSSPP_ARCH(AMD64) && !PPSSPP_PLATFORM(UWP)
	if (g_Config.bSoftwareRenderingJit) {
		addresses_[id] = GetCodePointer();
		SingleFunc func = CompileSingle(id);
		cache_[id] = func;
		return func;
	}
#endif
	return nullptr;
}

void ComputePixelBlendState(PixelBlendState &state, const PixelFuncID &id) {
	switch (id.AlphaBlendEq()) {
	case GE_BLENDMODE_MUL_AND_ADD:
	case GE_BLENDMODE_MUL_AND_SUBTRACT:
	case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE:
		state.usesFactors = true;
		break;

	case GE_BLENDMODE_MIN:
	case GE_BLENDMODE_MAX:
	case GE_BLENDMODE_ABSDIFF:
		break;
	}

	if (state.usesFactors) {
		switch (id.AlphaBlendSrc()) {
		case GE_SRCBLEND_DSTALPHA:
		case GE_SRCBLEND_INVDSTALPHA:
		case GE_SRCBLEND_DOUBLEDSTALPHA:
		case GE_SRCBLEND_DOUBLEINVDSTALPHA:
			state.usesDstAlpha = true;
			break;

		default:
			break;
		}

		switch (id.AlphaBlendDst()) {
		case GE_DSTBLEND_INVSRCALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == GE_SRCBLEND_SRCALPHA;
			break;

		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == GE_SRCBLEND_DOUBLESRCALPHA;
			break;

		case GE_DSTBLEND_DSTALPHA:
			state.usesDstAlpha = true;
			break;

		case GE_DSTBLEND_INVDSTALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == GE_SRCBLEND_DSTALPHA;
			state.usesDstAlpha = true;
			break;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
			state.usesDstAlpha = true;
			break;

		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			state.dstFactorIsInverse = id.AlphaBlendSrc() == GE_SRCBLEND_DOUBLEDSTALPHA;
			state.usesDstAlpha = true;
			break;

		default:
			break;
		}
	}
}

void PixelRegCache::Reset() {
	regs.clear();
}

void PixelRegCache::Release(PixelRegCache::Reg r, PixelRegCache::Type t, PixelRegCache::Purpose p) {
	RegStatus *status = FindReg(r, t);
	if (status) {
		_assert_msg_(status->locked > 0, "softjit Release() reg that isn't locked");
		_assert_msg_(!status->forceLocked, "softjit Release() reg that is force locked");
		status->purpose = p;
		status->locked--;
		return;
	}

	RegStatus newStatus;
	newStatus.reg = r;
	newStatus.purpose = p;
	newStatus.type = t;
	regs.push_back(newStatus);
}

void PixelRegCache::Unlock(PixelRegCache::Reg r, PixelRegCache::Type t) {
	RegStatus *status = FindReg(r, t);
	if (status) {
		_assert_msg_(status->locked > 0, "softjit Unlock() reg that isn't locked");
		status->locked--;
		return;
	}

	_assert_msg_(false, "softjit Unlock() reg that isn't there");
}

bool PixelRegCache::Has(PixelRegCache::Purpose p, PixelRegCache::Type t) {
	for (auto &reg : regs) {
		if (reg.purpose == p && reg.type == t) {
			return true;
		}
	}
	return false;
}

PixelRegCache::Reg PixelRegCache::Find(PixelRegCache::Purpose p, PixelRegCache::Type t) {
	for (auto &reg : regs) {
		if (reg.purpose == p && reg.type == t) {
			_assert_msg_(reg.locked <= 255, "softjit Find() reg has lots of locks");
			reg.locked++;
			return reg.reg;
		}
	}
	_assert_msg_(false, "softjit Find() reg that isn't there (%d)", p);
	return Reg(-1);
}

PixelRegCache::Reg PixelRegCache::Alloc(PixelRegCache::Purpose p, PixelRegCache::Type t) {
	_assert_msg_(!Has(p, t), "softjit Alloc() reg duplicate");
	RegStatus *best = nullptr;
	for (auto &reg : regs) {
		if (reg.locked != 0 || reg.forceLocked || reg.type != t)
			continue;

		if (best == nullptr)
			best = &reg;
		// Prefer a free/purposeless reg.
		if (reg.purpose == INVALID || reg.purpose >= TEMP0) {
			best = &reg;
			break;
		}
		// But also prefer a lower priority reg.
		if (reg.purpose < best->purpose)
			best = &reg;
	}

	if (best) {
		best->locked = 1;
		best->purpose = p;
		return best->reg;
	}

	_assert_msg_(false, "softjit Alloc() reg with none free (%d)", p);
	return Reg();
}

void PixelRegCache::ForceLock(PixelRegCache::Purpose p, PixelRegCache::Type t, bool state) {
	for (auto &reg : regs) {
		if (reg.purpose == p && reg.type == t) {
			reg.forceLocked = state;
			return;
		}
	}

	_assert_msg_(false, "softjit ForceLock() reg that isn't there");
}

void PixelRegCache::GrabReg(PixelRegCache::Reg r, PixelRegCache::Purpose p, PixelRegCache::Type t, bool &needsSwap, PixelRegCache::Reg swapReg) {
	for (auto &reg : regs) {
		if (reg.reg != r || reg.type != t)
			continue;

		// Easy version, it's free.
		if (reg.locked == 0 && !reg.forceLocked) {
			needsSwap = false;
			reg.purpose = p;
			reg.locked = 1;
			return;
		}

		// Okay, we need to swap.  Find that reg.
		needsSwap = true;
		RegStatus *swap = FindReg(swapReg, t);
		if (swap) {
			swap->purpose = reg.purpose;
			swap->forceLocked = reg.forceLocked;
			swap->locked = reg.locked;
		} else {
			RegStatus newStatus = reg;
			newStatus.reg = swapReg;
			regs.push_back(newStatus);
		}

		reg.purpose = p;
		reg.locked = 1;
		reg.forceLocked = false;
		return;
	}

	_assert_msg_(false, "softjit GrabReg() reg that isn't there");
}

PixelRegCache::RegStatus *PixelRegCache::FindReg(PixelRegCache::Reg r, PixelRegCache::Type t) {
	for (auto &reg : regs) {
		if (reg.reg == r && reg.type == t) {
			return &reg;
		}
	}

	return nullptr;
}

};
