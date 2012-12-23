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

#include "../../Core/MemMap.h"
#include "../../Core/Host.h"
#include "../../Core/System.h"
#include "../../native/gfx_es2/gl_state.h"

#include "../Math3D.h"
#include "../GPUState.h"
#include "../ge_constants.h"

#include "StateMapping.h"
#include "TextureCache.h"
#include "TransformPipeline.h"
#include "VertexDecoder.h"
#include "ShaderManager.h"
#include "DisplayListInterpreter.h"
#include "IndexGenerator.h"

const GLuint glprim[8] = {
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_TRIANGLES,	 // With OpenGL ES we have to expand sprites into triangles, tripling the data instead of doubling. sigh. OpenGL ES, Y U NO SUPPORT GL_QUADS?
};

u8 decoded[65536 * 32];
VertexDecoder dec;
uint16_t decIndex[65536];
int numVerts;

IndexGenerator indexGen;

TransformedVertex transformed[65536];
TransformedVertex transformedExpanded[65536];


// TODO: This should really return 2 colors, one for specular and one for diffuse.

// Convenient way to do precomputation to save the parts of the lighting calculation
// that's common between the many vertices of a draw call.
class Lighter {
public:
	Lighter();
	void Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], Vec3 pos, Vec3 normal, float dots[4]);

private:
	bool disabled_;
	Color4 globalAmbient;
	Color4 materialEmissive;
	Color4 materialAmbient;
	Color4 materialDiffuse;
	Color4 materialSpecular;
	float specCoef_;
	// Vec3 viewer_;
	bool doShadeMapping_;
	int materialUpdate_;
};

Lighter::Lighter() {
	disabled_ = false;
	doShadeMapping_ = (gstate.texmapmode & 0x3) == 2;
	if (!doShadeMapping_ && !(gstate.lightEnable[0]&1) && !(gstate.lightEnable[1]&1) && !(gstate.lightEnable[2]&1) && !(gstate.lightEnable[3]&1))
	{
		disabled_ = true;
	}
	materialEmissive.GetFromRGB(gstate.materialemissive);
	materialEmissive.a = 0.0f;
	globalAmbient.GetFromRGB(gstate.ambientcolor);
	globalAmbient.GetFromA(gstate.ambientalpha);
	materialAmbient.GetFromRGB(gstate.materialambient);
	materialAmbient.a = 1.0f;
	materialDiffuse.GetFromRGB(gstate.materialdiffuse);
	materialDiffuse.a = 1.0f;
	materialSpecular.GetFromRGB(gstate.materialspecular);
	materialSpecular.a = 1.0f;
	specCoef_ = getFloat24(gstate.materialspecularcoef);
	// viewer_ = Vec3(-gstate.viewMatrix[9], -gstate.viewMatrix[10], -gstate.viewMatrix[11]);
	materialUpdate_ = gstate.materialupdate & 7;
}

