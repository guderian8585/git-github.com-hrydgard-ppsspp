﻿// Copyright (c) 2012- PPSSPP Project.

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

#include <cmath>
#include <string>
#include <cstdio>

#ifdef _MSC_VER
#define snprintf _snprintf
#pragma execution_character_set("utf-8")
#endif

#include "base/display.h"
#include "base/logging.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "i18n/i18n.h"
#include "file/vfs.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "input/input_state.h"
#include "math/curves.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "ui_atlas.h"
#include "util/text/utf8.h"
#include "UIShader.h"

#include "Common/StringUtils.h"
#include "Common/KeyMap.h"
#include "Core/System.h"
#include "Core/CoreParameter.h"
#include "Core/HW/atrac3plus.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/Reporting.h"
#include "Core/SaveState.h"
#include "Core/HLE/sceUtility.h"

#include "UI/MenuScreens.h"
#include "UI/GameScreen.h"
#include "UI/EmuScreen.h"
#include "UI/PluginScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"

#include "GameInfoCache.h"
#include "android/jni/TestRunner.h"


#ifdef USING_QT_UI
#include <QFileDialog>
#include <QFile>
#include <QDir>
#endif

#ifdef _WIN32
#include "Core/Host.h"
#endif

#ifdef _WIN32
namespace MainWindow {
	enum {
		WM_USER_LOG_STATUS_CHANGED = WM_USER + 200
	};
	extern HWND hwndMain;
	void BrowseAndBoot(std::string defaultPath, bool browseDirectory = false);
	HINSTANCE GetHInstance();
}
#endif

#if !defined(nullptr)
#define nullptr NULL
#endif

// Ugly communication with NativeApp
extern std::string game_title;

// Detect jailbreak for iOS(Non-jailbreak iDevice doesn't support JIT)
#ifdef IOS
extern bool isJailed;
#endif

static const int symbols[4] = {
	I_CROSS,
	I_CIRCLE,
	I_SQUARE,
	I_TRIANGLE
};

static const uint32_t colors[4] = {
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
};

void DrawBackground(float alpha) {
	static float xbase[100] = {0};
	static float ybase[100] = {0};
	static int last_dp_xres = 0;
	static int last_dp_yres = 0;
	if (xbase[0] == 0.0f || last_dp_xres != dp_xres || last_dp_yres != dp_yres) {
		GMRng rng;
		for (int i = 0; i < 100; i++) {
			xbase[i] = rng.F() * dp_xres;
			ybase[i] = rng.F() * dp_yres;
		}
		last_dp_xres = dp_xres;
		last_dp_yres = dp_yres;
	}
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.1f,0.2f,0.43f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	ui_draw2d.DrawImageStretch(I_BG, 0, 0, dp_xres, dp_yres);
	float t = time_now();
	for (int i = 0; i < 100; i++) {
		float x = xbase[i];
		float y = ybase[i] + 40*cos(i * 7.2 + t * 1.3);
		float angle = sin(i + t);
		int n = i & 3;
		ui_draw2d.DrawImageRotated(symbols[n], x, y, 1.0f, angle, colorAlpha(colors[n], alpha * 0.1f));
	}
}

// ==================
//		Menu Screen
// ==================

MenuScreen::MenuScreen() : frames_(0) {
	// If first run, let's show the user an easy way to access the atrac3plus download screen.
	showAtracShortcut_ = g_Config.bFirstRun && !Atrac3plus_Decoder::IsInstalled();
}

void MenuScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	showAtracShortcut_ = showAtracShortcut_ && !Atrac3plus_Decoder::IsInstalled();
}


void MenuScreen::update(InputState &input_state) {
	globalUIState = UISTATE_MENU;
	frames_++;
}

void MenuScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
}

void MenuScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	double xoff = 150 - frames_ * frames_ * 0.4f;
	if (xoff < -20)
		xoff = -20;
	if (frames_ > 200)  // seems the above goes nuts after a while...
		xoff = -20;

	int w = LARGE_BUTTON_WIDTH + 70;

	ui_draw2d.DrawTextShadow(UBUNTU48, "PPSSPP", dp_xres + xoff - w/2, 75, 0xFFFFFFFF, ALIGN_HCENTER | ALIGN_BOTTOM);
	ui_draw2d.SetFontScale(0.7f, 0.7f);
	ui_draw2d.DrawTextShadow(UBUNTU24, PPSSPP_GIT_VERSION, dp_xres + xoff, 95, 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_BOTTOM);
	ui_draw2d.SetFontScale(1.0f, 1.0f);
	VLinear vlinear(dp_xres + xoff, 100, 20);

	I18NCategory *m = GetI18NCategory("MainMenu");
	I18NCategory *p = GetI18NCategory("Plugin");

	if (UIButton(GEN_ID, vlinear, w, 0, m->T("Load", "Load..."), ALIGN_RIGHT)) {
#if defined(USING_QT_UI) && !defined(MEEGO_EDITION_HARMATTAN)
		QString fileName = QFileDialog::getOpenFileName(NULL, "Load ROM", g_Config.currentDirectory.c_str(), "PSP ROMs (*.iso *.cso *.pbp *.elf)");
		if (QFile::exists(fileName)) {
			QDir newPath;
			g_Config.currentDirectory = newPath.filePath(fileName).toStdString();
			g_Config.Save();
			screenManager()->switchScreen(new EmuScreen(fileName.toStdString()));
		}
#elif _WIN32
		MainWindow::BrowseAndBoot("");
#else
		FileSelectScreenOptions options;
		options.allowChooseDirectory = true;
		options.filter = "iso:cso:pbp:elf:prx:";
		options.folderIcon = I_ICON_FOLDER;
		options.iconMapping["iso"] = I_ICON_UMD;
		options.iconMapping["cso"] = I_ICON_UMD;
		options.iconMapping["pbp"] = I_ICON_EXE;
		options.iconMapping["elf"] = I_ICON_EXE;
		screenManager()->switchScreen(new FileSelectScreen(options));
#endif
		UIReset();
	}

	if (UIButton(GEN_ID, vlinear, w, 0, m->T("Settings"), ALIGN_RIGHT)) {
		screenManager()->push(new SettingsScreen(), 0);
		UIReset();
	}

	if (UIButton(GEN_ID, vlinear, w, 0, m->T("Credits"), ALIGN_RIGHT)) {
		screenManager()->push(new CreditsScreen());
		UIReset();
	}

	if (UIButton(GEN_ID, vlinear, w, 0, m->T("Exit"), ALIGN_RIGHT)) {
		// TODO: Need a more elegant way to quit
#ifdef _WIN32
		PostMessage(MainWindow::hwndMain, WM_CLOSE, 0, 0);
#else
		// TODO: Save when setting changes, rather than when we quit
		NativeShutdown();
		exit(0);
#endif
	}

	if (UIButton(GEN_ID, vlinear, w, 0, "www.ppsspp.org", ALIGN_RIGHT)) {
		LaunchBrowser("http://www.ppsspp.org/");
	}

	// Skip the forum button if screen too small. Will be redesigned in new UI later.
	if (dp_yres > 510) {
		if (UIButton(GEN_ID, vlinear, w, 0, "forums.ppsspp.org", ALIGN_RIGHT)) {
			LaunchBrowser("http://forums.ppsspp.org/");
		}
	}

	if (showAtracShortcut_) {
		if (UIButton(GEN_ID, Pos(10, dp_yres - 10), 500, 50, p->T("Download audio plugin"), ALIGN_BOTTOMLEFT)) {
			screenManager()->push(new PluginScreen());
		}
	}

	int recentW = 350;
	if (g_Config.recentIsos.size()) {
		ui_draw2d.DrawTextShadow(UBUNTU24, m->T("Recent"), -xoff, 80, 0xFFFFFFFF, ALIGN_BOTTOMLEFT);
	}

	int spacing = 15;

	float textureButtonWidth = 144;
	float textureButtonHeight = 80;

	if (dp_yres < 480)
		spacing = 8;

	int extraSpace = 0;
	if (showAtracShortcut_)
		extraSpace = 60;

	if (showAtracShortcut_)
	// On small screens, we can't fit four vertically.
	if (100 + spacing * 6 + textureButtonHeight * 4 > dp_yres) {
		textureButtonHeight = (dp_yres - 100 - spacing * 6) / 4;
		textureButtonWidth = (textureButtonHeight / 80) * 144;
	}

	VGrid vgrid_recent(-xoff, 100, std::min(dp_yres-spacing*2-extraSpace, 480), spacing, spacing);

	for (size_t i = 0; i < g_Config.recentIsos.size(); i++) {
		std::string filename;
		std::string rec = g_Config.recentIsos[i];
		for (size_t j = 0; j < rec.size(); j++)
			if (rec[j] == '\\') rec[j] = '/';
		SplitPath(rec, nullptr, &filename, nullptr);

		UIContext *ctx = screenManager()->getUIContext();
		// This might create a texture so we must flush first.
		UIFlush();
		GameInfo *ginfo = g_gameInfoCache.GetInfo(g_Config.recentIsos[i], false);
		if (ginfo) {
			u32 color;
			if (ginfo->iconTexture == 0) {
				color = 0;
			} else {
				color = whiteAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));
			}
			if (UITextureButton(ctx, (int)GEN_ID_LOOP(i), vgrid_recent, textureButtonWidth, textureButtonHeight, ginfo->iconTexture, ALIGN_LEFT, color, I_DROP_SHADOW)) {
				UIEnd();

				// To try some new UI, enable this.
				// screenManager()->switchScreen(new GameScreen(g_Config.recentIsos[i]));
				screenManager()->switchScreen(new EmuScreen(g_Config.recentIsos[i]));
				return;
			}
		} else {
			if (UIButton((int)GEN_ID_LOOP(i), vgrid_recent, textureButtonWidth, textureButtonHeight, filename.c_str(), ALIGN_LEFT)) {
				UIEnd();
				// screenManager()->switchScreen(new GameScreen(g_Config.recentIsos[i]));
				screenManager()->switchScreen(new EmuScreen(g_Config.recentIsos[i]));
				return;
			}
		}
	}

