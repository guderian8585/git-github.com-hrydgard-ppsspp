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

#include <cmath>
#include <string>

#include "base/display.h"
#include "base/logging.h"
#include "base/colorutil.h"
#include "base/timeutil.h"
#include "base/NativeApp.h"
#include "gfx_es2/glsl_program.h"
#include "input/input_state.h"
#include "math/curves.h"
#include "ui/ui.h"
#include "ui_atlas.h"
#include "util/random/rng.h"
#include "UIShader.h"

#include "../../Core/Config.h"
#include "../../Core/CoreParameter.h"

#include "MenuScreens.h"
#include "EmuScreen.h"

static const int symbols[4] = {
	I_CROSS,
	I_CIRCLE,
	I_SQUARE,
	I_TRIANGLE
};

static const uint32_t colors[4] = {
	/*
	0xFF6666FF, // blue
	0xFFFF6666, // red
	0xFFFF66FF, // pink
	0xFF66FF66, // green
	*/
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
	0xC0FFFFFF,
};

static void DrawBackground(float alpha) {
	static float xbase[100] = {0};
	static float ybase[100] = {0};
	if (xbase[0] == 0.0f) {
		GMRng rng;
		printf("%i %i AAAH\n", dp_xres, dp_yres);
		for (int i = 0; i < 100; i++) {
			xbase[i] = rng.F() * dp_xres;
			ybase[i] = rng.F() * dp_yres;
		}
	}
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

// For private alphas, etc.
void DrawWatermark() {
	// ui_draw2d.DrawTextShadow(UBUNTU24, "PRIVATE BUILD", dp_xres / 2, 10, 0xFF0000FF, ALIGN_HCENTER);
}

void LogoScreen::update(InputState &input_state) {
	frames_++;
	if (frames_ > 180 || input_state.pointer_down[0]) {
		if (bootFilename_.size()) {
			screenManager()->switchScreen(new EmuScreen(bootFilename_));
		} else {
			screenManager()->switchScreen(new MenuScreen());
		}
	}
}

void LogoScreen::render() {
	float t = (float)frames_ / 60.0f;

	float alpha = t;
	if (t > 1.0f) alpha = 1.0f;
	float alphaText = alpha;
	if (t > 2.0f) alphaText = 3.0f - t;

	UIShader_Prepare();
	UIBegin();
	DrawBackground(alpha);

	ui_draw2d.SetFontScale(1.5f,1.5f);
	ui_draw2d.DrawText(UBUNTU48, "PPSSPP", dp_xres / 2, dp_yres / 2 - 30, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	ui_draw2d.SetFontScale(1.0f,1.0f); 
	ui_draw2d.DrawText(UBUNTU24, "Created by Henrik Rydgard", dp_xres / 2, dp_yres / 2 + 40, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	ui_draw2d.DrawText(UBUNTU24, "Free Software under GPL 2.0", dp_xres / 2, dp_yres / 2 + 70, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	ui_draw2d.DrawText(UBUNTU24, "www.ppsspp.org", dp_xres / 2, dp_yres / 2 + 130, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	if (bootFilename_.size()) {
		ui_draw2d.DrawText(UBUNTU24, bootFilename_.c_str(), dp_xres / 2, dp_yres / 2 + 180, colorAlpha(0xFFFFFFFF, alphaText), ALIGN_CENTER);
	}

	DrawWatermark();
	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}


// ==================
//		Menu Screen
// ==================

void MenuScreen::update(InputState &input_state) {
	frames_++;
}

void MenuScreen::render() {
	UIShader_Prepare();
	UIBegin();
	DrawBackground(1.0f);

	double xoff = 150 - frames_ * frames_ * 0.4f;
	if (xoff < -20)
		xoff = -20;

	int w = LARGE_BUTTON_WIDTH + 40;

	ui_draw2d.DrawTextShadow(UBUNTU48, "PPSSPP", dp_xres + xoff - w/2, 80, 0xFFFFFFFF, ALIGN_HCENTER | ALIGN_BOTTOM);
	ui_draw2d.SetFontScale(0.7f, 0.7f);
	ui_draw2d.DrawTextShadow(UBUNTU24, "V0.4", dp_xres + xoff, 80, 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_BOTTOM);
	ui_draw2d.SetFontScale(1.0f, 1.0f);
	VLinear vlinear(dp_xres + xoff, 95, 20);


	if (UIButton(GEN_ID, vlinear, w, "Load...", ALIGN_RIGHT)) {
		FileSelectScreenOptions options;
		options.allowChooseDirectory = true;
		options.filter = "iso:cso:pbp:elf:prx:";
		options.folderIcon = I_ICON_FOLDER;
		options.iconMapping["iso"] = I_ICON_UMD;
		options.iconMapping["cso"] = I_ICON_UMD;
		options.iconMapping["pbp"] = I_ICON_EXE;
		options.iconMapping["elf"] = I_ICON_EXE;
		screenManager()->switchScreen(new FileSelectScreen(options));
		UIReset();
	}

	if (UIButton(GEN_ID, vlinear, w, "Settings", ALIGN_RIGHT)) {
		screenManager()->switchScreen(new SettingsScreen());
		UIReset();
	}

	if (UIButton(GEN_ID, vlinear, w, "Credits", ALIGN_RIGHT)) {
		screenManager()->switchScreen(new CreditsScreen());
		UIReset();
	}

	if (UIButton(GEN_ID, vlinear, w, "Exit", ALIGN_RIGHT)) {
		// TODO: Need a more elegant way to quit
		exit(0);
	}

	if (UIButton(GEN_ID, vlinear, w, "www.ppsspp.org", ALIGN_RIGHT)) {
		LaunchBrowser("http://www.ppsspp.org/");
	}

	DrawWatermark();

	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}


void InGameMenuScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->finishDialog(this, DR_CANCEL);
	}
}

void InGameMenuScreen::render() {
	UIShader_Prepare();
	UIBegin();
	DrawBackground(1.0f);

	ui_draw2d.DrawText(UBUNTU48, "Emulation Paused", dp_xres / 2, 30, 0xFFFFFFFF, ALIGN_HCENTER);

	VLinear vlinear(dp_xres - 10, 160, 20);
	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH, "Continue", ALIGN_RIGHT)) {
		screenManager()->finishDialog(this, DR_CANCEL);
	}

	if (UIButton(GEN_ID, vlinear, LARGE_BUTTON_WIDTH, "Return to Menu", ALIGN_RIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}
	DrawWatermark();
	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}

void SettingsScreen::update(InputState &input) {
	if (input.pad_buttons_down & PAD_BUTTON_BACK) {
		g_Config.Save();
		screenManager()->switchScreen(new MenuScreen());
	}
}

void SettingsScreen::render() {
	UIShader_Prepare();
	UIBegin();
	DrawBackground(1.0f);

	ui_draw2d.DrawText(UBUNTU48, "Settings", dp_xres / 2, 30, 0xFFFFFFFF, ALIGN_HCENTER);

	// VLinear vlinear(10, 80, 10);
	int x = 30;
	int y = 50;
	UICheckBox(GEN_ID, x, y += 50, "Enable Sound Emulation", ALIGN_TOPLEFT, &g_Config.bEnableSound);
	UICheckBox(GEN_ID, x, y += 50, "Buffered Rendering (may fix flicker)", ALIGN_TOPLEFT, &g_Config.bBufferedRendering);


	bool useFastInt = g_Config.iCpuCore == CPU_FASTINTERPRETER;
	UICheckBox(GEN_ID, x, y += 50, "Slightly faster interpreter (may crash)", ALIGN_TOPLEFT, &useFastInt);
	// ui_draw2d.DrawText(UBUNTU48, "much faster JIT coming later", x, y+=50, 0xcFFFFFFF, ALIGN_LEFT);
	UICheckBox(GEN_ID, x, y += 50, "On-screen Touch Controls", ALIGN_TOPLEFT, &g_Config.bShowTouchControls);
	if (g_Config.bShowTouchControls)
		UICheckBox(GEN_ID, x + 50, y += 50, "Show Analog Stick", ALIGN_TOPLEFT, &g_Config.bShowAnalogStick);
	g_Config.iCpuCore = useFastInt ? CPU_FASTINTERPRETER : CPU_INTERPRETER;
	// UICheckBox(GEN_ID, x, y += 50, "Draw raw framebuffer (for some homebrew)", ALIGN_TOPLEFT, &g_Config.bDisplayFramebuffer);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres-10), LARGE_BUTTON_WIDTH, "Back", ALIGN_RIGHT | ALIGN_BOTTOM)) {
		screenManager()->switchScreen(new MenuScreen());
	}

	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}


class FileListAdapter : public UIListAdapter {
public:
	FileListAdapter(const FileSelectScreenOptions &options, const std::vector<FileInfo> *items) : options_(options), items_(items) {}
	virtual size_t getCount() const { return items_->size(); }
	virtual void drawItem(int item, int x, int y, int w, int h, bool active) const;

private:
	const FileSelectScreenOptions &options_;
	const std::vector<FileInfo> *items_;
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
	int iconSpace = this->itemHeight(item);
	ui_draw2d.DrawImage2GridH(selected ? I_BUTTON_SELECTED: I_BUTTON, x, y, x + w);
	ui_draw2d.DrawTextShadow(UBUNTU24, (*items_)[item].name.c_str(), x + UI_SPACE + iconSpace, y + 25, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);
	if (icon != -1)
		ui_draw2d.DrawImage(icon, x + UI_SPACE, y + 25, 1.0f, 0xFFFFFFFF, ALIGN_VCENTER | ALIGN_LEFT);
}


FileSelectScreen::FileSelectScreen(const FileSelectScreenOptions &options) : options_(options) {
	currentDirectory_ = g_Config.currentDirectory;
	updateListing();
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
	FileListAdapter adapter(options_, &listing_);

	UIShader_Prepare();
	UIBegin();
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

			screenManager()->switchScreen(new EmuScreen(boot_filename));
		}
	}

	ui_draw2d.DrawImageStretch(I_BUTTON, 0, 0, dp_xres, 70);

	if (UIButton(GEN_ID, Pos(10,10), SMALL_BUTTON_WIDTH, "Up", ALIGN_TOPLEFT)) {
		currentDirectory_ = getDir(currentDirectory_);
		updateListing();
	}
	ui_draw2d.DrawTextShadow(UBUNTU24, currentDirectory_.c_str(), 20 + SMALL_BUTTON_WIDTH, 10 + 25, 0xFFFFFFFF, ALIGN_LEFT | ALIGN_VCENTER);

	/*
	if (UIButton(GEN_ID, Pos(dp_xres - 10, 10), SMALL_BUTTON_WIDTH, "Back", ALIGN_RIGHT)) {
		g_Config.Save();
		screenManager()->switchScreen(new MenuScreen());
	}*/
	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}

