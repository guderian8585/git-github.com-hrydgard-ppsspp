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

#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui_context.h"
#include "UI/EmuScreen.h"
#include "UI/PluginScreen.h"
#include "UI/MenuScreens.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "Core/Config.h"
#include "android/jni/TestRunner.h"
#include "GPU/GPUInterface.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "math/curves.h"
#include "Core/HW/atrac3plus.h"
#include "Core/System.h"

#ifdef _WIN32
namespace MainWindow {
	enum { WM_USER_LOG_STATUS_CHANGED = WM_USER + 200 };
	extern HWND hwndMain;
}
#endif

namespace UI {

// Reads and writes value to determine the current selection.
class PopupMultiChoice : public Choice {
public:
	PopupMultiChoice(int *value, const std::string &text, const char **choices, int minVal, int numChoices,
		I18NCategory *category, ScreenManager *screenManager, LayoutParams *layoutParams = 0)
		: Choice(text, "", false, layoutParams), value_(value), choices_(choices), minVal_(minVal), numChoices_(numChoices), 
		category_(category), screenManager_(screenManager) {
		if (*value < minVal) *value = minVal;
		OnClick.Handle(this, &PopupMultiChoice::HandleClick);
		UpdateText();
	}

	virtual void Draw(UIContext &dc);

private:
	void UpdateText();
	EventReturn HandleClick(EventParams &e);

	void ChoiceCallback(int num);

