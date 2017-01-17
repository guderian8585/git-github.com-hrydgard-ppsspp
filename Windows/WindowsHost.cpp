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

#include <algorithm>

// For shell links
#include "windows.h"
#include "winnls.h"
#include "shobjidl.h"
#include "objbase.h"
#include "objidl.h"
#include "shlguid.h"
#pragma warning(push)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#include "shlobj.h"
#pragma warning(pop)

// native stuff
#include "base/display.h"
#include "base/NativeApp.h"
#include "file/file_util.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "util/text/utf8.h"

#include "Common/StringUtils.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "Core/Debugger/SymbolMap.h"
#include "Windows/EmuThread.h"
#include "Windows/DSoundStream.h"
#include "Windows/WindowsHost.h"
#include "Windows/MainWindow.h"
#include "Windows/GPU/WindowsGLContext.h"
#include "Windows/GPU/WindowsVulkanContext.h"
#include "Windows/GPU/D3D9Context.h"
#include "Windows/GPU/D3D11Context.h"

#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"

#include "Windows/DinputDevice.h"
#include "Windows/XinputDevice.h"
#include "Windows/KeyboardDevice.h"

#include "Windows/main.h"
#include "UI/OnScreenDisplay.h"

static const int numCPUs = 1;

float g_mouseDeltaX = 0;
float g_mouseDeltaY = 0;

static BOOL PostDialogMessage(Dialog *dialog, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
	return PostMessage(dialog->GetDlgHandle(), message, wParam, lParam);
}

WindowsHost::WindowsHost(HINSTANCE hInstance, HWND mainWindow, HWND displayWindow)
	: gfx_(nullptr), hInstance_(hInstance),
		mainWindow_(mainWindow),
		displayWindow_(displayWindow)
{
	g_mouseDeltaX = 0;
	g_mouseDeltaY = 0;

	//add first XInput device to respond
	input.push_back(std::shared_ptr<InputDevice>(new XinputDevice()));
	//find all connected DInput devices of class GamePad
	size_t numDInputDevs = DinputDevice::getNumPads();
	for (size_t i = 0; i < numDInputDevs; i++) {
		input.push_back(std::shared_ptr<InputDevice>(new DinputDevice(static_cast<int>(i))));
	}
	keyboard = std::shared_ptr<KeyboardDevice>(new KeyboardDevice());
	input.push_back(keyboard);

	SetConsolePosition();
}

void WindowsHost::SetConsolePosition() {
	HWND console = GetConsoleWindow();
	if (console != NULL && g_Config.iConsoleWindowX != -1 && g_Config.iConsoleWindowY != -1)
		SetWindowPos(console, NULL, g_Config.iConsoleWindowX, g_Config.iConsoleWindowY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void WindowsHost::UpdateConsolePosition() {
	RECT rc;
	HWND console = GetConsoleWindow();
	if (console != NULL && GetWindowRect(console, &rc) && !IsIconic(console)) {
		g_Config.iConsoleWindowX = rc.left;
		g_Config.iConsoleWindowY = rc.top;
	}
}

bool WindowsHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	WindowsGraphicsContext *graphicsContext = nullptr;
	switch (g_Config.iGPUBackend) {
	case GPU_BACKEND_OPENGL:
		graphicsContext = new WindowsGLContext();
		break;
	case GPU_BACKEND_DIRECT3D9:
		graphicsContext = new D3D9Context();
		break;
	case GPU_BACKEND_DIRECT3D11:
		graphicsContext = new D3D11Context();
		break;
	case GPU_BACKEND_VULKAN:
		graphicsContext = new WindowsVulkanContext();
		break;
	default:
		return false;
	}

	if (graphicsContext->Init(hInstance_, displayWindow_, error_message)) {
		*ctx = graphicsContext;
		gfx_ = graphicsContext;
		return true;
	} else {
		delete graphicsContext;
		*ctx = nullptr;
		gfx_ = nullptr;
		return false;
	}
}

void WindowsHost::ShutdownGraphics() {
	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;
	PostMessage(mainWindow_, WM_CLOSE, 0, 0);
}

void WindowsHost::SetWindowTitle(const char *message) {
	std::wstring winTitle = ConvertUTF8ToWString(std::string("PPSSPP ") + PPSSPP_GIT_VERSION);
	if (message != nullptr) {
		winTitle.append(ConvertUTF8ToWString(" - "));
		winTitle.append(ConvertUTF8ToWString(message));
	}

	MainWindow::SetWindowTitle(winTitle.c_str());
	PostMessage(mainWindow_, MainWindow::WM_USER_WINDOW_TITLE_CHANGED, 0, 0);
}

void WindowsHost::InitSound() {
}

// UGLY!
extern WindowsAudioBackend *winAudioBackend;

void WindowsHost::UpdateSound() {
	if (winAudioBackend)
		winAudioBackend->Update();
}

void WindowsHost::ShutdownSound() {
}

void WindowsHost::UpdateUI() {
	PostMessage(mainWindow_, MainWindow::WM_USER_UPDATE_UI, 0, 0);
}

void WindowsHost::UpdateMemView() {
	for (int i = 0; i < numCPUs; i++)
		if (memoryWindow[i])
			PostDialogMessage(memoryWindow[i], WM_DEB_UPDATE);
}

void WindowsHost::UpdateDisassembly() {
	for (int i = 0; i < numCPUs; i++)
		if (disasmWindow[i])
			PostDialogMessage(disasmWindow[i], WM_DEB_UPDATE);
}

void WindowsHost::SetDebugMode(bool mode) {
	for (int i = 0; i < numCPUs; i++)
		if (disasmWindow[i])
			PostDialogMessage(disasmWindow[i], WM_DEB_SETDEBUGLPARAM, 0, (LPARAM)mode);
}

void WindowsHost::PollControllers(InputState &input_state) {
	bool doPad = true;
	for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
	{
		auto device = *iter;
		if (!doPad && device->IsPad())
			continue;
		if (device->UpdateState(input_state) == InputDevice::UPDATESTATE_SKIP_PAD)
			doPad = false;
	}

	g_mouseDeltaX *= 0.9f;
	g_mouseDeltaY *= 0.9f;

	// TODO: Tweak!
	float scaleFactor = g_dpi_scale * 0.01f;

	float mx = std::max(-1.0f, std::min(1.0f, g_mouseDeltaX * scaleFactor));
	float my = std::max(-1.0f, std::min(1.0f, g_mouseDeltaY * scaleFactor));
	AxisInput axisX, axisY;
	axisX.axisId = JOYSTICK_AXIS_MOUSE_REL_X;
	axisX.deviceId = DEVICE_ID_MOUSE;
	axisX.value = mx;
	axisY.axisId = JOYSTICK_AXIS_MOUSE_REL_Y;
	axisY.deviceId = DEVICE_ID_MOUSE;
	axisY.value = my;

	// Disabled for now as it makes the mapping dialog unusable!
	//if (fabsf(mx) > 0.1f) NativeAxis(axisX);
	//if (fabsf(my) > 0.1f) NativeAxis(axisY);
}

void WindowsHost::BootDone() {
	g_symbolMap->SortSymbols();
	SendMessage(mainWindow_, WM_USER + 1, 0, 0);

	SetDebugMode(!g_Config.bAutoRun);
	Core_EnableStepping(!g_Config.bAutoRun);
}

static std::string SymbolMapFilename(const char *currentFilename, char* ext) {
	FileInfo info;

	std::string result = currentFilename;

	// can't fail, definitely exists if it gets this far
	getFileInfo(currentFilename, &info);
	if (info.isDirectory) {
#ifdef _WIN32
		char* slash = "\\";
#else
		char* slash = "/";
#endif
		if (!endsWith(result,slash))
			result += slash;

		return result + ".ppsspp-symbols" + ext;
	} else {
		size_t dot = result.rfind('.');
		if (dot == result.npos)
			return result + ext;

		result.replace(dot, result.npos, ext);
		return result;
	}
}

bool WindowsHost::AttemptLoadSymbolMap() {
	bool result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(),".ppmap").c_str());
	// Load the old-style map file.
	if (!result1)
		result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(),".map").c_str());
	bool result2 = g_symbolMap->LoadNocashSym(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(),".sym").c_str());
	return result1 || result2;
}

