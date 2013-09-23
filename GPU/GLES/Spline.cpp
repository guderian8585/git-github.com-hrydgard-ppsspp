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

#include "TransformPipeline.h"
#include "Core/MemMap.h"
#include "GPU/Math3D.h"

// PSP compatible format so we can use the end of the pipeline
struct SimpleVertex {
	float uv[2];
	u8 color[4];
	Vec3f nrm;
	Vec3f pos;
};

// This normalizes a set of vertices in any format to SimpleVertex format, by processing away morphing AND skinning.
// The rest of the transform pipeline like lighting will go as normal, either hardware or software.
// The implementation is initially a bit inefficient but shouldn't be a big deal.
// An intermediate buffer of not-easy-to-predict size is stored at bufPtr.
u32 TransformDrawEngine::NormalizeVertices(u8 *outPtr, u8 *bufPtr, const u8 *inPtr, int lowerBound, int upperBound, u32 vertType) {
	// First, decode the vertices into a GPU compatible format. This step can be eliminated but will need a separate
	// implementation of the vertex decoder.
	VertexDecoder *dec = GetVertexDecoder(vertType);
	dec->DecodeVerts(bufPtr, inPtr, lowerBound, upperBound);

	// OK, morphing eliminated but bones still remain to be taken care of.
	// Let's do a partial software transform where we only do skinning.

	VertexReader reader(bufPtr, dec->GetDecVtxFmt(), vertType);

	SimpleVertex *sverts = (SimpleVertex *)outPtr;	

	const u8 defaultColor[4] = {
		
	};

	// Let's have two separate loops, one for non skinning and one for skinning.
	if ((vertType & GE_VTYPE_WEIGHT_MASK) != GE_VTYPE_WEIGHT_NONE) {
		int numBoneWeights = vertTypeGetNumBoneWeights(vertType);
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			}

			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}

			float nrm[3], pos[3];
			float bnrm[3], bpos[3];

			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tesselation anyway, not sure if any need to supply
				reader.ReadNrm(nrm);
			} else {
				nrm[0] = 0;
				nrm[1] = 0;
				nrm[2] = 1.0f;
			}
			reader.ReadPos(pos);

			// Apply skinning transform directly
			float weights[8];
			reader.ReadWeights(weights);
			// Skinning
			Vec3f psum(0,0,0);
			Vec3f nsum(0,0,0);
			for (int i = 0; i < numBoneWeights; i++) {
				if (weights[i] != 0.0f) {
					Vec3ByMatrix43(bpos, pos, gstate.boneMatrix+i*12);
					Vec3f tpos(bpos);
					psum += tpos * weights[i];

					Norm3ByMatrix43(bnrm, nrm, gstate.boneMatrix+i*12);
					Vec3f tnorm(bnrm);
					nsum += tnorm * weights[i];
				}
			}
			sv.pos = psum;
			sv.nrm = nsum;
		}
	} else {
		for (int i = lowerBound; i <= upperBound; i++) {
			reader.Goto(i);
			SimpleVertex &sv = sverts[i];
			if (vertType & GE_VTYPE_TC_MASK) {
				reader.ReadUV(sv.uv);
			} else {
				sv.uv[0] = 0;  // This will get filled in during tesselation
				sv.uv[1] = 0;
			}
			if (vertType & GE_VTYPE_COL_MASK) {
				reader.ReadColor0_8888(sv.color);
			} else {
				memcpy(sv.color, defaultColor, 4);
			}
			if (vertType & GE_VTYPE_NRM_MASK) {
				// Normals are generated during tesselation anyway, not sure if any need to supply
				reader.ReadNrm((float *)&sv.nrm);
			} else {
				sv.nrm.x = 0;
				sv.nrm.y = 0;
				sv.nrm.z = 1.0f;
			}
			reader.ReadPos((float *)&sv.pos);
		}
	}

	// Okay, there we are! Return the new type (but keep the index bits)
	return GE_VTYPE_TC_FLOAT | GE_VTYPE_COL_8888 | GE_VTYPE_NRM_FLOAT | GE_VTYPE_POS_FLOAT | (vertType & GE_VTYPE_IDX_MASK);
}

#define START_OPEN_U 1
#define END_OPEN_U 2
#define START_OPEN_V 4
#define END_OPEN_V 8