#if defined(_DEBUG) & defined(_WIN32)
	// Print the current dp_xres/yres in the corner. For UI scaling testing - just
	// resize to 800x480 to get an idea of what it will look like on a Nexus S.
	ui_draw2d.SetFontScale(0.6, 0.6);
	char temptext[64];
	sprintf(temptext, "%ix%i", dp_xres, dp_yres);
	ui_draw2d.DrawTextShadow(UBUNTU24, temptext, 5, dp_yres-5, 0xFFFFFFFF, ALIGN_BOTTOMLEFT);
	ui_draw2d.SetFontScale(1.0, 1.0);
#endif

	UIEnd();
}


void PauseScreen::update(InputState &input) {
	globalUIState = UISTATE_PAUSEMENU;
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->finishDialog(this, DR_CANCEL);
	}
}

void PauseScreen::sendMessage(const char *msg, const char *value) {
	if (!strcmp(msg, "run")) {
		screenManager()->finishDialog(this, DR_CANCEL);
	} else if (!strcmp(msg, "stop") || !strcmp(msg, "reset") || !strcmp(msg, "boot")) {
		// Close the menu and send the events to EmuScreen
		m_data = new PauseScreen::Message(msg, value);
		screenManager()->finishDialog(this, DR_YES);
	}
}

void PauseScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	std::string title = game_title.c_str();
	// Try to ignore (tm) etc.
	//if (UTF8StringNonASCIICount(game_title.c_str()) > 2) {
	//	title = "(can't display japanese title)";
	//} else {
	//}


	UIContext *ctx = screenManager()->getUIContext();
	// This might create a texture so we must flush first.
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo) {
		title = ginfo->title;
	}

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	}

	if (ginfo && ginfo->pic0Texture) {
		ginfo->pic0Texture->Bind(0);
		// Pic0 is drawn in the bottom right corner, overlaying pic1.
		float sizeX = dp_xres / 480 * ginfo->pic0Texture->Width() * 0.5;
		float sizeY = dp_yres / 272 * ginfo->pic0Texture->Height() * 0.5;
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 2)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(dp_xres - sizeX, dp_yres - sizeY, dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	}

	if (ginfo && ginfo->iconTexture) {
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 1.5));
		ginfo->iconTexture->Bind(0);

		// Maintain the icon's aspect ratio.  Minis are square, for example.
		float iconAspect = (float)ginfo->iconTexture->Width() / (float)ginfo->iconTexture->Height();
		float h = 80.0f;
		float w = 144.0f;
		float x = 10.0f + (w - h * iconAspect) / 2.0f;
		w = h * iconAspect;

		ui_draw2d.DrawTexRect(x, 10, x + w, 10 + h, 0, 0, 1, 1, 0xFFFFFFFF);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	}

	ui_draw2d.DrawTextShadow(UBUNTU24, title.c_str(), 10+144+10, 30, 0xFFFFFFFF, ALIGN_LEFT);

	I18NCategory *i = GetI18NCategory("Pause");

	VLinear vlinear(dp_xres - 10, 100, 20);
	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 40, 0, i->T("Continue"), ALIGN_RIGHT)) {
		screenManager()->finishDialog(this, DR_CANCEL);
		if (gpu) gpu->Resized();
	}

	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 40, 0, i->T("Settings"), ALIGN_RIGHT)) {
		screenManager()->push(new SettingsScreen(), 0);
	}

	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 40, 0, i->T("Back to Menu"), ALIGN_RIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	/*
	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH*2, 0, "Debug: Dump Next Frame", ALIGN_BOTTOMRIGHT)) {
		gpu->DumpNextFrame();
	}
	*/

	int x = 30;
	int y = 60;
	int stride = 40;
	int columnw = 400;

	// Shared with settings
	I18NCategory *ss = GetI18NCategory("System");
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *a = GetI18NCategory("Audio");
	
	UICheckBox(GEN_ID, x, y += stride, a->T("Enable Sound"), ALIGN_TOPLEFT, &g_Config.bEnableSound);
	// TODO: Maybe shouldn't show this if the screen ratios are very close...
#ifdef BLACKBERRY
	if (pixel_xres == pixel_yres)
		UICheckBox(GEN_ID, x, y += stride, gs->T("Partial Vertical Stretch"), ALIGN_TOPLEFT, &g_Config.bPartialStretch);