void Lighter::Light(float colorOut0[4], float colorOut1[4], const float colorIn[4], Vec3 pos, Vec3 normal, float dots[4])
{
	if (disabled_) {
		memcpy(colorOut0, colorIn, sizeof(float) * 4);
		memset(colorOut1, 0, sizeof(float) * 4);
		return;
	}

	Vec3 norm = normal.Normalized();
	Color4 in(colorIn);

	const Color4 *ambient;
	if (materialUpdate_ & 1)
		ambient = &in;
	else
		ambient = &materialAmbient;

	const Color4 *diffuse;
	if (materialUpdate_ & 2)
		diffuse = &in;
	else
		diffuse = &materialDiffuse;

	const Color4 *specular;
	if (materialUpdate_ & 4)
		specular = &in;
	else
		specular = &materialSpecular;

	Color4 lightSum0 = globalAmbient * *ambient + materialEmissive;
	Color4 lightSum1(0, 0, 0, 0);

	// Try lights.elf - there's something wrong with the lighting

	for (int l = 0; l < 4; l++)
	{
		// can we skip this light?
		if ((gstate.lightEnable[l] & 1) == 0 && !doShadeMapping_)
			continue;

		GELightComputation comp = (GELightComputation)(gstate.ltype[l] & 3);
		GELightType type = (GELightType)((gstate.ltype[l] >> 8) & 3);
		Vec3 toLight;

		if (type == GE_LIGHTTYPE_DIRECTIONAL)
			toLight = Vec3(gstate_c.lightpos[l]);  // lightdir is for spotlights
		else
			toLight = Vec3(gstate_c.lightpos[l]) - pos;

		bool doSpecular = (comp != GE_LIGHTCOMP_ONLYDIFFUSE);
		bool poweredDiffuse = comp == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE;

		float dot = toLight * norm;

		// Clamp dot to zero.
		if (dot < 0.0f) dot = 0.0f;

		if (poweredDiffuse)
			dot = powf(dot, specCoef_);

		float lightScale = 1.0f;
		float distance = toLight.Normalize();
		if (type != GE_LIGHTTYPE_DIRECTIONAL)
		{
			lightScale = 1.0f / (gstate_c.lightatt[l][0] + gstate_c.lightatt[l][1]*distance + gstate_c.lightatt[l][2]*distance*distance);
			if (lightScale > 1.0f) lightScale = 1.0f;
		}

		Color4 lightDiff(gstate_c.lightColor[1][l], 0.0f);
		Color4 diff = (lightDiff * *diffuse) * (dot * lightScale);

		// Real PSP specular
		Vec3 toViewer(0,0,1);
		// Better specular
		// Vec3 toViewer = (viewer - pos).Normalized();

		if (doSpecular)
		{
			Vec3 halfVec = toLight;
			halfVec += toViewer;
			halfVec.Normalize();

			dot = halfVec * norm;
			if (dot >= 0)
			{
				Color4 lightSpec(gstate_c.lightColor[2][l], 0.0f);
				lightSum1 += (lightSpec * *specular * (powf(dot, specCoef_)*lightScale));
			}
		}
		dots[l] = dot;
		if (gstate.lightEnable[l] & 1)
		{
			Color4 lightAmbient(gstate_c.lightColor[2][l], 1.0f);
			lightSum0 += lightAmbient * *ambient + diff;
		}
	}

	// 4?
	for (int i = 0; i < 4; i++) {
		colorOut0[i] = lightSum0[i] > 1.0f ? 1.0f : lightSum0[i];
		colorOut1[i] = lightSum1[i] > 1.0f ? 1.0f : lightSum1[i];
	}
}

struct GlTypeInfo {
	GLuint type;
	int count;
	GLboolean normalized;
};

const GlTypeInfo GLComp[8] = {
	{0}, // 	DEC_NONE,
	{GL_FLOAT, 1, GL_FALSE}, // 	DEC_FLOAT_1,
	{GL_FLOAT, 2, GL_FALSE}, // 	DEC_FLOAT_2,
	{GL_FLOAT, 3, GL_FALSE}, // 	DEC_FLOAT_3,
	{GL_FLOAT, 4, GL_FALSE}, // 	DEC_FLOAT_4,
	{GL_BYTE, 3, GL_TRUE}, // 	DEC_S8_3,
	{GL_SHORT, 3, GL_TRUE},// 	DEC_S16_3,
	{GL_UNSIGNED_BYTE, 4, GL_TRUE},// 	DEC_U8_4,
};

static inline void VertexAttribSetup(int attrib, int fmt, int stride, u8 *ptr) {
	if (attrib != -1 && fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		glEnableVertexAttribArray(attrib);
		glVertexAttribPointer(attrib, type.count, type.type, type.normalized, stride, ptr);
	}
}
static inline void VertexAttribDisable(int attrib, int fmt) {
	if (attrib != -1 && fmt) {
		glDisableVertexAttribArray(attrib);
	}
}