static float lerp(float a, float b, float x) {
	return a + x * (b - a);
}

static void lerpColor(u8 a[4], u8 b[4], float x, u8 out[4]) {
	for (int i = 0; i < 4; i++) {
		out[i] = (float)a[i] + x * ((float)b[i] - (float)a[i]);
	}
}

// We decode all vertices into a common format for easy interpolation and stuff.
// Not fast but can be optimized later.
struct HWSplinePatch {
	SimpleVertex *points[16];
	int type;

	// These are used to generate UVs.
	int u_index, v_index;

	// Interpolate colors between control points (bilinear, should be good enough).
	void sampleColor(float u, float v, u8 color[4]) const {
		u *= 3.0f;
		v *= 3.0f;
		int iu = (int)floorf(u);
		int iv = (int)floorf(v);
		int iu2 = iu + 1;
		int iv2 = iv + 1;
		float fracU = u - iu;
		float fracV = v - iv;
		if (iu2 > 3) iu2 = 3;
		if (iv2 > 3) iv2 = 3;

		int tl = iu + 4 * iv;
		int tr = iu2 + 4 * iv;
		int bl = iu + 4 * iv2;
		int br = iu2 + 4 * iv2;

		u8 upperColor[4], lowerColor[4];
		lerpColor(points[tl]->color, points[tr]->color, fracU, upperColor);
		lerpColor(points[bl]->color, points[br]->color, fracU, lowerColor);
		lerpColor(upperColor, lowerColor, fracV, color);
	}
	
	void sampleTexUV(float u, float v, float &tu, float &tv) const {
		u *= 3.0f;
		v *= 3.0f;
		int iu = (int)floorf(u);
		int iv = (int)floorf(v);
		int iu2 = iu + 1;
		int iv2 = iv + 1;
		float fracU = u - iu;
		float fracV = v - iv;
		if (iu2 > 3) iu2 = 3;
		if (iv2 > 3) iv2 = 3;

		int tl = iu + 4 * iv;
		int tr = iu2 + 4 * iv;
		int bl = iu + 4 * iv2;
		int br = iu2 + 4 * iv2;

		float upperTU = lerp(points[tl]->uv[0], points[tr]->uv[0], fracU);
		float upperTV = lerp(points[tl]->uv[1], points[tr]->uv[1], fracU);
		float lowerTU = lerp(points[bl]->uv[0], points[br]->uv[0], fracU);
		float lowerTV = lerp(points[bl]->uv[1], points[br]->uv[1], fracU);
		tu = lerp(upperTU, lowerTU, fracV);
		tv = lerp(upperTV, lowerTV, fracV);
	}
};

static void CopyTriangle(u8 *&dest, SimpleVertex *v1, SimpleVertex *v2, SimpleVertex* v3) {
	int vertexSize = sizeof(SimpleVertex);
	memcpy(dest, v1, vertexSize);
	dest += vertexSize;
	memcpy(dest, v2, vertexSize);
	dest += vertexSize;
	memcpy(dest, v3, vertexSize);
	dest += vertexSize;
}

#undef b2

// Bernstein basis functions
inline float bern0(float x) { return (1 - x) * (1 - x) * (1 - x); }
inline float bern1(float x) { return 3 * x * (1 - x) * (1 - x); }
inline float bern2(float x) { return 3 * x * x * (1 - x); }
inline float bern3(float x) { return x * x * x; }

// Not sure yet if these have any use
inline float bern0deriv(float x) { return -3 * (x - 1) * (x - 1); }
inline float bern1deriv(float x) { return 9 * x * x - 12 * x + 3; }
inline float bern2deriv(float x) { return 3 * (2 - 3 * x) * x; }
inline float bern3deriv(float x) { return 3 * x * x; }


// http://en.wikipedia.org/wiki/Bernstein_polynomial
Vec3f Bernstein3D(const Vec3f p0, const Vec3f p1, const Vec3f p2, const Vec3f p3, float x) {
	return p0 * bern0(x) + p1 * bern1(x) + p2 * bern2(x) + p3 * bern3(x);
}

