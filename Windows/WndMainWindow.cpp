// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include <windows.h>
#include <tchar.h>

#include "base/NativeApp.h"
#include "Globals.h"

#include "shellapi.h"
#include "commctrl.h"

#include "input/input_state.h"
#include "Core/Debugger/SymbolMap.h"
#include "Windows/OpenGLBase.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "main.h"

#include "Core/Core.h"
#include "Core/MemMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Windows/EmuThread.h"

#include "resource.h"

#include "Windows/WndMainWindow.h"
#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "native/image/png_load.h"
#include "GPU/GLES/TextureScaler.h"

#ifdef THEMES
#include "XPTheme.h"
#endif

#define ENABLE_TOUCH 0

BOOL g_bFullScreen = FALSE;
static RECT g_normalRC = {0};

extern InputState input_state;
#define TIMER_CURSORUPDATE 1
#define CURSORUPDATE_INTERVAL_MS 50
extern unsigned short analog_ctrl_map[];
extern unsigned int key_pad_map[];
extern const char * getVirtualKeyName(unsigned char key);
extern bool saveControlsToFile();
extern bool loadControlsFromFile();

namespace MainWindow
{
	HWND hwndMain;
	HWND hwndDisplay;
	HWND hwndGameList;
	static HMENU menu;

	static HINSTANCE hInst;

	//W32Util::LayeredWindow *layer;
#define MAX_LOADSTRING 100
	const TCHAR *szTitle = TEXT("PPSSPP");
	const TCHAR *szWindowClass = TEXT("PPSSPPWnd");
	const TCHAR *szDisplayClass = TEXT("PPSSPPDisplay");

	// Forward declarations of functions included in this code module:
	LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK DisplayProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK About(HWND, UINT, WPARAM, LPARAM);
	LRESULT CALLBACK Controls(HWND, UINT, WPARAM, LPARAM);

	HWND GetHWND()
	{
		return hwndMain;
	}

	HWND GetDisplayHWND()
	{
		return hwndDisplay;
	}

	void Init(HINSTANCE hInstance)
	{
#ifdef THEMES
		WTL::CTheme::IsThemingSupported();
#endif
		//Register classes
		WNDCLASSEX wcex;
		wcex.cbSize = sizeof(WNDCLASSEX); 
		wcex.style			= CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc	= (WNDPROC)WndProc;
		wcex.cbClsExtra		= 0;
		wcex.cbWndExtra		= 0;
		wcex.hInstance		= hInstance;
		wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_PPSSPP); 
		wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground	= (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName	= (LPCSTR)IDR_MENU1;
		wcex.lpszClassName	= szWindowClass;
		wcex.hIconSm		= (HICON)LoadImage(hInstance, (LPCTSTR)IDI_PPSSPP, IMAGE_ICON, 16,16,LR_SHARED);
		RegisterClassEx(&wcex);

		wcex.style = CS_HREDRAW | CS_VREDRAW;;
		wcex.lpfnWndProc = (WNDPROC)DisplayProc;
		wcex.hIcon = 0;
		wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wcex.lpszMenuName = 0;
		wcex.lpszClassName = szDisplayClass;
		wcex.hIconSm = 0;
		RegisterClassEx(&wcex);
	}

	void GetWindowRectAtZoom(int zoom, RECT &rcInner, RECT &rcOuter) {
		// GetWindowRect(hwndMain, &rcInner);
		rcInner.left = 0;
		rcInner.top = 0;

		rcInner.right=480*zoom;//+client edge
		rcInner.bottom=272*zoom; //+client edge

		rcOuter=rcInner;
		AdjustWindowRect(&rcOuter, WS_OVERLAPPEDWINDOW, TRUE);
		rcOuter.right += g_Config.iWindowX - rcOuter.left;
		rcOuter.bottom += g_Config.iWindowY - rcOuter.top;
		rcOuter.left = g_Config.iWindowX;
		rcOuter.top = g_Config.iWindowY;
	}

	void SavePosition() {
		WINDOWPLACEMENT placement;
		GetWindowPlacement(hwndMain, &placement);
		if (placement.showCmd == SW_SHOWNORMAL) {
			RECT rc;
			GetWindowRect(hwndMain, &rc);
			g_Config.iWindowX = rc.left;
			g_Config.iWindowY = rc.top;
		}
	}

	void ResizeDisplay(bool noWindowMovement = false) {
		RECT rc;
		GetClientRect(hwndMain, &rc);
		if (!noWindowMovement) {

			if ((rc.right - rc.left) == PSP_CoreParameter().pixelWidth &&
				(rc.bottom - rc.top) == PSP_CoreParameter().pixelHeight)
				return;
			PSP_CoreParameter().pixelWidth = rc.right - rc.left;
			PSP_CoreParameter().pixelHeight = rc.bottom - rc.top;
			MoveWindow(hwndDisplay, 0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, TRUE);
		}

		// round up to a zoom factor for the render size.
		int zoom = (rc.right - rc.left + 479) / 480;
		if (g_Config.SSAntiAliasing) zoom *= 2;
		PSP_CoreParameter().renderWidth = 480 * zoom;
		PSP_CoreParameter().renderHeight = 272 * zoom;
		PSP_CoreParameter().outputWidth = 480 * zoom;
		PSP_CoreParameter().outputHeight = 272 * zoom;

		if (gpu)
			gpu->Resized();
	}

