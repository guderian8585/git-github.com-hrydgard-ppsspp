// See comment in header for the purpose of the code in this file.

#include <algorithm>
#include <cmath>

#include "Common/Common.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/GPUState.h"

#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/DrawPixel.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"

#if defined(_M_SSE)
#include <emmintrin.h>
#endif

extern DSStretch g_DarkStalkerStretch;
// For Darkstalkers hack. Ugh.
extern bool currentDialogActive;

namespace Rasterizer {

// This essentially AlphaBlendingResult() with fixed src.a / 1 - src.a factors and ADD equation.
// It allows us to skip round trips between 32-bit and 16-bit color values.
static uint32_t StandardAlphaBlend(uint32_t source, uint32_t dst) {
#if defined(_M_SSE)
	const __m128i alpha = _mm_cvtsi32_si128(source >> 24);
	// Keep the alpha lane of the srcfactor zero, so we keep dest alpha.
	const __m128i srcfactor = _mm_shufflelo_epi16(alpha, _MM_SHUFFLE(1, 0, 0, 0));
	const __m128i dstfactor = _mm_sub_epi16(_mm_set1_epi16(255), srcfactor);

	const __m128i z = _mm_setzero_si128();
	const __m128i sourcevec = _mm_unpacklo_epi8(_mm_cvtsi32_si128(source), z);
	const __m128i dstvec = _mm_unpacklo_epi8(_mm_cvtsi32_si128(dst), z);

	// We switch to 16 bit to use mulhi, and we use 4 bits of decimal to make the 16 bit shift free.
	const __m128i half = _mm_set1_epi16(1 << 3);

	const __m128i srgb = _mm_add_epi16(_mm_slli_epi16(sourcevec, 4), half);
	const __m128i sf = _mm_add_epi16(_mm_slli_epi16(srcfactor, 4), half);
	const __m128i s = _mm_mulhi_epi16(srgb, sf);

	const __m128i drgb = _mm_add_epi16(_mm_slli_epi16(dstvec, 4), half);
	const __m128i df = _mm_add_epi16(_mm_slli_epi16(dstfactor, 4), half);
	const __m128i d = _mm_mulhi_epi16(drgb, df);

	const __m128i blended16 = _mm_adds_epi16(s, d);
	return _mm_cvtsi128_si32(_mm_packus_epi16(blended16, blended16));
#else
	Vec3<int> srcfactor = Vec3<int>::AssignToAll(source >> 24);
	Vec3<int> dstfactor = Vec3<int>::AssignToAll(255 - (source >> 24));

	static constexpr Vec3<int> half = Vec3<int>::AssignToAll(1);
	Vec3<int> lhs = ((Vec3<int>::FromRGB(source) * 2 + half) * (srcfactor * 2 + half)) / 1024;
	Vec3<int> rhs = ((Vec3<int>::FromRGB(dst) * 2 + half) * (dstfactor * 2 + half)) / 1024;
	Vec3<int> blended = lhs + rhs;

	return clamp_u8(blended.r()) | (clamp_u8(blended.g()) << 8) | (clamp_u8(blended.b()) << 16);
#endif
}

// Through mode, with the specific Darkstalker settings.
inline void DrawSinglePixel5551(u16 *pixel, const u32 color_in) {
	u32 new_color;
	// Because of this check, we only support src.a / 1-src.a blending.
	if ((color_in >> 24) == 255) {
		new_color = color_in & 0xFFFFFF;
	} else {
		const u32 old_color = RGBA5551ToRGBA8888(*pixel);
		new_color = StandardAlphaBlend(color_in, old_color);
	}
	new_color |= (*pixel & 0x8000) ? 0xff000000 : 0x00000000;
	*pixel = RGBA8888ToRGBA5551(new_color);
}

// Check if we can safely ignore the alpha test, assuming standard alpha blending.
static inline bool AlphaTestIsNeedless(const PixelFuncID &pixelID) {
	switch (pixelID.AlphaTestFunc()) {
	case GE_COMP_NEVER:
	case GE_COMP_EQUAL:
	case GE_COMP_LESS:
	case GE_COMP_LEQUAL:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_NOTEQUAL:
	case GE_COMP_GREATER:
	case GE_COMP_GEQUAL:
		if (pixelID.alphaTestRef != 0 || pixelID.hasAlphaTestMask)
			return false;
		return true;
	}

	return false;
}

bool UseDrawSinglePixel5551(const PixelFuncID &pixelID) {
	if (pixelID.clearMode || pixelID.colorTest || pixelID.stencilTest)
		return false;
	if (!AlphaTestIsNeedless(pixelID) || pixelID.DepthTestFunc() != GE_COMP_ALWAYS)
		return false;
	if (pixelID.FBFormat() != GE_FORMAT_5551 || !pixelID.alphaBlend)
		return false;
	// We skip blending when alpha = FF, so we can't allow other blend modes.
	if (pixelID.AlphaBlendEq() != GE_BLENDMODE_MUL_AND_ADD || pixelID.AlphaBlendSrc() != PixelBlendFactor::SRCALPHA)
		return false;
	if (pixelID.AlphaBlendDst() != PixelBlendFactor::INVSRCALPHA)
		return false;
	if (pixelID.dithering || pixelID.applyLogicOp || pixelID.applyColorWriteMask)
		return false;

	return true;
}

static inline Vec4IntResult SOFTRAST_CALL ModulateRGBA(Vec4IntArg prim_in, Vec4IntArg texcolor_in, const SamplerID &samplerID) {
	Vec4<int> out;
	Vec4<int> prim_color = prim_in;
	Vec4<int> texcolor = texcolor_in;

#if defined(_M_SSE)
	// Modulate weights slightly on the tex color, by adding one to prim and dividing by 256.
	const __m128i p = _mm_slli_epi16(_mm_packs_epi32(prim_color.ivec, prim_color.ivec), 4);
	const __m128i pboost = _mm_add_epi16(p, _mm_set1_epi16(1 << 4));
	__m128i t = _mm_slli_epi16(_mm_packs_epi32(texcolor.ivec, texcolor.ivec), 4);
	if (samplerID.useColorDoubling) {
		const __m128i amask = _mm_set_epi16(-1, 0, 0, 0, -1, 0, 0, 0);
		const __m128i a = _mm_and_si128(t, amask);
		const __m128i rgb = _mm_andnot_si128(amask, t);
		t = _mm_or_si128(_mm_slli_epi16(rgb, 1), a);
	}
	const __m128i b = _mm_mulhi_epi16(pboost, t);
	out.ivec = _mm_unpacklo_epi16(b, _mm_setzero_si128());
#else
	if (samplerID.useColorDoubling) {
		Vec4<int> tex = texcolor * Vec4<int>(2, 2, 2, 1);
		out = ((prim_color + Vec4<int>::AssignToAll(1)) * tex) / 256;
	} else {
		out = (prim_color + Vec4<int>::AssignToAll(1)) * texcolor / 256;
	}
#endif

	return ToVec4IntResult(out);
}

void DrawSprite(const VertexData &v0, const VertexData &v1, const BinCoords &range, const RasterizerState &state) {
	const u8 *texptr = state.texptr[0];

	GETextureFormat texfmt = state.samplerID.TexFmt();
	uint16_t texbufw = state.texbufw[0];

	Sampler::FetchFunc fetchFunc = Sampler::GetFetchFunc(state.samplerID);
	auto &pixelID = state.pixelID;
	auto &samplerID = state.samplerID;

	DrawingCoords pos0 = TransformUnit::ScreenToDrawing(v0.screenpos);
	// Include the ending pixel based on its center, not start.
	DrawingCoords pos1 = TransformUnit::ScreenToDrawing(v1.screenpos + ScreenCoords(7, 7, 0));

	DrawingCoords scissorTL = TransformUnit::ScreenToDrawing(range.x1, range.y1);
	DrawingCoords scissorBR = TransformUnit::ScreenToDrawing(range.x2, range.y2);

	const int z = v1.screenpos.z;
	constexpr int fog = 255;

	// Since it's flat, we can check depth range early.  Matters for earlyZChecks.
	if (pixelID.applyDepthRange && (z < pixelID.cached.minz || z > pixelID.cached.maxz))
		return;

	bool isWhite = v1.color0 == 0xFFFFFFFF;

	if (state.enableTextures) {
		// 1:1 (but with mirror support) texture mapping!
		int s_start = v0.texturecoords.x;
		int t_start = v0.texturecoords.y;
		int ds = v1.texturecoords.x > v0.texturecoords.x ? 1 : -1;
		int dt = v1.texturecoords.y > v0.texturecoords.y ? 1 : -1;

		if (ds < 0) {
			s_start += ds;
		}
		if (dt < 0) {
			t_start += dt;
		}

		// First clip the right and bottom sides, since we don't need to adjust the deltas.
		if (pos1.x > scissorBR.x) pos1.x = scissorBR.x + 1;
		if (pos1.y > scissorBR.y) pos1.y = scissorBR.y + 1;
		// Now clip the other sides.
		if (pos0.x < scissorTL.x) {
			s_start += (scissorTL.x - pos0.x) * ds;
			pos0.x = scissorTL.x;
		}
		if (pos0.y < scissorTL.y) {
			t_start += (scissorTL.y - pos0.y) * dt;
			pos0.y = scissorTL.y;
		}

		if (UseDrawSinglePixel5551(pixelID) && samplerID.TexFunc() == GE_TEXFUNC_MODULATE && samplerID.useTextureAlpha) {
			if (isWhite) {
				int t = t_start;
				for (int y = pos0.y; y < pos1.y; y++) {
					int s = s_start;
					u16 *pixel = fb.Get16Ptr(pos0.x, y, pixelID.cached.framebufStride);
					for (int x = pos0.x; x < pos1.x; x++) {
						u32 tex_color = Vec4<int>(fetchFunc(s, t, texptr, texbufw, 0, state.samplerID)).ToRGBA();
						if (tex_color & 0xFF000000) {
							DrawSinglePixel5551(pixel, tex_color);
						}
						s += ds;
						pixel++;
					}
					t += dt;
				}
			} else {
				int t = t_start;
				const Vec4<int> c0 = Vec4<int>::FromRGBA(v1.color0);
				for (int y = pos0.y; y < pos1.y; y++) {
					int s = s_start;
					u16 *pixel = fb.Get16Ptr(pos0.x, y, pixelID.cached.framebufStride);
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = c0;
						Vec4<int> tex_color = fetchFunc(s, t, texptr, texbufw, 0, state.samplerID);
						prim_color = Vec4<int>(ModulateRGBA(ToVec4IntArg(prim_color), ToVec4IntArg(tex_color), state.samplerID));
						if (prim_color.a() > 0) {
							DrawSinglePixel5551(pixel, prim_color.ToRGBA());
						}
						s += ds;
						pixel++;
					}
					t += dt;
				}
			}
		} else {
			float dsf = ds * (1.0f / (float)(1 << state.samplerID.width0Shift));
			float dtf = dt * (1.0f / (float)(1 << state.samplerID.height0Shift));
			float sf_start = s_start * (1.0f / (float)(1 << state.samplerID.width0Shift));
			float tf_start = t_start * (1.0f / (float)(1 << state.samplerID.height0Shift));

			float t = tf_start;
			const Vec4<int> c0 = Vec4<int>::FromRGBA(v1.color0);
			if (pixelID.earlyZChecks) {
				for (int y = pos0.y; y < pos1.y; y++) {
					float s = sf_start;
					// Not really that fast but faster than triangle.
					for (int x = pos0.x; x < pos1.x; x++) {
						if (CheckDepthTestPassed(pixelID.DepthTestFunc(), x, y, pixelID.cached.depthbufStride, z)) {
							Vec4<int> prim_color = state.nearest(s, t, ToVec4IntArg(c0), &texptr, &texbufw, 0, 0, state.samplerID);
							state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
						}

						s += dsf;
					}
					t += dtf;
				}
			} else {
				for (int y = pos0.y; y < pos1.y; y++) {
					float s = sf_start;
					// Not really that fast but faster than triangle.
					for (int x = pos0.x; x < pos1.x; x++) {
						Vec4<int> prim_color = state.nearest(s, t, ToVec4IntArg(c0), &texptr, &texbufw, 0, 0, state.samplerID);
						state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
						s += dsf;
					}
					t += dtf;
				}
			}
		}
	} else {
		if (pos1.x > scissorBR.x) pos1.x = scissorBR.x + 1;
		if (pos1.y > scissorBR.y) pos1.y = scissorBR.y + 1;
		if (pos0.x < scissorTL.x) pos0.x = scissorTL.x;
		if (pos0.y < scissorTL.y) pos0.y = scissorTL.y;
		if (UseDrawSinglePixel5551(pixelID)) {
			if (Vec4<int>::FromRGBA(v1.color0).a() == 0)
				return;

			for (int y = pos0.y; y < pos1.y; y++) {
				u16 *pixel = fb.Get16Ptr(pos0.x, y, pixelID.cached.framebufStride);
				for (int x = pos0.x; x < pos1.x; x++) {
					DrawSinglePixel5551(pixel, v1.color0);
					pixel++;
				}
			}
		} else if (pixelID.earlyZChecks) {
			const Vec4<int> prim_color = Vec4<int>::FromRGBA(v1.color0);
			for (int y = pos0.y; y < pos1.y; y++) {
				for (int x = pos0.x; x < pos1.x; x++) {
					if (!CheckDepthTestPassed(pixelID.DepthTestFunc(), x, y, pixelID.cached.depthbufStride, z))
						continue;

					state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
				}
			}
		} else {
			const Vec4<int> prim_color = Vec4<int>::FromRGBA(v1.color0);
			for (int y = pos0.y; y < pos1.y; y++) {
				for (int x = pos0.x; x < pos1.x; x++) {
					state.drawPixel(x, y, z, fog, ToVec4IntArg(prim_color), pixelID);
				}
			}
		}
	}

#if defined(SOFTGPU_MEMORY_TAGGING_BASIC) || defined(SOFTGPU_MEMORY_TAGGING_DETAILED)
	uint32_t bpp = pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	std::string tag = StringFromFormat("DisplayListR_%08x", state.listPC);
	std::string ztag = StringFromFormat("DisplayListRZ_%08x", state.listPC);

