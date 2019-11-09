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

#include "ppsspp_config.h"

#include "base/display.h"  // Only to check screen aspect ratio with pixel_yres/pixel_xres

#include "base/colorutil.h"
#include "base/stringutil.h"
#include "base/timeutil.h"
#include "math/curves.h"
#include "net/resolve.h"
#include "gfx_es2/gpu_features.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "util/text/utf8.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "UI/EmuScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"
#include "UI/TiltAnalogSettingsScreen.h"
#include "UI/TiltEventProcessor.h"
#include "UI/ComboKeyMappingScreen.h"
#include "UI/GPUDriverTestScreen.h"

#include "Common/KeyMap.h"
#include "Common/FileUtil.h"
#include "Common/OSVersion.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/TextureReplacer.h"
#include "Core/WebServer.h"
#include "GPU/Common/PostShader.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/FramebufferCommon.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#include "Windows/MainWindow.h"
#include <shlobj.h>
#include "Windows/W32Util/ShellUtil.h"
#endif

GameSettingsScreen::GameSettingsScreen(std::string gamePath, std::string gameID, bool editThenRestore)
	: UIDialogScreenWithGameBackground(gamePath), gameID_(gameID), enableReports_(false), editThenRestore_(editThenRestore) {
	lastVertical_ = UseVerticalLayout();
}

bool GameSettingsScreen::UseVerticalLayout() const {
	return dp_yres > dp_xres * 1.1f;
}

// This needs before run CheckGPUFeatures()
// TODO: Remove this if fix the issue
bool CheckSupportShaderTessellationGLES() {
#if PPSSPP_PLATFORM(UWP)
	return true;
#else
	// TODO: Make work with non-GL backends
	int maxVertexTextureImageUnits = gl_extensions.maxVertexTextureUnits;
	bool vertexTexture = maxVertexTextureImageUnits >= 3; // At least 3 for hardware tessellation

	bool textureFloat = gl_extensions.ARB_texture_float || gl_extensions.OES_texture_float;
	bool hasTexelFetch = gl_extensions.GLES3 || (!gl_extensions.IsGLES && gl_extensions.VersionGEThan(3, 3, 0)) || gl_extensions.EXT_gpu_shader4;

	return vertexTexture && textureFloat && hasTexelFetch;
#endif
}

bool DoesBackendSupportHWTess() {
	switch (GetGPUBackend()) {
	case GPUBackend::OPENGL:
		return CheckSupportShaderTessellationGLES();
	case GPUBackend::VULKAN:
	case GPUBackend::DIRECT3D11:
		return true;
	default:
		return false;
	}
}

static std::string PostShaderTranslateName(const char *value) {
	I18NCategory *ps = GetI18NCategory("PostShaders");
	const ShaderInfo *info = GetPostShaderInfo(value);
	if (info) {
		return ps->T(value, info ? info->name.c_str() : value);
	} else {
		return value;
	}
}

static std::string *GPUDeviceNameSetting() {
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		return &g_Config.sVulkanDevice;
	}
#ifdef _WIN32
	if (g_Config.iGPUBackend == (int)GPUBackend::DIRECT3D11) {
		return &g_Config.sD3D11Device;
	}
#endif
	return nullptr;
}

void GameSettingsScreen::CreateViews() {
	ReloadAllPostShaderInfo();

	if (editThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
		g_Config.loadGameConfig(gameID_, info->GetTitle());
	}

	iAlternateSpeedPercent1_ = g_Config.iFpsLimit1 < 0 ? -1 : (g_Config.iFpsLimit1 * 100) / 60;
	iAlternateSpeedPercent2_ = g_Config.iFpsLimit2 < 0 ? -1 : (g_Config.iFpsLimit2 * 100) / 60;

	bool vertical = UseVerticalLayout();

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *gr = GetI18NCategory("Graphics");
	I18NCategory *co = GetI18NCategory("Controls");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *sa = GetI18NCategory("Savedata");
	I18NCategory *sy = GetI18NCategory("System");
	I18NCategory *n = GetI18NCategory("Networking");
	I18NCategory *ms = GetI18NCategory("MainSettings");
	I18NCategory *dev = GetI18NCategory("Developer");
	I18NCategory *ri = GetI18NCategory("RemoteISO");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	TabHolder *tabHolder;
	if (vertical) {
		LinearLayout *verticalLayout = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		tabHolder = new TabHolder(ORIENT_HORIZONTAL, 200, new LinearLayoutParams(1.0f));
		verticalLayout->Add(tabHolder);
		verticalLayout->Add(new Choice(di->T("Back"), "", false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, Margins(0))))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		root_->Add(verticalLayout);
	} else {
		tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new AnchorLayoutParams(10, 0, 10, 0, false));
		root_->Add(tabHolder);
		AddStandardBack(root_);
	}
	tabHolder->SetTag("GameSettings");
	root_->SetDefaultFocusView(tabHolder);

	float leftSide = 40.0f;
	if (!vertical) {
		leftSide += 200.0f;
	}
	settingInfo_ = new SettingInfoMessage(ALIGN_CENTER | FLAG_WRAP_TEXT, new AnchorLayoutParams(dp_xres - leftSide - 40.0f, WRAP_CONTENT, leftSide, dp_yres - 80.0f - 40.0f, NONE, NONE));
	settingInfo_->SetBottomCutoff(dp_yres - 200.0f);
	root_->Add(settingInfo_);

	// TODO: These currently point to global settings, not game specific ones.

	// Graphics
	ViewGroup *graphicsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	graphicsSettingsScroll->SetTag("GameSettingsGraphics");
	LinearLayout *graphicsSettings = new LinearLayout(ORIENT_VERTICAL);
	graphicsSettings->SetSpacing(0);
	graphicsSettingsScroll->Add(graphicsSettings);
	tabHolder->AddTab(ms->T("Graphics"), graphicsSettingsScroll);

	graphicsSettings->Add(new ItemHeader(gr->T("Rendering Mode")));

#if !PPSSPP_PLATFORM(UWP)
	static const char *renderingBackend[] = { "OpenGL", "Direct3D 9", "Direct3D 11", "Vulkan" };
	PopupMultiChoice *renderingBackendChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iGPUBackend, gr->T("Backend"), renderingBackend, (int)GPUBackend::OPENGL, ARRAY_SIZE(renderingBackend), gr->GetName(), screenManager()));
	renderingBackendChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingBackend);

	if (!g_Config.IsBackendEnabled(GPUBackend::OPENGL))
		renderingBackendChoice->HideChoice((int)GPUBackend::OPENGL);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D9))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D9);
	if (!g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11))
		renderingBackendChoice->HideChoice((int)GPUBackend::DIRECT3D11);
	if (!g_Config.IsBackendEnabled(GPUBackend::VULKAN))
		renderingBackendChoice->HideChoice((int)GPUBackend::VULKAN);
#endif

	Draw::DrawContext *draw = screenManager()->getDrawContext();

	// Backends that don't allow a device choice will only expose one device.
	if (draw->GetDeviceList().size() > 1) {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting) {
			PopupMultiChoiceDynamic *deviceChoice = graphicsSettings->Add(new PopupMultiChoiceDynamic(deviceNameSetting, gr->T("Device"), draw->GetDeviceList(), nullptr, screenManager()));
			deviceChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingDevice);
		}
	}

	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering"};
	PopupMultiChoice *renderingModeChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gr->T("Mode"), renderingMode, 0, ARRAY_SIZE(renderingMode), gr->GetName(), screenManager()));
	renderingModeChoice->OnChoice.Add([=](EventParams &e) {
		switch (g_Config.iRenderingMode) {
		case FB_NON_BUFFERED_MODE:
			settingInfo_->Show(gr->T("RenderingMode NonBuffered Tip", "Faster, but graphics may be missing in some games"), e.v);
			break;
		case FB_BUFFERED_MODE:
			break;
		}
		return UI::EVENT_CONTINUE;
	});
	renderingModeChoice->OnChoice.Handle(this, &GameSettingsScreen::OnRenderingMode);
	renderingModeChoice->SetDisabledPtr(&g_Config.bSoftwareRendering);
	CheckBox *blockTransfer = graphicsSettings->Add(new CheckBox(&g_Config.bBlockTransferGPU, gr->T("Simulate Block Transfer", "Simulate Block Transfer")));
	blockTransfer->OnClick.Add([=](EventParams &e) {
		if (!g_Config.bBlockTransferGPU)
			settingInfo_->Show(gr->T("BlockTransfer Tip", "Some games require this to be On for correct graphics"), e.v);
		return UI::EVENT_CONTINUE;
	});
	blockTransfer->SetDisabledPtr(&g_Config.bSoftwareRendering);

	bool showSoftGPU = true;
#ifdef MOBILE_DEVICE
	// On Android, only show the software rendering setting if it's already enabled.
	// Can still be turned on through INI file editing.
	showSoftGPU = g_Config.bSoftwareRendering;