Vec3f Bernstein3DDerivative(const Vec3f p0, const Vec3f p1, const Vec3f p2, const Vec3f p3, float x) {
	return p0 * bern0deriv(x) + p1 * bern1deriv(x) + p2 * bern2deriv(x) + p3 * bern3deriv(x);
}

void TesselatePatch(u8 *&dest, int &count, const HWSplinePatch &patch, u32 origVertType) {
	const float third = 1.0f / 3.0f;
	
	bool realTesselation = false;

	if (!realTesselation) {
		// Fast and easy way - just draw the control points, generate some very basic normal vector subsitutes.
		// Very inaccurate though but okay for Loco Roco. Maybe should keep it as an option.

		const int tile_min_u = (patch.type & START_OPEN_U) ? 0 : 1;
		const int tile_min_v = (patch.type & START_OPEN_V) ? 0 : 1;
		const int tile_max_u = (patch.type & END_OPEN_U) ? 3 : 2;
		const int tile_max_v = (patch.type & END_OPEN_V) ? 3 : 2;

		float u_base = patch.u_index / 3.0f;
		float v_base = patch.v_index / 3.0f;

		for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
			for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
				int point_index = tile_u + tile_v * 4;

				SimpleVertex v0 = *patch.points[point_index];
				SimpleVertex v1 = *patch.points[point_index+1];
				SimpleVertex v2 = *patch.points[point_index+4];
				SimpleVertex v3 = *patch.points[point_index+5];

				// Generate UV. TODO: Do this even if UV specified in control points?
				if ((origVertType & GE_VTYPE_TC_MASK) == 0) {
					float u = u_base + tile_u * third;
					float v = v_base + tile_v * third;
					v0.uv[0] = u;
					v0.uv[1] = v;
					v1.uv[0] = u + third;
					v1.uv[1] = v;
					v2.uv[0] = u;
					v2.uv[1] = v + third;
					v3.uv[0] = u + third;
					v3.uv[1] = v + third;
				}

				// Generate normal if lighting is enabled (otherwise there's no point).
				// This is a really poor quality algorithm, we get facet normals.
				if (gstate.isLightingEnabled()) {
					Vec3f norm = Cross(v1.pos - v0.pos, v2.pos - v0.pos);
					norm.Normalize();
					if (gstate.patchfacing & 1)
						norm *= -1.0f;
					v0.nrm = norm;
					v1.nrm = norm;
					v2.nrm = norm;
					v3.nrm = norm;
				}

				CopyTriangle(dest, &v0, &v2, &v1);
				CopyTriangle(dest, &v1, &v2, &v3);
				count += 6;
			}
		}
	} else {
		// Full tesselation of spline patches.
		// TODO: This still has serious bugs, for example in Loco Roco, and there are gaps between patches for unknown reasons.

		int tess_u = gstate.getPatchDivisionU();
		int tess_v = gstate.getPatchDivisionV();
		
		const int tile_min_u = (patch.type & START_OPEN_U) ? 0 : tess_u / 3;
		const int tile_min_v = (patch.type & START_OPEN_V) ? 0 : tess_v / 3;
		const int tile_max_u = (patch.type & END_OPEN_U) ? tess_u : (tess_u + 0) * 2 / 3;
		const int tile_max_v = (patch.type & END_OPEN_V) ? tess_v : (tess_v + 0) * 2 / 3;

		// First compute all the vertices and put them in an array
		SimpleVertex *vertices = new SimpleVertex[(tess_u + 1) * (tess_v + 1)];

		bool computeNormals = gstate.isLightingEnabled();
		for (int tile_v = 0; tile_v < tess_v + 1; ++tile_v) {
			for (int tile_u = 0; tile_u < tess_u + 1; ++tile_u) {
				float u = ((float)tile_u / (float)tess_u);
				float v = ((float)tile_v / (float)tess_v);
				float bu = u;
				float bv = v;
				
				// TODO: Should be able to precompute the four curves per U, then just Bernstein per V. Will benefit large tesselation factors.
				Vec3f pos1 = Bernstein3D(patch.points[0]->pos, patch.points[1]->pos, patch.points[2]->pos, patch.points[3]->pos, bu);
				Vec3f pos2 = Bernstein3D(patch.points[4]->pos, patch.points[5]->pos, patch.points[6]->pos, patch.points[7]->pos, bu);
				Vec3f pos3 = Bernstein3D(patch.points[8]->pos, patch.points[9]->pos, patch.points[10]->pos, patch.points[11]->pos, bu);
				Vec3f pos4 = Bernstein3D(patch.points[12]->pos, patch.points[13]->pos, patch.points[14]->pos, patch.points[15]->pos, bu);

				SimpleVertex &vert = vertices[tile_v * (tess_u + 1) + tile_u];

				if (computeNormals) {
					Vec3f derivU1 = Bernstein3DDerivative(patch.points[0]->pos, patch.points[1]->pos, patch.points[2]->pos, patch.points[3]->pos, bu);
					Vec3f derivU2 = Bernstein3DDerivative(patch.points[4]->pos, patch.points[5]->pos, patch.points[6]->pos, patch.points[7]->pos, bu);
					Vec3f derivU3 = Bernstein3DDerivative(patch.points[8]->pos, patch.points[9]->pos, patch.points[10]->pos, patch.points[11]->pos, bu);
					Vec3f derivU4 = Bernstein3DDerivative(patch.points[12]->pos, patch.points[13]->pos, patch.points[14]->pos, patch.points[15]->pos, bu);
					Vec3f derivU = Bernstein3D(derivU1, derivU2, derivU3, derivU4, bv);
					Vec3f derivV = Bernstein3DDerivative(pos1, pos2, pos3, pos4, bv);

					// TODO: Interpolate normals instead of generating them, if available?
					vert.nrm = Cross(derivU, derivV).Normalized(); //.SetZero();
					if (gstate.patchfacing & 1)
						vert.nrm *= -1.0f;
				} else {
					vert.nrm.SetZero();
				}
				
				vert.pos = Bernstein3D(pos1, pos2, pos3, pos4, bv);
				
				if ((origVertType & GE_VTYPE_TC_MASK) == 0) {
					// Generate texcoord
					vert.uv[0] = u + patch.u_index * third;
					vert.uv[1] = v + patch.v_index * third;
				} else {
					// Sample UV from control points
					patch.sampleTexUV(u, v, vert.uv[0], vert.uv[1]);
				}

				if (origVertType & GE_VTYPE_COL_MASK) {
					patch.sampleColor(u, v, vert.color);
				} else {
					memcpy(vert.color, patch.points[0]->color, 4);
				}
			}
		}

		// Tesselate. TODO: Use indices so we only need to emit 4 vertices per pair of triangles instead of six.
		for (int tile_v = tile_min_v; tile_v < tile_max_v; ++tile_v) {
			for (int tile_u = tile_min_u; tile_u < tile_max_u; ++tile_u) {
				float u = ((float)tile_u / (float)tess_u);
				float v = ((float)tile_v / (float)tess_v);

				SimpleVertex *v0 = &vertices[tile_v * (tess_u + 1) + tile_u];
				SimpleVertex *v1 = &vertices[tile_v * (tess_u + 1) + tile_u + 1];
				SimpleVertex *v2 = &vertices[(tile_v + 1) * (tess_u + 1) + tile_u];
				SimpleVertex *v3 = &vertices[(tile_v + 1) * (tess_u + 1) + tile_u + 1];

				CopyTriangle(dest, v0, v2, v1);
				CopyTriangle(dest, v1, v2, v3);
				count += 6;
			}
		}

		delete [] vertices;
	}
}