	for (int y = pos0.y; y < pos1.y; y++) {
		uint32_t row = gstate.getFrameBufAddress() + y * pixelID.cached.framebufStride * bpp;
		NotifyMemInfo(MemBlockFlags::WRITE, row + pos0.x * bpp, (pos1.x - pos0.x) * bpp, tag.c_str(), tag.size());
	}
#endif
}

bool g_needsClearAfterDialog = false;

static inline bool NoClampOrWrap(const RasterizerState &state, const Vec2f &tc) {
	if (tc.x < 0 || tc.y < 0)
		return false;
	if (state.samplerID.cached.sizes[0].w > 512 || state.samplerID.cached.sizes[0].h > 512)
		return false;
	return tc.x <= state.samplerID.cached.sizes[0].w && tc.y <= state.samplerID.cached.sizes[0].h;
}

// Returns true if the normal path should be skipped.
bool RectangleFastPath(const VertexData &v0, const VertexData &v1, BinManager &binner) {
	const RasterizerState &state = binner.State();

	g_DarkStalkerStretch = DSStretch::Off;
	// Check for 1:1 texture mapping. In that case we can call DrawSprite.
	int xdiff = v1.screenpos.x - v0.screenpos.x;
	int ydiff = v1.screenpos.y - v0.screenpos.y;
	int udiff = (v1.texturecoords.x - v0.texturecoords.x) * (float)SCREEN_SCALE_FACTOR;
	int vdiff = (v1.texturecoords.y - v0.texturecoords.y) * (float)SCREEN_SCALE_FACTOR;
	bool coord_check =
		(xdiff == udiff || xdiff == -udiff) &&
		(ydiff == vdiff || ydiff == -vdiff);
	// Currently only works for TL/BR, which is the most common but not required.
	bool orient_check = xdiff >= 0 && ydiff >= 0;
	// We already have a fast path for clear in ClearRectangle.
	bool state_check = state.throughMode && !state.pixelID.clearMode && !state.samplerID.hasAnyMips && NoClampOrWrap(state, v0.texturecoords.uv()) && NoClampOrWrap(state, v1.texturecoords.uv());
	// This doesn't work well with offset drawing, see #15876.  Through never has a subpixel offset.
	bool subpixel_check = ((v0.screenpos.x | v0.screenpos.y | v1.screenpos.x | v1.screenpos.y) & 0xF) == 0;
	if ((coord_check || !state.enableTextures) && orient_check && state_check && subpixel_check) {
		binner.AddSprite(v0, v1);
		return true;
	}

	// Eliminate the stretch blit in DarkStalkers.
	// We compensate for that when blitting the framebuffer in SoftGpu.cpp.
	if (PSP_CoreParameter().compat.flags().DarkStalkersPresentHack && v0.texturecoords.x == 64.0f && v0.texturecoords.y == 16.0f && v1.texturecoords.x == 448.0f && v1.texturecoords.y == 240.0f) {
		// check for save/load dialog.
		if (!currentDialogActive) {
			if (v0.screenpos.x + gstate.getOffsetX16() == 0x7100 && v0.screenpos.y + gstate.getOffsetY16() == 0x7780 && v1.screenpos.x + gstate.getOffsetX16() == 0x8f00 && v1.screenpos.y + gstate.getOffsetY16() == 0x8880) {
				g_DarkStalkerStretch = DSStretch::Wide;
			} else if (v0.screenpos.x + gstate.getOffsetX16() == 0x7400 && v0.screenpos.y + gstate.getOffsetY16() == 0x7780 && v1.screenpos.x + gstate.getOffsetX16() == 0x8C00 && v1.screenpos.y + gstate.getOffsetY16() == 0x8880) {
				g_DarkStalkerStretch = DSStretch::Normal;
			} else {
				return false;
			}
			if (g_needsClearAfterDialog) {
				g_needsClearAfterDialog = false;
				// Afterwards, we also need to clear the actual destination. Can do a fast rectfill.
				gstate.textureMapEnable &= ~1;
				VertexData newV1 = v1;
				newV1.color0 = 0xFF000000;
				binner.AddSprite(v0, newV1);
				gstate.textureMapEnable |= 1;
			}
			return true;
		} else {
			g_needsClearAfterDialog = true;
		}
	}
	return false;
}