#endif
	if (showSoftGPU) {
		CheckBox *softwareGPU = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareRendering, gr->T("Software Rendering", "Software Rendering (slow)")));
		softwareGPU->OnClick.Add([=](EventParams &e) {
			if (g_Config.bSoftwareRendering)
				settingInfo_->Show(gr->T("SoftGPU Tip", "Currently VERY slow"), e.v);
			return UI::EVENT_CONTINUE;
		});
		softwareGPU->OnClick.Handle(this, &GameSettingsScreen::OnSoftwareRendering);
		if (PSP_IsInited())
			softwareGPU->SetEnabled(false);
	}

	graphicsSettings->Add(new ItemHeader(gr->T("Frame Rate Control")));
	static const char *frameSkip[] = {"Off", "1", "2", "3", "4", "5", "6", "7", "8"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkip, gr->T("Frame Skipping"), frameSkip, 0, ARRAY_SIZE(frameSkip), gr->GetName(), screenManager()));
	static const char *frameSkipType[] = {"Number of Frames", "Percent of FPS"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iFrameSkipType, gr->T("Frame Skipping Type"), frameSkipType, 0, ARRAY_SIZE(frameSkipType), gr->GetName(), screenManager()));
	frameSkipAuto_ = graphicsSettings->Add(new CheckBox(&g_Config.bAutoFrameSkip, gr->T("Auto FrameSkip")));
	frameSkipAuto_->OnClick.Handle(this, &GameSettingsScreen::OnAutoFrameskip);

	PopupSliderChoice *altSpeed1 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent1_, 0, 1000, gr->T("Alternative Speed", "Alternative speed"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed1->SetFormat("%i%%");
	altSpeed1->SetZeroLabel(gr->T("Unlimited"));
	altSpeed1->SetNegativeDisable(gr->T("Disabled"));

	PopupSliderChoice *altSpeed2 = graphicsSettings->Add(new PopupSliderChoice(&iAlternateSpeedPercent2_, 0, 1000, gr->T("Alternative Speed 2", "Alternative speed 2 (in %, 0 = unlimited)"), 5, screenManager(), gr->T("%, 0:unlimited")));
	altSpeed2->SetFormat("%i%%");
	altSpeed2->SetZeroLabel(gr->T("Unlimited"));
	altSpeed2->SetNegativeDisable(gr->T("Disabled"));

	graphicsSettings->Add(new ItemHeader(gr->T("Features")));
	// Hide postprocess option on unsupported backends to avoid confusion.
	if (GetGPUBackend() != GPUBackend::DIRECT3D9) {
		I18NCategory *ps = GetI18NCategory("PostShaders");
		postProcChoice_ = graphicsSettings->Add(new ChoiceWithValueDisplay(&g_Config.sPostShaderName, gr->T("Postprocessing Shader"), &PostShaderTranslateName));
		postProcChoice_->OnClick.Handle(this, &GameSettingsScreen::OnPostProcShader);
		postProcEnable_ = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
		postProcChoice_->SetEnabledPtr(&postProcEnable_);
	}

#if !defined(MOBILE_DEVICE)
	graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &GameSettingsScreen::OnFullscreenChange);
	if (System_GetPropertyInt(SYSPROP_DISPLAY_COUNT) > 1) {
		CheckBox *fullscreenMulti = new CheckBox(&g_Config.bFullScreenMulti, gr->T("Use all displays"));
		fullscreenMulti->SetEnabledPtr(&g_Config.bFullScreen);
		graphicsSettings->Add(fullscreenMulti)->OnClick.Handle(this, &GameSettingsScreen::OnFullscreenChange);
	}
#endif
	// Display Layout Editor: To avoid overlapping touch controls on large tablets, meet geeky demands for integer zoom/unstretched image etc.
	displayEditor_ = graphicsSettings->Add(new Choice(gr->T("Display layout editor")));
	displayEditor_->OnClick.Handle(this, &GameSettingsScreen::OnDisplayLayoutEditor);

#ifdef __ANDROID__
	// Hide Immersive Mode on pre-kitkat Android
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 19) {
		// Let's reuse the Fullscreen translation string from desktop.
		graphicsSettings->Add(new CheckBox(&g_Config.bImmersiveMode, gr->T("FullScreen", "Full Screen")))->OnClick.Handle(this, &GameSettingsScreen::OnImmersiveModeChange);
	}
#endif

	graphicsSettings->Add(new ItemHeader(gr->T("Performance")));
#ifndef MOBILE_DEVICE
	static const char *internalResolutions[] = {"Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP", "6x PSP", "7x PSP", "8x PSP", "9x PSP", "10x PSP" };
#else
	static const char *internalResolutions[] = {"Auto (1:1)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
#endif
	resolutionChoice_ = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iInternalResolution, gr->T("Rendering Resolution"), internalResolutions, 0, ARRAY_SIZE(internalResolutions), gr->GetName(), screenManager()));
	resolutionChoice_->OnChoice.Handle(this, &GameSettingsScreen::OnResolutionChange);
	resolutionEnable_ = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionChoice_->SetEnabledPtr(&resolutionEnable_);

#ifdef __ANDROID__
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_TV) {
		static const char *deviceResolutions[] = { "Native device resolution", "Auto (same as Rendering)", "1x PSP", "2x PSP", "3x PSP", "4x PSP", "5x PSP" };
		int max_res_temp = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 2;
		if (max_res_temp == 3)
			max_res_temp = 4;  // At least allow 2x
		int max_res = std::min(max_res_temp, (int)ARRAY_SIZE(deviceResolutions));
		UI::PopupMultiChoice *hwscale = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAndroidHwScale, gr->T("Display Resolution (HW scaler)"), deviceResolutions, 0, max_res, gr->GetName(), screenManager()));
		hwscale->OnChoice.Handle(this, &GameSettingsScreen::OnHwScaleChange);  // To refresh the display mode
	}
#endif

#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gr->T("VSync")));
#endif

	CheckBox *hwTransform = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gr->T("Hardware Transform")));
	hwTransform->OnClick.Handle(this, &GameSettingsScreen::OnHardwareTransform);
	hwTransform->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *swSkin = graphicsSettings->Add(new CheckBox(&g_Config.bSoftwareSkinning, gr->T("Software Skinning")));
	swSkin->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("SoftwareSkinning Tip", "Combine skinned model draws on the CPU, faster in most games"), e.v);
		return UI::EVENT_CONTINUE;
	});
	swSkin->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *vtxCache = graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gr->T("Vertex Cache")));
	vtxCache->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("VertexCache Tip", "Faster, but may cause temporary flicker"), e.v);
		return UI::EVENT_CONTINUE;
	});
	vtxCacheEnable_ = !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	vtxCache->SetEnabledPtr(&vtxCacheEnable_);

	CheckBox *texBackoff = graphicsSettings->Add(new CheckBox(&g_Config.bTextureBackoffCache, gr->T("Lazy texture caching", "Lazy texture caching (speedup)")));
	texBackoff->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *texSecondary_ = graphicsSettings->Add(new CheckBox(&g_Config.bTextureSecondaryCache, gr->T("Retain changed textures", "Retain changed textures (speedup, mem hog)")));
	texSecondary_->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("RetainChangedTextures Tip", "Makes many games slower, but some games a lot faster"), e.v);
		return UI::EVENT_CONTINUE;
	});
	texSecondary_->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *framebufferSlowEffects = graphicsSettings->Add(new CheckBox(&g_Config.bDisableSlowFramebufEffects, gr->T("Disable slower effects (speedup)")));
	framebufferSlowEffects->SetDisabledPtr(&g_Config.bSoftwareRendering);

	// Seems solid, so we hide the setting.
	/*CheckBox *vtxJit = graphicsSettings->Add(new CheckBox(&g_Config.bVertexDecoderJit, gr->T("Vertex Decoder JIT")));

	if (PSP_IsInited()) {
		vtxJit->SetEnabled(false);
	}*/

	static const char *quality[] = { "Low", "Medium", "High"};
	PopupMultiChoice *beziersChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iSplineBezierQuality, gr->T("LowCurves", "Spline/Bezier curves quality"), quality, 0, ARRAY_SIZE(quality), gr->GetName(), screenManager()));
	beziersChoice->OnChoice.Add([=](EventParams &e) {
		if (g_Config.iSplineBezierQuality != 0) {
			settingInfo_->Show(gr->T("LowCurves Tip", "Only used by some games, controls smoothness of curves"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});

	CheckBox *tessellationHW = graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTessellation, gr->T("Hardware Tessellation")));
	tessellationHW->OnClick.Add([=](EventParams &e) {
		settingInfo_->Show(gr->T("HardwareTessellation Tip", "Uses hardware to make curves"), e.v);
		return UI::EVENT_CONTINUE;
	});
	tessHWEnable_ = DoesBackendSupportHWTess() && !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	tessellationHW->SetEnabledPtr(&tessHWEnable_);

	// In case we're going to add few other antialiasing option like MSAA in the future.
	// graphicsSettings->Add(new CheckBox(&g_Config.bFXAA, gr->T("FXAA")));
	graphicsSettings->Add(new ItemHeader(gr->T("Texture Scaling")));
#ifndef MOBILE_DEVICE
	static const char *texScaleLevels[] = {"Auto", "Off", "2x", "3x", "4x", "5x"};
#else
	static const char *texScaleLevels[] = {"Auto", "Off", "2x", "3x"};
#endif

	PopupMultiChoice *texScalingChoice = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gr->T("Upscale Level"), texScaleLevels, 0, ARRAY_SIZE(texScaleLevels), gr->GetName(), screenManager()));
	// TODO: Better check?  When it won't work, it scales down anyway.
	if (!gl_extensions.OES_texture_npot && GetGPUBackend() == GPUBackend::OPENGL) {
		texScalingChoice->HideChoice(3); // 3x
		texScalingChoice->HideChoice(5); // 5x
	}
	texScalingChoice->OnChoice.Add([=](EventParams &e) {
		if (g_Config.iTexScalingLevel != 1) {
			settingInfo_->Show(gr->T("UpscaleLevel Tip", "CPU heavy - some scaling may be delayed to avoid stutter"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});
	texScalingChoice->SetDisabledPtr(&g_Config.bSoftwareRendering);

	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	PopupMultiChoice *texScalingType = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gr->T("Upscale Type"), texScaleAlgos, 0, ARRAY_SIZE(texScaleAlgos), gr->GetName(), screenManager()));
	texScalingType->SetDisabledPtr(&g_Config.bSoftwareRendering);

	CheckBox *deposterize = graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gr->T("Deposterize")));
	deposterize->OnClick.Add([=](EventParams &e) {
		if (g_Config.bTexDeposterize == true) {
			settingInfo_->Show(gr->T("Deposterize Tip", "Fixes visual banding glitches in upscaled textures"), e.v);
		}
		return UI::EVENT_CONTINUE;
	});
	deposterize->SetDisabledPtr(&g_Config.bSoftwareRendering);

	graphicsSettings->Add(new ItemHeader(gr->T("Texture Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	PopupMultiChoice *anisoFiltering = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gr->T("Anisotropic Filtering"), anisoLevels, 0, ARRAY_SIZE(anisoLevels), gr->GetName(), screenManager()));
	anisoFiltering->SetDisabledPtr(&g_Config.bSoftwareRendering);

	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Linear on FMV", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gr->T("Texture Filter"), texFilters, 1, ARRAY_SIZE(texFilters), gr->GetName(), screenManager()));

	static const char *bufFilters[] = { "Linear", "Nearest", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iBufFilter, gr->T("Screen Scaling Filter"), bufFilters, 1, ARRAY_SIZE(bufFilters), gr->GetName(), screenManager()));

#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(IOS)
	graphicsSettings->Add(new ItemHeader(gr->T("Cardboard VR Settings", "Cardboard VR Settings")));
	CheckBox *cardboardMode = graphicsSettings->Add(new CheckBox(&g_Config.bEnableCardboardVR, gr->T("Enable Cardboard VR", "Enable Cardboard VR")));
	cardboardMode->SetDisabledPtr(&g_Config.bSoftwareRendering);
	PopupSliderChoice * cardboardScreenSize = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardScreenSize, 30, 100, gr->T("Cardboard Screen Size", "Screen Size (in % of the viewport)"), 1, screenManager(), gr->T("% of viewport")));
	cardboardScreenSize->SetDisabledPtr(&g_Config.bSoftwareRendering);
	PopupSliderChoice *cardboardXShift = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardXShift, -100, 100, gr->T("Cardboard Screen X Shift", "X Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
	cardboardXShift->SetDisabledPtr(&g_Config.bSoftwareRendering);
	PopupSliderChoice *cardboardYShift = graphicsSettings->Add(new PopupSliderChoice(&g_Config.iCardboardYShift, -100, 100, gr->T("Cardboard Screen Y Shift", "Y Shift (in % of the void)"), 1, screenManager(), gr->T("% of the void")));
	cardboardYShift->SetDisabledPtr(&g_Config.bSoftwareRendering);
#endif

	graphicsSettings->Add(new ItemHeader(gr->T("Hack Settings", "Hack Settings (these WILL cause glitches)")));

	static const char *bloomHackOptions[] = { "Off", "Safe", "Balanced", "Aggressive" };
	PopupMultiChoice *bloomHack = graphicsSettings->Add(new PopupMultiChoice(&g_Config.iBloomHack, gr->T("Lower resolution for effects (reduces artifacts)"), bloomHackOptions, 0, ARRAY_SIZE(bloomHackOptions), gr->GetName(), screenManager()));
	bloomHackEnable_ = !g_Config.bSoftwareRendering && (g_Config.iInternalResolution != 1);
	bloomHack->SetEnabledPtr(&bloomHackEnable_);

	graphicsSettings->Add(new ItemHeader(gr->T("Overlay Information")));
	static const char *fpsChoices[] = { "None", "Speed", "FPS", "Both" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gr->T("Show FPS Counter"), fpsChoices, 0, ARRAY_SIZE(fpsChoices), gr->GetName(), screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bShowDebugStats, gr->T("Show Debug Statistics")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);

	// Developer tools are not accessible ingame, so it goes here.
	graphicsSettings->Add(new ItemHeader(gr->T("Debugging")));
	Choice *dump = graphicsSettings->Add(new Choice(gr->T("Dump next frame to log")));
	dump->OnClick.Handle(this, &GameSettingsScreen::OnDumpNextFrameToLog);
	if (!PSP_IsInited())
		dump->SetEnabled(false);

	// Audio
	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	audioSettingsScroll->SetTag("GameSettingsAudio");
	LinearLayout *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettings->SetSpacing(0);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab(ms->T("Audio"), audioSettingsScroll);

	audioSettings->Add(new ItemHeader(ms->T("Audio")));

	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));

	PopupSliderChoice *volume = audioSettings->Add(new PopupSliderChoice(&g_Config.iGlobalVolume, VOLUME_OFF, VOLUME_MAX, a->T("Global volume"), screenManager()));
	volume->SetEnabledPtr(&g_Config.bEnableSound);
	volume->SetZeroLabel(a->T("Mute"));

	PopupSliderChoice *altVolume = audioSettings->Add(new PopupSliderChoice(&g_Config.iAltSpeedVolume, VOLUME_OFF, VOLUME_MAX, a->T("Alternate speed volume"), screenManager()));
	altVolume->SetEnabledPtr(&g_Config.bEnableSound);
	altVolume->SetZeroLabel(a->T("Mute"));
	altVolume->SetNegativeDisable(a->T("Use global volume"));