void TransformDrawEngine::SubmitSpline(void* control_points, void* indices, int count_u, int count_v, int type_u, int type_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 24;
	
	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, sizeof(SimpleVertex));
	}
	const DecVtxFormat& vtxfmt = vdecoder->GetDecVtxFmt();
	
	int num_patches_u = count_u - 3;
	int num_patches_v = count_v - 3;

	// TODO: Do something less idiotic to manage this buffer
	HWSplinePatch* patches = new HWSplinePatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; ++patch_u) {
		for (int patch_v = 0; patch_v < num_patches_v; ++patch_v) {
			HWSplinePatch& patch = patches[patch_u + patch_v * num_patches_u];
			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u + point%4) + (patch_v + point/4) * count_u;
				if (indices)
					patch.points[point] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = simplified_control_points + idx;
			}
			patch.type = (type_u | (type_v << 2));
			if (patch_u != 0) patch.type &= ~START_OPEN_U;
			if (patch_v != 0) patch.type &= ~START_OPEN_V;
			if (patch_u != num_patches_u-1) patch.type &= ~END_OPEN_U;
			if (patch_v != num_patches_v-1) patch.type &= ~END_OPEN_V;
			patch.u_index = patch_u;
			patch.v_index = patch_v;
		}
	}

	u8 *decoded2 = decoded + 65536 * 36;

	int count = 0;
	u8 *dest = decoded2;

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		HWSplinePatch& patch = patches[patch_idx];
		TesselatePatch(dest, count, patch, origVertType);
	}
	delete[] patches;

	u32 vertTypeWithoutIndex = vertType & ~GE_VTYPE_IDX_MASK;

	SubmitPrim(decoded2, 0, GE_PRIM_TRIANGLES, count, vertTypeWithoutIndex, GE_VTYPE_IDX_NONE, 0);
	Flush();
}

