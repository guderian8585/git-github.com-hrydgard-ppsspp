#include "Common/GPU/OpenGL/GLTexture.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d.h"
#include "Common/Math/math_util.h"
#include "Common/Log.h"

GLRTexture::GLRTexture(const Draw::DeviceCaps &caps, int width, int height, int depth, int _numMips)
	: w(width), h(height), d(depth), numMips(_numMips) {
	if (caps.textureNPOTFullySupported) {
		canWrap = true;
	} else {
		canWrap = isPowerOf2(width) && isPowerOf2(height);
	}
}

GLRTexture::~GLRTexture() {
	// GLRFramebuffer can contain unused, uninitialized GLRTexture-s so need
	// to check texture here.
	if (texture) {
		if (texPool_) {
			texPool_->MoveToPoolFromDestructor(this);
		} else {
			glDeleteTextures(1, &texture);
		}
	}
}

#define SMALL_TEX_SIDE 64

inline bool IsSmall(int w, int h) {
	return w * h <= SMALL_TEX_SIDE * SMALL_TEX_SIDE;
}

GLRTexturePool::~GLRTexturePool() {
	_dbg_assert_(pool_.empty());
}

void GLRTexturePool::Allocate(GLRTexture *newTexture) {
	_dbg_assert_(!newTexture->texture);

	newTexture->texPool_ = this;

	// We'll grab a same-sized handle from the pool if available.
	// Loop backwards to make the erase cheaper.
	for (auto p = pool_.rbegin(), end = pool_.rend(); p != end; p++) {
		if (p->w == newTexture->w && p->h == newTexture->h && p->numMips == newTexture->numMips && p->isRenderTarget == newTexture->isRenderTarget) {
			// Match, perfect. Remove from pool.
			newTexture->texture = p->texture;
			// Strange idiom, but that's what you have to do to erase a reverse iterator.
			pool_.erase(std::next(p).base());
			return;
		}
	}

	// Didn't find a perfect match. Try for imperfect.
	if (!newTexture->texture && !pool_.empty()) {
		// Just grab the last entry in the pool that has the same number of mip levels.
		// Don't want to create invalid mipmap pyramids by reusing textures..
		for (auto p = pool_.rbegin(), end = pool_.rend(); p != end; p++) {
			if (p->numMips == newTexture->numMips && p->isRenderTarget == newTexture->isRenderTarget) {
				// Match, good enough. Remove from pool.
				newTexture->texture = p->texture;
				// Strange idiom, but that's what you have to do to erase a reverse iterator.
				pool_.erase(std::next(p).base());
				return;
			}
		}
	}

	// We simply need a new handle.
	glGenTextures(1, &newTexture->texture);
}

void GLRTexturePool::MoveToPoolFromDestructor(GLRTexture *texture) {
	if (!IsSmall(texture->w, texture->h) && texture->numMips == 1) {
		// Shrink the texture right down to a single pixel, to save memory without destroying (whether this actually saves memory, who knows...)
		glBindTexture(GL_TEXTURE_2D, texture->texture);
		uint32_t pixel = 0xFFFF00FF;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->isRenderTarget ? nullptr : &pixel);
		texture->w = 1;
		texture->h = 1;
	}

	pool_.push_back(PooledTexture{
		texture->texture,
		texture->w,
		texture->h,
		texture->numMips,
		texture->isRenderTarget,
	});
}

void GLRTexturePool::Clear() {
	for (auto &p : pool_) {
		glDeleteTextures(1, &p.texture);
	}
	pool_.clear();
}