#ifdef _WIN32
	if (IsVistaOrHigher()) {
		static const char *backend[] = { "Auto", "DSound (compatible)", "WASAPI (fast)" };
		PopupMultiChoice *audioBackend = audioSettings->Add(new PopupMultiChoice(&g_Config.iAudioBackend, a->T("Audio backend", "Audio backend (restart req.)"), backend, 0, ARRAY_SIZE(backend), a->GetName(), screenManager()));
		audioBackend->SetEnabledPtr(&g_Config.bEnableSound);
	}
#endif

#if defined(SDL)
	std::vector<std::string> audioDeviceList;
	SplitString(System_GetProperty(SYSPROP_AUDIO_DEVICE_LIST), '\0', audioDeviceList);
	audioDeviceList.insert(audioDeviceList.begin(), a->T("Auto"));
	PopupMultiChoiceDynamic *audioDevice = audioSettings->Add(new PopupMultiChoiceDynamic(&g_Config.sAudioDevice, a->T("Device"), audioDeviceList, nullptr, screenManager()));
	audioDevice->OnChoice.Handle(this, &GameSettingsScreen::OnAudioDevice);

	audioSettings->Add(new CheckBox(&g_Config.bAutoAudioDevice, a->T("Switch on new audio device")));
#endif

	static const char *latency[] = { "Low", "Medium", "High" };
	PopupMultiChoice *lowAudio = audioSettings->Add(new PopupMultiChoice(&g_Config.iAudioLatency, a->T("Audio Latency"), latency, 0, ARRAY_SIZE(latency), gr->GetName(), screenManager()));
	lowAudio->SetEnabledPtr(&g_Config.bEnableSound);
#if defined(__ANDROID__)
	CheckBox *extraAudio = audioSettings->Add(new CheckBox(&g_Config.bExtraAudioBuffering, a->T("AudioBufferingForBluetooth", "Bluetooth-friendly buffer (slower)")));
	extraAudio->SetEnabledPtr(&g_Config.bEnableSound);
#endif
	if (System_GetPropertyInt(SYSPROP_AUDIO_SAMPLE_RATE) == 44100) {
		CheckBox *resampling = audioSettings->Add(new CheckBox(&g_Config.bAudioResampler, a->T("Audio sync", "Audio sync (resampling)")));
		resampling->SetEnabledPtr(&g_Config.bEnableSound);
	}

	// Control
	ViewGroup *controlsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	controlsSettingsScroll->SetTag("GameSettingsControls");
	LinearLayout *controlsSettings = new LinearLayout(ORIENT_VERTICAL);
	controlsSettings->SetSpacing(0);
	controlsSettingsScroll->Add(controlsSettings);
	tabHolder->AddTab(ms->T("Controls"), controlsSettingsScroll);
	controlsSettings->Add(new ItemHeader(ms->T("Controls")));
	controlsSettings->Add(new Choice(co->T("Control Mapping")))->OnClick.Handle(this, &GameSettingsScreen::OnControlMapping);

#if defined(USING_WIN_UI)
	controlsSettings->Add(new CheckBox(&g_Config.bGamepadOnlyFocused, co->T("Ignore gamepads when not focused")));
#endif

#if defined(MOBILE_DEVICE)
	controlsSettings->Add(new CheckBox(&g_Config.bHapticFeedback, co->T("HapticFeedback", "Haptic Feedback (vibration)")));
	static const char *tiltTypes[] = { "None (Disabled)", "Analog Stick", "D-PAD", "PSP Action Buttons", "L/R Trigger Buttons"};
	controlsSettings->Add(new PopupMultiChoice(&g_Config.iTiltInputType, co->T("Tilt Input Type"), tiltTypes, 0, ARRAY_SIZE(tiltTypes), co->GetName(), screenManager()))->OnClick.Handle(this, &GameSettingsScreen::OnTiltTypeChange);

	Choice *customizeTilt = controlsSettings->Add(new Choice(co->T("Customize tilt")));
	customizeTilt->OnClick.Handle(this, &GameSettingsScreen::OnTiltCustomize);
	customizeTilt->SetEnabledPtr((bool *)&g_Config.iTiltInputType); //<- dirty int-to-bool cast
#endif

	// TVs don't have touch control, at least not yet.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_TV) {
		controlsSettings->Add(new ItemHeader(co->T("OnScreen", "On-Screen Touch Controls")));
		controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, co->T("OnScreen", "On-Screen Touch Controls")));
		layoutEditorChoice_ = controlsSettings->Add(new Choice(co->T("Custom layout...")));
		layoutEditorChoice_->OnClick.Handle(this, &GameSettingsScreen::OnTouchControlLayout);
		layoutEditorChoice_->SetEnabledPtr(&g_Config.bShowTouchControls);

		// Re-centers itself to the touch location on touch-down.
		CheckBox *floatingAnalog = controlsSettings->Add(new CheckBox(&g_Config.bAutoCenterTouchAnalog, co->T("Auto-centering analog stick")));
		floatingAnalog->SetEnabledPtr(&g_Config.bShowTouchControls);

		// Combo key setup
		Choice *comboKey = controlsSettings->Add(new Choice(co->T("Combo Key Setup")));
		comboKey->OnClick.Handle(this, &GameSettingsScreen::OnComboKey);
		comboKey->SetEnabledPtr(&g_Config.bShowTouchControls);

		// On non iOS systems, offer to let the user see this button.
		// Some Windows touch devices don't have a back button or other button to call up the menu.
		if (System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON)) {
			CheckBox *enablePauseBtn = controlsSettings->Add(new CheckBox(&g_Config.bShowTouchPause, co->T("Show Touch Pause Menu Button")));

			// Don't allow the user to disable it once in-game, so they can't lock themselves out of the menu.
			if (!PSP_IsInited()) {
				enablePauseBtn->SetEnabledPtr(&g_Config.bShowTouchControls);
			} else {
				enablePauseBtn->SetEnabled(false);
			}
		}

		CheckBox *disableDiags = controlsSettings->Add(new CheckBox(&g_Config.bDisableDpadDiagonals, co->T("Disable D-Pad diagonals (4-way touch)")));
		disableDiags->SetEnabledPtr(&g_Config.bShowTouchControls);
		PopupSliderChoice *opacity = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 0, 100, co->T("Button Opacity"), screenManager(), "%"));
		opacity->SetEnabledPtr(&g_Config.bShowTouchControls);
		opacity->SetFormat("%i%%");
		PopupSliderChoice *autoHide = controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonHideSeconds, 0, 300, co->T("Auto-hide buttons after seconds"), screenManager(), co->T("seconds, 0 : off")));
		autoHide->SetEnabledPtr(&g_Config.bShowTouchControls);
		autoHide->SetFormat("%is");
		autoHide->SetZeroLabel(co->T("Off"));
		static const char *touchControlStyles[] = {"Classic", "Thin borders", "Glowing borders"};
		View *style = controlsSettings->Add(new PopupMultiChoice(&g_Config.iTouchButtonStyle, co->T("Button style"), touchControlStyles, 0, ARRAY_SIZE(touchControlStyles), co->GetName(), screenManager()));
		style->SetEnabledPtr(&g_Config.bShowTouchControls);
	}

