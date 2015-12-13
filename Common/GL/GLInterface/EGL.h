// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <EGL/egl.h>

#include "Common/GL/GLInterfaceBase.h"

class cInterfaceEGL : public cInterfaceBase
{
protected:
	void DetectMode();
	EGLSurface egl_surf;
	EGLContext egl_ctx;
	EGLDisplay egl_dpy;

	virtual EGLDisplay OpenDisplay() = 0;
	virtual EGLNativeWindowType InitializePlatform(EGLNativeWindowType host_window, EGLConfig config) = 0;
	virtual void ShutdownPlatform() = 0;
public:
	void SwapInterval(int Interval);
	void Swap();
	void SetMode(u32 mode) { s_opengl_mode = mode; }
	void* GetFuncAddress(const std::string& name);
	bool Create(void *window_handle, bool core);
	bool MakeCurrent();
	bool ClearCurrent();
	void Shutdown();
};