#endif
	UICheckBox(GEN_ID, x, y += stride, gs->T("Stretch to Display"), ALIGN_TOPLEFT, &g_Config.bStretchToDisplay);

	UICheckBox(GEN_ID, x, y += stride, gs->T("Hardware Transform"), ALIGN_TOPLEFT, &g_Config.bHardwareTransform);
	bool enableFrameSkip = g_Config.iFrameSkip != 0;
	UICheckBox(GEN_ID, x, y += stride , gs->T("Frame Skipping"), ALIGN_TOPLEFT, &enableFrameSkip);
	if (enableFrameSkip) {
		if (g_Config.iFrameSkip == 0)
			g_Config.iFrameSkip = 3;

		char showFrameSkip[256];
		sprintf(showFrameSkip, "%s %d", gs->T("Frames :"), g_Config.iFrameSkip);
		ui_draw2d.DrawTextShadow(UBUNTU24, showFrameSkip, x + 60, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear2(x + 220, y, 20);
		if (UIButton(GEN_ID, hlinear2, 80, 0, gs->T("Auto"), ALIGN_LEFT))
			g_Config.iFrameSkip = 3;
		if (UIButton(GEN_ID, hlinear2, 40, 0, gs->T("-1"), ALIGN_LEFT))
			if (g_Config.iFrameSkip > 1)
				g_Config.iFrameSkip -= 1;
		if (UIButton(GEN_ID, hlinear2, 40, 0, gs->T("+1"), ALIGN_LEFT))
			if (g_Config.iFrameSkip < 9)
				g_Config.iFrameSkip += 1;
		y+=20;
	} else 
		g_Config.iFrameSkip = 0;
	
	y+=10;
	ui_draw2d.DrawTextShadow(UBUNTU24, gs->T("Save State :"), 30, y += 40, 0xFFFFFFFF, ALIGN_LEFT);
	HLinear hlinear4(x + 180 , y , 10);
	if (UIButton(GEN_ID, hlinear4, 60, 0, "1", ALIGN_LEFT)) {
		SaveState::SaveSlot(0, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear4, 60, 0, "2", ALIGN_LEFT)) {
		SaveState::SaveSlot(1, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear4, 60, 0, "3", ALIGN_LEFT)) {
		SaveState::SaveSlot(2, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear4, 60, 0, "4", ALIGN_LEFT)) {
		SaveState::SaveSlot(3, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear4, 60, 0, "5", ALIGN_LEFT)) {
		SaveState::SaveSlot(4, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}

	ui_draw2d.DrawTextShadow(UBUNTU24, gs->T("Load State :"), 30, y += 60, 0xFFFFFFFF, ALIGN_LEFT);
	HLinear hlinear3(x + 180 , y + 10 , 10);
	if (UIButton(GEN_ID, hlinear3, 60, 0, "1", ALIGN_LEFT)) {
		SaveState::LoadSlot(0, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear3, 60, 0, "2", ALIGN_LEFT)) {
		SaveState::LoadSlot(1, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear3, 60, 0, "3", ALIGN_LEFT)) {
		SaveState::LoadSlot(2, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear3, 60, 0, "4", ALIGN_LEFT)) {
		SaveState::LoadSlot(3, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}
	if (UIButton(GEN_ID, hlinear3, 60, 0, "5", ALIGN_LEFT)) {
		SaveState::LoadSlot(4, 0, 0);
		screenManager()->finishDialog(this, DR_CANCEL);
	}

	UIEnd();
}

void SettingsScreen::update(InputState &input) {
	globalUIState = UISTATE_MENU;
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void SettingsScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *ms = GetI18NCategory("MainSettings");

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, ms->T("Settings"), dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	VLinear vlinear(30, 135, 20);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres-10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	const int stride = 70;
	int w = LARGE_BUTTON_WIDTH + 25;
	int s = 270;
	int y = 90;

	if (UIButton(GEN_ID, vlinear, w, 0, ms->T("Audio"), ALIGN_BOTTOMLEFT)) {
		screenManager()->push(new AudioScreen());
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, ms->T("AudioDesc", "Adjust Audio Settings"), s, y, 0xFFFFFFFF, ALIGN_LEFT);

	if (UIButton(GEN_ID, vlinear, w, 0, ms->T("Graphics"), ALIGN_BOTTOMLEFT)) {
		screenManager()->push(new GraphicsScreenP1());
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, ms->T("GraphicsDesc", "Change graphics options"), s, y += stride, 0xFFFFFFFF, ALIGN_LEFT);

	if (UIButton(GEN_ID, vlinear, w, 0, ms->T("System"), ALIGN_BOTTOMLEFT)) {
		screenManager()->push(new SystemScreen());
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, ms->T("SystemDesc", "Turn on Dynarec (JIT), Fast Memory"), s, y += stride, 0xFFFFFFFF, ALIGN_LEFT);

	if (UIButton(GEN_ID, vlinear, w, 0, ms->T("Controls"), ALIGN_BOTTOMLEFT)) {
		screenManager()->push(new ControlsScreen());
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, ms->T("ControlsDesc", "On Screen Controls, Large Buttons"), s, y += stride, 0xFFFFFFFF, ALIGN_LEFT);

	if (UIButton(GEN_ID, vlinear, w, 0, ms->T("Developer"), ALIGN_BOTTOMLEFT)) {
		screenManager()->push(new DeveloperScreen());
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, ms->T("DeveloperDesc", "Run CPU test, Dump Next Frame Log"), s, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
	UIEnd();
}

// TODO: Move these into a superclass
void DeveloperScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void AudioScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void GraphicsScreenP1::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void GraphicsScreenP2::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void GraphicsScreenP3::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void SystemScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void ControlsScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void LanguageScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->finishDialog(this, DR_OK);
	}
}

void ControlsScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();

	I18NCategory *c = GetI18NCategory("Controls");
	I18NCategory *g = GetI18NCategory("General");

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, c->T("Controls Settings"), dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	int x = 30;
	int y = 35;
	int stride = 40;
	int columnw = 440;

	UICheckBox(GEN_ID, x, y += stride, c->T("OnScreen", "On-Screen Touch Controls"), ALIGN_TOPLEFT, &g_Config.bShowTouchControls);
	UICheckBox(GEN_ID, x, y += stride, c->T("Tilt", "Tilt to Analog (horizontal)"), ALIGN_TOPLEFT, &g_Config.bAccelerometerToAnalogHoriz);
	if (g_Config.bShowTouchControls) {
		UICheckBox(GEN_ID, x, y += stride, c->T("Show Left Analog Stick"), ALIGN_TOPLEFT, &g_Config.bShowAnalogStick);

		char scale[256];
		sprintf(scale, "%s %0.2f", c->T("Buttons Scaling:"), g_Config.fButtonScale);
		ui_draw2d.DrawTextShadow(UBUNTU24, scale, x + 60, y += stride , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 400, y, 20);
		if (UIButton(GEN_ID, hlinear1, 80, 0, c->T("Auto"), ALIGN_LEFT))
			g_Config.fButtonScale = 1.15;
		if (UIButton(GEN_ID, hlinear1, 60, 0, c->T("-0.1"), ALIGN_LEFT))
			if (g_Config.fButtonScale > 1.15)
				g_Config.fButtonScale -= 0.1;
		if (UIButton(GEN_ID, hlinear1, 60, 0, c->T("+0.1"), ALIGN_LEFT))
			if (g_Config.fButtonScale < 2.05)
				g_Config.fButtonScale += 0.1;
		y += 20;

		// This will be a slider in the new UI later
		bool bTransparent = g_Config.iTouchButtonOpacity < 65;
		bool prev = bTransparent;
		UICheckBox(GEN_ID, x, y += stride, c->T("Buttons Opacity"), ALIGN_TOPLEFT, &bTransparent);
		if (bTransparent) {
			char opacity[256];
			sprintf(opacity, "%s %d", c->T("Opacity :"), g_Config.iTouchButtonOpacity);
			ui_draw2d.DrawTextShadow(UBUNTU24, opacity, x + 60, y += stride , 0xFFFFFFFF, ALIGN_LEFT);
			HLinear hlinear1(x + 250, y, 20);
			if (UIButton(GEN_ID, hlinear1, 80, 0, c->T("Auto"), ALIGN_LEFT))
				g_Config.iTouchButtonOpacity = 15;
			if (UIButton(GEN_ID, hlinear1, 40, 0, c->T("-5"), ALIGN_LEFT))
				if (g_Config.iTouchButtonOpacity > 15)
					g_Config.iTouchButtonOpacity -= 5;
			if (UIButton(GEN_ID, hlinear1, 40, 0, c->T("+5"), ALIGN_LEFT))
				if (g_Config.iTouchButtonOpacity < 65)
					g_Config.iTouchButtonOpacity += 5;
			y += 20;
		}
		if (bTransparent != prev)
			g_Config.iTouchButtonOpacity = bTransparent ? 15 : 65;
	}
	y += 10;
	// Button to KeyMapping screen
	HLinear hlinear(x, y += stride, 20);
	if (UIButton(GEN_ID, hlinear, 250, 0, c->T("Key Mapping"), ALIGN_LEFT)) {
		screenManager()->push(new ControlMappingScreen());
	}
	if (UIButton(GEN_ID, hlinear, 250, 0, c->T("Default Mapping"), ALIGN_LEFT)) {
		KeyMap::RestoreDefault();
	}

	UIEnd();
}

void DeveloperScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *d = GetI18NCategory("Developer");
	I18NCategory *s = GetI18NCategory("System");

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, d->T("Developer Tools"), dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	int x = 50;
	int y = 40;
	const int stride = 40;
	const int w = 400;

	UICheckBox(GEN_ID, x, y += stride, s->T("Show Debug Statistics"), ALIGN_TOPLEFT, &g_Config.bShowDebugStats);

	bool reportingEnabled = Reporting::IsEnabled();
	const static std::string reportHostOfficial = "report.ppsspp.org";

	UICheckBox(GEN_ID, x, y += stride, d->T("Enable Logging"), ALIGN_TOPLEFT, &g_Config.bEnableLogging);
#ifdef _WIN32
	// Update the main menu bar.
	PostMessage(MainWindow::hwndMain, MainWindow::WM_USER_LOG_STATUS_CHANGED, 0, 0);