	int *value_;
	const char **choices_;
	int minVal_;
	int numChoices_;
	I18NCategory *category_;
	ScreenManager *screenManager_;
	std::string valueText_;
};

EventReturn PopupMultiChoice::HandleClick(EventParams &e) {
	std::vector<std::string> choices;
	for (int i = 0; i < numChoices_; i++) {
		choices.push_back(category_ ? category_->T(choices_[i]) : choices_[i]);
	}

	Screen *popupScreen = new ListPopupScreen(text_, choices, *value_ - minVal_,
		std::bind(&PopupMultiChoice::ChoiceCallback, this, placeholder::_1));
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

void PopupMultiChoice::UpdateText() {
	valueText_ = category_ ? category_->T(choices_[*value_ - minVal_]) : choices_[*value_ - minVal_];
}

void PopupMultiChoice::ChoiceCallback(int num) {
	*value_ = num + minVal_;
	UpdateText();
}

void PopupMultiChoice::Draw(UIContext &dc) {
	Choice::Draw(dc);
	dc.Draw()->DrawText(dc.theme->uiFont, valueText_.c_str(), bounds_.x2() - 8, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

class PopupSliderChoice : public Choice {
public:
	PopupSliderChoice(int *value, int minValue, int maxValue, const std::string &text, ScreenManager *screenManager, LayoutParams *layoutParams = 0)
		: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), screenManager_(screenManager) {
		OnClick.Handle(this, &PopupSliderChoice::HandleClick);
	}

	void Draw(UIContext &dc);

private:
	EventReturn HandleClick(EventParams &e);

	int *value_;
	int minValue_;
	int maxValue_;
	ScreenManager *screenManager_;
};

class PopupSliderChoiceFloat : public Choice {
public:
	PopupSliderChoiceFloat(float *value, float minValue, float maxValue, const std::string &text, ScreenManager *screenManager, LayoutParams *layoutParams = 0)
		: Choice(text, "", false, layoutParams), value_(value), minValue_(minValue), maxValue_(maxValue), screenManager_(screenManager) {
		OnClick.Handle(this, &PopupSliderChoiceFloat::HandleClick);
	}

	void Draw(UIContext &dc);

private:
	EventReturn HandleClick(EventParams &e);

	float *value_;
	float minValue_;
	float maxValue_;
	ScreenManager *screenManager_;
};

EventReturn PopupSliderChoice::HandleClick(EventParams &e) {
	Screen *popupScreen = new SliderPopupScreen(value_, minValue_, maxValue_, text_);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}


void PopupSliderChoice::Draw(UIContext &dc) {
	Choice::Draw(dc);
	char temp[4];
	sprintf(temp, "%i", *value_);
	dc.Draw()->DrawText(dc.theme->uiFont, temp, bounds_.x2() - 8, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

EventReturn PopupSliderChoiceFloat::HandleClick(EventParams &e) {
	Screen *popupScreen = new SliderFloatPopupScreen(value_, minValue_, maxValue_, text_);
	screenManager_->push(popupScreen);
	return EVENT_DONE;
}

void PopupSliderChoiceFloat::Draw(UIContext &dc) {
	Choice::Draw(dc);
	char temp[5];
	sprintf(temp, "%2.2f", *value_);
	dc.Draw()->DrawText(dc.theme->uiFont, temp, bounds_.x2() - 8, bounds_.centerY(), 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
}

}


void GameSettingsScreen::CreateViews() {
	GameInfo *info = g_gameInfoCache.GetInfo(gamePath_, true);

	cap60FPS_ = g_Config.iForceMaxEmulatedFPS == 60;

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *c = GetI18NCategory("Controls");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *s = GetI18NCategory("System");
	I18NCategory *ms = GetI18NCategory("MainSettings");

	Margins actionMenuMargins(0, 0, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new Spacer(new LinearLayoutParams(1.0)));
	leftColumn->Add(new Choice(g->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle(this, &GameSettingsScreen::OnBack);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, 200, new LinearLayoutParams(800, FILL_PARENT, actionMenuMargins));

	root_->Add(tabHolder);

	// TODO: These currently point to global settings, not game specific ones.

	// Graphics
	ViewGroup *graphicsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *graphicsSettings = new LinearLayout(ORIENT_VERTICAL);
	graphicsSettingsScroll->Add(graphicsSettings);
	tabHolder->AddTab(ms->T("Graphics"), graphicsSettingsScroll);

	graphicsSettings->Add(new ItemHeader(gs->T("Rendering Mode")));
#ifndef USING_GLES2
	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering", "Read Framebuffers To Memory(CPU)", "Read Framebuffers To Memory(GPU)"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gs->T("Mode"), renderingMode, 0, 4, gs, screenManager()));
#else
	static const char *renderingMode[] = { "Non-Buffered Rendering", "Buffered Rendering", "Read Framebuffers To Memory(GPU)"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iRenderingMode, gs->T("Mode"), renderingMode, 0, 3, gs, screenManager()));
#endif
	graphicsSettings->Add(new CheckBox(&g_Config.bAntiAliasing, gs->T("Anti-Aliasing")));
	graphicsSettings->Add(new ItemHeader(gs->T("Features")));
	graphicsSettings->Add(new CheckBox(&g_Config.bHardwareTransform, gs->T("Hardware Transform")));
	graphicsSettings->Add(new CheckBox(&g_Config.bVertexCache, gs->T("Vertex Cache")));
	graphicsSettings->Add(new CheckBox(&g_Config.bStretchToDisplay, gs->T("Stretch to Display")));
	graphicsSettings->Add(new CheckBox(&g_Config.bMipMap, gs->T("Mipmapping")));
	graphicsSettings->Add(new CheckBox(&g_Config.bTrueColor, gs->T("True Color")));
#ifdef _WIN32
	graphicsSettings->Add(new CheckBox(&g_Config.bVSync, gs->T("VSync")));
	graphicsSettings->Add(new CheckBox(&g_Config.bFullScreen, gs->T("FullScreen")));
#endif
	graphicsSettings->Add(new ItemHeader(gs->T("Frame Rate Control")));
	static const char *fpsChoices[] = {"None", "Speed", "FPS", "Both"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iShowFPSCounter, gs->T("Show FPS Counter"), fpsChoices, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bShowDebugStats, gs->T("Show Debug Statistics")));
	graphicsSettings->Add(new PopupSliderChoice(&g_Config.iFrameSkip, 0, 9, gs->T("Frame Skipping"), screenManager()));
	graphicsSettings->Add(new CheckBox(&cap60FPS_, gs->T("Force 60 FPS or less")));
	
	graphicsSettings->Add(new ItemHeader(gs->T("Anisotropic Filtering")));
	static const char *anisoLevels[] = { "Off", "2x", "4x", "8x", "16x" };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iAnisotropyLevel, gs->T("Anisotropic Filtering"), anisoLevels, 0, 5, gs, screenManager()));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Scaling")));
#ifndef USING_GLES2
	static const char *texScaleLevels[] = {"Off", "2x", "3x","4x", "5x"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 1, 5, gs, screenManager()));
#else
	static const char *texScaleLevels[] = {"Off", "2x", "3x"};
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingLevel, gs->T("Upscale Level"), texScaleLevels, 1, 3, gs, screenManager()));
#endif
	static const char *texScaleAlgos[] = { "xBRZ", "Hybrid", "Bicubic", "Hybrid + Bicubic", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexScalingType, gs->T("Upscale Type"), texScaleAlgos, 0, 4, gs, screenManager()));
	graphicsSettings->Add(new CheckBox(&g_Config.bTexDeposterize, gs->T("Deposterize")));
	graphicsSettings->Add(new ItemHeader(gs->T("Texture Filtering")));
	static const char *texFilters[] = { "Auto", "Nearest", "Linear", "Linear on FMV", };
	graphicsSettings->Add(new PopupMultiChoice(&g_Config.iTexFiltering, gs->T("Upscale Type"), texFilters, 1, 4, gs, screenManager()));

	// Audio
	ViewGroup *audioSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *audioSettings = new LinearLayout(ORIENT_VERTICAL);
	audioSettingsScroll->Add(audioSettings);
	tabHolder->AddTab(ms->T("Audio"), audioSettingsScroll);

	std::string atracString;
	atracString.assign(Atrac3plus_Decoder::IsInstalled() ? "Redownload Atrac3+ plugin" : "Download Atrac3+ plugin");
	audioSettings->Add(new Choice(a->T(atracString.c_str())))->OnClick.Handle(this, &GameSettingsScreen::OnDownloadPlugin);

	audioSettings->Add(new CheckBox(&g_Config.bEnableSound, a->T("Enable Sound")));
	audioSettings->Add(new CheckBox(&g_Config.bEnableAtrac3plus, a->T("Enable Atrac3+")));
	audioSettings->Add(new PopupSliderChoice(&g_Config.iSFXVolume, 0, 8, a->T("SFX volume"), screenManager()));
	audioSettings->Add(new PopupSliderChoice(&g_Config.iBGMVolume, 0, 8, a->T("BGM volume"), screenManager()));

	// Control
	ViewGroup *controlsSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *controlsSettings = new LinearLayout(ORIENT_VERTICAL);
	controlsSettingsScroll->Add(controlsSettings);
	tabHolder->AddTab(ms->T("Controls"), controlsSettingsScroll);
	controlsSettings->Add(new CheckBox(&g_Config.bShowTouchControls, c->T("OnScreen", "On-Screen Touch Controls")));
	controlsSettings->Add(new CheckBox(&g_Config.bShowAnalogStick, c->T("Show Left Analog Stick")));
	controlsSettings->Add(new CheckBox(&g_Config.bAccelerometerToAnalogHoriz, c->T("Tilt", "Tilt to Analog (horizontal)")));
	controlsSettings->Add(new Choice(gs->T("Control Mapping")))->OnClick.Handle(this, &GameSettingsScreen::OnControlMapping);
	controlsSettings->Add(new PopupSliderChoice(&g_Config.iTouchButtonOpacity, 0, 85, c->T("Button Opacity"), screenManager()));
	controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fButtonScale, 1.15, 2.05, c->T("Button Scaling"), screenManager()));
	
	// System
	ViewGroup *systemSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	ViewGroup *systemSettings = new LinearLayout(ORIENT_VERTICAL);
	systemSettingsScroll->Add(systemSettings);
	tabHolder->AddTab(ms->T("System"), systemSettingsScroll);
	systemSettings->Add(new CheckBox(&g_Config.bJit, s->T("Dynarec", "Dynarec (JIT)")));
	systemSettings->Add(new CheckBox(&g_Config.bSeparateCPUThread, s->T("Multithreaded (experimental)")));
	systemSettings->Add(new CheckBox(&g_Config.bFastMemory, s->T("Fast Memory", "Fast Memory (Unstable)")));
	systemSettings->Add(new PopupSliderChoice(&g_Config.iLockedCPUSpeed, 0, 1000, gs->T("Unlock CPU Clock"), screenManager()));
	systemSettings->Add(new CheckBox(&g_Config.bDayLightSavings, s->T("Day Light Saving")));
	static const char *dateFormat[] = { "YYYYMMDD", "MMDDYYYY", "DDMMYYYY"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iDateFormat, gs->T("Date Format"), dateFormat, 1, 3, s, screenManager()));
	static const char *timeFormat[] = { "12HR", "24HR"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iTimeFormat, gs->T("Time Format"), timeFormat, 1, 2, s, screenManager()));
	static const char *buttonPref[] = { "Use X to confirm", "Use O to confirm"};
	systemSettings->Add(new PopupMultiChoice(&g_Config.iButtonPreference, gs->T("Button Preference"), buttonPref, 1, 2, s, screenManager()));
}