#ifdef _WIN32
	static const char *inverseDeadzoneModes[] = { "Off", "X", "Y", "X + Y" };

	controlsSettings->Add(new ItemHeader(co->T("DInput Analog Settings", "DInput Analog Settings")));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fDInputAnalogDeadzone, 0.0f, 1.0f, co->T("Deadzone Radius"), 0.01f, screenManager(), "/ 1.0"));
	controlsSettings->Add(new PopupMultiChoice(&g_Config.iDInputAnalogInverseMode, co->T("Analog Mapper Mode"), inverseDeadzoneModes, 0, ARRAY_SIZE(inverseDeadzoneModes), co->GetName(), screenManager()));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fDInputAnalogInverseDeadzone, 0.0f, 1.0f, co->T("Analog Mapper Low End", "Analog Mapper Low End (Inverse Deadzone)"), 0.01f, screenManager(), "/ 1.0"));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fDInputAnalogSensitivity, 0.0f, 10.0f, co->T("Analog Mapper High End", "Analog Mapper High End (Axis Sensitivity)"), 0.01f, screenManager(), "x"));

	controlsSettings->Add(new ItemHeader(co->T("XInput Analog Settings", "XInput Analog Settings")));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fXInputAnalogDeadzone, 0.0f, 1.0f, co->T("Deadzone Radius"), 0.01f, screenManager(), "/ 1.0"));
	controlsSettings->Add(new PopupMultiChoice(&g_Config.iXInputAnalogInverseMode, co->T("Analog Mapper Mode"), inverseDeadzoneModes, 0, ARRAY_SIZE(inverseDeadzoneModes), co->GetName(), screenManager()));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fXInputAnalogInverseDeadzone, 0.0f, 1.0f, co->T("Analog Mapper Low End", "Analog Mapper Low End (Inverse Deadzone)"), 0.01f, screenManager(), "/ 1.0"));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fXInputAnalogSensitivity, 0.0f, 10.0f, co->T("Analog Mapper High End", "Analog Mapper High End (Axis Sensitivity)"), 0.01f, screenManager(), "x"));
#else
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fXInputAnalogSensitivity, 0.0f, 10.0f, co->T("Analog Axis Sensitivity", "Analog Axis Sensitivity"), 0.01f, screenManager(), "x"));
#endif

	controlsSettings->Add(new ItemHeader(co->T("Keyboard", "Keyboard Control Settings")));
#if defined(USING_WIN_UI)
	controlsSettings->Add(new CheckBox(&g_Config.bIgnoreWindowsKey, co->T("Ignore Windows Key")));
#endif // #if defined(USING_WIN_UI)
	auto analogLimiter = new PopupSliderChoiceFloat(&g_Config.fAnalogLimiterDeadzone, 0.0f, 1.0f, co->T("Analog Limiter"), 0.10f, screenManager(), "/ 1.0");
	controlsSettings->Add(analogLimiter);
	analogLimiter->OnChange.Add([=](EventParams &e) {
		settingInfo_->Show(co->T("AnalogLimiter Tip", "When the analog limiter button is pressed"), e.v);
		return UI::EVENT_CONTINUE;
	});
#if defined(USING_WIN_UI)
	controlsSettings->Add(new ItemHeader(co->T("Mouse", "Mouse settings")));
	CheckBox *mouseControl = controlsSettings->Add(new CheckBox(&g_Config.bMouseControl, co->T("Use Mouse Control")));
	mouseControl->OnClick.Add([=](EventParams &e) {
		if(g_Config.bMouseControl)
			settingInfo_->Show(co->T("MouseControl Tip", "You can now map mouse in control mapping screen by pressing the 'M' icon."), e.v);
		return UI::EVENT_CONTINUE;
	});
	controlsSettings->Add(new CheckBox(&g_Config.bMouseConfine, co->T("Confine Mouse", "Trap mouse within window/display area")))->SetEnabledPtr(&g_Config.bMouseControl);
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fMouseSensitivity, 0.01f, 1.0f, co->T("Mouse sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bMouseControl);
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fMouseSmoothing, 0.0f, 0.95f, co->T("Mouse smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&g_Config.bMouseControl);
#endif

	ViewGroup *networkingSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	networkingSettingsScroll->SetTag("GameSettingsNetworking");
	LinearLayout *networkingSettings = new LinearLayout(ORIENT_VERTICAL);
	networkingSettings->SetSpacing(0);
	networkingSettingsScroll->Add(networkingSettings);
	tabHolder->AddTab(ms->T("Networking"), networkingSettingsScroll);

	networkingSettings->Add(new ItemHeader(ms->T("Networking")));

	networkingSettings->Add(new Choice(n->T("Adhoc Multiplayer forum")))->OnClick.Handle(this, &GameSettingsScreen::OnAdhocGuides);

	networkingSettings->Add(new CheckBox(&g_Config.bEnableWlan, n->T("Enable networking", "Enable networking/wlan (beta)")));
	networkingSettings->Add(new CheckBox(&g_Config.bDiscordPresence, n->T("Send Discord Presence information")));

	networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.proAdhocServer, n->T("Change proAdhocServer Address"), (const char *)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeproAdhocServerAddress);
	networkingSettings->Add(new CheckBox(&g_Config.bEnableAdhocServer, n->T("Enable built-in PRO Adhoc Server", "Enable built-in PRO Adhoc Server")));
	networkingSettings->Add(new ChoiceWithValueDisplay(&g_Config.sMACAddress, n->T("Change Mac Address"), (const char *)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeMacAddress);
	networkingSettings->Add(new PopupSliderChoice(&g_Config.iPortOffset, 0, 60000, n->T("Port offset", "Port offset(0 = PSP compatibility)"), 100, screenManager()));

	ViewGroup *toolsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	toolsScroll->SetTag("GameSettingsTools");
	LinearLayout *tools = new LinearLayout(ORIENT_VERTICAL);
	tools->SetSpacing(0);
	toolsScroll->Add(tools);
	tabHolder->AddTab(ms->T("Tools"), toolsScroll);

	tools->Add(new ItemHeader(ms->T("Tools")));
	// These were moved here so use the wrong translation objects, to avoid having to change all inis... This isn't a sustainable situation :P
	tools->Add(new Choice(sa->T("Savedata Manager")))->OnClick.Handle(this, &GameSettingsScreen::OnSavedataManager);
	tools->Add(new Choice(dev->T("System Information")))->OnClick.Handle(this, &GameSettingsScreen::OnSysInfo);
	tools->Add(new Choice(sy->T("Developer Tools")))->OnClick.Handle(this, &GameSettingsScreen::OnDeveloperTools);
	tools->Add(new Choice(ri->T("Remote disc streaming")))->OnClick.Handle(this, &GameSettingsScreen::OnRemoteISO);

	// System
	ViewGroup *systemSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	systemSettingsScroll->SetTag("GameSettingsSystem");
	LinearLayout *systemSettings = new LinearLayout(ORIENT_VERTICAL);
	systemSettings->SetSpacing(0);
	systemSettingsScroll->Add(systemSettings);
	tabHolder->AddTab(ms->T("System"), systemSettingsScroll);

	systemSettings->Add(new ItemHeader(sy->T("UI Language")));
	systemSettings->Add(new Choice(dev->T("Language", "Language")))->OnClick.Handle(this, &GameSettingsScreen::OnLanguage);

	systemSettings->Add(new ItemHeader(sy->T("Help the PPSSPP team")));
	enableReports_ = Reporting::IsEnabled();
	enableReportsCheckbox_ = new CheckBox(&enableReports_, sy->T("Enable Compatibility Server Reports"));
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());
	systemSettings->Add(enableReportsCheckbox_);

	systemSettings->Add(new ItemHeader(sy->T("Emulation")));

	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, sy->T("Fast Memory", "Fast Memory (Unstable)")))->OnClick.Handle(this, &GameSettingsScreen::OnJitAffectingSetting);

	static const char *ioTimingMethods[] = { "Fast (lag on slow storage)", "Host (bugs, less lag)", "Simulate UMD delays" };
	View *ioTimingMethod = systemSettings->Add(new PopupMultiChoice(&g_Config.iIOTimingMethod, sy->T("IO timing method"), ioTimingMethods, 0, ARRAY_SIZE(ioTimingMethods), sy->GetName(), screenManager()));
	systemSettings->Add(new CheckBox(&g_Config.bForceLagSync, sy->T("Force real clock sync (slower, less lag)")));
	PopupSliderChoice *lockedMhz = systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, sy->T("Change CPU Clock", "Change CPU Clock (unstable)"), screenManager(), sy->T("MHz, 0:default")));
	lockedMhz->SetZeroLabel(sy->T("Auto"));
	PopupSliderChoice *rewindFreq = systemSettings->Add(new PopupSliderChoice(&g_Config.iRewindFlipFrequency, 0, 1800, sy->T("Rewind Snapshot Frequency", "Rewind Snapshot Frequency (mem hog)"), screenManager(), sy->T("frames, 0:off")));
	rewindFreq->SetZeroLabel(sy->T("Off"));

	systemSettings->Add(new CheckBox(&g_Config.bMemStickInserted, sy->T("Memory Stick inserted")));

	systemSettings->Add(new ItemHeader(sy->T("General")));

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_MOBILE) {
		static const char *screenRotation[] = {"Auto", "Landscape", "Portrait", "Landscape Reversed", "Portrait Reversed", "Landscape Auto"};
		PopupMultiChoice *rot = systemSettings->Add(new PopupMultiChoice(&g_Config.iScreenRotation, co->T("Screen Rotation"), screenRotation, 0, ARRAY_SIZE(screenRotation), co->GetName(), screenManager()));
		rot->OnChoice.Handle(this, &GameSettingsScreen::OnScreenRotation);

		if (System_GetPropertyBool(SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE)) {
			systemSettings->Add(new CheckBox(&g_Config.bSustainedPerformanceMode, sy->T("Sustained performance mode")))->OnClick.Handle(this, &GameSettingsScreen::OnSustainedPerformanceModeChange);
		}
	}
