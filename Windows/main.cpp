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

#include <windows.h>

#include "file/vfs.h"
#include "file/zip_read.h"

#include "../Core/Config.h"
#include "EmuThread.h"

#include "LogManager.h"
#include "ConsoleListener.h"
#include "Thread.h"

#include "Commctrl.h"

#include "Windows/resource.h"

#include "Windows/WndMainWindow.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"

#include "Windows/W32Util/DialogManager.h"

#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/CtrlMemView.h"
#include "Windows/Debugger/CtrlRegisterList.h"

#include "Windows/WindowsHost.h"
#include "Windows/main.h"

CDisasm *disasmWindow[MAX_CPUCOUNT];
CMemoryDlg *memoryWindow[MAX_CPUCOUNT];

int WINAPI WinMain(HINSTANCE _hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow)
{
	Common::EnableCrashingOnCrashes();

	const char *fileToStart = NULL;
	const char *fileToLog = NULL;
	bool hideLog = true;
	bool autoRun = true;

#ifdef _DEBUG
	hideLog = false;
#endif

	g_Config.Load();
	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));

	for (int i = 1; i < __argc; ++i)
	{
		if (__argv[i][0] == '\0')
			continue;

		if (__argv[i][0] == '-')
		{
			switch (__argv[i][1])
			{
			case 'j':
				g_Config.iCpuCore = CPU_JIT;
				break;
			case 'i':
				g_Config.iCpuCore = CPU_INTERPRETER;
				break;
			case 'f':
				g_Config.iCpuCore = CPU_FASTINTERPRETER;
				break;
			case 'l':
				hideLog = false;
				break;
			case 's':
				autoRun = false;
				break;
			case '-':
				if (!strcmp(__argv[i], "--log") && i < __argc - 1)
					fileToLog = __argv[++i];
				if (!strncmp(__argv[i], "--log=", strlen("--log=")) && strlen(__argv[i]) > strlen("--log="))
					fileToLog = __argv[i] + strlen("--log=");
				break;
			}
		}
		else if (fileToStart == NULL)
		{
			fileToStart = __argv[i];
			if (!File::Exists(fileToStart))
			{
				fprintf(stderr, "File not found: %s\n", fileToStart);
				exit(1);
			}
		}
		else
		{
			fprintf(stderr, "Can only boot one file");
			exit(1);
		}
	}

	//Windows, API init stuff
	INITCOMMONCONTROLSEX comm;
	comm.dwSize = sizeof(comm);
	comm.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
	InitCommonControlsEx(&comm);
	timeBeginPeriod(1);
	MainWindow::Init(_hInstance);

	HACCEL hAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_ACCELS);
	g_hPopupMenus = LoadMenu(_hInstance, (LPCSTR)IDR_POPUPMENUS);

	MainWindow::Show(_hInstance, iCmdShow);
	host = new WindowsHost(MainWindow::GetHWND(), MainWindow::GetDisplayHWND());

	HWND hwndMain = MainWindow::GetHWND();
	HMENU menu = GetMenu(hwndMain);

	//initialize custom controls
	CtrlDisAsmView::init();
	CtrlMemView::init();
	CtrlRegisterList::init();

	DialogManager::AddDlg(memoryWindow[0] = new CMemoryDlg(_hInstance, hwndMain, currentDebugMIPS));
	DialogManager::AddDlg(vfpudlg = new CVFPUDlg(_hInstance, hwndMain, currentDebugMIPS));

	MainWindow::Update();
	MainWindow::UpdateMenus();

	LogManager::Init();
	if (fileToLog != NULL)
		LogManager::GetInstance()->ChangeFileLog(fileToLog);
	LogManager::GetInstance()->GetConsoleListener()->Open(hideLog, 150, 120, "PPSSPP Debug Console");
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LERROR);
	if (fileToStart != NULL)
	{
		MainWindow::SetPlaying(fileToStart);
		MainWindow::Update();
		MainWindow::UpdateMenus();

		EmuThread_Start(fileToStart);
	}
	else
		MainWindow::BrowseAndBoot();

	if (autoRun && fileToLog != NULL)
		MainWindow::SetNextState(CORE_RUNNING);

	//so.. we're at the message pump of the GUI thread
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))	//while no quit
	{
		//DSound_UpdateSound();

		//hack to make it possible to get to main window from floating windows with Esc
		if (msg.hwnd != hwndMain && msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE)
			BringWindowToTop(hwndMain);

		//Translate accelerators and dialog messages...
		if (!TranslateAccelerator(hwndMain, hAccelTable, &msg))
		{
			if (!DialogManager::IsDialogMessage(&msg))
			{
				//and finally translate and dispatch
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	LogManager::Shutdown();
	DialogManager::DestroyAll();
	timeEndPeriod(1);
	g_Config.Save();
	delete host;
	return 0;
}
