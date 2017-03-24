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

#pragma once

#include <unordered_map>

#include <d3d9.h>

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"

struct DecVtxFormat;
struct UVScale;

namespace DX9 {

class VSShader;
class ShaderManagerDX9;
class TextureCacheDX9;
class FramebufferManagerDX9;

// States transitions:
// On creation: DRAWN_NEW
// DRAWN_NEW -> DRAWN_HASHING
// DRAWN_HASHING -> DRAWN_RELIABLE
// DRAWN_HASHING -> DRAWN_UNRELIABLE
// DRAWN_ONCE -> UNRELIABLE
// DRAWN_RELIABLE -> DRAWN_SAFE
// UNRELIABLE -> death
// DRAWN_ONCE -> death
// DRAWN_RELIABLE -> death

enum {
	VAI_FLAG_VERTEXFULLALPHA = 1,
};

// Avoiding the full include of TextureDecoder.h.
#if (defined(_M_SSE) && defined(_M_X64)) || defined(ARM64)
typedef u64 ReliableHashType;
#else
typedef u32 ReliableHashType;
#endif

// Try to keep this POD.
class VertexArrayInfoDX9 {
public:
	VertexArrayInfoDX9() {
		status = VAI_NEW;
		vbo = 0;
		ebo = 0;
		prim = GE_PRIM_INVALID;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFlips;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
		flags = 0;
	}
	~VertexArrayInfoDX9();

	enum Status {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	ReliableHashType hash;
	u32 minihash;

	Status status;

	LPDIRECT3DVERTEXBUFFER9 vbo;
	LPDIRECT3DINDEXBUFFER9 ebo;

	// Precalculated parameter for drawRangeElements
	u16 numVerts;
	u16 maxIndex;
	s8 prim;

	// ID information
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
	u8 flags;
};

// Handles transform, lighting and drawing.
class DrawEngineDX9 : public DrawEngineCommon {
public:
	DrawEngineDX9(Draw::DrawContext *draw);
	virtual ~DrawEngineDX9();

	void SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead);

	void SetShaderManager(ShaderManagerDX9 *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheDX9 *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerDX9 *fbManager) {
		framebufferManager_ = fbManager;
	}
	void InitDeviceObjects();
	void DestroyDeviceObjects();
	void GLLost() {};

	void ClearTrackedVertexArrays() override;
	void DecimateTrackedVertexArrays();

	void SetupVertexDecoder(u32 vertType);
	void SetupVertexDecoderInternal(u32 vertType);

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DecodeVerts();
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

private:
	void DecodeVerts();
	void DecodeVertsStep();
	void DoFlush();

	void ApplyDrawState(int prim);
	void ApplyDrawStateLate();
	void ResetShaderBlending();

	IDirect3DVertexDeclaration9 *SetupDecFmtForDraw(VSShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt);

	u32 ComputeMiniHash();
	ReliableHashType ComputeHash();  // Reads deferred vertex data.
	void MarkUnreliable(VertexArrayInfoDX9 *vai);

	LPDIRECT3DDEVICE9 device_;

	// Defer all vertex decoding to a Flush, so that we can hash and cache the
	// generated buffers without having to redecode them every time.
	struct DeferredDrawCall {
		void *verts;
		void *inds;
		u32 vertType;
		u8 indexType;
		s8 prim;
		u32 vertexCount;
		u16 indexLowerBound;
		u16 indexUpperBound;
	};

	// Vertex collector state
	IndexGenerator indexGen;
	int decodedVerts_;
	GEPrimitiveType prevPrim_;
	
	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	std::unordered_map<u32, VertexArrayInfoDX9 *> vai_;
	std::unordered_map<u32, IDirect3DVertexDeclaration9 *> vertexDeclMap_;

	// SimpleVertex
	IDirect3DVertexDeclaration9* transformedVertexDecl_;

	// Other
	ShaderManagerDX9 *shaderManager_;
	TextureCacheDX9 *textureCache_;
	FramebufferManagerDX9 *framebufferManager_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };

	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
	int vertexCountInDrawCalls;

	int decimationCounter_;
	int decodeCounter_;
	u32 dcid_;

	UVScale uvScale[MAX_DEFERRED_DRAW_CALLS];

	// Hardware tessellation
	LPDIRECT3DVERTEXBUFFER9 pInstanceBuffer = NULL;
	void InitInstanceIndexData(LPDIRECT3DDEVICE9 device_) {
#define MAX_INSTANCES 2048
			device_->CreateVertexBuffer(MAX_INSTANCES * sizeof(float), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &pInstanceBuffer, NULL);

			float *instanceData;
			pInstanceBuffer->Lock(0, 0, (void **)&instanceData, 0);
			for (UINT i = 0; i < MAX_INSTANCES; i++) {
				instanceData[i] = (float)i;
			}
			pInstanceBuffer->Unlock();
#undef MAX_INSTANCES
	}

	class TessellationDataTransferDX9 : public TessellationDataTransfer {
	private:
		LPDIRECT3DTEXTURE9 data_tex[3];
		LPDIRECT3DDEVICE9 device_;
	public:
		TessellationDataTransferDX9(LPDIRECT3DDEVICE9 device_) : TessellationDataTransfer(), data_tex(), device_(device_) {
			// Vertex texture sampler state 0
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			// Vertex texture sampler state 1
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			// Vertex texture sampler state 2
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER2, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			device_->SetSamplerState(D3DVERTEXTEXTURESAMPLER2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		}
		~TessellationDataTransferDX9() {}
		void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) override;
		void PrepareBuffers(float *&pos, float *&tex, float *&col, int size, bool hasColor, bool hasTexCoords) override;
	};
};

}  // namespace