#endif

	systemSettings->Add(new CheckBox(&g_Config.bCheckForNewVersion, sy->T("VersionCheck", "Check for new versions of PPSSPP")));
	if (g_Config.iMaxRecent > 0)
		systemSettings->Add(new Choice(sy->T("Clear Recent Games List")))->OnClick.Handle(this, &GameSettingsScreen::OnClearRecents);

	const std::string bgPng = GetSysDirectory(DIRECTORY_SYSTEM) + "background.png";
	const std::string bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) + "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		backgroundChoice_ = systemSettings->Add(new Choice(sy->T("Clear UI background")));
	} else if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER)) {
		backgroundChoice_ = systemSettings->Add(new Choice(sy->T("Set UI background...")));
	} else {
		backgroundChoice_ = nullptr;
	}
	if (backgroundChoice_ != nullptr) {
		backgroundChoice_->OnClick.Handle(this, &GameSettingsScreen::OnChangeBackground);
	}

	systemSettings->Add(new Choice(sy->T("Restore Default Settings")))->OnClick.Handle(this, &GameSettingsScreen::OnRestoreDefaultSettings);
	systemSettings->Add(new CheckBox(&g_Config.bEnableStateUndo, sy->T("Savestate slot backups")));
	static const char *autoLoadSaveStateChoices[] = { "Off", "Oldest Save", "Newest Save", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iAutoLoadSaveState, sy->T("Auto Load Savestate"), autoLoadSaveStateChoices, 0, ARRAY_SIZE(autoLoadSaveStateChoices), sy->GetName(), screenManager()));
#if defined(USING_WIN_UI)
	systemSettings->Add(new CheckBox(&g_Config.bBypassOSKWithKeyboard, sy->T("Enable Windows native keyboard", "Enable Windows native keyboard")));
#endif
#if PPSSPP_PLATFORM(ANDROID)
	auto memstickPath = systemSettings->Add(new ChoiceWithValueDisplay(&g_Config.memStickDirectory, sy->T("Change Memory Stick folder"), (const char *)nullptr));
	memstickPath->SetEnabled(!PSP_IsInited());
	memstickPath->OnClick.Handle(this, &GameSettingsScreen::OnChangeMemStickDir);
#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	SavePathInMyDocumentChoice = systemSettings->Add(new CheckBox(&installed_, sy->T("Save path in My Documents", "Save path in My Documents")));
	SavePathInMyDocumentChoice->OnClick.Handle(this, &GameSettingsScreen::OnSavePathMydoc);
	SavePathInOtherChoice = systemSettings->Add(new CheckBox(&otherinstalled_, sy->T("Save path in installed.txt", "Save path in installed.txt")));
	SavePathInOtherChoice->SetEnabled(false);
	SavePathInOtherChoice->OnClick.Handle(this, &GameSettingsScreen::OnSavePathOther);
	wchar_t myDocumentsPath[MAX_PATH];
	const HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
	const std::string PPSSPPpath = File::GetExeDirectory();
	const std::string installedFile = PPSSPPpath + "installed.txt";
	installed_ = File::Exists(installedFile);
	otherinstalled_ = false;
	if (!installed_ && result == S_OK) {
		if (File::CreateEmptyFile(PPSSPPpath + "installedTEMP.txt")) {
			// Disable the setting whether cannot create & delete file
			if (!(File::Delete(PPSSPPpath + "installedTEMP.txt")))
				SavePathInMyDocumentChoice->SetEnabled(false);
			else
				SavePathInOtherChoice->SetEnabled(true);
		} else
			SavePathInMyDocumentChoice->SetEnabled(false);
	} else {
		if (installed_ && (result == S_OK)) {
#ifdef _MSC_VER
			std::ifstream inputFile(ConvertUTF8ToWString(installedFile));
#else
			std::ifstream inputFile(installedFile);
#endif
			if (!inputFile.fail() && inputFile.is_open()) {
				std::string tempString;
				std::getline(inputFile, tempString);

				// Skip UTF-8 encoding bytes if there are any. There are 3 of them.
				if (tempString.substr(0, 3) == "\xEF\xBB\xBF")
					tempString = tempString.substr(3);
				SavePathInOtherChoice->SetEnabled(true);
				if (!(tempString == "")) {
					installed_ = false;
					otherinstalled_ = true;
				}
			}
			inputFile.close();
		} else if (result != S_OK)
			SavePathInMyDocumentChoice->SetEnabled(false);
	}
#endif

#if defined(_M_X64)
	systemSettings->Add(new CheckBox(&g_Config.bCacheFullIsoInRam, sy->T("Cache ISO in RAM", "Cache full ISO in RAM")));
#endif

//#ifndef __ANDROID__
	systemSettings->Add(new ItemHeader(sy->T("Cheats", "Cheats (experimental, see forums)")));
	systemSettings->Add(new CheckBox(&g_Config.bEnableCheats, sy->T("Enable Cheats")));
//#endif
	systemSettings->SetSpacing(0);

	systemSettings->Add(new ItemHeader(sy->T("PSP Settings")));
	static const char *models[] = {"PSP-1000", "PSP-2000/3000"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iPSPModel, sy->T("PSP Model"), models, 0, ARRAY_SIZE(models), sy->GetName(), screenManager()))->SetEnabled(!PSP_IsInited());
	// TODO: Come up with a way to display a keyboard for mobile users,
	// so until then, this is Windows/Desktop only.
#if !defined(MOBILE_DEVICE)  // TODO: Add all platforms where KEY_CHAR support is added
	systemSettings->Add(new PopupTextInputChoice(&g_Config.sNickName, sy->T("Change Nickname"), "", 32, screenManager()));
#elif defined(__ANDROID__)
	systemSettings->Add(new ChoiceWithValueDisplay(&g_Config.sNickName, sy->T("Change Nickname"), (const char *)nullptr))->OnClick.Handle(this, &GameSettingsScreen::OnChangeNickname);
#endif

	systemSettings->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, sy->T("Screenshots as PNG")));

#if defined(_WIN32) || (defined(USING_QT_UI) && !defined(MOBILE_DEVICE))
	systemSettings->Add(new CheckBox(&g_Config.bDumpFrames, sy->T("Record Display")));
	systemSettings->Add(new CheckBox(&g_Config.bUseFFV1, sy->T("Use Lossless Video Codec (FFV1)")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpVideoOutput, sy->T("Use output buffer (with overlay) for recording")));
	systemSettings->Add(new CheckBox(&g_Config.bDumpAudio, sy->T("Record Audio")));
	systemSettings->Add(new CheckBox(&g_Config.bSaveLoadResetsAVdumping, sy->T("Reset Recording on Save/Load State")));
#endif
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, sy->T("Day Light Saving")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, sy->T("Date Format"), dateFormat, 1, 3, sy->GetName(), screenManager()));
	static const char *timeFormat[] = { "12HR", "24HR"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, sy->T("Time Format"), timeFormat, 1, 2, sy->GetName(), screenManager()));
	static const char *buttonPref[] = { "Use O to confirm", "Use X to confirm" };
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, sy->T("Confirmation Button"), buttonPref, 0, 2, sy->GetName(), screenManager()));
}