static bool AreCoordsRectangleCompatible(const RasterizerState &state, const ClipVertexData &data0, const ClipVertexData &data1) {
	if (data1.v.color0 != data0.v.color0)
		return false;
	if (data1.v.screenpos.z != data0.v.screenpos.z) {
		// Sometimes, we don't actually care about z.
		if (state.pixelID.depthWrite || state.pixelID.DepthTestFunc() != GE_COMP_ALWAYS)
			return false;
	}
	if (!state.throughMode) {
		if (data1.v.color1 != data0.v.color1)
			return false;
		// This means it should be culled, outside range.
		if (data1.OutsideRange() || data0.OutsideRange())
			return false;
		// Do we have to think about perspective correction or slope mip level?
		if (state.enableTextures && data1.clippos.w != data0.clippos.w) {
			// If the w is off by less than a factor of 1/512, it should be safe to treat as a rectangle.
			static constexpr float halftexel = 0.5f / 512.0f;
			if (data1.clippos.w - halftexel > data0.clippos.w || data1.clippos.w + halftexel < data0.clippos.w)
				return false;
		}
		// If we're projecting textures, only allow an exact match for simplicity.
		if (state.enableTextures && data1.v.texturecoords.q() != data0.v.texturecoords.q())
			return false;
		if (state.pixelID.applyFog && data1.v.fogdepth != data0.v.fogdepth) {
			// Similar to w, this only matters if they're farther apart than 1/255.
			static constexpr float foghalfstep = 0.5f / 255.0f;
			if (data1.v.fogdepth - foghalfstep > data0.v.fogdepth || data1.v.fogdepth + foghalfstep < data0.v.fogdepth)
				return false;
		}
	}
	return true;
}