// TODO: Use VBO and get rid of the vertexData pointers - with that, we will supply only offsets
static void SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt, u8 *vertexData) {
	VertexAttribSetup(program->a_weight0123, decFmt.w0fmt, decFmt.stride, vertexData + decFmt.w0off);
	VertexAttribSetup(program->a_weight4567, decFmt.w1fmt, decFmt.stride, vertexData + decFmt.w1off);
	VertexAttribSetup(program->a_texcoord, decFmt.uvfmt, decFmt.stride, vertexData + decFmt.uvoff);
	VertexAttribSetup(program->a_color0, decFmt.c0fmt, decFmt.stride, vertexData + decFmt.c0off);
	VertexAttribSetup(program->a_color1, decFmt.c1fmt, decFmt.stride, vertexData + decFmt.c1off);
	VertexAttribSetup(program->a_normal, decFmt.nrmfmt, decFmt.stride, vertexData + decFmt.nrmoff);
	VertexAttribSetup(program->a_position, decFmt.posfmt, decFmt.stride, vertexData + decFmt.posoff);
}

static void DesetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt) {
	VertexAttribDisable(program->a_weight0123, decFmt.w0fmt);
	VertexAttribDisable(program->a_weight4567, decFmt.w1fmt);
	VertexAttribDisable(program->a_texcoord, decFmt.uvfmt);
	VertexAttribDisable(program->a_color0, decFmt.c0fmt);
	VertexAttribDisable(program->a_color1, decFmt.c1fmt);
	VertexAttribDisable(program->a_normal, decFmt.nrmfmt);
	VertexAttribDisable(program->a_position, decFmt.posfmt);
}

// This is the software transform pipeline, which is necessary for supporting RECT
// primitives correctly, and may be easier to use for debugging than the hardware
// transform pipeline.

// There's code here that simply expands transformed RECTANGLES into plain triangles.

// We're gonna have to keep software transforming RECTANGLES, unless we use a geom shader which we can't on OpenGL ES 2.0.
// Usually, though, these primitives don't use lighting etc so it's no biggie performance wise, but it would be nice to get rid of
// this code.

// Actually, if we find the camera-relative right and down vectors, it might even be possible to add the extra points in pre-transformed
// space and thus make decent use of hardware transform.