#endif

	if (UICheckBox(GEN_ID, x, y += stride, d->T("Report","Enable Compatibility Server Reports"), ALIGN_TOPLEFT, &reportingEnabled)) {
		g_Config.sReportHost = reportingEnabled ? reportHostOfficial : "";
	}

	UICheckBox(GEN_ID, x, y += stride, d->T("New UI"), ALIGN_TOPLEFT, &g_Config.bNewUI);

	VLinear vlinear(x, y + stride + 12, 16);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 80, 0, d->T("Load language ini"), ALIGN_LEFT)) {
		i18nrepo.LoadIni(g_Config.languageIni);
		// After this, g and s are no longer valid. Need to reload them.
		g = GetI18NCategory("General");
		d = GetI18NCategory("Developer");
	}

	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 80, 0, d->T("Save language ini"), ALIGN_LEFT)) {
		i18nrepo.SaveIni(g_Config.languageIni);
	}

	if (!gpu) {
		if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 80, 0, d->T("Run CPU Tests"), ALIGN_LEFT)) {
			// TODO: Run tests
			RunTests();
			// screenManager()->push(new EmuScreen())
		}
	}

	if (gpu) {
		if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH + 80, 0, d->T("Dump next frame"), ALIGN_LEFT)) {
			gpu->DumpNextFrame();
		}
	}
	
	UIEnd();
}

void AudioScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *a = GetI18NCategory("Audio");
	I18NCategory *p = GetI18NCategory("Plugin");

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, a->T("Audio Settings"), dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres-10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	int x = 30;
	int y = 35;
	int stride = 40;
	int columnw = 400;
	UICheckBox(GEN_ID, x, y += stride, a->T("Enable Sound"), ALIGN_TOPLEFT, &g_Config.bEnableSound);
	if (g_Config.bEnableSound) {
		
		if(PSP_IsInited() && !IsAudioInitialised()) {
			Audio_Init();
		}

		if (Atrac3plus_Decoder::IsInstalled()) {
			UICheckBox(GEN_ID, x, y += stride, a->T("Enable Atrac3+"), ALIGN_TOPLEFT, &g_Config.bEnableAtrac3plus);

			if(g_Config.bEnableAtrac3plus)
				Atrac3plus_Decoder::Init();
			else Atrac3plus_Decoder::Shutdown();
		} 

		// Show the download button even if not installed - might want to upgrade.
		VLinear vlinear(30, 400, 20);
		std::string atracString;
		atracString.assign(Atrac3plus_Decoder::IsInstalled() ? p->T("Redownload Atrac3+ plugin") : p->T("Download Atrac3+ plugin"));
		if (UIButton(GEN_ID, vlinear, 400, 0, a->T(atracString.c_str()), ALIGN_LEFT)) {
			screenManager()->push(new PluginScreen());
		}

		y+=10;
		char bgmvol[256];
		sprintf(bgmvol, "%s: %i", a->T("BGM Volume"), g_Config.iBGMVolume);
		ui_draw2d.DrawTextShadow(UBUNTU24, bgmvol, x, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 250, y, 20);
		if (UIButton(GEN_ID, hlinear1, 80, 0, a->T("Auto"), ALIGN_LEFT))
			g_Config.iBGMVolume = 3;
		if (UIButton(GEN_ID, hlinear1, 50, 0, a->T("-1"), ALIGN_LEFT))
			if (g_Config.iBGMVolume > 1)
				g_Config.iBGMVolume -= 1;
		if (UIButton(GEN_ID, hlinear1, 50, 0, a->T("+1"), ALIGN_LEFT))
			if (g_Config.iBGMVolume < 8)
				g_Config.iBGMVolume += 1;
		y+=20;
		char sfxvol[256];
		sprintf(sfxvol, "%s: %i", a->T("SFX Volume"), g_Config.iSFXVolume);
		ui_draw2d.DrawTextShadow(UBUNTU24, sfxvol, x, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear2(x + 250, y, 20);
		if (UIButton(GEN_ID, hlinear2, 80, 0, a->T("Auto"), ALIGN_LEFT))
			g_Config.iSFXVolume = 3;
		if (UIButton(GEN_ID, hlinear2, 50, 0, a->T("-1"), ALIGN_LEFT))
			if (g_Config.iSFXVolume > 1)
				g_Config.iSFXVolume -= 1;
		if (UIButton(GEN_ID, hlinear2, 50, 0, a->T("+1"), ALIGN_LEFT))
			if (g_Config.iSFXVolume < 8)
				g_Config.iSFXVolume += 1;

		y+=10;

	} else
		g_Config.bEnableAtrac3plus = false;

	UIEnd();
}

void GraphicsScreenP1::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");
	
	char temp[256];
	sprintf(temp, "%s 1/3", gs->T("Graphics Settings"));

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, temp, dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_BOTTOMRIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	HLinear hlinear(10, dp_yres - 10, 20.0f);
	if (UIButton(GEN_ID, hlinear, LARGE_BUTTON_WIDTH + 10, 0, g->T("Prev Page"), ALIGN_BOTTOMLEFT)) {
		screenManager()->switchScreen(new GraphicsScreenP3());
	}
	if (UIButton(GEN_ID, hlinear, LARGE_BUTTON_WIDTH + 10, 0, g->T("Next Page"), ALIGN_BOTTOMLEFT)) {
		screenManager()->switchScreen(new GraphicsScreenP2());
	}

	int x = 30;
	int y = 35;
	int stride = 40;
	int columnw = 400;

#ifndef __SYMBIAN32__
	UICheckBox(GEN_ID, x, y += stride, gs->T("Hardware Transform"), ALIGN_TOPLEFT, &g_Config.bHardwareTransform);
#endif
	UICheckBox(GEN_ID, x, y += stride, gs->T("Vertex Cache"), ALIGN_TOPLEFT, &g_Config.bVertexCache);

	UICheckBox(GEN_ID, x, y += stride, gs->T("Mipmapping"), ALIGN_TOPLEFT, &g_Config.bMipMap);
	
	UICheckBox(GEN_ID, x, y += stride, gs->T("AA", "Anti-Aliasing"), ALIGN_TOPLEFT, &g_Config.bAntiAliasing);

#ifdef _WIN32
	//bool Vsync = g_Config.iVSyncInterval != 0;
	//UICheckBox(GEN_ID, x, y += stride, gs->T("VSync"), ALIGN_TOPLEFT, &Vsync);
	//g_Config.iVSyncInterval = Vsync ? 1 : 0;
	//UICheckBox(GEN_ID, x, y += stride, gs->T("Fullscreen"), ALIGN_TOPLEFT, &g_Config.bFullScreen);
#endif
	y+=20;

	bool nonrendering = g_Config.iRenderingMode == 0;
	if (UICheckBox(GEN_ID, x, y += stride, gs->T("Non-Buffered Rendering"), ALIGN_TOPLEFT, &nonrendering))
	g_Config.iRenderingMode = 0;

	bool rendering = g_Config.iRenderingMode == 1;
	if (UICheckBox(GEN_ID, x, y += stride, gs->T("Buffered Rendering"), ALIGN_TOPLEFT, &rendering))
	g_Config.iRenderingMode = rendering ? 1 : 0;

	bool useFBO = g_Config.iRenderingMode == 2;
	if (UICheckBox(GEN_ID, x, y += stride, gs->T("Read Framebuffers to Memory (CPU)"), ALIGN_TOPLEFT, &useFBO))
	g_Config.iRenderingMode = useFBO ? 2 : 0;

	bool useCPU = g_Config.iRenderingMode == 3;
	if (UICheckBox(GEN_ID, x, y += stride, gs->T("Read Framebuffers to Memory (GPU)"), ALIGN_TOPLEFT, &useCPU))
	g_Config.iRenderingMode = useCPU ? 3 : 0;
	
	UIEnd();
}