bool DetectRectangleFromStrip(const RasterizerState &state, const ClipVertexData data[4], int *tlIndex, int *brIndex) {
	// Color and Z must be flat.  Also find the TL and BR meanwhile.
	int tl = 0, br = 0;
	for (int i = 1; i < 4; ++i) {
		if (!AreCoordsRectangleCompatible(state, data[i], data[0]))
			return false;

		if (data[i].v.screenpos.x <= data[tl].v.screenpos.x && data[i].v.screenpos.y <= data[tl].v.screenpos.y)
			tl = i;
		if (data[i].v.screenpos.x >= data[br].v.screenpos.x && data[i].v.screenpos.y >= data[br].v.screenpos.y)
			br = i;
	}

	*tlIndex = tl;
	*brIndex = br;

	// OK, now let's look at data to detect rectangles. There are a few possibilities
	// but we focus on Darkstalkers for now.
	if (data[0].v.screenpos.x == data[1].v.screenpos.x &&
		data[0].v.screenpos.y == data[2].v.screenpos.y &&
		data[2].v.screenpos.x == data[3].v.screenpos.x &&
		data[1].v.screenpos.y == data[3].v.screenpos.y) {
		// Okay, this is in the shape of a rectangle, but what about texture?
		if (!state.enableTextures)
			return true;

		if (data[0].v.texturecoords.x == data[1].v.texturecoords.x &&
			data[0].v.texturecoords.y == data[2].v.texturecoords.y &&
			data[2].v.texturecoords.x == data[3].v.texturecoords.x &&
			data[1].v.texturecoords.y == data[3].v.texturecoords.y) {
			// It's a rectangle!
			return true;
		}
		return false;
	}
	// There's the other vertex order too...
	if (data[0].v.screenpos.x == data[2].v.screenpos.x &&
		data[0].v.screenpos.y == data[1].v.screenpos.y &&
		data[1].v.screenpos.x == data[3].v.screenpos.x &&
		data[2].v.screenpos.y == data[3].v.screenpos.y) {
		// Okay, this is in the shape of a rectangle, but what about texture?
		if (!state.enableTextures)
			return true;

		if (data[0].v.texturecoords.x == data[2].v.texturecoords.x &&
			data[0].v.texturecoords.y == data[1].v.texturecoords.y &&
			data[1].v.texturecoords.x == data[3].v.texturecoords.x &&
			data[2].v.texturecoords.y == data[3].v.texturecoords.y) {
			// It's a rectangle!
			return true;
		}
		return false;
	}
	return false;
}