// Actually again, single quads could be drawn more efficiently using GL_TRIANGLE_STRIP, no need to duplicate verts as for
// GL_TRIANGLES. Still need to sw transform to compute the extra two corners though.
void SoftwareTransformAndDraw(int prim, LinkedShader *program, int vertexCount, void *inds, int indexType, const DecVtxFormat &decVtxFormat, int maxIndex)
{
	/*
	DEBUG_LOG(G3D, "View matrix:");
	const float *m = &gstate.viewMatrix[0];
	DEBUG_LOG(G3D, "%f %f %f", m[0], m[1], m[2]);
	DEBUG_LOG(G3D, "%f %f %f", m[3], m[4], m[5]);
	DEBUG_LOG(G3D, "%f %f %f", m[6], m[7], m[8]);
	DEBUG_LOG(G3D, "%f %f %f", m[9], m[10], m[11]);
	*/

	// Temporary storage for RECTANGLES emulation
	float v2[3] = {0};
	float uv2[2] = {0};

	bool throughmode = (gstate.vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.

#if defined(USING_GLES2)
	if (vertexCount > 0x10000/3)
		vertexCount = 0x10000/3;
#endif

	Lighter lighter;

	VertexReader reader(decoded, decVtxFormat);
	for (int index = 0; index < maxIndex; index++)
	{	
		reader.Goto(index);

		float v[3] = {0, 0, 0};
		float c0[4] = {1, 1, 1, 1};
		float c1[4] = {0, 0, 0, 0};
		float uv[2] = {0, 0};

		if (throughmode)
		{
			// Do not touch the coordinates or the colors. No lighting.
			reader.ReadPos(v);
			if (reader.hasColor0()) {
				reader.ReadColor0(c0);
				for (int j = 0; j < 4; j++) {
					c1[j] = 0.0f;
				}
			}
			else
			{
				c0[0] = (gstate.materialambient & 0xFF) / 255.f;
				c0[1] = ((gstate.materialambient >> 8) & 0xFF) / 255.f;
				c0[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
				c0[3] = (gstate.materialalpha & 0xFF) / 255.f;
			}

			if (reader.hasUV()) {
				reader.ReadUV(uv);
			}
			// Scale UV?
		}
		else
		{
			// We do software T&L for now
			float out[3], norm[3];
			float pos[3], nrm[3] = {0};
			reader.ReadPos(pos);
			if (reader.hasNormal())
				reader.ReadNrm(nrm);

			if ((gstate.vertType & GE_VTYPE_WEIGHT_MASK) == GE_VTYPE_WEIGHT_NONE)
			{
				Vec3ByMatrix43(out, pos, gstate.worldMatrix);
				if (reader.hasNormal()) {
					Norm3ByMatrix43(norm, nrm, gstate.worldMatrix);
				} else {
					memset(norm, 0, 12);
				}
			}
			else
			{
				float weights[8];
				reader.ReadWeights(weights);
				// Skinning
				Vec3 psum(0,0,0);
				Vec3 nsum(0,0,0);
				int nweights = ((gstate.vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT) + 1;
				for (int i = 0; i < nweights; i++)
				{
					if (weights[i] != 0.0f) {
						Vec3ByMatrix43(out, pos, gstate.boneMatrix+i*12);
						Vec3 tpos(out);
						psum += tpos * weights[i];
						if (reader.hasNormal()) {
							Norm3ByMatrix43(norm, nrm, gstate.boneMatrix+i*12);
							Vec3 tnorm(norm);
							nsum += tnorm * weights[i];
						}
					}
				}
				
				// Yes, we really must multiply by the world matrix too.
				Vec3ByMatrix43(out, psum.v, gstate.worldMatrix);
				if (reader.hasNormal()) {
					Norm3ByMatrix43(norm, nsum.v, gstate.worldMatrix);
				}
			}

			// Perform lighting here if enabled. don't need to check through, it's checked above.
			float dots[4] = {0,0,0,0};
			float unlitColor[4] = {1, 1, 1, 1};
			if (reader.hasColor0()) {
				reader.ReadColor0(unlitColor);
			} else {
				unlitColor[0] = (gstate.materialambient & 0xFF) / 255.f;
				unlitColor[1] = ((gstate.materialambient >> 8) & 0xFF) / 255.f;
				unlitColor[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
				unlitColor[3] = (gstate.materialalpha & 0xFF) / 255.f;
			}
			float litColor0[4];
			float litColor1[4];
			lighter.Light(litColor0, litColor1, unlitColor, out, norm, dots);

			if (gstate.lightingEnable & 1)
			{
				// Don't ignore gstate.lmode - we should send two colors in that case
				if (gstate.lmode & 1) {
					// Separate colors
					for (int j = 0; j < 4; j++) {
						c0[j] = litColor0[j];
						c1[j] = litColor1[j];
					}
				} else {
					// Summed color into c0
					for (int j = 0; j < 4; j++) {
						c0[j] = litColor0[j] + litColor1[j];
						c1[j] = 0.0f;
					}
				}
			}
			else
			{
				if (reader.hasColor0()) {
					for (int j = 0; j < 4; j++) {
						c0[j] = unlitColor[j];
						c1[j] = 0.0f;
					}
				} else {
					c0[0] = (gstate.materialambient & 0xFF) / 255.f;
					c0[1] = ((gstate.materialambient >> 8) & 0xFF) / 255.f;
					c0[2] = ((gstate.materialambient >> 16) & 0xFF) / 255.f;
					c0[3] = (gstate.materialalpha & 0xFF) / 255.f;
				}
			}

			if (reader.hasUV()) {
				float ruv[2];
				reader.ReadUV(ruv);
				// Perform texture coordinate generation after the transform and lighting - one style of UV depends on lights.
				switch (gstate.getUVGenMode())
				{
				case 0:	// UV mapping
					// Texture scale/offset is only performed in this mode.
					uv[0] = ruv[0]*gstate_c.uScale + gstate_c.uOff;
					uv[1] = ruv[1]*gstate_c.vScale + gstate_c.vOff;
					break;
				case 1:
					{
						// Projection mapping
						Vec3 source;
						switch (gstate.getUVProjMode())
						{
						case 0: // Use model space XYZ as source
							source = pos;
							break;
						case 1: // Use unscaled UV as source
							source = Vec3(ruv[0], ruv[1], 0.0f);
							break;
						case 2: // Use normalized normal as source
							source = Vec3(norm).Normalized();
							break;
						case 3: // Use non-normalized normal as source!
							source = Vec3(norm);
							break;
						}

						float uvw[3];
						Vec3ByMatrix43(uvw, &source.x, gstate.tgenMatrix);
						uv[0] = uvw[0];
						uv[1] = uvw[1];
					}
					break;
				case 2:
					// Shade mapping - use dot products from light sources to generate U and V.
					{
						uv[0] = dots[gstate.getUVLS0()];
						uv[1] = dots[gstate.getUVLS1()];
					}
					break;
				case 3:
					// Illegal
					break;
				}
			}

			// Transform the coord by the view matrix.
			Vec3ByMatrix43(v, out, gstate.viewMatrix);
		}

		// TODO: Write to a flexible buffer, we don't always need all four components.
		memcpy(&transformed[index].x, v, 3 * sizeof(float));
		memcpy(&transformed[index].uv, uv, 2 * sizeof(float));
		memcpy(&transformed[index].color0, c0, 4 * sizeof(float));
		memcpy(&transformed[index].color1, c1, 3 * sizeof(float));
	}

	// Step 2: expand rectangles.
	const TransformedVertex *drawBuffer = transformed;
	int numTrans = 0;

	bool drawIndexed = false;

	if (prim != GE_PRIM_RECTANGLES) {
		// We can simply draw the unexpanded buffer.
		numTrans = vertexCount;
		drawIndexed = true;
	} else {
		numTrans = 0;
		drawBuffer = transformedExpanded;
		TransformedVertex *trans = &transformedExpanded[0];
		TransformedVertex saved;
		for (int i = 0; i < vertexCount; i++) {
			int index = ((u16*)inds)[i];

			TransformedVertex &transVtx = transformed[index];
			if ((i & 1) == 0)
			{
				// Save this vertex so we can generate when we get the next one. Color is taken from the last vertex.
				saved = transVtx;
			}
			else
			{
				// We have to turn the rectangle into two triangles, so 6 points. Sigh.

				// TODO: there's supposed to be extra magic here to rotate the UV coordinates depending on if upside down etc.

				// bottom right
				*trans = transVtx;
				trans++;

				// top left
				*trans = transVtx;
				trans->x = saved.x;
				trans->uv[0] = saved.uv[0];
				trans->y = saved.y;
				trans->uv[1] = saved.uv[1];
				trans++;

				// top right
				*trans = transVtx;
				trans->x = saved.x;
				trans->uv[0] = saved.uv[0];
				trans++;

				// bottom left
				*trans = transVtx;
				trans->y = saved.y;
				trans->uv[1] = saved.uv[1];
				trans++;

				// bottom right
				*trans = transVtx;
				trans->x = saved.x;
				trans->uv[0] = saved.uv[0];
				trans->y = saved.y;
				trans->uv[1] = saved.uv[1];
				trans++;

				// top left
				*trans = transVtx;
				trans++;

				numTrans += 6;
			}
		}
	}

	// TODO: Make a cache for glEnableVertexAttribArray and glVertexAttribPtr states,
	// these spam the gDebugger log.
	glEnableVertexAttribArray(program->a_position);
	if (program->a_texcoord != -1) glEnableVertexAttribArray(program->a_texcoord);
	if (program->a_color0 != -1) glEnableVertexAttribArray(program->a_color0);
	if (program->a_color1 != -1) glEnableVertexAttribArray(program->a_color1);
	const int vertexSize = sizeof(transformed[0]);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, vertexSize, drawBuffer);
	if (program->a_texcoord != -1) glVertexAttribPointer(program->a_texcoord, 2, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 3 * 4);
	if (program->a_color0 != -1) glVertexAttribPointer(program->a_color0, 4, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 5 * 4);
	if (program->a_color1 != -1) glVertexAttribPointer(program->a_color1, 3, GL_FLOAT, GL_FALSE, vertexSize, ((uint8_t*)drawBuffer) + 9 * 4);
	if (drawIndexed) {
		glDrawElements(glprim[prim], numTrans, GL_UNSIGNED_SHORT, (GLvoid *)inds);
	} else {
		glDrawArrays(glprim[prim], 0, numTrans);
	}
	glDisableVertexAttribArray(program->a_position);
	if (program->a_texcoord != -1) glDisableVertexAttribArray(program->a_texcoord);
	if (program->a_color0 != -1) glDisableVertexAttribArray(program->a_color0);
	if (program->a_color1 != -1) glDisableVertexAttribArray(program->a_color1);
}

void GLES_GPU::InitTransform() {
	indexGen.Setup(decIndex);
	numVerts = 0;
}

void GLES_GPU::TransformAndDrawPrim(void *verts, void *inds, int prim, int vertexCount, float *customUV, int forceIndexType, int *bytesRead)
{
	// For the future
	if (!indexGen.PrimCompatible(prim))
		Flush();

	if (!indexGen.Empty()) {
		gpuStats.numJoins++;
	}
	gpuStats.numDrawCalls++;
	gpuStats.numVertsTransformed += vertexCount;

	indexGen.SetIndex(numVerts);
	int indexLowerBound, indexUpperBound;
	// First, decode the verts and apply morphing
	dec.SetVertexType(gstate.vertType);
	dec.DecodeVerts(decoded + numVerts * (int)dec.GetDecVtxFmt().stride, verts, inds, prim, vertexCount, &indexLowerBound, &indexUpperBound);
	numVerts += indexUpperBound - indexLowerBound + 1;

	if (bytesRead)
		*bytesRead = vertexCount * dec.VertexSize();

	int indexType = (gstate.vertType & GE_VTYPE_IDX_MASK);
	if (forceIndexType != -1) indexType = forceIndexType;
	switch (indexType) {
	case GE_VTYPE_IDX_NONE:
		switch (prim) {
		case GE_PRIM_POINTS: indexGen.AddPoints(vertexCount); break;
		case GE_PRIM_LINES: indexGen.AddLineList(vertexCount); break;
		case GE_PRIM_LINE_STRIP: indexGen.AddLineStrip(vertexCount); break;
		case GE_PRIM_TRIANGLES: indexGen.AddList(vertexCount); break;
		case GE_PRIM_TRIANGLE_STRIP: indexGen.AddStrip(vertexCount); break;
		case GE_PRIM_TRIANGLE_FAN: indexGen.AddFan(vertexCount); break;
		case GE_PRIM_RECTANGLES: indexGen.AddRectangles(vertexCount); break;  // Same
		}
		break;

	case GE_VTYPE_IDX_8BIT:
		switch (prim) {
		case GE_PRIM_POINTS: indexGen.TranslatePoints(vertexCount, (const u8 *)inds, -indexLowerBound); break;
		case GE_PRIM_LINES: indexGen.TranslateLineList(vertexCount, (const u8 *)inds, -indexLowerBound); break;
		case GE_PRIM_LINE_STRIP: indexGen.TranslateLineStrip(vertexCount, (const u8 *)inds, -indexLowerBound); break;
		case GE_PRIM_TRIANGLES: indexGen.TranslateList(vertexCount, (const u8 *)inds, -indexLowerBound); break;
		case GE_PRIM_TRIANGLE_STRIP: indexGen.TranslateStrip(vertexCount, (const u8 *)inds, -indexLowerBound); break;
		case GE_PRIM_TRIANGLE_FAN: indexGen.TranslateFan(vertexCount, (const u8 *)inds, -indexLowerBound); break;
		case GE_PRIM_RECTANGLES: indexGen.TranslateRectangles(vertexCount, (const u8 *)inds, -indexLowerBound); break;  // Same
		}
		break;

	case GE_VTYPE_IDX_16BIT:
		switch (prim) {
		case GE_PRIM_POINTS: indexGen.TranslatePoints(vertexCount, (const u16 *)inds, -indexLowerBound); break;
		case GE_PRIM_LINES: indexGen.TranslateLineList(vertexCount, (const u16 *)inds, -indexLowerBound); break;
		case GE_PRIM_LINE_STRIP: indexGen.TranslateLineStrip(vertexCount, (const u16 *)inds, -indexLowerBound); break;
		case GE_PRIM_TRIANGLES: indexGen.TranslateList(vertexCount, (const u16 *)inds, -indexLowerBound); break;
		case GE_PRIM_TRIANGLE_STRIP: indexGen.TranslateStrip(vertexCount, (const u16 *)inds, -indexLowerBound); break;
		case GE_PRIM_TRIANGLE_FAN: indexGen.TranslateFan(vertexCount, (const u16 *)inds, -indexLowerBound); break;
		case GE_PRIM_RECTANGLES: indexGen.TranslateRectangles(vertexCount, (const u16 *)inds, -indexLowerBound); break;  // Same
		}
		break;
	}
	Flush();
}

void GLES_GPU::Flush()
{
	if (indexGen.Empty())
		return;
	// From here on out, the index type is ALWAYS 16-bit. Deal with it.

	// And here we should return, having collected the morphed but untransformed vertices.
	// Note that DecodeVerts should convert strips into indexed lists etc, adding to our
	// current vertex buffer and index buffer.

	// The rest below here should only execute on Flush.

#if 0
	for (int i = indexLowerBound; i <= indexUpperBound; i++) {
		PrintDecodedVertex(decoded[i], gstate.vertType);
	}
#endif
	// Check if anything needs updating
	if (gstate_c.textureChanged)
	{
		if ((gstate.textureMapEnable & 1) && !gstate.isModeClear())
		{
			PSPSetTexture();
		}
		gstate_c.textureChanged = false;
	}
	gpuStats.numFlushes++;

	// TODO: This should not be done on every drawcall, we should collect vertex data
	// until critical state changes. That's when we draw (flush).

	int prim = indexGen.Prim();

	ApplyDrawState();
	UpdateViewportAndProjection();

	LinkedShader *program = shaderManager_->ApplyShader(prim);

	DEBUG_LOG(G3D, "Flush prim %i! %i verts in one go", prim, numVerts);

	if (CanUseHardwareTransform(prim)) {
		SetupDecFmtForDraw(program, dec.GetDecVtxFmt(), decoded);
		glDrawElements(glprim[prim], indexGen.VertexCount(), GL_UNSIGNED_SHORT, (GLvoid *)decIndex);
		DesetupDecFmtForDraw(program, dec.GetDecVtxFmt());
	} else {
		SoftwareTransformAndDraw(prim, program, indexGen.VertexCount(), (void *)decIndex, GE_VTYPE_IDX_16BIT, dec.GetDecVtxFmt(),
			indexGen.MaxIndex());
	}

	indexGen.Reset();
	numVerts = 0;
}