	void SetZoom(float zoom) {
		if (zoom < 5)
			g_Config.iWindowZoom = (int) zoom;
		RECT rc, rcOuter;
		GetWindowRectAtZoom((int) zoom, rc, rcOuter);
		MoveWindow(hwndMain, rcOuter.left, rcOuter.top, rcOuter.right - rcOuter.left, rcOuter.bottom - rcOuter.top, TRUE);
		ResizeDisplay();
	}

	void setTexScalingLevel(int num) {
		g_Config.iTexScalingLevel = num;
		if(gpu) gpu->ClearCacheNextFrame();
	}
	void setTexScalingType(int num) {
		g_Config.iTexScalingType = num;
		if(gpu) gpu->ClearCacheNextFrame();
	}

	BOOL Show(HINSTANCE hInstance, int nCmdShow)
	{
		hInst = hInstance; // Store instance handle in our global variable

		int zoom = g_Config.iWindowZoom;
		if (zoom < 1) zoom = 1;
		if (zoom > 4) zoom = 4;
		
		RECT rc,rcOrig;
		GetWindowRectAtZoom(zoom, rcOrig, rc);

		u32 style = WS_OVERLAPPEDWINDOW;

		hwndMain = CreateWindowEx(0,szWindowClass, "", style,
			rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, NULL, NULL, hInstance, NULL);
		SetTimer(hwndMain, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);
		SetPlaying(0);
		if (!hwndMain)
			return FALSE;

		menu = GetMenu(hwndMain);
#ifdef FINAL
		RemoveMenu(menu,2,MF_BYPOSITION);
		RemoveMenu(menu,2,MF_BYPOSITION);
#endif
		MENUINFO info;
		ZeroMemory(&info,sizeof(MENUINFO));
		info.cbSize = sizeof(MENUINFO);
		info.cyMax = 0;
		info.dwStyle = MNS_CHECKORBMP;
		info.fMask = MIM_STYLE;
		for (int i = 0; i < GetMenuItemCount(menu); i++)
		{
			SetMenuInfo(GetSubMenu(menu,i),&info);
		}

		hwndDisplay = CreateWindowEx(0,szDisplayClass,TEXT(""),
			WS_CHILD|WS_VISIBLE,
			0,0,/*rcOrig.left,rcOrig.top,*/rcOrig.right-rcOrig.left,rcOrig.bottom-rcOrig.top,hwndMain,0,hInstance,0);

		ShowWindow(hwndMain, nCmdShow);
		//accept dragged files
		DragAcceptFiles(hwndMain, TRUE);

#if ENABLE_TOUCH
		RegisterTouchWindow(hwndDisplay, TWF_WANTPALM);
#endif

		SetFocus(hwndMain);
		SetFocus(hwndDisplay);

		return TRUE;
	}