bool DetectRectangleFromFan(const RasterizerState &state, const ClipVertexData *data, int *tlIndex, int *brIndex) {
	// Color and Z must be flat.
	int tl = 0, br = 0;
	for (int i = 1; i < 4; ++i) {
		if (!AreCoordsRectangleCompatible(state, data[i], data[0]))
			return false;

		if (data[i].v.screenpos.x <= data[tl].v.screenpos.x && data[i].v.screenpos.y <= data[tl].v.screenpos.y)
			tl = i;
		if (data[i].v.screenpos.x >= data[br].v.screenpos.x && data[i].v.screenpos.y >= data[br].v.screenpos.y)
			br = i;
	}

	*tlIndex = tl;
	*brIndex = br;

	int tr = 1, bl = 1;
	for (int i = 0; i < 4; ++i) {
		if (i == tl || i == br)
			continue;

		if (data[i].v.screenpos.x <= data[tl].v.screenpos.x && data[i].v.screenpos.y >= data[tl].v.screenpos.y)
			bl = i;
		if (data[i].v.screenpos.x >= data[br].v.screenpos.x && data[i].v.screenpos.y <= data[br].v.screenpos.y)
			tr = i;
	}

	// Must have found each of the coordinates.
	if (tl + tr + bl + br != 6)
		return false;

	// Note the common case is a single TL-TR-BR-BL.
	const auto &postl = data[tl].v.screenpos, &postr = data[tr].v.screenpos;
	const auto &posbr = data[br].v.screenpos, &posbl = data[bl].v.screenpos;
	if (postl.x == posbl.x && postr.x == posbr.x && postl.y == postr.y && posbl.y == posbr.y) {
		// Do we need to think about rotation?
		if (!state.enableTextures)
			return true;

		const auto &textl = data[tl].v.texturecoords, &textr = data[tr].v.texturecoords;
		const auto &texbl = data[bl].v.texturecoords, &texbr = data[br].v.texturecoords;

		if (textl.x == texbl.x && textr.x == texbr.x && textl.y == textr.y && texbl.y == texbr.y) {
			// Okay, the texture is also good, but let's avoid rotation issues.
			return textl.y < texbr.y && postl.y < posbr.y && textl.x < texbr.x && postl.x < posbr.x;
		}
	}

	return false;
}

