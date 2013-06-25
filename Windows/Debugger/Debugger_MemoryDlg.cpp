// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#include "stdafx.h"
#include <windowsx.h>
#include "..\resource.h"

#include "../../Core/Debugger/SymbolMap.h"
#include "Debugger_MemoryDlg.h"

#include "CtrlMemView.h"

#include "../../Core/MIPS/MIPSDebugInterface.h" //	BAD

RECT CMemoryDlg::slRect;

FAR WNDPROC DefAddressEditProc;
HWND AddressEditParentHwnd;

LRESULT CALLBACK AddressEditProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
	case WM_KEYUP:
		if( wParam == VK_RETURN )
		{
			SendMessage(AddressEditParentHwnd,WM_USER+2,0,0);
			return 0;
		}
	default:
		return (LRESULT)CallWindowProc((WNDPROC)DefAddressEditProc,hDlg,message,wParam,lParam);
	};

	return 0;
}


CMemoryDlg::CMemoryDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu) : Dialog((LPCSTR)IDD_MEMORY, _hInstance,_hParent)
{
  cpu = _cpu;
	TCHAR temp[256];
	sprintf(temp,"Memory Viewer - %s",cpu->GetName());
	SetWindowText(m_hDlg,temp);
	ShowWindow(m_hDlg,SW_HIDE);
	CtrlMemView *ptr = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
  ptr->setDebugger(_cpu);

  Button_SetCheck(GetDlgItem(m_hDlg,IDC_RAM), TRUE);
	Button_SetCheck(GetDlgItem(m_hDlg,IDC_MODESYMBOLS), TRUE);

	GetWindowRect(GetDlgItem(m_hDlg,IDC_SYMBOLS),&slRect);

	// subclass the edit box
	HWND editWnd = GetDlgItem(m_hDlg,IDC_ADDRESS);
	DefAddressEditProc = (WNDPROC)GetWindowLong(editWnd,GWL_WNDPROC);
	SetWindowLong(editWnd,GWL_WNDPROC,(long)AddressEditProc); 
	AddressEditParentHwnd = m_hDlg;

	Size();
}


CMemoryDlg::~CMemoryDlg(void)
{
}

void CMemoryDlg::Update(void)
{
	if (m_hDlg != NULL)
	{
		CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
		if (mv != NULL)
			mv->redraw();
	}	
}

void CMemoryDlg::NotifyMapLoaded()
{
	if (m_hDlg)
	{
		HWND list = GetDlgItem(m_hDlg,IDC_SYMBOLS);
		symbolMap.FillSymbolListBox(list,ST_DATA);	
		HWND lb = GetDlgItem(m_hDlg,IDC_REGIONS);
		int sel = ComboBox_GetCurSel(lb);
		ComboBox_ResetContent(lb);
    /*
		for (int i = 0; i < cpu->getMemMap()->numRegions; i++)
		{
			int n = ComboBox_AddString(lb,cpu->getMemMap()->regions[i].name);
			ComboBox_SetItemData(lb,n,cpu->getMemMap()->regions[i].start);
		}*/
		ComboBox_SetCurSel(lb,sel>=0?sel:0);
	}
	Update(); 
}


BOOL CMemoryDlg::DlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{

	switch(message)
	{
	case WM_COMMAND:
		{
			CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
			switch (LOWORD(wParam))
			{
			case IDC_REGIONS:
				switch (HIWORD(wParam)) 
				{ 
				case LBN_DBLCLK:
					{
						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ComboBox_GetCurSel(lb);
						if (n!=-1)
						{
							unsigned int addr = (unsigned int)ComboBox_GetItemData(lb,n);
							mv->gotoAddr(addr);
						}
					}
					break;
				};
				break;
			case IDC_SYMBOLS: 
				switch (HIWORD(wParam)) 
				{ 
				case LBN_DBLCLK:
					{

						HWND lb = GetDlgItem(m_hDlg,LOWORD(wParam));
						int n = ListBox_GetCurSel(lb);
						if (n!=-1)
						{
							unsigned int addr = (unsigned int)ListBox_GetItemData(lb,n);
							mv->gotoAddr(addr);
						}
					}
					break;
				};
				break;		
			case IDC_MODENORMAL:
				mv->setMode(MV_NORMAL);
				break;
			case IDC_MODESYMBOLS:
				mv->setMode(MV_NORMAL);
	//			mv->setMode(MV_SYMBOLS);
				break;
			}
		}
		break;
	case WM_USER+1:
		NotifyMapLoaded();
		break;
	case WM_USER+2:	// enter hit on address edit
	{
		CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(m_hDlg,IDC_MEMVIEW));
		char temp[256];
		u32 addr;
		GetWindowText(GetDlgItem(m_hDlg,IDC_ADDRESS),temp,255);
		sscanf(temp,"%08x",&addr);
		mv->gotoAddr(addr);
		break;
	}
	case WM_INITDIALOG:
		{
			return TRUE;
		}
		break;
	case WM_SIZE:
		Size();
		break;
	case WM_CLOSE:
		Show(false);
		break;
	}
		
	return FALSE;
}

void CMemoryDlg::Goto(u32 addr)
{
	Show(true);
	CtrlMemView *mv = CtrlMemView::getFrom(GetDlgItem(CMemoryDlg::m_hDlg,IDC_MEMVIEW));
	mv->gotoAddr(addr);
}


void CMemoryDlg::Size()
{
	RECT rc;
	GetClientRect(m_hDlg,&rc);
	int dw=rc.right-rc.left;
	int dh=rc.bottom-rc.top;
	HWND memView = GetDlgItem(m_hDlg, IDC_MEMVIEW);
	HWND symList = GetDlgItem(m_hDlg, IDC_SYMBOLS);
	int wf = slRect.right-slRect.left;
	int w = dw-3-wf;
	int top = 48;
	MoveWindow(symList,0,top,wf,dh-top,TRUE);
	MoveWindow(memView,wf+4,top,w,dh-top,TRUE);
}