void GraphicsScreenP2::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");
	
	char temp[256];
	sprintf(temp, "%s 2/3", gs->T("Graphics Settings"));

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, temp, dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	HLinear hlinear(10, dp_yres - 10, 20.0f);
	if (UIButton(GEN_ID, hlinear, LARGE_BUTTON_WIDTH + 10, 0, g->T("Prev Page"), ALIGN_BOTTOMLEFT)) {
		screenManager()->switchScreen(new GraphicsScreenP1());
	}
	if (UIButton(GEN_ID, hlinear, LARGE_BUTTON_WIDTH + 10, 0, g->T("Next Page"), ALIGN_BOTTOMLEFT)) {
		screenManager()->switchScreen(new GraphicsScreenP3());
	}

	int x = 30;
	int y = 35;
	int stride = 40;
	int columnw = 400;

	bool AnisotropicFiltering = g_Config.iAnisotropyLevel != 0;
	UICheckBox(GEN_ID, x, y += stride, gs->T("Anisotropic Filtering"), ALIGN_TOPLEFT, &AnisotropicFiltering);
	if (AnisotropicFiltering) {
		if (g_Config.iAnisotropyLevel == 0)
			g_Config.iAnisotropyLevel = 1;

		char showAF[256];
		sprintf(showAF, "%s %dx", gs->T("Level :"), 1 << g_Config.iAnisotropyLevel);
		ui_draw2d.DrawTextShadow(UBUNTU24, showAF, x + 60, (y += stride) , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 300, y , 20);
		if (UIButton(GEN_ID, hlinear1, 60, 0, gs->T("2x"), ALIGN_LEFT))
			g_Config.iAnisotropyLevel = 1;
		if (UIButton(GEN_ID, hlinear1, 60, 0, gs->T("4x"), ALIGN_LEFT))
			g_Config.iAnisotropyLevel = 2;
		if (UIButton(GEN_ID, hlinear1, 60, 0, gs->T("8x"), ALIGN_LEFT))
			g_Config.iAnisotropyLevel = 3;
		if (UIButton(GEN_ID, hlinear1, 60, 0, gs->T("16x"), ALIGN_LEFT))
			g_Config.iAnisotropyLevel = 4;

		y += 20;
	} else 
		g_Config.iAnisotropyLevel = 0;
	
	bool TexFiltering = g_Config.iTexFiltering > 1;
	UICheckBox(GEN_ID, x, y += stride, gs->T("Texture Filtering"), ALIGN_TOPLEFT, &TexFiltering);
	if (TexFiltering) {
		if (g_Config.iTexFiltering <= 1)
			g_Config.iTexFiltering = 2;

		char showType[256];
		std::string type;
		switch (g_Config.iTexFiltering) {
		case 2:	type = "Nearest"; break;
		case 3: type = "Linear"; break;
		case 4:	type = "Linear(CG)"; break;
		}
		sprintf(showType, "%s %s", gs->T("Type :"), type.c_str());
		ui_draw2d.DrawTextShadow(UBUNTU24, showType, x + 60, (y += stride) , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear1, 170, 0, gs->T("Nearest"), ALIGN_LEFT)) 
			g_Config.iTexFiltering = 2;
		if (UIButton(GEN_ID, hlinear1, 170, 0, gs->T("Linear"), ALIGN_LEFT))
			g_Config.iTexFiltering = 3;
		if (UIButton(GEN_ID, hlinear1, 170, 0, gs->T("Linear(CG)"), ALIGN_LEFT))
			g_Config.iTexFiltering = 4;
		y += 20;
	} else
		g_Config.iTexFiltering = 0;

	bool TexScaling = g_Config.iTexScalingLevel > 1;
	UICheckBox(GEN_ID, x, y += stride, gs->T("Texture Scaling"), ALIGN_TOPLEFT, &TexScaling);
	if (TexScaling) {
		if (g_Config.iTexScalingLevel <= 1)
			g_Config.iTexScalingLevel = 2;

		char showType[256];
		std::string type;

		switch (g_Config.iTexScalingType) {
		case 0:	type = "xBRZ";break;
		case 1: type = "Hybrid";break;
		case 2:	type = "Bicubic";break;
		case 3: type = "H+B";break;
		}
		sprintf(showType, "%s %s", gs->T("Type :"), type.c_str());
		ui_draw2d.DrawTextShadow(UBUNTU24, showType, x + 60, (y += stride) , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear1, 80, 0, gs->T("xBRZ"), ALIGN_LEFT))
			g_Config.iTexScalingType = 0;
		if (UIButton(GEN_ID, hlinear1, 120, 0, gs->T("Hybrid", "(H)ybrid"), ALIGN_LEFT))
			g_Config.iTexScalingType = 1;
		if (UIButton(GEN_ID, hlinear1, 130, 0, gs->T("Bicubic", "(B)icubic"), ALIGN_LEFT))
			g_Config.iTexScalingType = 2;
		if (UIButton(GEN_ID, hlinear1, 80, 0, gs->T("H+B", "H+B"), ALIGN_LEFT))
			g_Config.iTexScalingType = 3;
		y += 20;
		char showLevel[256];
		sprintf(showLevel, "%s %dx", gs->T("Level :"), g_Config.iTexScalingLevel);
		ui_draw2d.DrawTextShadow(UBUNTU24, showLevel, x + 60, (y += stride) , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear2(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear2, 50, 0, gs->T("2x"), ALIGN_LEFT))
			g_Config.iTexScalingLevel = 2;
		if (UIButton(GEN_ID, hlinear2, 50, 0, gs->T("3x"), ALIGN_LEFT))
			g_Config.iTexScalingLevel = 3;
#ifdef _WIN32
		if (UIButton(GEN_ID, hlinear2, 50, 0, gs->T("4x"), ALIGN_LEFT))
			g_Config.iTexScalingLevel = 4;
		if (UIButton(GEN_ID, hlinear2, 50, 0, gs->T("5x"), ALIGN_LEFT))
			g_Config.iTexScalingLevel = 5;
#endif
		UICheckBox(GEN_ID, x + 60, y += stride + 20, gs->T("Deposterize"), ALIGN_LEFT, &g_Config.bTexDeposterize);
	} else 
		g_Config.iTexScalingLevel = 1;
	
	UIEnd();
}