void DrawBackground(float alpha);

void GameSettingsScreen::DrawBackground(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, true);
	dc.Flush();

	dc.RebindTexture();
	::DrawBackground(1.0f);
	dc.Flush();

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		dc.Draw()->DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		dc.Flush();
		dc.RebindTexture();
	}
	/*
	if (ginfo && ginfo->pic0Texture) {
		ginfo->pic0Texture->Bind(0);
		// Pic0 is drawn in the bottom right corner, overlaying pic1.
		float sizeX = dp_xres / 480 * ginfo->pic0Texture->Width();
		float sizeY = dp_yres / 272 * ginfo->pic0Texture->Height();
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 2)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(dp_xres - sizeX, dp_yres - sizeY, dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		dc.RebindTexture();
	}*/
}

void GameSettingsScreen::update(InputState &input) {
	UIScreen::update(input);
	g_Config.iForceMaxEmulatedFPS = cap60FPS_ ? 60 : 0;
}

UI::EventReturn GameSettingsScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);

	if(g_Config.bEnableSound) {
		if(PSP_IsInited() && !IsAudioInitialised())
			Audio_Init();
	}

	return UI::EVENT_DONE;
}

void GlobalSettingsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	enableReports_ = g_Config.sReportHost != "";

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");

	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->Add(new ItemHeader(g->T("General")));
	list->Add(new CheckBox(&g_Config.bNewUI, gs->T("Enable New UI")));
	list->Add(new CheckBox(&enableReports_, gs->T("Enable Compatibility Server Reports")));
	list->Add(new CheckBox(&g_Config.bEnableCheats, gs->T("Enable Cheats")));
	list->Add(new CheckBox(&g_Config.bScreenshotsAsPNG, gs->T("Screenshots as PNG")));
	list->Add(new CheckBox(&g_Config.bDirectLoad, gs->T("Enable Direct Load")));
	list->Add(new Choice(gs->T("System Language")))->OnClick.Handle(this, &GlobalSettingsScreen::OnLanguage);
	list->Add(new Choice(gs->T("Developer Tools")))->OnClick.Handle(this, &GlobalSettingsScreen::OnDeveloperTools);
	list->Add(new Choice(g->T("Back")))->OnClick.Handle(this, &GlobalSettingsScreen::OnBack);
}