void TransformDrawEngine::SubmitBezier(void* control_points, void* indices, int count_u, int count_v, GEPatchPrimType prim_type, u32 vertType) {
	Flush();

	if (prim_type != GE_PATCHPRIM_TRIANGLES) {
		// Only triangles supported!
		return;
	}

	u16 index_lower_bound = 0;
	u16 index_upper_bound = count_u * count_v - 1;
	bool indices_16bit = (vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT;
	const u8* indices8 = (const u8*)indices;
	const u16* indices16 = (const u16*)indices;
	if (indices)
		GetIndexBounds(indices, count_u*count_v, vertType, &index_lower_bound, &index_upper_bound);

	// Simplify away bones and morph before proceeding
	SimpleVertex *simplified_control_points = (SimpleVertex *)(decoded + 65536 * 12);
	u8 *temp_buffer = decoded + 65536 * 24;

	u32 origVertType = vertType;
	vertType = NormalizeVertices((u8 *)simplified_control_points, temp_buffer, (u8 *)control_points, index_lower_bound, index_upper_bound, vertType);

	VertexDecoder *vdecoder = GetVertexDecoder(vertType);

	int vertexSize = vdecoder->VertexSize();
	if (vertexSize != sizeof(SimpleVertex)) {
		ERROR_LOG(G3D, "Something went really wrong, vertex size: %i vs %i", vertexSize, sizeof(SimpleVertex));
	}
	const DecVtxFormat& vtxfmt = vdecoder->GetDecVtxFmt();

	// Bezier patches share less control points than spline patches. Otherwise they are pretty much the same (except bezier don't support the open/close thing)
	int num_patches_u = (count_u - 1) / 3;
	int num_patches_v = (count_v - 1) / 3;
	HWSplinePatch* patches = new HWSplinePatch[num_patches_u * num_patches_v];
	for (int patch_u = 0; patch_u < num_patches_u; patch_u++) {
		for (int patch_v = 0; patch_v < num_patches_v; patch_v++) {
			HWSplinePatch& patch = patches[patch_u + patch_v * num_patches_u];
			for (int point = 0; point < 16; ++point) {
				int idx = (patch_u * 3 + point%4) + (patch_v * 3 + point/4) * count_u;
				if (indices)
					patch.points[point] = simplified_control_points + (indices_16bit ? indices16[idx] : indices8[idx]);
				else
					patch.points[point] = simplified_control_points + idx;
			}
			patch.u_index = patch_u * 3;
			patch.v_index = patch_v * 3;
			patch.type = START_OPEN_U | START_OPEN_V | END_OPEN_U | END_OPEN_V;
		}
	}

	u8 *decoded2 = decoded + 65536 * 36;

	int count = 0;
	u8 *dest = decoded2;

	for (int patch_idx = 0; patch_idx < num_patches_u*num_patches_v; ++patch_idx) {
		HWSplinePatch& patch = patches[patch_idx];
		TesselatePatch(dest, count, patch, origVertType);
	}
	delete[] patches;

	u32 vertTypeWithoutIndex = vertType & ~GE_VTYPE_IDX_MASK;

	SubmitPrim(decoded2, 0, GE_PRIM_TRIANGLES, count, vertTypeWithoutIndex, GE_VTYPE_IDX_NONE, 0);
	Flush();
}