void GraphicsScreenP3::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *g = GetI18NCategory("General");
	I18NCategory *gs = GetI18NCategory("Graphics");
	
	char temp[256];
	sprintf(temp, "%s 3/3", gs->T("Graphics Settings"));

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, temp, dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	HLinear hlinear(10, dp_yres - 10, 20.0f);
	if (UIButton(GEN_ID, hlinear, LARGE_BUTTON_WIDTH + 10, 0, g->T("Prev Page"), ALIGN_BOTTOMLEFT)) {
		screenManager()->switchScreen(new GraphicsScreenP2());
	}
	if (UIButton(GEN_ID, hlinear, LARGE_BUTTON_WIDTH + 10, 0, g->T("Next Page"), ALIGN_BOTTOMLEFT)) {
		screenManager()->switchScreen(new GraphicsScreenP1());
	}

	int x = 30;
	int y = 35;
	int stride = 40;
	int columnw = 400;
	
	bool ForceMaxEmulatedFPS60 = g_Config.iForceMaxEmulatedFPS == 60;
	if (UICheckBox(GEN_ID, x, y += stride, gs->T("Force 60 FPS or less"), ALIGN_TOPLEFT, &ForceMaxEmulatedFPS60))
		g_Config.iForceMaxEmulatedFPS = ForceMaxEmulatedFPS60 ? 60 : 0;

	bool ShowCounter = g_Config.iShowFPSCounter > 0;
	UICheckBox(GEN_ID, x, y += stride, gs->T("Show speed / internal FPS"), ALIGN_TOPLEFT, &ShowCounter);
	if (ShowCounter) {
#ifdef _WIN32
	const int checkboxH = 32;
#else
	const int checkboxH = 48;
#endif
		ui_draw2d.DrawTextShadow(UBUNTU24, gs->T("(60.0 is full speed, internal FPS depends on game)"), x + UI_SPACE + 29, (y += stride) + checkboxH / 2, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);

		if (g_Config.iShowFPSCounter <= 0)
			g_Config.iShowFPSCounter = 1;

		const char *type;
		switch (g_Config.iShowFPSCounter) {
		case 1: type = gs->T("Display: Speed"); break;
		case 2:	type = gs->T("Display: FPS"); break;
		case 3: type = gs->T("Display: Both"); break;
		default: type = ""; break;
		}

		ui_draw2d.DrawTextShadow(UBUNTU24, type, x + 60, y += stride , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear1, 100, 0, gs->T("Speed"), ALIGN_LEFT))
			g_Config.iShowFPSCounter = 1;
		if (UIButton(GEN_ID, hlinear1, 100, 0, gs->T("FPS"), ALIGN_LEFT))
			g_Config.iShowFPSCounter = 2;
		if (UIButton(GEN_ID, hlinear1, 100, 0, gs->T("Both"), ALIGN_LEFT))
			g_Config.iShowFPSCounter = 3;

		y += 20;
	} else 
		g_Config.iShowFPSCounter = 0;

	bool FpsLimit = g_Config.iFpsLimit != 0;
	UICheckBox(GEN_ID, x, y += stride, gs->T("Toggled Speed Limit"), ALIGN_TOPLEFT, &FpsLimit);
	if (FpsLimit) {
		if (g_Config.iFpsLimit == 0)
			g_Config.iFpsLimit = 60;
		
		char showFps[256];
		sprintf(showFps, "%s %d", gs->T("Speed:"), g_Config.iFpsLimit);
		ui_draw2d.DrawTextShadow(UBUNTU24, showFps, x + 60, y += stride , 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear1, 100, 0, gs->T("Auto"), ALIGN_LEFT))
			g_Config.iFpsLimit = 60;
		if (UIButton(GEN_ID, hlinear1, 50, 0, gs->T("-1"), ALIGN_LEFT))
			if(g_Config.iFpsLimit > 10)
				g_Config.iFpsLimit -= 1;
		if (UIButton(GEN_ID, hlinear1, 50, 0, gs->T("+1"), ALIGN_LEFT))
				g_Config.iFpsLimit += 1;
		if (UIButton(GEN_ID, hlinear1, 65, 0, gs->T("-10"), ALIGN_LEFT))
			if(g_Config.iFpsLimit > 10)
				g_Config.iFpsLimit -= 10;
		if (UIButton(GEN_ID, hlinear1, 65, 0, gs->T("+10"), ALIGN_LEFT))
				g_Config.iFpsLimit += 10;

		y += 20;
	} else 
		g_Config.iFpsLimit = 0;

	bool enableFrameSkip = g_Config.iFrameSkip != 0;
	UICheckBox(GEN_ID, x, y += stride , gs->T("Frame Skipping"), ALIGN_TOPLEFT, &enableFrameSkip);
	if (enableFrameSkip) {
		if (g_Config.iFrameSkip == 0)
			g_Config.iFrameSkip = 3;

		char showFrameSkip[256];
		sprintf(showFrameSkip, "%s %d", gs->T("Frames:"), g_Config.iFrameSkip);
		ui_draw2d.DrawTextShadow(UBUNTU24, showFrameSkip, x + 60, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear2(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear2, 100, 0, gs->T("Auto"), ALIGN_LEFT))
			g_Config.iFrameSkip = 3;
		if (UIButton(GEN_ID, hlinear2, 50, 0, gs->T("-1"), ALIGN_LEFT))
			if (g_Config.iFrameSkip > 1)
				g_Config.iFrameSkip -= 1;
		if (UIButton(GEN_ID, hlinear2, 50, 0, gs->T("+1"), ALIGN_LEFT))
			if (g_Config.iFrameSkip < 9)
				g_Config.iFrameSkip += 1;

		y += 20;
	} else 
		g_Config.iFrameSkip = 0;

	UIEnd();
}

LanguageScreen::LanguageScreen()
{
#ifdef ANDROID
	VFSGetFileListing("assets/lang", &langs_, "ini");
#else
	VFSGetFileListing("lang", &langs_, "ini");
#endif
}

std::map<std::string, std::pair<std::string, int>> GetLangValuesMapping() {
	std::map<std::string, std::pair<std::string, int>> langValuesMapping;
	langValuesMapping["ja_JP"] = std::make_pair("日本語", PSP_SYSTEMPARAM_LANGUAGE_JAPANESE);
	langValuesMapping["en_US"] = std::make_pair("English",PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["fr_FR"] = std::make_pair("Français", PSP_SYSTEMPARAM_LANGUAGE_FRENCH);
	langValuesMapping["es_ES"] = std::make_pair("Castellano", PSP_SYSTEMPARAM_LANGUAGE_SPANISH);
	langValuesMapping["es_LA"] = std::make_pair("Latino", PSP_SYSTEMPARAM_LANGUAGE_SPANISH);
	langValuesMapping["de_DE"] = std::make_pair("Deutsch", PSP_SYSTEMPARAM_LANGUAGE_GERMAN);
	langValuesMapping["it_IT"] = std::make_pair("Italiano", PSP_SYSTEMPARAM_LANGUAGE_ITALIAN); 
	langValuesMapping["nl_NL"] = std::make_pair("Nederlands", PSP_SYSTEMPARAM_LANGUAGE_DUTCH);
	langValuesMapping["pt_PT"] = std::make_pair("Português", PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE);
	langValuesMapping["pt_BR"] = std::make_pair("Brasileiro", PSP_SYSTEMPARAM_LANGUAGE_PORTUGUESE);
	langValuesMapping["ru_RU"] = std::make_pair("Русский", PSP_SYSTEMPARAM_LANGUAGE_RUSSIAN);
	langValuesMapping["ko_KR"] = std::make_pair("한국어", PSP_SYSTEMPARAM_LANGUAGE_KOREAN);
	langValuesMapping["zh_TW"] = std::make_pair("繁體中文", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL);
	langValuesMapping["zh_CN"] = std::make_pair("简体中文", PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED);

	//langValuesMapping["ar_AE"] = std::make_pair("العربية", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["az_AZ"] = std::make_pair("Azeri", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["ca_ES"] = std::make_pair("Català", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["gr_EL"] = std::make_pair("ελληνικά", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["he_IL"] = std::make_pair("עברית", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["hu_HU"] = std::make_pair("Magyar", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["id_ID"] = std::make_pair("Indonesia", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["pl_PL"] = std::make_pair("Polski", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["ro_RO"] = std::make_pair("Român", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["sv_SE"] = std::make_pair("Svenska", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["tr_TR"] = std::make_pair("Türk", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	langValuesMapping["uk_UA"] = std::make_pair("Українська", PSP_SYSTEMPARAM_LANGUAGE_ENGLISH);
	return langValuesMapping;
}

void LanguageScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *s = GetI18NCategory("System");
	I18NCategory *g = GetI18NCategory("General");

	bool small = dp_xres < 790;

	if (!small) {
		ui_draw2d.SetFontScale(1.5f, 1.5f);
		ui_draw2d.DrawTextShadow(UBUNTU24, s->T("Language"), dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
		ui_draw2d.SetFontScale(1.0f, 1.0f);
	}

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres-10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	int buttonW = LARGE_BUTTON_WIDTH - 30;

	if (small) {
		buttonW = LARGE_BUTTON_WIDTH - 50;
	}

	VGrid vlang(20, small ? 20 : 100, dp_yres - 50, 10, 10);
	std::string text;
	
	
	for (size_t i = 0; i < langs_.size(); i++) {
		// Skip README
		if (langs_[i].name.find("README") != std::string::npos) {
			continue;
		}

		std::string code;
		size_t dot = langs_[i].name.find('.');
		if (dot != std::string::npos)
			code = langs_[i].name.substr(0, dot);

		std::string buttonTitle = langs_[i].name;

		std::map<std::string, std::pair<std::string, int>> langValuesMapping = GetLangValuesMapping();

		if (!code.empty()) {
			if(langValuesMapping.find(code) == langValuesMapping.end()) {
				// No title found, show locale code
				buttonTitle = code;
			} else {
				buttonTitle = langValuesMapping[code].first;
			}
		}

		if (UIButton(GEN_ID_LOOP(i), vlang, buttonW, 0, buttonTitle.c_str(), ALIGN_TOPLEFT)) {
			std::string oldLang = g_Config.languageIni;
			g_Config.languageIni = code;

			if (i18nrepo.LoadIni(g_Config.languageIni)) {
				// Dunno what else to do here.

				if(langValuesMapping.find(code) == langValuesMapping.end()) {
					//Fallback to English
					g_Config.ilanguage = PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
				} else {
					g_Config.ilanguage = langValuesMapping[code].second;
				}

				// After this, g and s are no longer valid. Let's return, some flicker is okay.
				g = GetI18NCategory("General");
				s = GetI18NCategory("System");
			} else {
				g_Config.languageIni = oldLang;
			}
		}
	}
	UIEnd();
}

void SystemScreen::render() {
	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);
	
	UIContext *ctx = screenManager()->getUIContext();
	UIFlush();
	GameInfo *ginfo = g_gameInfoCache.GetInfo(PSP_CoreParameter().fileToStart, true);

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		ui_draw2d.DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		ui_draw2d.Flush();
		ctx->RebindTexture();
	} 
	
	I18NCategory *s = GetI18NCategory("System");
	I18NCategory *g = GetI18NCategory("General");

	ui_draw2d.SetFontScale(1.5f, 1.5f);
	ui_draw2d.DrawTextShadow(UBUNTU24, s->T("System Settings"), dp_xres / 2, 10, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.SetFontScale(1.0f, 1.0f);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), LARGE_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	int x = 30;
	int y = 35;
	int stride = 40;
	int columnw = 400;