bool DetectRectangleFromPair(const RasterizerState &state, const ClipVertexData data[6], int *tlIndex, int *brIndex) {
	// Color and Z must be flat.  Also find the TL and BR meanwhile.
	int tl = 0, br = 0;
	for (int i = 1; i < 6; ++i) {
		if (!AreCoordsRectangleCompatible(state, data[i], data[0]))
			return false;

		if (data[i].v.screenpos.x <= data[tl].v.screenpos.x && data[i].v.screenpos.y <= data[tl].v.screenpos.y)
			tl = i;
		if (data[i].v.screenpos.x >= data[br].v.screenpos.x && data[i].v.screenpos.y >= data[br].v.screenpos.y)
			br = i;
	}

	*tlIndex = tl;
	*brIndex = br;

	auto xat = [&](int i) { return data[i].v.screenpos.x; };
	auto yat = [&](int i) { return data[i].v.screenpos.y; };
	auto uat = [&](int i) { return data[i].v.texturecoords.x; };
	auto vat = [&](int i) { return data[i].v.texturecoords.y; };

	// A likely order would be: TL, TR, BR, TL, BR, BL.  We'd have the last index of each.
	// TODO: Make more generic.
	if (tl == 3 && br == 4) {
		bool x1_match = xat(0) == xat(3) && xat(0) == xat(5);
		bool x2_match = xat(1) == xat(2) && xat(1) == xat(4);
		bool y1_match = yat(0) == yat(1) && yat(0) == yat(3);
		bool y2_match = yat(2) == yat(4) && yat(2) == yat(5);
		if (x1_match && y1_match && x2_match && y2_match) {
			// Do we need to think about rotation or UVs?
			if (!state.enableTextures)
				return true;

			x1_match = uat(0) == uat(3) && uat(0) == uat(5);
			x2_match = uat(1) == uat(2) && uat(1) == uat(4);
			y1_match = vat(0) == vat(1) && vat(0) == vat(3);
			y2_match = vat(2) == vat(4) && vat(2) == vat(5);
			if (x1_match && y1_match && x2_match && y2_match) {
				// Double check rotation direction.
				return vat(tl) < vat(br) && yat(tl) < yat(br) && uat(tl) < uat(br) && xat(tl) < xat(br);
			}
		}
	}

	return false;
}