	void BrowseAndBoot(std::string defaultPath)
	{
		std::string fn;
		std::string filter = "";

		filter += "PSP";
		filter += "|";
		filter += "*.pbp;*.elf;*.iso;*.cso;*.prx";
		filter += "|";
		filter += "|";
		for (int i=0; i<(int)filter.length(); i++)
		{
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		if (W32Util::BrowseForFileName(true, GetHWND(), "Load File", defaultPath.size() ? defaultPath.c_str() : 0, filter.c_str(),"*.pbp;*.elf;*.iso;*.cso;",fn))
		{
			// decode the filename with fullpath
			std::string fullpath = fn;
			char drive[MAX_PATH];
			char dir[MAX_PATH];
			char fname[MAX_PATH];
			char ext[MAX_PATH];
			_splitpath(fullpath.c_str(), drive, dir, fname, ext);

			std::string executable = std::string(drive) + std::string(dir) + std::string(fname) + std::string(ext);
			NativeMessageReceived("boot", executable.c_str());
		}
	}

	LRESULT CALLBACK DisplayProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message) 
		{
		case WM_ACTIVATE:
			break;
		case WM_SETFOCUS:
			break;
		case WM_SIZE:
			break;

		case WM_ERASEBKGND:
	  	return DefWindowProc(hWnd, message, wParam, lParam);

		case WM_LBUTTONDOWN:
			{
				lock_guard guard(input_state.lock);
				input_state.mouse_valid = true;
				input_state.pointer_down[0] = true;
				input_state.pointer_x[0] = GET_X_LPARAM(lParam); 
				input_state.pointer_y[0] = GET_Y_LPARAM(lParam);

				if (g_Config.iWindowZoom == 1)
				{
					input_state.pointer_x[0] *= 2;
					input_state.pointer_y[0] *= 2;
				}
			}
			break;

		case WM_MOUSEMOVE:
			{
				lock_guard guard(input_state.lock);
				input_state.pointer_x[0] = GET_X_LPARAM(lParam); 
				input_state.pointer_y[0] = GET_Y_LPARAM(lParam);

				if (g_Config.iWindowZoom == 1)
				{
					input_state.pointer_x[0] *= 2;
					input_state.pointer_y[0] *= 2;
				}
			}
			break;

		case WM_LBUTTONUP:
			{
				lock_guard guard(input_state.lock);
				input_state.pointer_down[0] = false;
				input_state.pointer_x[0] = GET_X_LPARAM(lParam); 
				input_state.pointer_y[0] = GET_Y_LPARAM(lParam);

				if (g_Config.iWindowZoom == 1)
				{
					input_state.pointer_x[0] *= 2;
					input_state.pointer_y[0] *= 2;
				}
			}
			break;

		case WM_TOUCH:
			{
				// TODO: Enabling this section will probably break things on Windows XP.
				// We probably need to manually fetch pointers to GetTouchInputInfo and CloseTouchInputHandle.
#if ENABLE_TOUCH
				UINT inputCount = LOWORD(wParam);
				TOUCHINPUT *inputs = new TOUCHINPUT[inputCount];
				if (GetTouchInputInfo((HTOUCHINPUT)lParam,
					inputCount,
					inputs,
					sizeof(TOUCHINPUT)))
				{
					for (int i = 0; i < inputCount; i++) {
						// TODO: process inputs here!

					}

					if (!CloseTouchInputHandle((HTOUCHINPUT)lParam))
					{
						// error handling
					}
				}
				else
				{
					// GetLastError() and error handling
				}
				delete [] inputs;
				return DefWindowProc(hWnd, message, wParam, lParam);
#endif
			}


		case WM_PAINT:
			return DefWindowProc(hWnd, message, wParam, lParam);
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		int wmId, wmEvent;
		std::string fn;

		switch (message) 
		{
		case WM_CREATE:
			loadControlsFromFile();
			break;

		case WM_MOVE:
			SavePosition();
			ResizeDisplay();
			break;

		case WM_SIZE:
			SavePosition();
			ResizeDisplay();
			break;

		case WM_TIMER:
			// Hack: Take the opportunity to also show/hide the mouse cursor in fullscreen mode.
			if (g_bFullScreen && globalUIState == UISTATE_INGAME) {
				ShowCursor(FALSE);
			} else {
				ShowCursor(TRUE);
				SetCursor(LoadCursor(NULL, IDC_ARROW));
			}
			SetTimer(hWnd, TIMER_CURSORUPDATE, CURSORUPDATE_INTERVAL_MS, 0);
			return 0;

		case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
			case ID_FILE_LOAD:
				BrowseAndBoot("");
				break;

			case ID_FILE_LOAD_MEMSTICK:
				{
					std::string memStickDir, flash0dir;
					GetSysDirectories(memStickDir, flash0dir);
					memStickDir += "PSP\\GAME\\";
					BrowseAndBoot(memStickDir);
				}
				break;

			case ID_FILE_REFRESHGAMELIST:
				break;

			case ID_FILE_MEMSTICK:
				{
					std::string memStickDir, flash0dir;
					GetSysDirectories(memStickDir, flash0dir);
					ShellExecuteA(NULL, "open", memStickDir.c_str(), 0, 0, SW_SHOW);
				}
				break;

			case ID_EMULATION_RUN:
				if (Core_IsStepping()) {
					Core_EnableStepping(false);
				} else {
					NativeMessageReceived("run", "");
				}
				if (disasmWindow[0])
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_GO, 0);
				break;

			case ID_EMULATION_STOP:
				if (memoryWindow[0]) {
					SendMessage(memoryWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);
				}
				if (disasmWindow[0]) {
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);
				}
				if (Core_IsStepping()) {
					Core_EnableStepping(false);
				}
				NativeMessageReceived("stop", "");
				SetPlaying(0);
				Update();
				break;