#ifdef IOS
	if(!isJailed)
		UICheckBox(GEN_ID, x, y += stride, s->T("Dynarec", "Dynarec (JIT)"), ALIGN_TOPLEFT, &g_Config.bJit);
	else
	{
		UICheckBox(GEN_ID, x, y += stride, s->T("DynarecisJailed", "Dynarec (JIT) - (Not jailbroken - JIT not available)"), ALIGN_TOPLEFT, &g_Config.bJit);
		g_Config.bJit = false;
	}
#else
	UICheckBox(GEN_ID, x, y += stride, s->T("Dynarec", "Dynarec (JIT)"), ALIGN_TOPLEFT, &g_Config.bJit);
#endif
	if (g_Config.bJit)
		UICheckBox(GEN_ID, x, y += stride, s->T("Fast Memory", "Fast Memory (unstable)"), ALIGN_TOPLEFT, &g_Config.bFastMemory);

	UICheckBox(GEN_ID, x, y += stride, s->T("Multithreaded (experimental)"), ALIGN_TOPLEFT, &g_Config.bSeparateCPUThread);

	bool LockCPUSpeed = g_Config.iLockedCPUSpeed != 0;
	UICheckBox(GEN_ID, x, y += stride, s->T("Unlock CPU Clock"), ALIGN_TOPLEFT, &LockCPUSpeed);
	if(LockCPUSpeed) {
		if(g_Config.iLockedCPUSpeed <= 0)
			g_Config.iLockedCPUSpeed = 222;
		char showCPUSpeed[256];
		sprintf(showCPUSpeed, "%s %d", s->T("Frequency :"), g_Config.iLockedCPUSpeed);
		ui_draw2d.DrawTextShadow(UBUNTU24, showCPUSpeed, x + 60, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
		HLinear hlinear1(x + 300, y, 20);
		if (UIButton(GEN_ID, hlinear1, 80, 0, s->T("Auto"), ALIGN_LEFT))
			g_Config.iLockedCPUSpeed = 222;
		if (UIButton(GEN_ID, hlinear1, 40, 0, s->T("-1"), ALIGN_LEFT))
			if (g_Config.iLockedCPUSpeed > 1)
				g_Config.iLockedCPUSpeed -= 1;
		if (UIButton(GEN_ID, hlinear1, 40, 0, s->T("+1"), ALIGN_LEFT))
			if (g_Config.iLockedCPUSpeed < 666)
				g_Config.iLockedCPUSpeed += 1;
		if (UIButton(GEN_ID, hlinear1, 60, 0, s->T("-10"), ALIGN_LEFT))
			if (g_Config.iLockedCPUSpeed > 1)
				g_Config.iLockedCPUSpeed -= 10;
		if (UIButton(GEN_ID, hlinear1, 60, 0, s->T("+10"), ALIGN_LEFT))
			if (g_Config.iLockedCPUSpeed < 666)
				g_Config.iLockedCPUSpeed += 10;
		y += 10;
	}
	else 
		g_Config.iLockedCPUSpeed = 0;
		
	UICheckBox(GEN_ID, x, y += stride, s->T("Enable Cheats"), ALIGN_TOPLEFT, &g_Config.bEnableCheats);
	if (g_Config.bEnableCheats) {
		HLinear hlinear1(x + 60, y += stride, 20);
		if (UIButton(GEN_ID, hlinear1, LARGE_BUTTON_WIDTH + 50, 0, s->T("Reload Cheats"), ALIGN_TOPLEFT)) 
			g_Config.bReloadCheats = true;
		y += 10;
	}
	y += 10;

	char lang[256];
	std::string type;
	switch (g_Config.ilanguage) {
				case 0:	type = "日本語";break;
				case 1: type = "English";break;
				case 2:	type = "Français";break;
				case 3: type = "Castellano";break;
				case 4:	type = "Deutsch";break;
				case 5: type = "Italiano";break;
				case 6:	type = "Nederlands";break;
				case 7: type = "Português";break;
				case 8: type = "Русский";break;
				case 9:	type = "한국어";break;
				case 10: type = "繁體中文";break;
				case 11: type = "简体中文";break;
	}

	sprintf(lang, "%s %s", s->T("System Language :"), type.c_str());
	ui_draw2d.DrawTextShadow(UBUNTU24, lang, x, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
	HLinear hlinear2(x + 400, y, 20);
	if (UIButton(GEN_ID, hlinear2, 220, 0, s->T("Language"), ALIGN_TOPLEFT)) {
		if (g_Config.bNewUI)
			screenManager()->push(new NewLanguageScreen());
		else
			screenManager()->push(new LanguageScreen());
	} 
	y+=20;

	const char *buttonPreferenceTitle;
	switch (g_Config.iButtonPreference) {
	case PSP_SYSTEMPARAM_BUTTON_CIRCLE:
		buttonPreferenceTitle = s->T("Button Pref : O to Confirm");
		break;
	case PSP_SYSTEMPARAM_BUTTON_CROSS:
	default:
		buttonPreferenceTitle = s->T("Button Pref : X to Confirm");
		break;
	}

	ui_draw2d.DrawTextShadow(UBUNTU24, buttonPreferenceTitle, x, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
	// 29 is the width of the checkbox, new UI will replace.
	HLinear hlinearButtonPref(x + 400, y, 10);
	if (UIButton(GEN_ID, hlinearButtonPref, 105, 0, s->T("Use O"), ALIGN_LEFT))
		g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CIRCLE;
	if (UIButton(GEN_ID, hlinearButtonPref, 105, 0, s->T("Use X"), ALIGN_LEFT))
		g_Config.iButtonPreference = PSP_SYSTEMPARAM_BUTTON_CROSS;
	y += 20;

	char recents[256];
	sprintf(recents, "%s %i", s->T("Max. No of Recents :"), g_Config.iMaxRecent);
	ui_draw2d.DrawTextShadow(UBUNTU24, recents, x, y += stride, 0xFFFFFFFF, ALIGN_LEFT);
	HLinear hlinear4(x + 400, y, 10);
	if (UIButton(GEN_ID, hlinear4, 50, 0, s->T("-1"), ALIGN_LEFT))
		if (g_Config.iMaxRecent > 4)
			g_Config.iMaxRecent -= 1;
	if (UIButton(GEN_ID, hlinear4, 50, 0, s->T("+1"), ALIGN_LEFT))
		if (g_Config.iMaxRecent < 40)
			g_Config.iMaxRecent += 1;
	if (UIButton(GEN_ID, hlinear4, 100, 0, s->T("Clear"), ALIGN_LEFT)) {
		g_Config.recentIsos.clear();
	}
	y += 20;

	// TODO: Come up with a way to display a keyboard for mobile users,
	// so until then, this is Windows/Desktop only.
#ifdef _WIN32
	char nickname[512];
	memset(nickname, 0, sizeof(nickname));

	sprintf(nickname, "%s %s", s->T("System Nickname:"), g_Config.sNickName.c_str());
	ui_draw2d.DrawTextShadow(UBUNTU24, nickname, x, y += stride, 0xFFFFFFFF, ALIGN_LEFT);

	HLinear hlinearNick(x + 400, y, 10);
	if(UIButton(GEN_ID, hlinearNick, 110, 0, s->T("Change"), ALIGN_LEFT)) {
		const size_t name_len = 256;
		
		char name[name_len];
		memset(name, 0, sizeof(name));

		if(host->InputBoxGetString("Enter a new PSP nickname", "PPSSPP", name, name_len))
			g_Config.sNickName.assign(name);
		else
			g_Config.sNickName.assign("PPSSPP");
	}
#endif
	/*
	bool time = g_Config.iTimeFormat > 0 ;
	UICheckBox(GEN_ID, x, y += stride, s->T("Time Format"), ALIGN_TOPLEFT, &time);
	if (time) {
			if (g_Config.iTimeFormat <= 0)
				g_Config.iTimeFormat = 1;

			char button[256];
			std::string type;
			switch (g_Config.iTimeFormat) {
				case 1:	type = "12HR";break;
				case 2: type = "24HR";break;
			}
			sprintf(button, "%s %s", s->T("Format :"), type.c_str());
			ui_draw2d.DrawTextShadow(UBUNTU24, button, x + 60, y += stride , 0xFFFFFFFF, ALIGN_LEFT);
			HLinear hlinear1(x + 280, y, 20);
			if (UIButton(GEN_ID, hlinear1, 80, 0, s->T("12HR"), ALIGN_LEFT))
					g_Config.iTimeFormat = 1;
			if (UIButton(GEN_ID, hlinear1, 80, 0, s->T("24HR"), ALIGN_LEFT))
					g_Config.iTimeFormat = 2;
			y += 10;
	} else
		g_Config.iTimeFormat = 0 ;

	bool date = g_Config.iDateFormat > 0;
	UICheckBox(GEN_ID, x, y += stride, s->T("Date Format"), ALIGN_TOPLEFT, &date);
	if (date) {
			if (g_Config.iDateFormat <= 0)
				g_Config.iDateFormat = 1;
			char button[256];
			std::string type;
			switch (g_Config.iDateFormat) {
				case 1: type = "YYYYMMDD";break;
				case 2: type = "MMDDYYYY";break;
				case 3:	type = "DDMMYYYY";break;
			}
			sprintf(button, "%s %s", s->T("Format :"), type.c_str());
			ui_draw2d.DrawTextShadow(UBUNTU24, button, x + 60, y += stride , 0xFFFFFFFF, ALIGN_LEFT);
			HLinear hlinear1(x + 350, y, 10);
			if (UIButton(GEN_ID, hlinear1, 70, 0, s->T("YMD"), ALIGN_LEFT))
					g_Config.iDateFormat = 1;
			if (UIButton(GEN_ID, hlinear1, 70, 0, s->T("MDY"), ALIGN_LEFT))
					g_Config.iDateFormat = 2;
			if (UIButton(GEN_ID, hlinear1, 70, 0, s->T("DMY"), ALIGN_LEFT))
					g_Config.iDateFormat = 3;
			y += 10;
	} else
		g_Config.iDateFormat = 0;
	*/
	
	
	UIEnd();
}


class FileListAdapter : public UIListAdapter {
public:
	FileListAdapter(const FileSelectScreenOptions &options, const std::vector<FileInfo> *items, UIContext *ctx) 
		: options_(options), items_(items), ctx_(ctx) {}
	virtual size_t getCount() const { return items_->size(); }
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const;

private:
	const FileSelectScreenOptions &options_;
	const std::vector<FileInfo> *items_;
	const UIContext *ctx_;
};

void FileListAdapter::drawItem(int item, int x, int y, int w, int h, bool selected) const
{
	int icon = -1;
	if ((*items_)[item].isDirectory) {
		icon = options_.folderIcon;
	} else {
		std::string extension = getFileExtension((*items_)[item].name);
		auto iter = options_.iconMapping.find(extension);
		if (iter != options_.iconMapping.end())
			icon = iter->second;
	}

	float scaled_h = ui_atlas.images[I_BUTTON].h;
	float scaled_w = scaled_h * (144.f / 80.f);

	int iconSpace = scaled_w + 10;
	ui_draw2d.DrawImage2GridH(selected ? I_BUTTON_SELECTED: I_BUTTON, x, y, x + w);
	ui_draw2d.DrawTextShadow(UBUNTU24, (*items_)[item].name.c_str(), x + UI_SPACE + iconSpace, y + 25, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);

	// This might create a texture so we must flush first.
	UIFlush();
	GameInfo *ginfo = 0;
	if (!(*items_)[item].isDirectory) {
		ginfo = g_gameInfoCache.GetInfo((*items_)[item].fullName, false);
		if (!ginfo) {
			ELOG("No ginfo :( %s", (*items_)[item].fullName.c_str());
		}
	}
	if (ginfo) {
		if (ginfo->iconTexture) {
			uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));
			UIFlush();
			ginfo->iconTexture->Bind(0);
			ui_draw2d.DrawTexRect(x + 10, y, x + 10 + scaled_w, y + scaled_h, 0, 0, 1, 1, color);
			ui_draw2d.Flush();
			ctx_->RebindTexture();
		}
	} else {
		if (icon != -1)
			ui_draw2d.DrawImage(icon, x + UI_SPACE, y + 25, 1.0f, 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_LEFT);
	}
}