bool DetectRectangleThroughModeSlices(const RasterizerState &state, const ClipVertexData data[4]) {
	// Color and Z must be flat.
	for (int i = 1; i < 4; ++i) {
		if (!(data[i].v.color0 == data[0].v.color0))
			return false;
		if (!(data[i].v.screenpos.z == data[0].v.screenpos.z)) {
			// Sometimes, we don't actually care about z.
			if (state.pixelID.depthWrite || state.pixelID.DepthTestFunc() != GE_COMP_ALWAYS)
				return false;
		}
	}

	// Games very commonly use vertical strips of rectangles.  Detect and combine.
	const auto &tl1 = data[0].v.screenpos, &br1 = data[1].v.screenpos;
	const auto &tl2 = data[2].v.screenpos, &br2 = data[3].v.screenpos;
	if (tl1.y == tl2.y && br1.y == br2.y && br1.y > tl1.y) {
		if (br1.x == tl2.x && tl1.x < br1.x && tl2.x < br2.x) {
			if (!state.enableTextures)
				return true;

			const auto &textl1 = data[0].v.texturecoords, &texbr1 = data[1].v.texturecoords;
			const auto &textl2 = data[2].v.texturecoords, &texbr2 = data[3].v.texturecoords;
			if (textl1.y != textl2.y || texbr1.y != texbr2.y || textl1.y > texbr1.y)
				return false;
			if (texbr1.x != textl2.x || textl1.x > texbr1.x || textl2.x > texbr2.x)
				return false;

			// We might be able to compare ratios, but let's expect 1:1.
			int texdiff1 = (texbr1.x - textl1.x) * (float)SCREEN_SCALE_FACTOR;
			int texdiff2 = (texbr2.x - textl2.x) * (float)SCREEN_SCALE_FACTOR;
			int posdiff1 = br1.x - tl1.x;
			int posdiff2 = br2.x - tl2.x;
			return texdiff1 == posdiff1 && texdiff2 == posdiff2;
		}
	}

	return false;
}

}  // namespace Rasterizer

