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

#include "IndexGenerator.h"

#include "Common/Common.h"

// Points don't need indexing...
static const u8 indexedPrimitiveType[7] = {
	GE_PRIM_POINTS,
	GE_PRIM_LINES,
	GE_PRIM_LINES,
	GE_PRIM_TRIANGLES,
	GE_PRIM_TRIANGLES,
	GE_PRIM_TRIANGLES,
	GE_PRIM_RECTANGLES,
};

void IndexGenerator::Reset() {
	prim_ = GE_PRIM_INVALID;
	count_ = 0;
	index_ = 0;
	seenPrims_ = 0;
	pureCount_ = 0;
	this->inds_ = indsBase_;
}

bool IndexGenerator::PrimCompatible(int prim1, int prim2) {
	if (prim1 == GE_PRIM_INVALID || prim2 == GE_PRIM_KEEP_PREVIOUS)
		return true;
	return indexedPrimitiveType[prim1] == indexedPrimitiveType[prim2];
}

bool IndexGenerator::PrimCompatible(int prim) const {
	if (prim_ == GE_PRIM_INVALID || prim == GE_PRIM_KEEP_PREVIOUS)
		return true;
	return indexedPrimitiveType[prim] == prim_;
}

void IndexGenerator::Setup(u16 *inds) {
	this->indsBase_ = inds;
	Reset();
}

void IndexGenerator::AddPrim(int prim, int vertexCount) {
	switch (prim) {
	case GE_PRIM_POINTS: AddPoints(vertexCount); break;
	case GE_PRIM_LINES: AddLineList(vertexCount); break;
	case GE_PRIM_LINE_STRIP: AddLineStrip(vertexCount); break;
	case GE_PRIM_TRIANGLES: AddList(vertexCount); break;
	case GE_PRIM_TRIANGLE_STRIP: AddStrip(vertexCount); break;
	case GE_PRIM_TRIANGLE_FAN: AddFan(vertexCount); break;
	case GE_PRIM_RECTANGLES: AddRectangles(vertexCount); break;  // Same
	}
}

void IndexGenerator::AddPoints(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numVerts; i++)
		*outInds++ = startIndex + i;
	inds_ = outInds;
	// ignore overflow verts
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_POINTS;
	seenPrims_ |= 1 << GE_PRIM_POINTS;
}

void IndexGenerator::AddList(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numVerts; i += 3) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
		*outInds++ = startIndex + i + 2;
	}
	inds_ = outInds;
	// ignore overflow verts
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= 1 << GE_PRIM_TRIANGLES;
}

void IndexGenerator::AddStrip(int numVerts) {
	int wind = 1;
	const int numTris = numVerts - 2;
	u16 *outInds = inds_;
	int ibase = index_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = ibase;
		*outInds++ = ibase + wind;
		wind ^= 3;  // toggle between 1 and 2
		*outInds++ = ibase + wind;
		ibase++;
	}
	inds_ = outInds;
	index_ += numVerts;
	if (numTris > 0)
		count_ += numTris * 3;
	// This is so we can detect one single strip by just looking at seenPrims_.
	if (!seenPrims_) {
		seenPrims_ = 1 << GE_PRIM_TRIANGLE_STRIP;
		prim_ = GE_PRIM_TRIANGLE_STRIP;
		pureCount_ = numVerts;
	} else {
		seenPrims_ |= (1 << GE_PRIM_TRIANGLE_STRIP) | (1 << GE_PRIM_TRIANGLES);
		prim_ = GE_PRIM_TRIANGLES;
		pureCount_ = 0;
	}
}

void IndexGenerator::AddFan(int numVerts) {
	const int numTris = numVerts - 2;
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = startIndex;
		*outInds++ = startIndex + i + 1;
		*outInds++ = startIndex + i + 2;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numTris * 3;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= 1 << GE_PRIM_TRIANGLE_FAN;
}

//Lines
void IndexGenerator::AddLineList(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numVerts; i += 2) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= 1 << prim_;
}

void IndexGenerator::AddLineStrip(int numVerts) {
	const int numLines = numVerts - 1;
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numLines; i++) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numLines * 2;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= 1 << GE_PRIM_LINE_STRIP;
}

void IndexGenerator::AddRectangles(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numVerts = numVerts & ~1;
	for (int i = 0; i < numVerts; i += 2) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_RECTANGLES;
	seenPrims_ |= 1 << GE_PRIM_RECTANGLES;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslatePoints(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	for (int i = 0; i < numInds; i++)
		*outInds++ = indexOffset + inds[i];
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_POINTS;
	seenPrims_ |= (1 << GE_PRIM_POINTS) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateLineList(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	numInds = numInds & ~1;
	for (int i = 0; i < numInds; i += 2) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
	}
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= (1 << GE_PRIM_LINES) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateLineStrip(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	int numLines = numInds - 1;
	u16 *outInds = inds_;
	for (int i = 0; i < numLines; i++) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
	}
	inds_ = outInds;
	count_ += numLines * 2;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= (1 << GE_PRIM_LINE_STRIP) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateList(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	int numTris = numInds / 3;  // Round to whole triangles
	numInds = numTris * 3;
	for (int i = 0; i < numInds; i += 3) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
		*outInds++ = indexOffset + inds[i + 2];
	}
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= (1 << GE_PRIM_TRIANGLES) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateStrip(int numInds, const ITypeLE *inds, int indexOffset) {
	int wind = 1;
	indexOffset = index_ - indexOffset;
	int numTris = numInds - 2;
	u16 *outInds = inds_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + wind];
		wind ^= 3;  // Toggle between 1 and 2
		*outInds++ = indexOffset + inds[i + wind];
	}
	inds_ = outInds;
	count_ += numTris * 3;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= (1 << GE_PRIM_TRIANGLE_STRIP) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateFan(int numInds, const ITypeLE *inds, int indexOffset) {
	if (numInds <= 0) return;
	indexOffset = index_ - indexOffset;
	int numTris = numInds - 2;
	u16 *outInds = inds_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = indexOffset + inds[0];
		*outInds++ = indexOffset + inds[i + 1];
		*outInds++ = indexOffset + inds[i + 2];
	}
	inds_ = outInds;
	count_ += numTris * 3;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= (1 << GE_PRIM_TRIANGLE_FAN) | flag;
}

template <class ITypeLE, int flag>
inline void IndexGenerator::TranslateRectangles(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numInds = numInds & ~1;
	for (int i = 0; i < numInds; i += 2) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i+1];
	}
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_RECTANGLES;
	seenPrims_ |= (1 << GE_PRIM_RECTANGLES) | flag;
}

// Could template this too, but would have to define in header.
void IndexGenerator::TranslatePrim(int prim, int numInds, const u8 *inds, int indexOffset) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;  // Same
	}
}

void IndexGenerator::TranslatePrim(int prim, int numInds, const u16_le *inds, int indexOffset) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;  // Same
	}
}

void IndexGenerator::TranslatePrim(int prim, int numInds, const u32_le *inds, int indexOffset) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;  // Same
	}
}