			case ID_EMULATION_PAUSE:
				if (disasmWindow[0])
				{
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOP, 0);
				} else if (globalUIState == UISTATE_INGAME) {
					Core_EnableStepping(true);
				}
				break;

			case ID_EMULATION_RESET:
				NativeMessageReceived("reset", "");
				break;

			case ID_EMULATION_SPEEDLIMIT:
				g_Config.bSpeedLimit = !g_Config.bSpeedLimit;
				break;

			case ID_FILE_LOADSTATEFILE:
				if (W32Util::BrowseForFileName(true, hWnd, "Load state",0,"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0","ppst",fn))
				{
					SetCursor(LoadCursor(0,IDC_WAIT));
					SaveState::Load(fn, SaveStateActionFinished);
				}
				break;

			case ID_FILE_SAVESTATEFILE:
				if (W32Util::BrowseForFileName(false, hWnd, "Save state",0,"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0","ppst",fn))
				{
					SetCursor(LoadCursor(0,IDC_WAIT));
					SaveState::Save(fn, SaveStateActionFinished);
				}
				break;

			// TODO: Add UI for multiple slots

			case ID_FILE_QUICKLOADSTATE:
				SetCursor(LoadCursor(0,IDC_WAIT));
				SaveState::LoadSlot(0, SaveStateActionFinished);
				break;

			case ID_FILE_QUICKSAVESTATE:
				SetCursor(LoadCursor(0,IDC_WAIT));
				SaveState::SaveSlot(0, SaveStateActionFinished);
				break;

			case ID_OPTIONS_SCREEN1X:
				SetZoom(1);
				break;
			case ID_OPTIONS_SCREEN2X:
				SetZoom(2);
				break;
			case ID_OPTIONS_SCREEN3X:
				SetZoom(3);
				break;
			case ID_OPTIONS_SCREEN4X:
				SetZoom(4);
				break;

			case ID_OPTIONS_MIPMAP:
				g_Config.bMipMap = !g_Config.bMipMap;
				break;

			case ID_TEXTURESCALING_OFF:
				setTexScalingLevel(1);
				break;
			case ID_TEXTURESCALING_2X:
				setTexScalingLevel(2);
				break;
			case ID_TEXTURESCALING_3X:
				setTexScalingLevel(3);
				break;
			case ID_TEXTURESCALING_4X:
				setTexScalingLevel(4);
				break;
			case ID_TEXTURESCALING_5X:
				setTexScalingLevel(5);
				break;

			case ID_TEXTURESCALING_XBRZ:
				setTexScalingType(TextureScaler::XBRZ);
				break;
			case ID_TEXTURESCALING_HYBRID:
				setTexScalingType(TextureScaler::HYBRID);
				break;
			case ID_TEXTURESCALING_BICUBIC:
				setTexScalingType(TextureScaler::BICUBIC);
				break;
			case ID_TEXTURESCALING_HYBRID_BICUBIC:
				setTexScalingType(TextureScaler::HYBRID_BICUBIC);
				break;

			case ID_TEXTURESCALING_DEPOSTERIZE:
				g_Config.bTexDeposterize = !g_Config.bTexDeposterize;
				if(gpu) gpu->ClearCacheNextFrame();
				break;

			case ID_OPTIONS_BUFFEREDRENDERING:
				g_Config.bBufferedRendering = !g_Config.bBufferedRendering;
				if (gpu)
					gpu->Resized();  // easy way to force a clear...
				break;

			case ID_OPTIONS_SHOWDEBUGSTATISTICS:
				g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
				break;

			case ID_OPTIONS_HARDWARETRANSFORM:
				g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
				break;

			case ID_OPTIONS_STRETCHDISPLAY:
				g_Config.bStretchToDisplay = !g_Config.bStretchToDisplay;
				if (gpu)
					gpu->Resized();  // easy way to force a clear...
				break;

			case ID_OPTIONS_FRAMESKIP:
				g_Config.iFrameSkip = g_Config.iFrameSkip == 0 ? 1 : 0;
				break;

			case ID_OPTIONS_USEMEDIAENGINE:
				g_Config.bUseMediaEngine = !g_Config.bUseMediaEngine;
				break;

			case ID_FILE_EXIT:
				DestroyWindow(hWnd);
				break;

			case ID_CPU_DYNAREC:
				g_Config.bJit = true;
				break;	

			case ID_CPU_INTERPRETER:
				g_Config.bJit = false;
				break;

			case ID_EMULATION_RUNONLOAD:
				g_Config.bAutoRun = !g_Config.bAutoRun;
				break;

			case ID_DEBUG_DUMPNEXTFRAME:
				if (gpu)
					gpu->DumpNextFrame();
				break;

			case ID_DEBUG_LOADMAPFILE:
				if (W32Util::BrowseForFileName(true, hWnd, "Load .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn)) {
					symbolMap.LoadSymbolMap(fn.c_str());
//					HLE_PatchFunctions();
					if (disasmWindow[0])
						disasmWindow[0]->NotifyMapLoaded();
					if (memoryWindow[0])
						memoryWindow[0]->NotifyMapLoaded();
				}
				break;
			case ID_DEBUG_SAVEMAPFILE:
				if (W32Util::BrowseForFileName(false, hWnd, "Save .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn))
					symbolMap.SaveSymbolMap(fn.c_str());
				break;
		
			case ID_DEBUG_RESETSYMBOLTABLE:
				symbolMap.ResetSymbolMap();
				for (int i=0; i<numCPUs; i++)
					if (disasmWindow[i])
						disasmWindow[i]->NotifyMapLoaded();
				for (int i=0; i<numCPUs; i++)
					if (memoryWindow[i])
						memoryWindow[i]->NotifyMapLoaded();
				break;
			case ID_DEBUG_DISASSEMBLY:
				if (disasmWindow[0])
					disasmWindow[0]->Show(true);
				break;
			case ID_DEBUG_MEMORYVIEW:
				if (memoryWindow[0])
					memoryWindow[0]->Show(true);
				break;
			case ID_DEBUG_LOG:
				LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
				break;

			case ID_OPTIONS_IGNOREILLEGALREADS:
				g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
				break;

			case ID_OPTIONS_FULLSCREEN:
				g_Config.bFullScreen = !g_Config.bFullScreen ;
				if(g_bFullScreen) {
					_ViewNormal(hWnd); 
				} else {
					_ViewFullScreen(hWnd);
				}
				break;

			case ID_OPTIONS_WIREFRAME:
				g_Config.bDrawWireframe = !g_Config.bDrawWireframe;
				break;
			case ID_OPTIONS_VERTEXCACHE:
				g_Config.bVertexCache = !g_Config.bVertexCache;
				break;
			case ID_OPTIONS_SHOWFPS:
				g_Config.bShowFPSCounter = !g_Config.bShowFPSCounter;
				break;
			case ID_OPTIONS_DISPLAYRAWFRAMEBUFFER:
				g_Config.bDisplayFramebuffer = !g_Config.bDisplayFramebuffer;
				break;
			case ID_OPTIONS_FASTMEMORY:
				g_Config.bFastMemory = !g_Config.bFastMemory;
				break;
			case ID_OPTIONS_USEVBO:
				g_Config.bUseVBO = !g_Config.bUseVBO;
				break;
			case ID_OPTIONS_LINEARFILTERING:
				g_Config.bLinearFiltering = !g_Config.bLinearFiltering;
				break;
			case ID_OPTIONS_SIMPLE2XSSAA:
				g_Config.SSAntiAliasing = !g_Config.SSAntiAliasing;
				ResizeDisplay(true);
				break;
			case ID_OPTIONS_CONTROLS:
				DialogManager::EnableAll(FALSE);
				DialogBox(hInst, (LPCTSTR)IDD_CONTROLS, hWnd, (DLGPROC)Controls);
				DialogManager::EnableAll(TRUE);
				break;
			case ID_EMULATION_SOUND:
				g_Config.bEnableSound = !g_Config.bEnableSound;
				break;
      			case ID_HELP_OPENWEBSITE:
				ShellExecute(NULL, "open", "http://www.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
        			break;

      			case ID_HELP_ABOUT:
				DialogManager::EnableAll(FALSE);
				DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				DialogManager::EnableAll(TRUE);
				break;

			default:
				{
					MessageBox(hwndMain,"Unimplemented","Sorry",0);
				}
				break;
			}
			break;
		case WM_KEYDOWN:
			{
				static int mojs=0;
				mojs ^= 1;
				//SetSkinMode(mojs);
			}
			return 0;
		case WM_DROPFILES:
			{
				HDROP hdrop = (HDROP)wParam;
				int count = DragQueryFile(hdrop,0xFFFFFFFF,0,0);
				if (count != 1)
				{
					MessageBox(hwndMain,"You can only load one file at a time","Error",MB_ICONINFORMATION);
				}
				else
				{
					TCHAR filename[512];
					DragQueryFile(hdrop,0,filename,512);
					TCHAR *type = filename+_tcslen(filename)-3;

					SendMessage(hWnd, WM_COMMAND, ID_EMULATION_STOP, 0);
					// Ugly, need to wait for the stop message to process in the EmuThread.
					Sleep(20);
					
					MainWindow::SetPlaying(filename);
					MainWindow::Update();

					NativeMessageReceived("boot", filename);
				}
			}
			break;

		case WM_CLOSE:
			Core_Stop();
			Core_WaitInactive(200);
			EmuThread_Stop();

			/*
			if (g_Config.bConfirmOnQuit && CCore::IsRunning())
			{
				if (IDNO==MessageBox(hwndMain,"A game is in progress. Are you sure you want to exit?","Are you sure?",MB_YESNO|MB_ICONQUESTION))
					return 1;//or 1?
				else
					return DefWindowProc(hWnd,message,wParam,lParam);
				break;
			}
			else
			*/
			return DefWindowProc(hWnd,message,wParam,lParam);

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		case WM_USER+1:
			if (disasmWindow[0])
				SendMessage(disasmWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);
			if (memoryWindow[0])
				SendMessage(memoryWindow[0]->GetDlgHandle(), WM_CLOSE, 0, 0);

			disasmWindow[0] = new CDisasm(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(disasmWindow[0]);
			disasmWindow[0]->Show(g_Config.bShowDebuggerOnLoad);
			if (g_Config.bFullScreen)
				_ViewFullScreen(hWnd);
			memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
			DialogManager::AddDlg(memoryWindow[0]);
			if (disasmWindow[0])
				disasmWindow[0]->NotifyMapLoaded();
			if (memoryWindow[0])
				memoryWindow[0]->NotifyMapLoaded();

			SetForegroundWindow(hwndMain);
			break;


		case WM_MENUSELECT:
			// This happens when a menu drops down, so this is the only place
			// we need to call UpdateMenus.
			UpdateMenus();
			break;

		// Turn off the screensaver.
		// Note that if there's a screensaver password, this simple method
		// doesn't work on Vista or higher.
		case WM_SYSCOMMAND:
			{
				switch (wParam)
				{
				case SC_SCREENSAVE:  
					return 0;
				case SC_MONITORPOWER:
					return 0;      
				}
				return DefWindowProc(hWnd, message, wParam, lParam);
			}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	void UpdateMenus()
	{
		HMENU menu = GetMenu(GetHWND());
#define CHECKITEM(item,value) 	CheckMenuItem(menu,item,MF_BYCOMMAND | ((value) ? MF_CHECKED : MF_UNCHECKED));

		CHECKITEM(ID_EMULATION_SPEEDLIMIT,g_Config.bSpeedLimit);
//		CHECK(ID_OPTIONS_ENABLEFRAMEBUFFER,g_Config.bEnableFrameBuffer);
//		CHECK(ID_OPTIONS_EMULATESYSCALL,g_bEmulateSyscall);
		CHECKITEM(ID_OPTIONS_DISPLAYRAWFRAMEBUFFER, g_Config.bDisplayFramebuffer);
		CHECKITEM(ID_OPTIONS_IGNOREILLEGALREADS,g_Config.bIgnoreBadMemAccess);
		CHECKITEM(ID_CPU_INTERPRETER,g_Config.bJit == false);
		CHECKITEM(ID_CPU_DYNAREC,g_Config.bJit == true);
		CHECKITEM(ID_OPTIONS_BUFFEREDRENDERING, g_Config.bBufferedRendering);
		CHECKITEM(ID_OPTIONS_SHOWDEBUGSTATISTICS, g_Config.bShowDebugStats);
		CHECKITEM(ID_OPTIONS_WIREFRAME, g_Config.bDrawWireframe);
		CHECKITEM(ID_OPTIONS_HARDWARETRANSFORM, g_Config.bHardwareTransform);
		CHECKITEM(ID_OPTIONS_FASTMEMORY, g_Config.bFastMemory);
		CHECKITEM(ID_OPTIONS_LINEARFILTERING, g_Config.bLinearFiltering);
		CHECKITEM(ID_OPTIONS_SIMPLE2XSSAA, g_Config.SSAntiAliasing);
		CHECKITEM(ID_OPTIONS_STRETCHDISPLAY, g_Config.bStretchToDisplay);
		CHECKITEM(ID_EMULATION_RUNONLOAD, g_Config.bAutoRun);
		CHECKITEM(ID_OPTIONS_USEVBO, g_Config.bUseVBO);
		CHECKITEM(ID_OPTIONS_VERTEXCACHE, g_Config.bVertexCache);
		CHECKITEM(ID_OPTIONS_SHOWFPS, g_Config.bShowFPSCounter);
		CHECKITEM(ID_OPTIONS_FRAMESKIP, g_Config.iFrameSkip != 0);
		CHECKITEM(ID_OPTIONS_USEMEDIAENGINE, g_Config.bUseMediaEngine);
		CHECKITEM(ID_OPTIONS_MIPMAP, g_Config.bMipMap);
		CHECKITEM(ID_EMULATION_SOUND, g_Config.bEnableSound); 
		CHECKITEM(ID_TEXTURESCALING_DEPOSTERIZE, g_Config.bTexDeposterize); 

		EnableMenuItem(menu,ID_EMULATION_RUN, (Core_IsStepping() || globalUIState == UISTATE_PAUSEMENU) ? MF_ENABLED : MF_GRAYED);
		EnableMenuItem(menu,ID_EMULATION_PAUSE, globalUIState == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED);
		EnableMenuItem(menu,ID_EMULATION_STOP, globalUIState == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED);
		EnableMenuItem(menu,ID_EMULATION_RESET, globalUIState == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED);

		UINT enable = globalUIState == UISTATE_MENU ? MF_ENABLED : MF_GRAYED;
		EnableMenuItem(menu,ID_FILE_LOAD,enable);
		EnableMenuItem(menu,ID_FILE_LOAD_MEMSTICK,enable);
		EnableMenuItem(menu,ID_FILE_SAVESTATEFILE,!enable);
		EnableMenuItem(menu,ID_FILE_LOADSTATEFILE,!enable);
		EnableMenuItem(menu,ID_FILE_QUICKSAVESTATE,!enable);
		EnableMenuItem(menu,ID_FILE_QUICKLOADSTATE,!enable);
		EnableMenuItem(menu,ID_CPU_DYNAREC,enable);
		EnableMenuItem(menu,ID_CPU_INTERPRETER,enable);
		EnableMenuItem(menu,ID_EMULATION_STOP,!enable);
		
		static const int zoomitems[4] = {
			ID_OPTIONS_SCREEN1X,
			ID_OPTIONS_SCREEN2X,
			ID_OPTIONS_SCREEN3X,
			ID_OPTIONS_SCREEN4X,
		};
		for (int i = 0; i < 4; i++) {
			CheckMenuItem(menu, zoomitems[i], MF_BYCOMMAND | ((i == g_Config.iWindowZoom - 1) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texscalingitems[] = {
			ID_TEXTURESCALING_OFF,
			ID_TEXTURESCALING_2X,
			ID_TEXTURESCALING_3X,
			ID_TEXTURESCALING_4X,
			ID_TEXTURESCALING_5X,
		};
		for (int i = 0; i < 5; i++) {
			CheckMenuItem(menu, texscalingitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingLevel-1) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texscalingtypeitems[] = {
			ID_TEXTURESCALING_XBRZ,
			ID_TEXTURESCALING_HYBRID,
			ID_TEXTURESCALING_BICUBIC,
			ID_TEXTURESCALING_HYBRID_BICUBIC,
		};
		for (int i = 0; i < 4; i++) {
			CheckMenuItem(menu, texscalingtypeitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingType) ? MF_CHECKED : MF_UNCHECKED));
		}
	}


	// Message handler for about box.
	LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_INITDIALOG:
			W32Util::CenterWindow(hDlg);
			{
				HWND versionBox = GetDlgItem(hDlg, IDC_VERSION);
				char temp[256];
				sprintf(temp, "PPSSPP %s", PPSSPP_GIT_VERSION);
				SetWindowText(versionBox, temp);
			}
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
		}
		return FALSE;
	}

	static const char *controllist[] = {
		"TURBO MODE\tHold TAB",
		"Start\tSpace",
		"Select\tV",
		"Square\tA",
		"Triangle\tS",
		"Circle\tX",
		"Cross\tZ",
		"Left Trigger\tQ",
		"Right Trigger\tW",
		"Up\tArrow Up",
		"Down\tArrow Down",
		"Left\tArrow Left",
		"Right\tArrow Right",
		"Analog Up\tI",
		"Analog Down\tK",
		"Analog Left\tJ",
		"Analog Right\tL",
		"Rapid Fire\tShift",
	};

	static HHOOK pKeydownHook;
	static const int control_map_size = IDC_EDIT_KEY_ANALOG_RIGHT - IDC_EDIT_KEY_TURBO + 1;
	static u8 control_map[control_map_size];
	RECT getRedrawRect(HWND hWnd) {
		RECT rc;
		HWND hDlg = GetParent(hWnd);
		GetWindowRect(hWnd, &rc);
		POINT pt = {0, 0};
		ScreenToClient(hDlg, &pt);
		rc.left += pt.x;
		rc.right += pt.x;
		rc.top += pt.y;
		rc.bottom += pt.y;
		
		return rc;
	}

	LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		HWND hEdit = GetFocus();
		UINT nCtrlID = GetDlgCtrlID(hEdit);
		if (nCtrlID < IDC_EDIT_KEY_TURBO || nCtrlID > IDC_EDIT_KEY_ANALOG_RIGHT) {
			return CallNextHookEx(pKeydownHook, nCode, wParam, lParam);
		}
		if (!(lParam&(1<<31))) {
			// key down
			HWND hDlg = GetParent(hEdit);
			const char *str = getVirtualKeyName(wParam);
			if (str) {
				control_map[nCtrlID - IDC_EDIT_KEY_TURBO] = wParam;
				SetWindowTextA(hEdit, str);
				RECT rc = getRedrawRect(hEdit);
				InvalidateRect(hDlg, &rc, false);
			}
			else
				MessageBoxA(hDlg, "Not supported!", "controller", MB_OK);
		}
		return 1;
	}

	HBITMAP LoadImageFromResource(HINSTANCE hInstance,LPCTSTR pszResourceName, LPCTSTR lpType)
	{
		HRSRC hrsrc = FindResource(hInstance, pszResourceName, lpType);
		if (!hrsrc)
			return FALSE;
		DWORD dwlen = SizeofResource(hInstance, hrsrc);
		BYTE *lpRsrc = (BYTE*)LoadResource(hInstance, hrsrc);
		if (!lpRsrc)
			return FALSE;
		int width, height;
		unsigned char *image_data = 0;
		bool bResult = pngLoadPtr(lpRsrc, dwlen, &width, &height, &image_data, false) != 0;
		FreeResource(lpRsrc);
		if (!bResult)
			return 0;
		HBITMAP hbm = CreateBitmap(width, height, 1, 32, image_data);
		free(image_data);
		return hbm;
	}
	void BitBlt(HBITMAP hbm, HDC dstDC, int dstX, int dstY, int width, int height, int srcX, int srcY)
	{
		HDC hCompDC = CreateCompatibleDC(dstDC);
		HBITMAP oldbm = (HBITMAP)SelectObject(hCompDC, hbm);
		BitBlt(dstDC, dstX, dstY, width, height, hCompDC, srcX, srcY, SRCCOPY);
		SelectObject(hCompDC, oldbm);
		DeleteObject(hCompDC);
	}

	// Message handler for control box.
	LRESULT CALLBACK Controls(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
	{
		static HBITMAP hbm = 0;
		switch (message)
		{
		case WM_INITDIALOG:
			W32Util::CenterWindow(hDlg);
			{
				// TODO: connect to keyboard device instead
				{
					HBITMAP hResBM = LoadImageFromResource(hInst, MAKEINTRESOURCE(IDB_IMAGE_PSP), "IMAGE");
					HDC hDC = GetDC(hDlg);
					RECT clientRect;
					GetClientRect(hDlg, &clientRect);
					HBITMAP hMemBM = CreateCompatibleBitmap(hDC, clientRect.right, clientRect.bottom);
					HDC hResDC = CreateCompatibleDC(hDC);
					HDC hMemDC = CreateCompatibleDC(hDC);
					SelectObject(hResDC, hResBM);
					SelectObject(hMemDC, hMemBM);

					BITMAP bm;
					GetObject(hResBM, sizeof(BITMAP), &bm);
					SetStretchBltMode(hMemDC, HALFTONE);
					StretchBlt(hMemDC, 0, 0, clientRect.right, clientRect.bottom, hResDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY); 
					if (hbm)
						DeleteObject(hbm);
					hbm = hMemBM;

					DeleteDC(hMemDC);
					DeleteDC(hResDC);
					ReleaseDC(hDlg, hDC);
					DeleteObject(hResBM);
				}
				int key_pad_size = (IDC_EDIT_KEYRIGHT - IDC_EDIT_KEY_TURBO + 1);
				for (u32 i = 0; i <= IDC_EDIT_KEY_ANALOG_RIGHT - IDC_EDIT_KEY_TURBO; i++) {
					HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_KEY_TURBO + i);
					if (IDC_EDIT_KEY_TURBO + i <= IDC_EDIT_KEYRIGHT)
						control_map[i] = key_pad_map[i * 2];
					else
						control_map[i] = analog_ctrl_map[(i - key_pad_size) * 2];
					SetWindowTextA(hEdit, getVirtualKeyName(control_map[i]));
				}
				ComboBox_AddString(GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE), "None");
				ComboBox_AddString(GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE), "XInput");
				ComboBox_AddString(GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE), "DirectInput");
				if ((g_Config.iForceInputDevice < 0) || (g_Config.iForceInputDevice > 1))
				{
					ComboBox_SetCurSel(GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE), 0);
				}
				else
				{
					ComboBox_SetCurSel(GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE), (g_Config.iForceInputDevice + 1));
				}
				DWORD dwThreadID = GetWindowThreadProcessId(hDlg, NULL);
				pKeydownHook = SetWindowsHookEx(WH_KEYBOARD,KeyboardProc, NULL, dwThreadID);
			}
			return TRUE;
		case WM_PAINT:
			{
				PAINTSTRUCT pst;
				HDC hdc = BeginPaint(hDlg, &pst);
				BITMAP bm;
				GetObject(hbm, sizeof(BITMAP), &bm);
				int width = bm.bmWidth;
				int height = bm.bmHeight;
				BitBlt(hbm, hdc, 0, 0, width, height, 0 , 0);
				EndPaint(hDlg, &pst);
				return TRUE;
			}
		case WM_CTLCOLORSTATIC:
			{
				HDC hdc=(HDC)wParam;
				SetBkMode(hdc, TRANSPARENT);
				return (LRESULT)GetStockObject(NULL_BRUSH); 
			}
		case WM_CTLCOLOREDIT:
			{
				if ((HWND)lParam == GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE))
					return FALSE;
				HDC hdc = (HDC)wParam;
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, RGB(255, 0, 0));
				HWND hEdit = (HWND)lParam;
				RECT rc = getRedrawRect(hEdit);
				RECT clientrc;
				GetClientRect(hEdit, &clientrc);
				BitBlt(hbm, hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top);
				char str[11];
				GetWindowTextA(hEdit, str, 10);
				DrawTextA(hdc, str, (int)strlen(str), &clientrc, DT_CENTER|DT_SINGLELINE);
				return (LRESULT)GetStockObject(NULL_BRUSH);
			}
		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				if (LOWORD(wParam) == IDOK) {
					g_Config.iForceInputDevice = (ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_FORCE_INPUT_DEVICE)) - 1);
					int key_pad_size = (IDC_EDIT_KEYRIGHT - IDC_EDIT_KEY_TURBO + 1);
					for (u32 i = 0; i <= IDC_EDIT_KEY_ANALOG_RIGHT - IDC_EDIT_KEY_TURBO; i++) {
						if (IDC_EDIT_KEY_TURBO + i <= IDC_EDIT_KEYRIGHT)
							key_pad_map[i * 2] = control_map[i];
					else
						analog_ctrl_map[(i - key_pad_size) * 2] = control_map[i];
					}
					saveControlsToFile();
				}
				UnhookWindowsHookEx(pKeydownHook);
				EndDialog(hDlg, LOWORD(wParam));
				if (hbm) {
					DeleteObject(hbm);
					hbm = 0;
				}
				return TRUE;
			}
			break;
		}
		return FALSE;
	}

	void Update()
	{
		InvalidateRect(hwndDisplay,0,0);
		UpdateWindow(hwndDisplay);
		SendMessage(hwndMain,WM_SIZE,0,0);
	}

	void Redraw()
	{
		InvalidateRect(hwndDisplay,0,0);
	}

	void _ViewNormal(HWND hWnd)
	{
		ShowCursor(TRUE);
		SetCursor(LoadCursor(NULL, IDC_ARROW));

		// put caption and border styles back
		DWORD dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwNewStyle = dwOldStyle | WS_CAPTION | WS_THICKFRAME;
		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// put back the menu bar
		::SetMenu(hWnd, menu);

		// resize to normal view
		// NOTE: use SWP_FRAMECHANGED to force redraw non-client
		const int x = g_normalRC.left;
		const int y = g_normalRC.top;
		const int cx = g_normalRC.right - g_normalRC.left;
		const int cy = g_normalRC.bottom - g_normalRC.top;
		::SetWindowPos(hWnd, HWND_NOTOPMOST, x, y, cx, cy, SWP_FRAMECHANGED);

		// reset full screen indicator
		g_bFullScreen = FALSE;
		ResizeDisplay();
	}

	void _ViewFullScreen(HWND hWnd)
	{
		if (globalUIState == UISTATE_INGAME)
			ShowCursor(FALSE);

		// keep in mind normal window rectangle
		::GetWindowRect(hWnd, &g_normalRC);

		// remove caption and border styles
		DWORD dwOldStyle = ::GetWindowLong(hWnd, GWL_STYLE);
		DWORD dwNewStyle = dwOldStyle & ~(WS_CAPTION | WS_THICKFRAME);
		::SetWindowLong(hWnd, GWL_STYLE, dwNewStyle);

		// remove the menu bar
		::SetMenu(hWnd, NULL);

		// resize to full screen view
		// NOTE: use SWP_FRAMECHANGED to force redraw non-client
		const int x = 0;
		const int y = 0;
		const int cx = ::GetSystemMetrics(SM_CXSCREEN);
		const int cy = ::GetSystemMetrics(SM_CYSCREEN);
		::SetWindowPos(hWnd, HWND_TOPMOST, x, y, cx, cy, SWP_FRAMECHANGED);

		// set full screen indicator
		g_bFullScreen = TRUE;
		ResizeDisplay();
	}

	void SetPlaying(const char *text)
	{
		char temp[256];
		if (text == 0)
			snprintf(temp, 256, "PPSSPP %s", PPSSPP_GIT_VERSION);
		else
			snprintf(temp, 256, "%s - PPSSPP %s", text, PPSSPP_GIT_VERSION);
		temp[255] = '\0';
		SetWindowText(hwndMain, temp);
	}

	void SaveStateActionFinished(bool result, void *userdata)
	{
		if (!result)
			MessageBox(0, "Savestate failure.  Using savestates between different PPSSPP versions is not supported.", "Sorry", MB_OK);
		SetCursor(LoadCursor(0, IDC_ARROW));
	}

	HINSTANCE GetHInstance()
	{
		return hInst;
	}
}