UI::EventReturn GameSettingsScreen::OnAutoFrameskip(UI::EventParams &e) {
	if (g_Config.bAutoFrameSkip && g_Config.iFrameSkip == 0) {
		g_Config.iFrameSkip = 1;
	}
	if (g_Config.bAutoFrameSkip && g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		g_Config.iRenderingMode = FB_BUFFERED_MODE;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSoftwareRendering(UI::EventParams &e) {
	vtxCacheEnable_ = !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	postProcEnable_ = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionEnable_ = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	bloomHackEnable_ = !g_Config.bSoftwareRendering && (g_Config.iInternalResolution != 1);
	tessHWEnable_ = DoesBackendSupportHWTess() && !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnHardwareTransform(UI::EventParams &e) {
	vtxCacheEnable_ = !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	tessHWEnable_ = DoesBackendSupportHWTess() && !g_Config.bSoftwareRendering && g_Config.bHardwareTransform;
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnScreenRotation(UI::EventParams &e) {
	ILOG("New display rotation: %d", g_Config.iScreenRotation);
	ILOG("Sending rotate");
	System_SendMessage("rotate", "");
	ILOG("Got back from rotate");
	return UI::EVENT_DONE;
}

void RecreateActivity() {
	const int SYSTEM_JELLYBEAN = 16;
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= SYSTEM_JELLYBEAN) {
		ILOG("Sending recreate");
		System_SendMessage("recreate", "");
		ILOG("Got back from recreate");
	} else {
		I18NCategory *gr = GetI18NCategory("Graphics");
		System_SendMessage("toast", gr->T("Must Restart", "You must restart PPSSPP for this change to take effect"));
	}
}

UI::EventReturn GameSettingsScreen::OnAdhocGuides(UI::EventParams &e) {
	LaunchBrowser("https://forums.ppsspp.org/forumdisplay.php?fid=34");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnImmersiveModeChange(UI::EventParams &e) {
	System_SendMessage("immersive", "");
	if (g_Config.iAndroidHwScale != 0) {
		RecreateActivity();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSustainedPerformanceModeChange(UI::EventParams &e) {
	System_SendMessage("sustainedPerfMode", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingMode(UI::EventParams &e) {
	// We do not want to report when rendering mode is Framebuffer to memory - so many issues
	// are caused by that (framebuffer copies overwriting display lists, etc).
	Reporting::UpdateConfig();
	enableReports_ = Reporting::IsEnabled();
	enableReportsCheckbox_->SetEnabled(Reporting::IsSupported());

	postProcEnable_ = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);
	resolutionEnable_ = !g_Config.bSoftwareRendering && (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);

	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		g_Config.bAutoFrameSkip = false;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	NativeMessageReceived("clear jit", "");
	return UI::EVENT_DONE;
}

#if PPSSPP_PLATFORM(ANDROID)

UI::EventReturn GameSettingsScreen::OnChangeMemStickDir(UI::EventParams &e) {
	I18NCategory *sy = GetI18NCategory("System");
	System_SendMessage("inputbox", (std::string(sy->T("Memory Stick Folder")) + ":" + g_Config.memStickDirectory).c_str());
	return UI::EVENT_DONE;
}

#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

UI::EventReturn GameSettingsScreen::OnSavePathMydoc(UI::EventParams &e) {
	const std::string PPSSPPpath = File::GetExeDirectory();
	const std::string installedFile = PPSSPPpath + "installed.txt";
	installed_ = File::Exists(installedFile);
	if (otherinstalled_) {
		File::Delete(PPSSPPpath + "installed.txt");
		File::CreateEmptyFile(PPSSPPpath + "installed.txt");
		otherinstalled_ = false;
		wchar_t myDocumentsPath[MAX_PATH];
		const HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
		const std::string myDocsPath = ConvertWStringToUTF8(myDocumentsPath) + "/PPSSPP/";
		g_Config.memStickDirectory = myDocsPath;
	}
	else if (installed_) {
		File::Delete(PPSSPPpath + "installed.txt");
		installed_ = false;
		g_Config.memStickDirectory = PPSSPPpath + "memstick/";
	}
	else {
		std::ofstream myfile;
		myfile.open(PPSSPPpath + "installed.txt");
		if (myfile.is_open()){
			myfile.close();
		}

		wchar_t myDocumentsPath[MAX_PATH];
		const HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
		const std::string myDocsPath = ConvertWStringToUTF8(myDocumentsPath) + "/PPSSPP/";
		g_Config.memStickDirectory = myDocsPath;
		installed_ = true;
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSavePathOther(UI::EventParams &e) {
	const std::string PPSSPPpath = File::GetExeDirectory();
	if (otherinstalled_) {
		I18NCategory *di = GetI18NCategory("Dialog");
		std::string folder = W32Util::BrowseForFolder(MainWindow::GetHWND(), di->T("Choose PPSSPP save folder"));
		if (folder.size()) {
			g_Config.memStickDirectory = folder;
			FILE *f = File::OpenCFile(PPSSPPpath + "installed.txt", "wb");
			if (f) {
				std::string utfstring("\xEF\xBB\xBF");
				utfstring.append(folder);
				fwrite(utfstring.c_str(), 1, utfstring.length(), f);
				fclose(f);
			}
			installed_ = false;
		}
		else
			otherinstalled_ = false;
	}
	else {
		File::Delete(PPSSPPpath + "installed.txt");
		SavePathInMyDocumentChoice->SetEnabled(true);
		otherinstalled_ = false;
		installed_ = false;
		g_Config.memStickDirectory = PPSSPPpath + "memstick/";
	}
	return UI::EVENT_DONE;
}

#endif

UI::EventReturn GameSettingsScreen::OnClearRecents(UI::EventParams &e) {
	g_Config.recentIsos.clear();
	OnRecentChanged.Trigger(e);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeBackground(UI::EventParams &e) {
	const std::string bgPng = GetSysDirectory(DIRECTORY_SYSTEM) + "background.png";
	const std::string bgJpg = GetSysDirectory(DIRECTORY_SYSTEM) + "background.jpg";
	if (File::Exists(bgPng) || File::Exists(bgJpg)) {
		if (File::Exists(bgPng)) {
			File::Delete(bgPng);
		}
		if (File::Exists(bgJpg)) {
			File::Delete(bgJpg);
		}

		NativeMessageReceived("bgImage_updated", "");
	} else {
		if (System_GetPropertyBool(SYSPROP_HAS_IMAGE_BROWSER)) {
			System_SendMessage("bgImage_browse", "");
		}
	}

	// Change to a browse or clear button.
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnFullscreenChange(UI::EventParams &e) {
	System_SendMessage("toggle_fullscreen", g_Config.bFullScreen ? "1" : "0");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDisplayLayoutEditor(UI::EventParams &e) {
	screenManager()->push(new DisplayLayoutScreen());
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnResolutionChange(UI::EventParams &e) {
	if (g_Config.iAndroidHwScale == 1) {
		RecreateActivity();
	}
	bloomHackEnable_ = !g_Config.bSoftwareRendering && g_Config.iInternalResolution != 1;
	Reporting::UpdateConfig();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnHwScaleChange(UI::EventParams &e) {
	RecreateActivity();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDumpNextFrameToLog(UI::EventParams &e) {
	if (gpu) {
		gpu->DumpNextFrame();
	}
	return UI::EVENT_DONE;
}

void GameSettingsScreen::update() {
	UIScreen::update();

	g_Config.iFpsLimit1 = iAlternateSpeedPercent1_ < 0 ? -1 : (iAlternateSpeedPercent1_ * 60) / 100;
	g_Config.iFpsLimit2 = iAlternateSpeedPercent2_ < 0 ? -1 : (iAlternateSpeedPercent2_ * 60) / 100;

	bool vertical = UseVerticalLayout();
	if (vertical != lastVertical_) {
		RecreateViews();
		lastVertical_ = vertical;
	}
}

void GameSettingsScreen::onFinish(DialogResult result) {
	if (g_Config.bEnableSound) {
		if (PSP_IsInited() && !IsAudioInitialised())
			Audio_Init();
	}

	Reporting::Enable(enableReports_, "report.ppsspp.org");
	Reporting::UpdateConfig();
	g_Config.Save("GameSettingsScreen::onFinish");
	if (editThenRestore_) {
		// In case we didn't have the title yet before, try again.
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
		g_Config.changeGameSpecific(gameID_, info->GetTitle());
		g_Config.unloadGameConfig();
	}

	host->UpdateUI();

	KeyMap::UpdateNativeMenuKeys();

	// Wipe some caches after potentially changing settings.
	NativeMessageReceived("gpu_resized", "");
	NativeMessageReceived("gpu_clearCache", "");
}

void GameSettingsScreen::sendMessage(const char *message, const char *value) {
	UIDialogScreenWithGameBackground::sendMessage(message, value);

	I18NCategory *sy = GetI18NCategory("System");
	I18NCategory *di = GetI18NCategory("Dialog");

	if (!strcmp(message, "inputbox_completed")) {
		std::vector<std::string> inputboxValue;
		SplitString(value, ':', inputboxValue);

#if PPSSPP_PLATFORM(ANDROID)
		if (inputboxValue.size() >= 2 && inputboxValue[0] == sy->T("Memory Stick Folder")) {
			// Allow colons in the path.
			std::string newPath = std::string(value).substr(inputboxValue[0].size() + 1);
			size_t pos = newPath.find_last_not_of("/");
			// Gotta have at least something but a /, and also needs to start with a /.
			if (newPath.empty() || pos == newPath.npos || newPath[0] != '/') {
				settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
				return;
			}
			if (pos != newPath.size() - 1) {
				newPath = newPath.substr(0, pos + 1);
			}

			pendingMemstickFolder_ = newPath;
			std::string promptMessage = sy->T("ChangingMemstickPath", "Save games, save states, and other data will not be copied to this folder.\n\nChange the Memory Stick folder?");
			if (!File::Exists(newPath)) {
				promptMessage = sy->T("ChangingMemstickPathNotExists", "That folder doesn't exist yet.\n\nSave games, save states, and other data will not be copied to this folder.\n\nCreate a new Memory Stick folder?");
			}
			// Add the path for clarity and proper confirmation.
			promptMessage += "\n\n" + newPath + "/";
			screenManager()->push(new PromptScreen(promptMessage, di->T("Yes"), di->T("No"), std::bind(&GameSettingsScreen::CallbackMemstickFolder, this, std::placeholders::_1)));
		}
#endif
	}
}

#if PPSSPP_PLATFORM(ANDROID)
void GameSettingsScreen::CallbackMemstickFolder(bool yes) {
	I18NCategory *sy = GetI18NCategory("System");

	if (yes) {
		std::string memstickDirFile = g_Config.internalDataDirectory + "/memstick_dir.txt";
		std::string testWriteFile = pendingMemstickFolder_ + "/.write_verify_file";

		// Already, create away.
		if (!File::Exists(pendingMemstickFolder_)) {
			File::CreateFullPath(pendingMemstickFolder_);
		}
		if (!writeDataToFile(true, "1", 1, testWriteFile.c_str())) {
			settingInfo_->Show(sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), nullptr);
			return;
		}
		File::Delete(testWriteFile);

		writeDataToFile(true, pendingMemstickFolder_.c_str(), pendingMemstickFolder_.size(), memstickDirFile.c_str());
		// Save so the settings, at least, are transferred.
		g_Config.memStickDirectory = pendingMemstickFolder_ + "/";
		g_Config.Save("MemstickPathChanged");
		screenManager()->RecreateAllViews();
	}
}
#endif

void GameSettingsScreen::CallbackRenderingBackend(bool yes) {
	// If the user ends up deciding not to restart, set the config back to the current backend
	// so it doesn't get switched by accident.
	if (yes) {
		// Extra save here to make sure the choice really gets saved even if there are shutdown bugs in
		// the GPU backend code.
		g_Config.Save("GameSettingsScreen::RenderingBackendYes");
		System_SendMessage("graphics_restart", "");
	} else {
		g_Config.iGPUBackend = (int)GetGPUBackend();
	}
}

void GameSettingsScreen::CallbackRenderingDevice(bool yes) {
	// If the user ends up deciding not to restart, set the config back to the current backend
	// so it doesn't get switched by accident.
	if (yes) {
		// Extra save here to make sure the choice really gets saved even if there are shutdown bugs in
		// the GPU backend code.
		g_Config.Save("GameSettingsScreen::RenderingDeviceYes");
		System_SendMessage("graphics_restart", "");
	} else {
		std::string *deviceNameSetting = GPUDeviceNameSetting();
		if (deviceNameSetting)
			*deviceNameSetting = GetGPUBackendDevice();
		// Needed to redraw the setting.
		RecreateViews();
	}
}

UI::EventReturn GameSettingsScreen::OnRenderingBackend(UI::EventParams &e) {
	I18NCategory *di = GetI18NCategory("Dialog");

	// It only makes sense to show the restart prompt if the backend was actually changed.
	if (g_Config.iGPUBackend != (int)GetGPUBackend()) {
		screenManager()->push(new PromptScreen(di->T("ChangingGPUBackends", "Changing GPU backends requires PPSSPP to restart. Restart now?"), di->T("Yes"), di->T("No"),
			std::bind(&GameSettingsScreen::CallbackRenderingBackend, this, std::placeholders::_1)));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRenderingDevice(UI::EventParams &e) {
	I18NCategory *di = GetI18NCategory("Dialog");

	// It only makes sense to show the restart prompt if the device was actually changed.
	std::string *deviceNameSetting = GPUDeviceNameSetting();
	if (deviceNameSetting && *deviceNameSetting != GetGPUBackendDevice()) {
		screenManager()->push(new PromptScreen(di->T("ChangingGPUBackends", "Changing GPU backends requires PPSSPP to restart. Restart now?"), di->T("Yes"), di->T("No"),
			std::bind(&GameSettingsScreen::CallbackRenderingDevice, this, std::placeholders::_1)));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnAudioDevice(UI::EventParams &e) {
	I18NCategory *a = GetI18NCategory("Audio");
	if (g_Config.sAudioDevice == a->T("Auto")) {
		g_Config.sAudioDevice.clear();
	}
	System_SendMessage("audio_resetDevice", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeNickname(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI)
	const size_t name_len = 256;

	char name[name_len];
	memset(name, 0, sizeof(name));

	if (System_InputBoxGetString("Enter a new PSP nickname", g_Config.sNickName.c_str(), name, name_len)) {
		g_Config.sNickName = StripSpaces(name);
	}
#elif defined(__ANDROID__)
	// TODO: The return value is handled in NativeApp::inputbox_completed. This is horrific.
	System_SendMessage("inputbox", ("nickname:" + g_Config.sNickName).c_str());
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeproAdhocServerAddress(UI::EventParams &e) {
	I18NCategory *sy = GetI18NCategory("System");

#if defined(__ANDROID__)
	System_SendMessage("inputbox", ("IP:" + g_Config.proAdhocServer).c_str());
#else
	screenManager()->push(new HostnameSelectScreen(&g_Config.proAdhocServer, sy->T("proAdhocServer Address:")));
#endif

	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnChangeMacAddress(UI::EventParams &e) {
	g_Config.sMACAddress = CreateRandMAC();

	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnComboKey(UI::EventParams &e) {
	screenManager()->push(new Combo_keyScreen(&g_Config.iComboMode));
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnLanguage(UI::EventParams &e) {
	I18NCategory *dev = GetI18NCategory("Developer");
	auto langScreen = new NewLanguageScreen(dev->T("Language"));
	langScreen->OnChoice.Handle(this, &GameSettingsScreen::OnLanguageChange);
	if (e.v)
		langScreen->SetPopupOrigin(e.v);
	screenManager()->push(langScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnLanguageChange(UI::EventParams &e) {
	screenManager()->RecreateAllViews();

	if (host) {
		host->UpdateUI();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnPostProcShader(UI::EventParams &e) {
	I18NCategory *gr = GetI18NCategory("Graphics");
	auto procScreen = new PostProcScreen(gr->T("Postprocessing Shader"));
	procScreen->OnChoice.Handle(this, &GameSettingsScreen::OnPostProcShaderChange);
	if (e.v)
		procScreen->SetPopupOrigin(e.v);
	screenManager()->push(procScreen);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnPostProcShaderChange(UI::EventParams &e) {
	NativeMessageReceived("gpu_resized", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDeveloperTools(UI::EventParams &e) {
screenManager()->push(new DeveloperToolsScreen());
return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnRemoteISO(UI::EventParams &e) {
	screenManager()->push(new RemoteISOScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnControlMapping(UI::EventParams &e) {
	screenManager()->push(new ControlMappingScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnTouchControlLayout(UI::EventParams &e) {
	screenManager()->push(new TouchControlLayoutScreen());
	return UI::EVENT_DONE;
}

//when the tilt event type is modified, we need to reset all tilt settings.
//refer to the ResetTiltEvents() function for a detailed explanation.
UI::EventReturn GameSettingsScreen::OnTiltTypeChange(UI::EventParams &e) {
	TiltEventProcessor::ResetTiltEvents();
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnTiltCustomize(UI::EventParams &e) {
	screenManager()->push(new TiltAnalogSettingsScreen());
	return UI::EVENT_DONE;
};

UI::EventReturn GameSettingsScreen::OnSavedataManager(UI::EventParams &e) {
	auto saveData = new SavedataScreen("");
	screenManager()->push(saveData);
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnSysInfo(UI::EventParams &e) {
	screenManager()->push(new SystemInfoScreen());
	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	ScrollView *settingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	settingsScroll->SetTag("DevToolsSettings");
	root_->Add(settingsScroll);

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *dev = GetI18NCategory("Developer");
	I18NCategory *gr = GetI18NCategory("Graphics");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *sy = GetI18NCategory("System");

	AddStandardBack(root_);

	LinearLayout *list = settingsScroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->SetSpacing(0);
	list->Add(new ItemHeader(sy->T("General")));

	bool canUseJit = true;
	// iOS can now use JIT on all modes, apparently.
	// The bool may come in handy for future non-jit platforms though (UWP XB1?)

	static const char *cpuCores[] = {"Interpreter", "Dynarec (JIT)", "IR Interpreter"};
	PopupMultiChoice *core = list->Add(new PopupMultiChoice(&g_Config.iCpuCore, gr->T("CPU Core"), cpuCores, 0, ARRAY_SIZE(cpuCores), sy->GetName(), screenManager()));
	core->OnChoice.Handle(this, &DeveloperToolsScreen::OnJitAffectingSetting);
	if (!canUseJit) {
		core->HideChoice(1);
	}

	list->Add(new Choice(dev->T("JIT debug tools")))->OnClick.Handle(this, &DeveloperToolsScreen::OnJitDebugTools);
	list->Add(new CheckBox(&g_Config.bShowDeveloperMenu, dev->T("Show Developer Menu")));
	list->Add(new CheckBox(&g_Config.bDumpDecryptedEboot, dev->T("Dump Decrypted Eboot", "Dump Decrypted EBOOT.BIN (If Encrypted) When Booting Game")));

#if !PPSSPP_PLATFORM(UWP)
	Choice *cpuTests = new Choice(dev->T("Run CPU Tests"));
	list->Add(cpuTests)->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);

	cpuTests->SetEnabled(TestsAvailable());
#endif
	// For now, we only implement GPU driver tests for Vulkan and OpenGL. This is simply
	// because the D3D drivers are generally solid enough to not need this type of investigation.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN || g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		list->Add(new Choice(dev->T("GPU Driver Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnGPUDriverTest);
	}
	list->Add(new Choice(dev->T("Touchscreen Test")))->OnClick.Handle(this, &DeveloperToolsScreen::OnTouchscreenTest);

	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
	CheckBox *allowDebugger = new CheckBox(&allowDebugger_, dev->T("Allow remote debugger"));
	list->Add(allowDebugger)->OnClick.Handle(this, &DeveloperToolsScreen::OnRemoteDebugger);
	allowDebugger->SetEnabledPtr(&canAllowDebugger_);

	list->Add(new CheckBox(&g_Config.bEnableLogging, dev->T("Enable Logging")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoggingChanged);
	list->Add(new CheckBox(&g_Config.bLogFrameDrops, dev->T("Log Dropped Frame Statistics")));
	list->Add(new Choice(dev->T("Logging Channels")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLogConfig);
	list->Add(new ItemHeader(dev->T("Language")));
	list->Add(new Choice(dev->T("Load language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnLoadLanguageIni);
	list->Add(new Choice(dev->T("Save language ini")))->OnClick.Handle(this, &DeveloperToolsScreen::OnSaveLanguageIni);
	list->Add(new ItemHeader(dev->T("Texture Replacement")));
	list->Add(new CheckBox(&g_Config.bSaveNewTextures, dev->T("Save new textures")));
	list->Add(new CheckBox(&g_Config.bReplaceTextures, dev->T("Replace textures")));

#if !defined(MOBILE_DEVICE)
	Choice *createTextureIni = list->Add(new Choice(dev->T("Create/Open textures.ini file for current game")));
	createTextureIni->OnClick.Handle(this, &DeveloperToolsScreen::OnOpenTexturesIniFile);
	if (!PSP_IsInited()) {
		createTextureIni->SetEnabled(false);
	}
#endif
}

void DeveloperToolsScreen::onFinish(DialogResult result) {
	g_Config.Save("DeveloperToolsScreen::onFinish");
}

void GameSettingsScreen::CallbackRestoreDefaults(bool yes) {
	if (yes)
		g_Config.RestoreDefaults();
	host->UpdateUI();
}

UI::EventReturn GameSettingsScreen::OnRestoreDefaultSettings(UI::EventParams &e) {
	I18NCategory *dev = GetI18NCategory("Developer");
	I18NCategory *di = GetI18NCategory("Dialog");
	if (g_Config.bGameSpecific)
	{
		screenManager()->push(
			new PromptScreen(dev->T("RestoreGameDefaultSettings", "Are you sure you want to restore the game-specific settings back to the ppsspp defaults?\n"), di->T("OK"), di->T("Cancel"),
			std::bind(&GameSettingsScreen::CallbackRestoreDefaults, this, std::placeholders::_1)));
	}
	else
	{
		screenManager()->push(
			new PromptScreen(dev->T("RestoreDefaultSettings", "Are you sure you want to restore all settings(except control mapping)\nback to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings."), di->T("OK"), di->T("Cancel"),
			std::bind(&GameSettingsScreen::CallbackRestoreDefaults, this, std::placeholders::_1)));
	}

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoggingChanged(UI::EventParams &e) {
	host->ToggleDebugConsoleVisibility();
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRunCPUTests(UI::EventParams &e) {
#if !PPSSPP_PLATFORM(UWP)
	RunTests();
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnSaveLanguageIni(UI::EventParams &e) {
	i18nrepo.SaveIni(g_Config.sLanguageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLoadLanguageIni(UI::EventParams &e) {
	i18nrepo.LoadIni(g_Config.sLanguageIni);
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnOpenTexturesIniFile(UI::EventParams &e) {
	std::string gameID = g_paramSFO.GetDiscID();
	std::string generatedFilename;
	if (TextureReplacer::GenerateIni(gameID, &generatedFilename)) {
		File::openIniFile(generatedFilename);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnLogConfig(UI::EventParams &e) {
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnJitDebugTools(UI::EventParams &e) {
	screenManager()->push(new JitDebugScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnGPUDriverTest(UI::EventParams &e) {
	screenManager()->push(new GPUDriverTestScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnTouchscreenTest(UI::EventParams &e) {
	screenManager()->push(new TouchTestScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnJitAffectingSetting(UI::EventParams &e) {
	NativeMessageReceived("clear jit", "");
	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRemoteDebugger(UI::EventParams &e) {
	if (allowDebugger_) {
		StartWebServer(WebServerFlags::DEBUGGER);
	} else {
		StopWebServer(WebServerFlags::DEBUGGER);
	}
	// Persist the setting.  Maybe should separate?
	g_Config.bRemoteDebuggerOnStartup = allowDebugger_;
	return UI::EVENT_CONTINUE;
}

void DeveloperToolsScreen::update() {
	UIDialogScreenWithBackground::update();
	allowDebugger_ = !WebServerStopped(WebServerFlags::DEBUGGER);
	canAllowDebugger_ = !WebServerStopping(WebServerFlags::DEBUGGER);
}

void HostnameSelectScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	I18NCategory *sy = GetI18NCategory("System");
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *n = GetI18NCategory("Networking");

	LinearLayout *valueRow = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, Margins(0, 0, 0, 10)));

	addrView_ = new TextEdit(*value_, "");
	addrView_->SetTextAlign(FLAG_DYNAMIC_ASCII);
	valueRow->Add(addrView_);
	parent->Add(valueRow);

	LinearLayout *buttonsRow1 = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	LinearLayout *buttonsRow2 = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	parent->Add(buttonsRow1);
	parent->Add(buttonsRow2);

	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, G_LEFT)));
	for (char c = '0'; c <= '9'; ++c) {
		char label[] = { c, '\0' };
		auto button = buttonsRow1->Add(new Button(label));
		button->OnClick.Handle(this, &HostnameSelectScreen::OnNumberClick);
		button->SetTag(label);
	}
	buttonsRow1->Add(new Button("."))->OnClick.Handle(this, &HostnameSelectScreen::OnPointClick);
	buttonsRow1->Add(new Spacer(new LinearLayoutParams(1.0, G_RIGHT)));

	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, G_LEFT)));
	buttonsRow2->Add(new Button(di->T("Delete")))->OnClick.Handle(this, &HostnameSelectScreen::OnDeleteClick);
	buttonsRow2->Add(new Button(di->T("Delete all")))->OnClick.Handle(this, &HostnameSelectScreen::OnDeleteAllClick);
	buttonsRow2->Add(new Spacer(new LinearLayoutParams(1.0, G_RIGHT)));

	errorView_ = parent->Add(new TextView(n->T("Invalid IP or hostname"), ALIGN_HCENTER, false, new LinearLayoutParams(Margins(0, 10, 0, 0))));
	errorView_->SetTextColor(0xFF3030FF);
	errorView_->SetVisibility(V_GONE);

	progressView_ = parent->Add(new TextView(n->T("Validating address..."), ALIGN_HCENTER, false, new LinearLayoutParams(Margins(0, 10, 0, 0))));
	progressView_->SetVisibility(V_GONE);
}

void HostnameSelectScreen::SendEditKey(int keyCode, int flags) {
	auto oldView = UI::GetFocusedView();
	UI::SetFocusedView(addrView_);
	KeyInput fakeKey{ DEVICE_ID_KEYBOARD, keyCode, KEY_DOWN | flags };
	addrView_->Key(fakeKey);
	UI::SetFocusedView(oldView);
}

UI::EventReturn HostnameSelectScreen::OnNumberClick(UI::EventParams &e) {
	std::string text = e.v ? e.v->Tag() : "";
	if (text.length() == 1 && text[0] >= '0' && text[0] <= '9') {
		SendEditKey(text[0], KEY_CHAR);
	}
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnPointClick(UI::EventParams &e) {
	SendEditKey('.', KEY_CHAR);
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnDeleteClick(UI::EventParams &e) {
	SendEditKey(NKCODE_DEL);
	return UI::EVENT_DONE;
}

UI::EventReturn HostnameSelectScreen::OnDeleteAllClick(UI::EventParams &e) {
	addrView_->SetText("");
	return UI::EVENT_DONE;
}

void HostnameSelectScreen::ResolverThread() {
	std::unique_lock<std::mutex> guard(resolverLock_);

	while (resolverState_ != ResolverState::QUIT) {
		resolverCond_.wait(guard);

		if (resolverState_ == ResolverState::QUEUED) {
			resolverState_ = ResolverState::PROGRESS;

			addrinfo *resolved = nullptr;
			std::string err;
			toResolveResult_ = net::DNSResolve(toResolve_, "80", &resolved, err);
			if (resolved)
				net::DNSResolveFree(resolved);

			resolverState_ = ResolverState::READY;
		}
	}
}

bool HostnameSelectScreen::CanComplete(DialogResult result) {
	if (result != DR_OK)
		return true;

	std::string value = addrView_->GetText();
	if (lastResolved_ == value) {
		return true;
	}

	// Currently running.
	if (resolverState_ == ResolverState::PROGRESS)
		return false;

	std::lock_guard<std::mutex> guard(resolverLock_);
	switch (resolverState_) {
	case ResolverState::PROGRESS:
	case ResolverState::QUIT:
		return false;

	case ResolverState::QUEUED:
	case ResolverState::WAITING:
		break;

	case ResolverState::READY:
		if (toResolve_ == value) {
			// Reset the state, nothing there now.
			resolverState_ = ResolverState::WAITING;
			toResolve_.clear();
			lastResolved_ = value;
			lastResolvedResult_ = toResolveResult_;

			if (lastResolvedResult_) {
				errorView_->SetVisibility(UI::V_GONE);
			} else {
				errorView_->SetVisibility(UI::V_VISIBLE);
			}
			progressView_->SetVisibility(UI::V_GONE);

			return true;
		}

		// Throw away that last result, it was for a different value.
		break;
	}

	resolverState_ = ResolverState::QUEUED;
	toResolve_ = value;
	resolverCond_.notify_one();

	progressView_->SetVisibility(UI::V_VISIBLE);
	errorView_->SetVisibility(UI::V_GONE);

	return false;
}

void HostnameSelectScreen::OnCompleted(DialogResult result) {
	if (result == DR_OK)
		*value_ = addrView_->GetText();
}

SettingInfoMessage::SettingInfoMessage(int align, UI::AnchorLayoutParams *lp)
	: UI::LinearLayout(UI::ORIENT_HORIZONTAL, lp) {
	using namespace UI;
	SetSpacing(0.0f);
	Add(new UI::Spacer(10.0f));
	text_ = Add(new UI::TextView("", align, false, new LinearLayoutParams(1.0, Margins(0, 10))));
	Add(new UI::Spacer(10.0f));
}

void SettingInfoMessage::Show(const std::string &text, UI::View *refView) {
	if (refView) {
		Bounds b = refView->GetBounds();
		const UI::AnchorLayoutParams *lp = GetLayoutParams()->As<UI::AnchorLayoutParams>();
		if (b.y >= cutOffY_) {
			ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, 80.0f, lp->right, lp->bottom, lp->center));
		} else {
			ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, dp_yres - 80.0f - 40.0f, lp->right, lp->bottom, lp->center));
		}
	}
	text_->SetText(text);
	timeShown_ = time_now_d();
}

void SettingInfoMessage::Draw(UIContext &dc) {
	static const double FADE_TIME = 1.0;
	static const float MAX_ALPHA = 0.9f;

	// Let's show longer messages for more time (guesstimate at reading speed.)
	// Note: this will give multibyte characters more time, but they often have shorter words anyway.
	double timeToShow = std::max(1.5, text_->GetText().size() * 0.05);

	double sinceShow = time_now_d() - timeShown_;
	float alpha = MAX_ALPHA;
	if (timeShown_ == 0.0 || sinceShow > timeToShow + FADE_TIME) {
		alpha = 0.0f;
	} else if (sinceShow > timeToShow) {
		alpha = MAX_ALPHA - MAX_ALPHA * (float)((sinceShow - timeToShow) / FADE_TIME);
	}

	if (alpha >= 0.1f) {
		UI::Style style = dc.theme->popupTitle;
		style.background.color = colorAlpha(style.background.color, alpha - 0.1f);
		dc.FillRect(style.background, bounds_);
	}

	text_->SetTextColor(whiteAlpha(alpha));
	ViewGroup::Draw(dc);
}