UI::EventReturn GlobalSettingsScreen::OnFactoryReset(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GlobalSettingsScreen::OnLanguage(UI::EventParams &e) {
	screenManager()->push(new NewLanguageScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GlobalSettingsScreen::OnDeveloperTools(UI::EventParams &e) {
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnControlMapping(UI::EventParams &e) {
	screenManager()->push(new KeyMappingScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GlobalSettingsScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	g_Config.sReportHost = enableReports_ ? "report.ppsspp.org" : "";
	g_Config.Save();
	return UI::EVENT_DONE;
}

void DeveloperToolsScreen::CreateViews() {
	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL);

	enableLogging_ = g_Config.bEnableLogging;

	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *d = GetI18NCategory("Developer");
	I18NCategory *a = GetI18NCategory("Audio");

	LinearLayout *list = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
	list->Add(new ItemHeader(g->T("General")));
	list->Add(new Choice(d->T("Run CPU Tests")))->OnClick.Handle(this, &DeveloperToolsScreen::OnRunCPUTests);
	list->Add(new CheckBox(&enableLogging_, d->T("Enable Debug Logging")));
	list->Add(new Choice(g->T("Back")))->OnClick.Handle(this, &DeveloperToolsScreen::OnBack);
}

UI::EventReturn DeveloperToolsScreen::OnBack(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);

	g_Config.bEnableLogging = enableLogging_;
#ifdef _WIN32
	PostMessage(MainWindow::hwndMain, MainWindow::WM_USER_LOG_STATUS_CHANGED, 0, 0);
#endif
	g_Config.Save();

	return UI::EVENT_DONE;
}

UI::EventReturn DeveloperToolsScreen::OnRunCPUTests(UI::EventParams &e) {
	RunTests();
	return UI::EVENT_DONE;
}

UI::EventReturn GameSettingsScreen::OnDownloadPlugin(UI::EventParams &e) {
	screenManager()->push(new PluginScreen());
	return UI::EVENT_DONE;
}