void CreditsScreen::update(InputState &input_state) {
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->switchScreen(new MenuScreen());
	}
	frames_++;
}

static const char *credits[] =
{
	"PPSSPP v0.4",
	"",
	"",
	"A fast and portable PSP emulator",
	"(well, an early prototype of that)",
	"",
	"Created by Henrik Rydgard",
	"",
	"Contributors:",
	"unknownbrackets",
	"tmaul",
	"orphis",
	"artart78",
	"ced2911",
	"soywiz",
	"kovensky",
	"xsacha",
	"",
	"Written in C++ for speed and portability",
	"",
	"",
	"Free tools used:",
#ifdef ANDROID
	"Android SDK + NDK",
#elif defined(BLACKBERRY)
	"Blackberry NDK",
#elif defined(__SYMBIAN32__)
	"Qt",
#else
	"SDL",
#endif
	"CMake",
	"freetype2",
	"zlib",
	"PSP SDK",
	"",
	"",
	"Check out the website:",
	"www.ppsspp.org",
	"compatibility lists, forums, and development info",
	"",
	"",
	"Also check out Dolphin, the best Wii/GC emu around:",
	"http://www.dolphin-emu.org",
	"",
	"",
	"PPSSPP is intended for educational purposes only.",
	"",
	"Please make sure that you own the rights to any games",
	"you play by owning the UMD or buying the digital",
	"download from the PSN store on your real PSP.",
	"",
	"",
	"PSP is a trademark by Sony, Inc.",
};