FileSelectScreen::FileSelectScreen(const FileSelectScreenOptions &options) : options_(options) {
	currentDirectory_ = g_Config.currentDirectory;
#ifdef _WIN32
	// HACK
	// currentDirectory_ = "E:/PSP ISO/";
#endif
	updateListing();
}

void FileSelectScreen::key(const KeyInput &key) {
	if (key.flags & KEY_DOWN) {
		switch (key.keyCode) {
		case NKCODE_EXT_MOUSEWHEEL_UP:
			list_.scrollRelative(-50);
			break;
		case NKCODE_EXT_MOUSEWHEEL_DOWN:
			list_.scrollRelative(50);
			break;
		}
	}
}

void FileSelectScreen::updateListing() {
	listing_.clear();
	getFilesInDir(currentDirectory_.c_str(), &listing_, options_.filter);
	g_Config.currentDirectory = currentDirectory_;
	list_.contentChanged();
}

void FileSelectScreen::update(InputState &input_state) {
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->switchScreen(new MenuScreen());
	}
}

void FileSelectScreen::render() {
	FileListAdapter adapter(options_, &listing_, screenManager()->getUIContext());

	I18NCategory *g = GetI18NCategory("General");

	UIShader_Prepare();
	UIBegin(UIShader_Get());
	DrawBackground(1.0f);

	if (list_.Do(GEN_ID, 10, BUTTON_HEIGHT + 20, dp_xres-20, dp_yres - BUTTON_HEIGHT - 30, &adapter)) {
		if (listing_[list_.selected].isDirectory) {
			currentDirectory_ = listing_[list_.selected].fullName;
			ILOG("%s", currentDirectory_.c_str());
			updateListing();
			list_.selected = -1;
		} else {
			std::string boot_filename = listing_[list_.selected].fullName;
			ILOG("Selected: %i : %s", list_.selected, boot_filename.c_str());
			list_.selected = -1;
			g_Config.Save();
			UIEnd();
			screenManager()->switchScreen(new EmuScreen(boot_filename));
			return;
		}
	}

	ui_draw2d.DrawImageStretch(I_BUTTON, 0, 0, dp_xres, 70);

	if (UIButton(GEN_ID, Pos(10,10), SMALL_BUTTON_WIDTH, 0, g->T("Up"), ALIGN_TOPLEFT)) {
		currentDirectory_ = getDir(currentDirectory_);
		updateListing();
	}
	if (UIButton(GEN_ID, Pos(SMALL_BUTTON_WIDTH + 20,10), SMALL_BUTTON_WIDTH, 0, g->T("Home"), ALIGN_TOPLEFT)) {
		currentDirectory_ = g_Config.externalDirectory;
		updateListing();
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, currentDirectory_.c_str(), 30 + SMALL_BUTTON_WIDTH*2, 10 + 25, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);
	if (UIButton(GEN_ID, Pos(dp_xres - 10, 10), SMALL_BUTTON_WIDTH, 0, g->T("Back"), ALIGN_RIGHT)) {
		g_Config.Save();
		screenManager()->switchScreen(new MenuScreen());
	}

	UIEnd();
}