void WindowsHost::SaveSymbolMap() {
	g_symbolMap->SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str(),".ppmap").c_str());
}

bool WindowsHost::IsDebuggingEnabled() {
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

// http://msdn.microsoft.com/en-us/library/aa969393.aspx
HRESULT CreateLink(LPCWSTR lpszPathObj, LPCWSTR lpszArguments, LPCWSTR lpszPathLink, LPCWSTR lpszDesc) { 
	HRESULT hres; 
	IShellLink* psl; 
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// Get a pointer to the IShellLink interface. It is assumed that CoInitialize
	// has already been called.
	hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl); 
	if (SUCCEEDED(hres)) { 
		IPersistFile* ppf; 

		// Set the path to the shortcut target and add the description. 
		psl->SetPath(lpszPathObj); 
		psl->SetArguments(lpszArguments);
		psl->SetDescription(lpszDesc); 

		// Query IShellLink for the IPersistFile interface, used for saving the 
		// shortcut in persistent storage. 
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 

		if (SUCCEEDED(hres)) { 
			// Save the link by calling IPersistFile::Save. 
			hres = ppf->Save(lpszPathLink, TRUE); 
			ppf->Release(); 
		} 
		psl->Release(); 
	}
	CoUninitialize();

	return hres; 
}

bool WindowsHost::CanCreateShortcut() { 
	return false;  // Turn on when below function fixed
}

bool WindowsHost::CreateDesktopShortcut(std::string argumentPath, std::string gameTitle) {
	// TODO: not working correctly
	return false;


	// Get the desktop folder
	wchar_t *pathbuf = new wchar_t[MAX_PATH + gameTitle.size() + 100];
	SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, pathbuf);
	
	// Sanitize the game title for banned characters.
	const char bannedChars[] = "<>:\"/\\|?*";
	for (size_t i = 0; i < gameTitle.size(); i++) {
		for (size_t c = 0; c < strlen(bannedChars); c++) {
			if (gameTitle[i] == bannedChars[c]) {
				gameTitle[i] = '_';
				break;
			}
		}
	}

	wcscat(pathbuf, L"\\");
	wcscat(pathbuf, ConvertUTF8ToWString(gameTitle).c_str());

	wchar_t module[MAX_PATH];
	GetModuleFileName(NULL, module, MAX_PATH);

	CreateLink(module, ConvertUTF8ToWString(argumentPath).c_str(), pathbuf, ConvertUTF8ToWString(gameTitle).c_str());

	delete [] pathbuf;
	return false;
}

void WindowsHost::GoFullscreen(bool viewFullscreen) {
	MainWindow::SendToggleFullscreen(viewFullscreen);
}

void WindowsHost::ToggleDebugConsoleVisibility() {
	MainWindow::ToggleDebugConsoleVisibility();
}

void WindowsHost::NotifyUserMessage(const std::string &message, float duration, u32 color, const char *id) {
	osm.Show(message, duration, color, -1, true, id);
}