void CreditsScreen::render() {
	UIShader_Prepare();
	UIBegin();
	DrawBackground(1.0f);

	const int numItems = ARRAY_SIZE(credits);
	int itemHeight = 36;
	int totalHeight = numItems * itemHeight + dp_yres + 200;
	int y = dp_yres - (frames_ % totalHeight);
	for (int i = 0; i < numItems; i++) {
		float alpha = linearInOut(y+32, 64, dp_yres - 192, 64);
		if (alpha > 0.0f) {
			UIText(dp_xres/2, y, credits[i], whiteAlpha(alpha), ease(alpha), ALIGN_HCENTER);
		}
		y += itemHeight;
	}

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), 200, "Back", ALIGN_BOTTOMRIGHT)) {
		screenManager()->switchScreen(new MenuScreen());
	}

	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}

void ErrorScreen::update(InputState &input_state) {
	if (input_state.pad_buttons_down & PAD_BUTTON_BACK) {
		screenManager()->finishDialog(this, DR_OK);
	}
}

void ErrorScreen::render()
{
	UIShader_Prepare();
	UIBegin();
	DrawBackground(1.0f);

	ui_draw2d.DrawText(UBUNTU48, errorTitle_.c_str(), dp_xres / 2, 30, 0xFFFFFFFF, ALIGN_HCENTER);
	ui_draw2d.DrawText(UBUNTU24, errorMessage_.c_str(), 40, 120, 0xFFFFFFFF, ALIGN_LEFT);

	if (UIButton(GEN_ID, Pos(dp_xres - 10, dp_yres - 10), 200, "Back", ALIGN_BOTTOMRIGHT)) {
		screenManager()->finishDialog(this, DR_OK);
	}

	UIEnd();

	glsl_bind(UIShader_Get());
	ui_draw2d.Flush(UIShader_Get());
}
