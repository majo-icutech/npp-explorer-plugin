/*
This file is part of Explorer Plugin for Notepad++
Copyright (C)2006 Jens Lorenz <jens.plugin.npp@gmx.de>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/


#include "stdafx.h"
#include "FileList.h"
#include "resource.h"
#include <windows.h>
#include <algorithm>

#define LVIS_SELANDFOC	(LVIS_SELECTED|LVIS_FOCUSED)

static HWND		hWndServer		= NULL;
static HHOOK	hookMouse		= NULL;

static LRESULT CALLBACK hookProcMouse(INT nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
		switch (wParam)
		{
			case WM_MOUSEMOVE:
			case WM_NCMOUSEMOVE:
				::PostMessage(hWndServer, wParam, 0, 0);
				break;
			case WM_LBUTTONUP:
			case WM_NCLBUTTONUP:
				::PostMessage(hWndServer, wParam, 0, 0);
				return TRUE;
			default: 
				break;
		}
	}
	return ::CallNextHookEx(hookMouse, nCode, wParam, lParam);
}


DWORD WINAPI FileOverlayThread(LPVOID lpParam)
{
	FileList	*pFileList	= (FileList*)lpParam;
	pFileList->UpdateOverlayIcon();
	::ExitThread(0);
	return 0;
}


FileList::FileList(void)
{
	_hOverThread		= NULL;
	_hFont				= NULL;
	_hFontUnder			= NULL;
	_hHeader			= NULL;
	_bmpSortUp			= NULL;
	_bmpSortDown		= NULL;
	_iMouseTrackItem	= 0;
	_lMouseTrackPos		= 0;
	_iBltPos			= 0;
	_isStackRec			= TRUE;
	_bOldAddExtToName	= FALSE;
	_bOldViewLong		= FALSE;
	_bSearchFile		= FALSE;
	_isScrolling		= FALSE;
	_isDnDStarted		= FALSE;
	_uMaxFolders		= 0;
	_uMaxElements		= 0;
	_uMaxElementsOld	= 0;
	_tcscpy(_strSearchFile, _T(""));
	_tcscpy(_szFileFilter, _T("*.*"));
	_vFileList.clear();
	ZeroMemory(&_numberFmt, sizeof(_numberFmt));
}

FileList::~FileList(void)
{
}

void FileList::initFont()
{
	if (_hFont)
	{
		::DeleteObject(_hFont);
	}

	if (_hFontUnder)
	{
		::DeleteObject(_hFontUnder);
	}

	/* get font for drawing */
	HFONT defaultFont = (HFONT)::SendMessage(_hSelf, WM_GETFONT, 0, 0);

	LOGFONT	lf = { 0 };
	::GetObject(defaultFont, sizeof(LOGFONT), &lf);
	if (_pExProp->iFontSize != 0)
	{
		lf.lfHeight = -_pExProp->iFontSize;
	}
	_hFont = ::CreateFontIndirect(&lf);

	lf.lfUnderline = TRUE;
	_hFontUnder = ::CreateFontIndirect(&lf);

	SendMessage(_hSelf, WM_SETFONT, (WPARAM)_hFont, TRUE);
}

void FileList::init(HINSTANCE hInst, HWND hParent, HWND hParentList)
{
	/* this is the list element */
	Window::init(hInst, hParent);
	_hSelf = hParentList;

	/* create semaphore for thead */
	_hSemaphore = ::CreateSemaphore(NULL, 1, 1, NULL);

	/* create events for thread */
	for (INT i = 0; i < FL_EVT_MAX; i++) 
		_hEvent[i] = ::CreateEvent(NULL, FALSE, FALSE, NULL);

	/* create thread */
	DWORD	dwFlags	= 0;
	_hOverThread = ::CreateThread(NULL, 0, FileOverlayThread, this, 0, &dwFlags);

	/* Get decimal and thousands separator */
	GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, _szDecimalSeparator, size(_szDecimalSeparator));
	GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, _szThousandsSeparator, size(_szThousandsSeparator));

	_numberFmt.NumDigits = 0;
	_numberFmt.Grouping = 3;
	_numberFmt.lpDecimalSep = _szDecimalSeparator;
	_numberFmt.lpThousandSep = _szThousandsSeparator;
	GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_INEGNUMBER | LOCALE_RETURN_NUMBER, (LPWSTR)&_numberFmt.NegativeOrder, sizeof(_numberFmt.NegativeOrder));
	GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_ILZERO | LOCALE_RETURN_NUMBER, (LPWSTR)&_numberFmt.LeadingZero, sizeof(_numberFmt.LeadingZero));

	initFont();

	/* load sort bitmaps */
	if (gWinVersion < WV_XP)
	{
		_bmpSortUp	 = (HBITMAP)::LoadImage(hInst, MAKEINTRESOURCE(IDB_SORTUP), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS);
		_bmpSortDown = (HBITMAP)::LoadImage(hInst, MAKEINTRESOURCE(IDB_SORTDOWN), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS);
	}

	/* keep sure to support virtual list with icons */
	DWORD	dwStyle = ::GetWindowLong(_hSelf, GWL_STYLE);
	::SetWindowLongPtr(_hSelf, GWL_STYLE, dwStyle | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);

	/* enable full row select */
	ListView_SetExtendedListViewStyle(_hSelf, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	ListView_SetCallbackMask(_hSelf, LVIS_OVERLAYMASK | LVIS_CUT);

	/* subclass list control */
	SetWindowSubclass(_hSelf, wndDefaultListProc, _idSubclassListProc, (DWORD_PTR)this);

	/* set image list and icon */
	_hImlParent = GetSmallImageList(FALSE);
	_hImlListSys = GetSmallImageList(_pExProp->bUseSystemIcons);
	ListView_SetImageList(_hSelf, _hImlListSys, LVSIL_SMALL);

	/* get header control and subclass it */
	hWndServer = _hHeader = ListView_GetHeader(_hSelf);	
	SetWindowSubclass(_hHeader, wndDefaultHeaderProc, _idSubclassHeaderProc, (DWORD_PTR)this);

	/* set here the columns */
	SetColumns();

	/* initialize droping */
	::RegisterDragDrop(_hSelf, this);

	/* create the supported formats */
	FORMATETC fmtetc	= {0}; 
	fmtetc.cfFormat		= CF_HDROP; 
	fmtetc.dwAspect		= DVASPECT_CONTENT; 
	fmtetc.lindex		= -1; 
	fmtetc.tymed		= TYMED_HGLOBAL;
	AddSuportedFormat(_hSelf, fmtetc); 
}

void FileList::destroy()
{
	RemoveWindowSubclass(_hSelf, wndDefaultListProc, _idSubclassListProc);
	RemoveWindowSubclass(_hHeader, wndDefaultHeaderProc, _idSubclassHeaderProc);
}

void FileList::initProp(tExProp* prop)
{
	/* set properties */
	_pExProp		= prop;
	SetFilter(_pExProp->strLastFilter.c_str());
}


/****************************************************************************
 *	Draw header list
 */
LRESULT FileList::runListProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_GETDLGCODE:
		{
			return DLGC_WANTALLKEYS | DefSubclassProc(hwnd, Message, wParam, lParam);
		}
		case WM_CHAR:
		{
			CHAR	charkey = (CHAR)tolower(wParam);

			/* do selection of items by user keyword typing or cut/copy/paste */
			switch (charkey) {
				case SHORTCUT_CUT:
					onCut();
					return TRUE;
				case SHORTCUT_COPY:  
					onCopy(); 
					return TRUE;
				case SHORTCUT_PASTE: 
					onPaste();	
					return TRUE;
				case SHORTCUT_ALL: 
					onSelectAll(); 
					return TRUE;
				case SHORTCUT_DELETE: 
					onDelete();
					return TRUE;
				case SHORTCUT_REFRESH:
					::SendMessage(_hParent, EXM_USER_ICONBAR, IDM_EX_UPDATE, 0);
					return TRUE;
				default: 
					onSelectItem(charkey); 
					break;
			}
			return TRUE;
		}
		case WM_KEYDOWN:
		{
			if ((wParam == VK_DELETE) && !((0x80 & ::GetKeyState(VK_CONTROL)) == 0x80))
			{
				onDelete((0x80 & ::GetKeyState(VK_SHIFT)) == 0x80);
				return TRUE;
			}
			if (wParam == VK_F5)
			{
				::SendMessage(_hParent, EXM_USER_ICONBAR, IDM_EX_UPDATE, 0);
				return TRUE;
			}
			break;
		}
		case WM_SYSKEYDOWN:
		{
			if ((0x80 & ::GetKeyState(VK_MENU)) == 0x80)
			{
				if (wParam == VK_LEFT) {
					::SendMessage(_hParent, EXM_USER_ICONBAR, IDM_EX_PREV, 0);
					return TRUE;
				} else if (wParam == VK_RIGHT) {
					::SendMessage(_hParent, EXM_USER_ICONBAR, IDM_EX_NEXT, 0);
					return TRUE;
				}
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			LVHITTESTINFO	hittest			= {0};

			/* get position */
			::GetCursorPos(&hittest.pt);
			ScreenToClient(_hSelf, &hittest.pt);
			::SendMessage(_hSelf, LVM_SUBITEMHITTEST, 0, (LPARAM)&hittest);

			if (_isDnDStarted == TRUE) {
				for (UINT i = 0; i < _uMaxElements; i++)
					ListView_SetItemState(_hSelf, i, 0, LVIS_DROPHILITED);
				_isDnDStarted = FALSE;
			}
			break;
		}
		case WM_DESTROY:
		{
			if (gWinVersion < WV_XP)
			{
				::DeleteObject(_bmpSortUp);
				::DeleteObject(_bmpSortDown);
			}

			ImageList_Destroy(_hImlParent);
			::DeleteObject(_hFont);
			::DeleteObject(_hFontUnder);

			if (_hSemaphore) {
				::SetEvent(_hEvent[FL_EVT_EXIT]);
				::CloseHandle(_hOverThread);
				for (INT i = 0; i < FL_EVT_MAX; i++)
					::CloseHandle(_hEvent[i]);
				::CloseHandle(_hSemaphore);
			}

			_vDirStack.clear();
			_vFileList.clear();
			break;
		}
		case WM_TIMER:
		{
			if (wParam == EXT_SEARCHFILE)
			{
				::KillTimer(_hSelf, EXT_SEARCHFILE);
				_bSearchFile = FALSE;
				_tcscpy(_strSearchFile, _T(""));
				return FALSE;
			}
			else if (wParam == EXT_SCROLLLISTUP)
			{
				RECT	rc		= {0};
				Header_GetItemRect(_hHeader, 0, &rc);

				UINT	iItem	= ListView_GetTopIndex(_hSelf);		
				eScDir	scrDir	= GetScrollDirection(_hSelf, rc.bottom - rc.top);

				if ((scrDir != SCR_UP) || (iItem == 0) || (!m_bAllowDrop)) {
					::KillTimer(_hSelf, EXT_SCROLLLISTUP);
					_isScrolling = FALSE;
				} else {
					ListView_Scroll(_hSelf, 0, -12);
				}
				return FALSE;
			}
			else if (wParam == EXT_SCROLLLISTDOWN)
			{
				UINT	iItem	= ListView_GetTopIndex(_hSelf) + ListView_GetCountPerPage(_hSelf) - 1;
				eScDir	scrDir	= GetScrollDirection(_hSelf);

				if ((scrDir != SCR_DOWN) || (iItem >= _uMaxElements) || (!m_bAllowDrop)) {
					::KillTimer(_hSelf, EXT_SCROLLLISTDOWN);
					_isScrolling = FALSE;
				} else {
					ListView_Scroll(_hSelf, 0, 12);
				}
				return FALSE;
			}
			break;
		}
		case EXM_UPDATE_OVERICON:
		{
			INT			iIcon		= 0;
			INT			iSelected	= 0;
			RECT		rcIcon		= {0};
			UINT		iPos		= (UINT)wParam;
			eDevType	type		= (eDevType)lParam;

			if (iPos < _uMaxElements)
			{
				/* test if overlay icon is need to be updated and if it's changed do a redraw */
				if (_vFileList[iPos].iOverlay == 0) {
					ExtractIcons(_pExProp->szCurrentPath, _vFileList[iPos].strNameExt.c_str(), 
						type, &iIcon, &iSelected, &_vFileList[iPos].iOverlay);
				}

				if (_vFileList[iPos].iOverlay != 0) {
					ListView_GetSubItemRect(_hSelf, iPos, 0, LVIR_ICON, &rcIcon);
					::RedrawWindow(_hSelf, &rcIcon, NULL, TRUE);
				}

				::SetEvent(_hEvent[FL_EVT_NEXT]);
			}
			break;
		}
		case EXM_QUERYDROP:
		{
			if (_isScrolling == FALSE)
			{
				/* get hight of header */
				RECT	rc = {0};
				Header_GetItemRect(_hHeader, 0, &rc);
				eScDir scrDir = GetScrollDirection(_hSelf, rc.bottom - rc.top);

				if (scrDir == SCR_UP) {
					::SetTimer(_hSelf, EXT_SCROLLLISTUP, 300, NULL);
					_isScrolling = TRUE;
				} else if (scrDir == SCR_DOWN) {
					::SetTimer(_hSelf, EXT_SCROLLLISTDOWN, 300, NULL);
					_isScrolling = TRUE;
				}
			}

			/* select item */
			LVHITTESTINFO	hittest			= {0};
			::GetCursorPos(&hittest.pt);
			ScreenToClient(_hSelf, &hittest.pt);
			ListView_SubItemHitTest(_hSelf, &hittest);

			for (UINT i = 0; i < _uMaxFolders; i++)
			{
				ListView_SetItemState(_hSelf, i, 
					i == hittest.iItem ? LVIS_DROPHILITED : 0, 
					LVIS_DROPHILITED);
			}

			_isDnDStarted = TRUE;

			return TRUE;
		}
		case EXM_DRAGLEAVE:
		{
			/* stop scrolling if still enabled while DnD */
			/* unselect DnD highlight */
			if (_isDnDStarted == TRUE) {
				for (UINT i = 0; i < _uMaxElements; i++)
					ListView_SetItemState(_hSelf, i, 0, LVIS_DROPHILITED);
				_isDnDStarted = FALSE;
			}
			if (_isScrolling == TRUE)
			{
				::KillTimer(_hSelf, EXT_SCROLLLISTUP);
				::KillTimer(_hSelf, EXT_SCROLLLISTDOWN);
				_isScrolling = FALSE;
			}
			return TRUE;
		}
		default:
			break;
	}
	
	return DefSubclassProc(hwnd, Message, wParam, lParam);
}

/****************************************************************************
 *	Message handling of header
 */
LRESULT FileList::runHeaderProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_LBUTTONUP:
		{
			if (_lMouseTrackPos != 0)
			{
				POINT	pt	= {0};
				::GetCursorPos(&pt);

				/* erase divider line */
				DrawDivider(_iBltPos);
				_iBltPos		= 0;

				/* recalc position */
				INT iWidth = ListView_GetColumnWidth(_hSelf, _iMouseTrackItem) - (_lMouseTrackPos - pt.x);
				ListView_SetColumnWidth(_hSelf, _iMouseTrackItem, iWidth);
				::RedrawWindow(_hSelf, NULL, NULL, TRUE);

				_lMouseTrackPos = 0;

				/* update here the header column width */
				_pExProp->iColumnPosName = ListView_GetColumnWidth(_hSelf, 0);
				_pExProp->iColumnPosExt  = ListView_GetColumnWidth(_hSelf, 1);
				if (_pExProp->bViewLong == TRUE) {
					_pExProp->iColumnPosSize = ListView_GetColumnWidth(_hSelf, 2);
					_pExProp->iColumnPosDate = ListView_GetColumnWidth(_hSelf, 3);
				}

				if (hookMouse)
				{
					::UnhookWindowsHookEx(hookMouse);
					hookMouse = NULL;
				}
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			if (_lMouseTrackPos != 0)
			{
				POINT	pt		= {0};
				::GetCursorPos(&pt);
				::ScreenToClient(_hSelf, &pt);

				if (_iBltPos != 0)
					DrawDivider(_iBltPos);
				DrawDivider(pt.x);
				_iBltPos = pt.x;
			}
			break;
		}
		default:
			break;
	}

	return DefSubclassProc(hwnd, Message, wParam, lParam);
}

void FileList::DrawDivider(UINT x)
{
	UINT		posTop		= 0;
	RECT		rc			= {0};
	HDC			hDc			= ::GetWindowDC(_hSelf);
	HBITMAP		hBm			= NULL;
	HBRUSH		hBrush		= NULL;
	HANDLE		hBrushOrig	= NULL;

	/* get hight of header */
	Header_GetItemRect(_hHeader, 0, &rc);
	posTop = rc.bottom;

	/* set clip rect */
	::GetClientRect(_hSelf, &rc);
	::IntersectClipRect(hDc, rc.left, posTop + 2, rc.right, rc.bottom);

	/* Create a brush with the appropriate bitmap pattern to draw our drag rectangle */
	hBm = ::CreateBitmap(8, 8, 1, 1, DotPattern);
	hBrush = ::CreatePatternBrush(hBm);

	::SetBrushOrgEx(hDc, rc.left, rc.top, 0);
	hBrushOrig = ::SelectObject(hDc, hBrush);

	/* draw devider line */
	::PatBlt(hDc, x, rc.top, 1, rc.bottom, PATINVERT);

	/* destroy resources */
	::SelectObject(hDc, hBrushOrig);
	::DeleteObject(hBrush);
	::DeleteObject(hBm);
}

/****************************************************************************
 *	Parent notification
 */
BOOL FileList::notify(WPARAM wParam, LPARAM lParam)
{
	LPNMHDR	 nmhdr	= (LPNMHDR)lParam;

	if (nmhdr->hwndFrom == _hSelf)
	{
		switch (nmhdr->code)
		{
			case LVN_GETDISPINFO:
			{
				NMLVDISPINFO* dispInfo = (LV_DISPINFO*)lParam;
				LV_ITEM& lvItem = dispInfo->item;

				if (lvItem.mask & LVIF_TEXT)
				{
					/* must be a cont array */
					static TCHAR	str[MAX_PATH];

					ReadArrayToList(str, lvItem.iItem, lvItem.iSubItem);
					lvItem.pszText = str;
					lvItem.cchTextMax = _tcslen(str);
				}
				if (lvItem.mask & LVIF_IMAGE && !_vFileList[lvItem.iItem].bParent)
				{
					INT			iIcon = 0;
					INT			iOverlay = 0;
					
					ReadIconToList(lvItem.iItem, &iIcon, &iOverlay);

					lvItem.iImage = iIcon;
					lvItem.state = INDEXTOOVERLAYMASK(iOverlay);					
				}
				if (lvItem.mask & LVIF_STATE && _vFileList[lvItem.iItem].bHidden)
				{
					lvItem.state |= LVIS_CUT;
				}
				break;
			}
			case LVN_COLUMNCLICK:
			{
				/* store the marked items */
				for (UINT i = 0; i < _uMaxElements; i++) {
					_vFileList[i].state = ListView_GetItemState(_hSelf, i, LVIS_SELECTED);
				}
				
				INT iPos  = ((LPNMLISTVIEW)lParam)->iSubItem;

				if (iPos != _pExProp->iSortPos)
					_pExProp->iSortPos = iPos;
				else
					_pExProp->bAscending ^= TRUE;
				SetOrder();
				UpdateList();

				/* mark old items */
				for (UINT i = 0; i < _uMaxElements; i++) {
					ListView_SetItemState(_hSelf, i, _vFileList[i].state, 0xFF);
				}
				break;
			}
			case LVN_KEYDOWN:
			{
				switch (((LPNMLVKEYDOWN)lParam)->wVKey)
				{
					case VK_RETURN:
					{
						UINT	selRow		= ListView_GetSelectionMark(_hSelf);

						if (selRow != -1) {
							if (selRow < _uMaxFolders) {
								::SendMessage(_hParent, EXM_OPENDIR, 0, (LPARAM)_vFileList[selRow].strName.c_str());
							} else {
								::SendMessage(_hParent, EXM_OPENFILE, 0, (LPARAM)_vFileList[selRow].strNameExt.c_str());
							}
						}
						break;
					}
					case VK_BACK:
					{
						::SendMessage(_hParent, EXM_OPENDIR, 0, (LPARAM)"..");
						break;
					}
					case VK_TAB:
					{
						::SetFocus(::GetNextWindow(_hSelf, (0x80 & ::GetKeyState(VK_SHIFT)) ? GW_HWNDPREV : GW_HWNDNEXT));
						break;
					}
					default:
						break;
				}
				break;
			}
			case NM_CLICK:
			{
				break;
			}
			case NM_RCLICK:
			{
				onRMouseBtn();
				break;
			}
			case LVN_BEGINDRAG:
			{
				CIDropSource	dropSrc;
				CIDataObject	dataObj(&dropSrc);
				FolderExChange(&dropSrc, &dataObj, DROPEFFECT_COPY | DROPEFFECT_MOVE);
				break;
			}
			case NM_DBLCLK:
			{
				onLMouseBtnDbl();
				break;
			}
			case LVN_ITEMCHANGED:
			{
				UpdateSelItems();
				break;
			}
			case NM_CUSTOMDRAW:
			{
				LPNMLVCUSTOMDRAW lpCD = (LPNMLVCUSTOMDRAW)lParam;

				switch (lpCD->nmcd.dwDrawStage)
				{
					case CDDS_PREPAINT:
					{
						SetWindowLongPtr(_hParent, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
						return TRUE;
					}
					case CDDS_ITEMPREPAINT:
					{
						/* set correct bk color*/
						lpCD->clrTextBk = ListView_GetBkColor(_hSelf);

						/* get correct font */
						UINT	iItem = (UINT)lpCD->nmcd.dwItemSpec;
						if (iItem >= _uMaxFolders) {
							wstring	strFilePath = _pExProp->szCurrentPath + _vFileList[iItem].strNameExt;
							if (IsFileOpen(strFilePath.c_str()) == TRUE) {
								::SelectObject(lpCD->nmcd.hdc, _hFontUnder);
								SetWindowLongPtr(_hParent, DWLP_MSGRESULT, CDRF_NEWFONT);
							}
						}
						
						if (_vFileList[iItem].bParent)
						{
							SetWindowLongPtr(_hParent, DWLP_MSGRESULT, CDRF_NEWFONT | CDRF_NOTIFYPOSTPAINT);
						}
						else
						{
							SetWindowLongPtr(_hParent, DWLP_MSGRESULT, CDRF_NEWFONT);
						}

						return TRUE;
					}					
					case CDDS_ITEMPOSTPAINT:
					{
						UINT	iItem = (UINT)lpCD->nmcd.dwItemSpec;
						
						RECT rc = { 0 };
						ListView_GetSubItemRect(_hSelf, iItem, lpCD->iSubItem, LVIR_ICON, &rc);

						UINT state = ListView_GetItemState(_hSelf, iItem, LVIS_SELECTED);
						bool isSel = ((state & LVIS_SELECTED) ? (::GetFocus() == _hSelf) : ((state & LVIS_DROPHILITED) == LVIS_DROPHILITED));
						
						ImageList_Draw(_hImlParent, ICON_PARENT, lpCD->nmcd.hdc, rc.left, rc.top, ILD_NORMAL | (isSel ? ILD_SELECTED : 0));
						
						return TRUE;
						
						break;
					}
					default:
						return FALSE;
				}
				break;
			}
			default:
				break;
		}
	}
	else if (nmhdr->hwndFrom == _hHeader)
	{
		switch (nmhdr->code)
		{
			case HDN_BEGINTRACK:
			{
				/* activate static change of column size */
				POINT	pt	= {0};
				::GetCursorPos(&pt);
				_lMouseTrackPos = pt.x;
				_iMouseTrackItem = ((LPNMHEADER)lParam)->iItem;
				SetWindowLongPtr(_hParent, DWLP_MSGRESULT, TRUE);

				/* start hooking */
				if (gWinVersion < WV_NT) {
					hookMouse = ::SetWindowsHookEx(WH_MOUSE, (HOOKPROC)hookProcMouse, _hInst, 0);
				} else {
					hookMouse = ::SetWindowsHookEx(WH_MOUSE_LL, (HOOKPROC)hookProcMouse, _hInst, 0);
				}

				if (!hookMouse)
				{
					DWORD dwError = ::GetLastError();
					TCHAR  str[128];
					::wsprintf(str, _T("GetLastError() returned %lu"), dwError);
					::MessageBox(NULL, str, _T("SetWindowsHookEx(MOUSE) failed"), MB_OK | MB_ICONERROR);
				}

				return TRUE;
			}
			case HDN_ITEMCHANGING:
			{
				/* avoid resize when double click */
				LPNMHEADER	pnmh = (LPNMHEADER)lParam;

				UINT uWidth = ListView_GetColumnWidth(_hSelf, pnmh->iItem);
				if ((_pExProp->bAddExtToName == TRUE) && (_lMouseTrackPos == 0) && 
					(pnmh->iItem == 0) && (pnmh->pitem->cxy != uWidth) && (pnmh->pitem->cxy != 0))
				{
					TCHAR	pszItemText[MAX_PATH];
					HDC		hDc			= ::GetDC(_hSelf);
					SIZE	size		= {0};
					INT		iWidthMax	= 0;

					/* get font length */
					HFONT hDefFont = (HFONT)::SelectObject(hDc, _hFont);

					for (UINT i = 0; i < _uMaxElements; i++)
					{
						if ((i < (INT)_uMaxFolders) && (_pExProp->bViewBraces == TRUE)) {
							_stprintf(pszItemText, _T("[%s]"), _vFileList[i].strName.c_str());
						} else {
							_tcscpy(pszItemText, _vFileList[i].strName.c_str());
						}
						::GetTextExtentPoint32(hDc, pszItemText, _tcslen(pszItemText), &size);

						if (iWidthMax < size.cx)
							iWidthMax = size.cx;
					}
					SelectObject(hDc, hDefFont);
					_lMouseTrackPos = -1;
					ListView_SetColumnWidth(_hSelf, 0, iWidthMax + 24);
					_lMouseTrackPos = 0;
					
					SetWindowLongPtr(_hParent, DWLP_MSGRESULT, TRUE);
					return TRUE;
				}
				break;
			}
			default:
				break;
		}
	}

	return FALSE;
}

void FileList::UpdateOverlayIcon(void)
{
	UINT	i = 0;

	while (1)
	{
		DWORD	dwCase = ::WaitForMultipleObjects(FL_EVT_MAX, _hEvent, FALSE, INFINITE);

		switch (dwCase)
		{
			case FL_EVT_EXIT:
			{
				LIST_UNLOCK();
				return;
			}
			case FL_EVT_INT:
			{
				i = _uMaxElements;
				LIST_UNLOCK();
				break;
			}
			case FL_EVT_START:
			{
				i = 0;
				if (_vFileList.size() == 0)
					break;

				LIST_LOCK();

				/* step over parent icon */
				if ((_uMaxFolders != 0) && (_vFileList[0].bParent == FALSE)) {
					i = 1;
				}

				::SetEvent(_hEvent[FL_EVT_NEXT]);
				break;
			}
			case FL_EVT_NEXT:
			{
				if (::WaitForSingleObject(_hEvent[FL_EVT_INT], 1) == WAIT_TIMEOUT)
				{
					if (i < _uMaxFolders) {
						::PostMessage(_hSelf, EXM_UPDATE_OVERICON, i, (LPARAM)DEVT_DIRECTORY);
					} else if (i < _uMaxElements) {
						::PostMessage(_hSelf, EXM_UPDATE_OVERICON, i, (LPARAM)DEVT_FILE);
					} else {
						LIST_UNLOCK();
					}
					i++;
				} else {
					::SetEvent(_hEvent[FL_EVT_INT]);
				}
				break;
			}
		}
	}
}

void FileList::ReadIconToList(UINT iItem, LPINT piIcon, LPINT piOverlay)
{
	INT			iIconSelected	= 0;
	eDevType	type			= (iItem < _uMaxFolders ? DEVT_DIRECTORY : DEVT_FILE);

	if (_vFileList[iItem].iIcon == -1) {
		ExtractIcons(_pExProp->szCurrentPath, _vFileList[iItem].strNameExt.c_str(), 
			type, &_vFileList[iItem].iIcon, &iIconSelected, NULL);
	}
	*piIcon		= _vFileList[iItem].iIcon;
	*piOverlay	= _vFileList[iItem].iOverlay;
}

void FileList::ReadArrayToList(LPTSTR szItem, INT iItem ,INT iSubItem)
{
	/* copy into temp */
	switch (iSubItem)
	{
		case 0:
			if ((iItem < (INT)_uMaxFolders) && (_pExProp->bViewBraces == TRUE)) {
				_stprintf(szItem, _T("[%s]"), _vFileList[iItem].strName.c_str());
			} else {
				_stprintf(szItem, _T("%s"), _vFileList[iItem].strName.c_str());
			}
			break;
		case 1:
			if ((iItem < (INT)_uMaxFolders) || (_pExProp->bAddExtToName == FALSE)) {
				_tcscpy(szItem, _vFileList[iItem].strExt.c_str());
			} else {
				szItem[0] = '\0';
			}
			break;
		case 2:
			_tcscpy(szItem, _vFileList[iItem].strSize.c_str());
			break;
		default:
			_tcscpy(szItem, _vFileList[iItem].strDate.c_str());
			break;
	}
}

void FileList::viewPath(LPCTSTR currentPath, BOOL redraw)
{
	TCHAR					TEMP[MAX_PATH];
	WIN32_FIND_DATA			Find			= {0};
	HANDLE					hFind			= NULL;
	vector<tFileListData>	vFoldersTemp;
	vector<tFileListData>	vFilesTemp;

	/* end thread if it is in run mode */
	::SetEvent(_hEvent[FL_EVT_INT]);

	/* add backslash if necessary */
	_tcsncpy(TEMP, currentPath, MAX_PATH-1);
	if (TEMP[_tcslen(TEMP) - 1] != '\\')
		_tcscat(TEMP, _T("\\"));

	/* clear data */
	_uMaxElementsOld = _uMaxElements;

	/* find every element in folder */
	_tcscat(TEMP, _T("*"));
	hFind = ::FindFirstFile(TEMP, &Find);

	if (hFind != INVALID_HANDLE_VALUE)
	{
		/* get current filters */
		tFileListData	tempData;
		TCHAR	szFilter[MAX_PATH]		= _T("\0");
		TCHAR	szExFilter[MAX_PATH]	= _T("\0");
		GetFilterLists(szFilter, szExFilter);

		do 
		{
			if (IsValidFolder(Find) == TRUE)
			{
				/* get data in order of list elements */
				tempData.bParent		= FALSE;
				tempData.iIcon			= -1;
				tempData.iOverlay		= 0;
				tempData.bFolder		= TRUE;
				tempData.bHidden		= ((Find.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0);
				tempData.strName		= Find.cFileName;
				tempData.strNameExt		= Find.cFileName;
				tempData.strExt			= _T("");

				if (_pExProp->bViewLong == TRUE)
				{
					tempData.strSize	= _T("<DIR>");
					tempData.i64Size	= 0;
					GetDate(Find.ftLastWriteTime, tempData.strDate);
					tempData.i64Date	= 0;
				}

				vFoldersTemp.push_back(tempData);
			}
			else if ((IsValidFile(Find) == TRUE) && (DoFilter(Find.cFileName, szFilter, szExFilter) == TRUE))
			{
				/* store for correct sorting the complete name (with extension) */
				tempData.strNameExt		= Find.cFileName;
				tempData.bParent		= FALSE;
				tempData.bFolder		= FALSE;
				tempData.iIcon			= -1;
				tempData.iOverlay		= 0;
				tempData.bHidden		= ((Find.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0);

				/* extract name and extension */
				LPTSTR	extBeg = _tcsrchr(&Find.cFileName[1], '.');

				if (extBeg != NULL) {
					*extBeg = '\0';
					tempData.strExt		= &extBeg[1];
				} else {
					tempData.strExt		= _T("");
				}

				if ((_pExProp->bAddExtToName == TRUE) && (extBeg != NULL)) {
					*extBeg = '.';
				}
				tempData.strName		= Find.cFileName;


				if (_pExProp->bViewLong == TRUE)
				{
					tempData.i64Size	= (((__int64)Find.nFileSizeHigh) << 32) + Find.nFileSizeLow;
					GetSize(tempData.i64Size, tempData.strSize);
					tempData.i64Date	= (((__int64)Find.ftLastWriteTime.dwHighDateTime) << 32) + Find.ftLastWriteTime.dwLowDateTime;
					GetDate(Find.ftLastWriteTime, tempData.strDate);
				}

				vFilesTemp.push_back(tempData);
			}
			else if ((IsValidParentFolder(Find) == TRUE) && (TEMP[4] != NULL))
			{
				/* if 'Find' is not a folder but a parent one */
				tempData.bParent		= TRUE;
				tempData.bHidden		= FALSE;
				tempData.bFolder		= TRUE;
				tempData.strName		= Find.cFileName;
				tempData.strNameExt		= Find.cFileName;
				tempData.strExt			= _T("");

				if (_pExProp->bViewLong == TRUE)
				{
					tempData.strSize	= _T("<DIR>");
					tempData.i64Size	= 0;
					GetDate(Find.ftLastWriteTime, tempData.strDate);
					tempData.i64Date	= 0;
				}

				vFoldersTemp.push_back(tempData);
			}
		} while (FindNextFile(hFind, &Find));

		/* close file search */
		::FindClose(hFind);
	}

	/* save current path */
	_tcscpy(_pExProp->szCurrentPath, currentPath);

	/* add current dir to stack */
	PushDir(currentPath);

	LIST_LOCK();

	/* delete old global list */
	_vFileList.clear();

	/* copy folders and files list into global array */
	_vFileList.insert(_vFileList.end(), vFoldersTemp.begin(), vFoldersTemp.end());
	_vFileList.insert(_vFileList.end(), vFilesTemp.begin(), vFilesTemp.end());

	/* set max elements in list */
	_uMaxFolders	= vFoldersTemp.size();
	_uMaxElements	= _uMaxFolders + vFilesTemp.size();

	/* update list content */
	UpdateList();

	/* select first entry */
	if (redraw == TRUE)
		SetFocusItem(0);

	LIST_UNLOCK();

	/* start with update of overlay icons */
	::SetEvent(_hEvent[FL_EVT_START]);
}

void FileList::filterFiles(LPCTSTR currentFilter)
{
	SetFilter(currentFilter);
	viewPath(_pExProp->szCurrentPath, TRUE);
}

void FileList::SetFilter(LPCTSTR pszNewFilter)
{
	TCHAR	szTempFilter[MAX_PATH];

	/* delete current filter */
	_szFileFilter[0] = '\0';

	/* copy new filter into buffer for tok */
	_tcscpy(szTempFilter, pszNewFilter);

	/* get new filter (remove spaces) */
	LPTSTR	ptr = _tcstok(szTempFilter, _T(" "));

	while (ptr != NULL) {
		_tcscat(_szFileFilter, ptr);
		ptr = _tcstok(NULL, _T(" "));
	}
}

void FileList::SelectFolder(LPTSTR selFile)
{
	for (UINT uFolder = 0; uFolder < _uMaxFolders; uFolder++)
	{
		if (_tcsicmp(_vFileList[uFolder].strName.c_str(), selFile) == 0)
		{
			SetFocusItem(uFolder);
			return;
		}
	}
}

void FileList::SelectCurFile(void)
{
	extern TCHAR g_currentFile[MAX_PATH];

	for (UINT uFile = _uMaxFolders; uFile < _uMaxElements; uFile++) {
		if (_tcsstr(g_currentFile, _vFileList[uFile].strNameExt.c_str()) != NULL ) {
			SetFocusItem(uFile);
			return;
		}
	}
}

void FileList::UpdateList(void)
{
	/* sort files and folders*/
	FileListComparer comparer(_pExProp->iSortPos, _pExProp->bAscending);
	std::sort(_vFileList.begin(), _vFileList.end(), comparer);

	/* avoid flickering */
	if (_uMaxElementsOld != _uMaxElements) {
		ListView_SetItemCountEx(_hSelf, _uMaxElements, LVSICF_NOSCROLL);
	} else {
		::RedrawWindow(_hSelf, NULL, NULL, TRUE);
	}
}

void FileList::SetColumns(void)
{
	LVCOLUMN	ColSetup			= {0};

	ListView_DeleteColumn(_hSelf, 3);
	ListView_DeleteColumn(_hSelf, 2);
	ListView_DeleteColumn(_hSelf, 1);
	ListView_DeleteColumn(_hSelf, 0);

	if ((_bOldAddExtToName != _pExProp->bAddExtToName) && (_pExProp->iSortPos > 0))
	{
		if (_pExProp->bAddExtToName == FALSE)
			_pExProp->iSortPos++;
		else
			_pExProp->iSortPos--;

		_bOldAddExtToName = _pExProp->bAddExtToName;
	}
	if (_bOldViewLong != _pExProp->bViewLong)
	{
		if ((_pExProp->bViewLong == FALSE) &&
			(((_pExProp->iSortPos > 0) && (_pExProp->bAddExtToName == TRUE)) ||
			 ((_pExProp->iSortPos > 1) && (_pExProp->bAddExtToName == FALSE))))
			_pExProp->iSortPos = 0;
		_bOldViewLong = _pExProp->bViewLong;
	}

	ColSetup.mask		= LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
	ColSetup.fmt		= LVCFMT_LEFT;
	ColSetup.pszText	= cColumns[0];
	ColSetup.cchTextMax = _tcslen(cColumns[0]);
	ColSetup.cx			= _pExProp->iColumnPosName;
	ListView_InsertColumn(_hSelf, 0, &ColSetup);
	ColSetup.pszText	= cColumns[1];
	ColSetup.cchTextMax = _tcslen(cColumns[1]);
	ColSetup.cx			= _pExProp->iColumnPosExt;
	ListView_InsertColumn(_hSelf, 1, &ColSetup);

	if (_pExProp->bViewLong)
	{
		ColSetup.fmt		= LVCFMT_RIGHT;
		ColSetup.pszText	= cColumns[2];
		ColSetup.cchTextMax = _tcslen(cColumns[2]);
		ColSetup.cx			= _pExProp->iColumnPosSize;
		ListView_InsertColumn(_hSelf, 2, &ColSetup);
		ColSetup.fmt		= LVCFMT_LEFT;
		ColSetup.pszText	= cColumns[3];
		ColSetup.cchTextMax = _tcslen(cColumns[3]);
		ColSetup.cx			= _pExProp->iColumnPosDate;
		ListView_InsertColumn(_hSelf, 3, &ColSetup);
	}
	SetOrder();
}

#ifndef HDF_SORTDOWN
#define HDF_SORTDOWN	0x0200
#define HDF_SORTUP		0x0400
#endif

void FileList::SetOrder(void)
{
	HDITEM	hdItem		= {0};
	UINT	uMaxHeader	= Header_GetItemCount(_hHeader);

	for (UINT i = 0; i < uMaxHeader; i++)
	{
		hdItem.mask	= HDI_FORMAT;
		Header_GetItem(_hHeader, i, &hdItem);

		if (gWinVersion < WV_XP)
		{
			hdItem.mask &= ~HDI_BITMAP;
			hdItem.fmt  &= ~(HDF_BITMAP | HDF_BITMAP_ON_RIGHT);
			if (i == _pExProp->iSortPos)
			{
				hdItem.mask |= HDI_BITMAP;
				hdItem.fmt  |= (HDF_BITMAP | HDF_BITMAP_ON_RIGHT);
				hdItem.hbm   = _pExProp->bAscending ? _bmpSortUp : _bmpSortDown;
			}
		}
		else
		{
			hdItem.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
			if (i == _pExProp->iSortPos)
			{
				hdItem.fmt |= _pExProp->bAscending ? HDF_SORTUP : HDF_SORTDOWN;
			}
		}
		Header_SetItem(_hHeader, i, &hdItem);
	}
}

/*********************************************************************************
 *	User interactions
 */ 
void FileList::onRMouseBtn(void)
{
	vector<wstring>	data;
	BOOL			isParent = FALSE;

	/* create data */
	for (UINT uList = 0; uList < _uMaxElements; uList++)
	{
		if (ListView_GetItemState(_hSelf, uList, LVIS_SELECTED) == LVIS_SELECTED) 
		{
			if (uList == 0) 
			{
				if (_tcsicmp(_vFileList[0].strName.c_str(), _T("..")) == 0) 
				{
					ListView_SetItemState(_hSelf, uList, 0, 0xFF);
					isParent = TRUE;
					continue;
				}
			}

			if (uList < _uMaxFolders) {
				data.push_back(_pExProp->szCurrentPath + _vFileList[uList].strName + _T("\\"));
			} else {
				data.push_back(_pExProp->szCurrentPath + _vFileList[uList].strNameExt);
			}
		}
	}

	if (data.size() == 0) {
		data.push_back(_pExProp->szCurrentPath);
	}
	::SendMessage(_hParent, EXM_RIGHTCLICK, isParent && (data.size() == 1), (LPARAM)&data);
}

void FileList::onLMouseBtnDbl(void)
{
	UINT	selRow		= ListView_GetSelectionMark(_hSelf);

	if (selRow != -1) {
		if (selRow < _uMaxFolders) {
			::SendMessage(_hParent, EXM_OPENDIR, 0, (LPARAM)_vFileList[selRow].strName.c_str());
		} else {
			::SendMessage(_hParent, EXM_OPENFILE, 0, (LPARAM)_vFileList[selRow].strNameExt.c_str());
		}
	}
}

void FileList::onSelectItem(TCHAR charkey)
{
	UINT	selRow		= ListView_GetSelectionMark(_hSelf);

	/* restart timer */
	::KillTimer(_hSelf, EXT_SEARCHFILE);
	::SetTimer(_hSelf, EXT_SEARCHFILE, 1000, NULL);

	/* initilize again if error previous occured */
	if (selRow < 0) selRow = 0;

	/* add character to string */
	_tcsncat(_strSearchFile, &charkey, 1);

	/* on first call start searching on next element */
	if (_bSearchFile == FALSE)
		selRow++;

	UINT startPos	= selRow;
	BOOL bRet		= FindNextItemInList(_uMaxFolders, _uMaxElements, &selRow);
	if ((bRet == FALSE) && (_bSearchFile == TRUE))
	{
		_strSearchFile[_tcslen(_strSearchFile)-1] = '\0';
		selRow++;
		bRet = FindNextItemInList(_uMaxFolders, _uMaxElements, &selRow);
	}

	if (bRet == TRUE)
	{
		/* select only one item */
		for (UINT i = 0; i < _uMaxElements; i++)
		{
			ListView_SetItemState(_hSelf, i, (selRow == i ? LVIS_SELANDFOC : 0), 0xFF);
		}
		ListView_SetSelectionMark(_hSelf, selRow);
		ListView_EnsureVisible(_hSelf, selRow, TRUE);
	}

	/* mark that we starting with searching */
	_bSearchFile = TRUE;
}

void FileList::onSelectAll(void)
{
	INT firstRow	= 0;
	if (_uMaxFolders != 0)
		firstRow = (_vFileList[0].bParent ? 0 : -1);

	for (UINT i = 0; i < _uMaxElements; i++)
	{
		ListView_SetItemState(_hSelf, i, (i == firstRow) ? 0 : LVIS_SELANDFOC, 0xFF);
	}
	ListView_SetSelectionMark(_hSelf, firstRow);
	ListView_EnsureVisible(_hSelf, firstRow, TRUE);
}

void FileList::onCut(void)
{
	CIDataObject	dataObj(NULL);
	FolderExChange(NULL, &dataObj, DROPEFFECT_MOVE);
}

void FileList::onCopy(void)
{
	CIDataObject	dataObj(NULL);
	FolderExChange(NULL, &dataObj, DROPEFFECT_COPY);
}

void FileList::onPaste(void)
{
	/* Insure desired format is there, and open clipboard */
	if (::IsClipboardFormatAvailable(CF_HDROP) == TRUE) {
		if (::OpenClipboard(NULL) == FALSE)
			return;
	} else {
		return;
	}

	/* Get handle to Dropped Filelist data, and number of files */
	LPDROPFILES hFiles	= (LPDROPFILES)::GlobalLock(::GetClipboardData(CF_HDROP));
	if (hFiles == NULL) {
		ErrorMessage(::GetLastError());
		return;
	}
	LPBYTE	hEffect	= (LPBYTE)::GlobalLock(::GetClipboardData(::RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT)));
	if (hEffect == NULL) {
		ErrorMessage(::GetLastError());
		return;
	}
	if (hEffect[0] == 2) { 
		doPaste(_pExProp->szCurrentPath, hFiles, DROPEFFECT_MOVE);
	} else if (hEffect[0] == 5) {
		doPaste(_pExProp->szCurrentPath, hFiles, DROPEFFECT_COPY);
	}
	::GlobalUnlock(hFiles);
	::GlobalUnlock(hEffect);
	::CloseClipboard();

	::KillTimer(_hParent, EXT_UPDATEACTIVATEPATH);
	::SetTimer(_hParent, EXT_UPDATEACTIVATEPATH, 200, NULL);
}

void FileList::onDelete(bool immediate)
{
	UINT	lengthPath	= _tcslen(_pExProp->szCurrentPath);
	UINT	bufSize		= ListView_GetSelectedCount(_hSelf) * MAX_PATH;
	LPTSTR	lpszFiles	= new TCHAR[bufSize];
	::ZeroMemory(lpszFiles, bufSize);

	/* add files to payload and seperate with "\0" */
	UINT	offset	= 0;
	for (UINT i = 0; i < _uMaxElements; i++)
	{
		if (ListView_GetItemState(_hSelf, i, LVIS_SELECTED) == LVIS_SELECTED)
		{
			if ((i == 0) && (_vFileList[i].bParent == TRUE)) {
				continue;
			}
			_tcscpy(&lpszFiles[offset], _pExProp->szCurrentPath);
			_tcscpy(&lpszFiles[offset+lengthPath], _vFileList[i].strNameExt.c_str());
			offset += lengthPath + _vFileList[i].strNameExt.size() + 1;
		}
	}

	/* delete folder into recycle bin */
	SHFILEOPSTRUCT	fileOp	= {0};
	fileOp.hwnd				= _hParent;
	fileOp.pFrom			= lpszFiles;
	fileOp.fFlags			= (immediate ? 0 : FOF_ALLOWUNDO);
	fileOp.wFunc			= FO_DELETE;
	SHFileOperation(&fileOp);
}

BOOL FileList::FindNextItemInList(UINT maxFolder, UINT maxData, LPUINT puPos)
{
	TCHAR	pszFileName[MAX_PATH];
	BOOL	bRet		= FALSE;
	UINT	iStartPos	= *puPos;

	/* search in list */
	for (UINT i = iStartPos; i != (iStartPos-1); i++)
	{
		/* if max data is reached, set iterator to zero */
		if (i == maxData)
		{
			if (iStartPos <= 1)
				break;
			else
				i = 0;
		}

		_tcscpy(pszFileName, _vFileList[i].strName.c_str());

		/* trancate the compare length */
		pszFileName[_tcslen(_strSearchFile)] = '\0';

		if (_tcsicmp(pszFileName, _strSearchFile) == 0)
		{
			/* string found in any following case */
			bRet	= TRUE;
			*puPos	= i;
			break;
		}
	}

	return bRet;
}

void FileList::GetSize(__int64 size, wstring & str)
{
	TCHAR origSize[MAX_PATH];
	TCHAR formattedSize[MAX_PATH];
	str = _T("");

	switch (_pExProp->fmtSize)
	{
		case SFMT_BYTES:
		case SFMT_KBYTE:
		{
			size = _pExProp->fmtSize == SFMT_KBYTE ? size / 1024 : size;
			_stprintf(origSize, _T("%I64d"), size);
			GetNumberFormat(LOCALE_USER_DEFAULT, 0, origSize, &_numberFmt, formattedSize, MAX_PATH);
			str = formattedSize;
			if (_pExProp->fmtSize == SFMT_KBYTE)
			{
				str = str + _T(" kB");
			}
			break;
		}
		case SFMT_DYNAMIC:
		case SFMT_DYNAMIC_EX:
		{
			int i = 0;
			double dblSize = size;

			while (dblSize > 1024 && i < 3)
			{
				dblSize /= 1024.;
				i++;
			}

			NUMBERFMT nfCopy = _numberFmt;
			nfCopy.NumDigits = _pExProp->fmtSize == SFMT_DYNAMIC_EX ? 1 : 0;

			_stprintf(origSize, _T("%f"), dblSize);
			GetNumberFormat(LOCALE_USER_DEFAULT, 0, origSize, &nfCopy, formattedSize, MAX_PATH);
			str = formattedSize;

			switch (i)
			{
			case 0:
				str = str + _T(" b"); break;
			case 1:
				str = str + _T(" k"); break;
			case 2:
				str = str + _T(" M"); break;
			default:
				str = str + _T(" G"); break;
			}
			break;
		}
	}
}

void FileList::GetDate(FILETIME ftLastWriteTime, wstring & str)
{
	FILETIME		ftLocalTime;
	SYSTEMTIME		sysTime;
	TCHAR			TEMP[32];

	FileTimeToLocalFileTime(&ftLastWriteTime, &ftLocalTime);
	FileTimeToSystemTime(&ftLocalTime, &sysTime);

	if (_pExProp->fmtDate == DFMT_ENG)
		_stprintf_s(TEMP, 32, _T("%02d/%02d/%02d %02d:%02d"), sysTime.wYear % 100, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute);
	else
		_stprintf_s(TEMP, 32, _T("%02d.%02d.%04d %02d:%02d"), sysTime.wDay, sysTime.wMonth, sysTime.wYear, sysTime.wHour, sysTime.wMinute);

	str = TEMP;
}

void FileList::GetFilterLists(LPTSTR pszFilter, LPTSTR pszExFilter)
{
	LPTSTR	ptr			= _szFileFilter;
	LPTSTR	exc_beg		= NULL;
	LPTSTR	exc_end		= NULL;

	do
	{
		exc_beg = _tcsstr(ptr, _T("[^"));

		if (exc_beg == NULL)
		{
			_tcscat(pszFilter, ptr);
		}
		else
		{
			exc_end = _tcsstr(exc_beg, _T("]"));

			if (exc_end > exc_beg)
			{
				_tcsncat(pszFilter, ptr, exc_beg-ptr);
				_tcsncat(pszExFilter, &exc_beg[2], exc_end-exc_beg-2);
				ptr = exc_end+1;
			}
		}
	} while (exc_beg != NULL);
}

BOOL FileList::DoFilter(LPCTSTR pszFileName, LPTSTR pszFilter, LPTSTR pszExFilter)
{
	TCHAR	TEMP[256];
	LPTSTR	ptr		= NULL;

	if ((pszFileName == NULL) || (pszFilter == NULL) || (pszExFilter == NULL))
		return FALSE;

	if (_tcsicmp(pszFilter, _T("*.*")) == 0)
		return TRUE;
	
	if (pszExFilter[0] != '\0')
	{
		_tcsncpy(TEMP, pszExFilter, 256);

		ptr		= _tcstok(TEMP, _T(";"));
		while (ptr != NULL)
		{
			if (WildCmp(pszFileName, ptr) != NULL)
				return FALSE;
			ptr = _tcstok(NULL, _T(";"));
		}
	}

	_tcsncpy(TEMP, pszFilter, 256);

	ptr		= _tcstok(TEMP, _T(";"));
	while (ptr != NULL)
	{
		if (WildCmp(pszFileName, ptr) != NULL)
			return TRUE;
		ptr = _tcstok(NULL, _T(";"));
	}
	return FALSE;
}

INT FileList::WildCmp(LPCTSTR string, LPCTSTR wild)
{
	// Written by Jack Handy - jakkhandy@hotmail.com
	// See: http://www.codeproject.com/string/wildcmp.asp
	LPCTSTR		cp = NULL;
	LPCTSTR		mp = NULL;

	while ((*string) && (*wild != '*')) {
		if ((tolower(*wild) != tolower(*string)) && (*wild != '?')) {
			return 0;
		}
		wild++;
		string++;
	}

	while (*string) {
		if (*wild == '*') {
			if (!*++wild) {
				return 1;
			}
			mp = wild;
			cp = string+1;
		} else if ((tolower(*wild) == tolower(*string)) || (*wild == '?')) {
			wild++;
			string++;
		} else {
			wild = mp;
			string = cp++;
		}
	}

	while (*wild == '*') {
		wild++;
	}
	return !*wild;
}

/**************************************************************************************
 *	Stack functions
 */
void FileList::SetToolBarInfo(ToolBar *ToolBar, UINT idUndo, UINT idRedo)
{
	_pToolBar	= ToolBar;
	_idUndo		= idUndo;
	_idRedo		= idRedo;
	ResetDirStack();
}

void FileList::ResetDirStack(void)
{
	_vDirStack.clear();
	_itrPos = _vDirStack.end();
	UpdateToolBarElements();
}

void FileList::ToggleStackRec(void)
{
	_isStackRec ^= TRUE;
}

void FileList::PushDir(LPCTSTR pszPath)
{
	if (_isStackRec == TRUE)
	{
		tStaInfo	StackInfo;
		StackInfo.strPath = pszPath;

		if (_itrPos != _vDirStack.end())
		{
			_vDirStack.erase(_itrPos + 1, _vDirStack.end());

			if (_tcsicmp(pszPath, _itrPos->strPath.c_str()) != 0)
			{
				_vDirStack.push_back(StackInfo);
				_itrPos = _vDirStack.end() - 1;
			}
		}
		else
		{
			_vDirStack.push_back(StackInfo);
			_itrPos = _vDirStack.end() - 1;
		}
	}

	UpdateToolBarElements();
}

bool FileList::GetPrevDir(LPTSTR pszPath, vector<wstring> & vStrItems)
{
	if (_vDirStack.size() > 1)
	{
		if (_itrPos != _vDirStack.begin())
		{
			_itrPos--;
			_tcscpy(pszPath, _itrPos->strPath.c_str());
			vStrItems = _itrPos->vStrItems;
			return true;
		}
	}
	return false;
}

bool FileList::GetNextDir(LPTSTR pszPath, vector<wstring> & vStrItems)
{
	if (_vDirStack.size() > 1)
	{
		if (_itrPos != _vDirStack.end() - 1)
		{
			_itrPos++;
			_tcscpy(pszPath, _itrPos->strPath.c_str());
			vStrItems = _itrPos->vStrItems;
			return true;
		}
	}
	return false;
}

INT FileList::GetPrevDirs(LPTSTR *pszPathes)
{
	INT i = 0;
	vector<tStaInfo>::iterator	itr	= _itrPos;

	if (_vDirStack.size() > 1)
	{
		while (1) {
			if (itr != _vDirStack.begin()) {
				itr--;
				if (pszPathes) {
					_tcscpy(pszPathes[i], itr->strPath.c_str());
				}
			} else {
				break;
			}
			i++;
		}
	}
	return i;
}

INT FileList::GetNextDirs(LPTSTR	*pszPathes)
{
	INT i = 0;
	vector<tStaInfo>::iterator	itr	= _itrPos;

	if (_vDirStack.size() > 1)
	{
		while (1) {
			if (itr != _vDirStack.end() - 1) {
				itr++;
				if (pszPathes) {
					_tcscpy(pszPathes[i], itr->strPath.c_str());
				}
			} else {
				break;
			}
			i++;
		}
	}
	return i;
}

void FileList::OffsetItr(INT offsetItr, vector<wstring> & vStrItems)
{
	_itrPos += offsetItr;
	vStrItems = _itrPos->vStrItems;
	UpdateToolBarElements();
}

void FileList::UpdateToolBarElements(void)
{
	bool canRedo = !_vDirStack.empty() && (_itrPos != _vDirStack.end() - 1);
	_pToolBar->enable(_idRedo, canRedo);
	_pToolBar->enable(_idUndo, _itrPos != _vDirStack.begin());
}

void FileList::UpdateSelItems(void)
{
	if (_isStackRec == TRUE)
	{
		_itrPos->vStrItems.clear();

		for (UINT i = 0; i < _uMaxElements; i++)
		{
			if (ListView_GetItemState(_hSelf, i, LVIS_SELECTED) == LVIS_SELECTED) {
				_itrPos->vStrItems.push_back(_vFileList[i].strNameExt);
			}
		}
	}
}

void FileList::SetItems(vector<wstring> vStrItems)
{
	UINT	selType = LVIS_SELANDFOC;

	for (UINT i = 0; i < _uMaxElements; i++)
	{
		for (UINT itemPos = 0; itemPos < vStrItems.size(); itemPos++)
		{
			if (_vFileList[i].strNameExt == vStrItems[itemPos])
			{
				ListView_SetItemState(_hSelf, i, selType, 0xFF);

				/* set first item in view */
				if (selType == LVIS_SELANDFOC) {
					ListView_EnsureVisible(_hSelf, _uMaxElements - 1, FALSE);
					ListView_EnsureVisible(_hSelf, i, FALSE);
				}

				/* delete last found item to be faster in compare */
				vStrItems.erase(vStrItems.begin() + itemPos);

				/* if last item were delete return from function */
				if (vStrItems.size() == 0)
					return;

				selType = LVIS_SELECTED;
				break;
			}
			else
			{
				ListView_SetItemState(_hSelf, i, 0, 0xFF);
			}
		}
	}
}


/***************************************************************************************
 *  Drag'n'Drop, Cut and Copy of folders
 */
void FileList::FolderExChange(CIDropSource* pdsrc, CIDataObject* pdobj, UINT dwEffect)
{
	UINT	parsz = _tcslen(_pExProp->szCurrentPath);
	UINT	bufsz = sizeof(DROPFILES) + sizeof(TCHAR);

	/* get buffer size */
	for (UINT i = 0; i < _uMaxElements; i++) {
		if (ListView_GetItemState(_hSelf, i, LVIS_SELECTED) == LVIS_SELECTED) {
			if ((i == 0) && (_vFileList[i].bParent == TRUE)) continue;
			bufsz += (parsz + _vFileList[i].strNameExt.size() + 1) * sizeof(TCHAR);
		}
	}

	HDROP hDrop = (HDROP)GlobalAlloc(GHND|GMEM_SHARE, bufsz);

	if (NULL == hDrop)
		return;

	LPDROPFILES lpDropFileStruct = (LPDROPFILES)::GlobalLock(hDrop);
	if (NULL == lpDropFileStruct) {
		GlobalFree(hDrop);
		return;
	}				
	::ZeroMemory(lpDropFileStruct, bufsz);

	lpDropFileStruct->pFiles = sizeof(DROPFILES);
	lpDropFileStruct->pt.x = 0;
	lpDropFileStruct->pt.y = 0;
	lpDropFileStruct->fNC = FALSE;
	lpDropFileStruct->fWide = TRUE;

	/* add files to payload and seperate with "\0" */
	UINT	offset	= 0;
	LPTSTR	szPath	= (LPTSTR)&lpDropFileStruct[1];
	for (UINT i = 0; i < _uMaxElements; i++)
	{
		if (ListView_GetItemState(_hSelf, i, LVIS_SELECTED) == LVIS_SELECTED)
		{
			if ((i == 0) && (_vFileList[i].bParent == TRUE)) {
				continue;
			}
			_tcscpy(&szPath[offset], _pExProp->szCurrentPath);
			_tcscpy(&szPath[offset+parsz], _vFileList[i].strNameExt.c_str());
			offset += parsz + _vFileList[i].strNameExt.size() + 1;
		}
	}

	GlobalUnlock(hDrop);

	/* Init the supported format */
	FORMATETC fmtetc	= {0}; 
	fmtetc.cfFormat		= CF_HDROP; 
	fmtetc.dwAspect		= DVASPECT_CONTENT; 
	fmtetc.lindex		= -1; 
	fmtetc.tymed		= TYMED_HGLOBAL;

	/* Init the medium used */
	STGMEDIUM medium = {0};
	medium.tymed	= TYMED_HGLOBAL;
	medium.hGlobal	= hDrop;

	/* Add it to DataObject */
	pdobj->SetData(&fmtetc, &medium, TRUE);

	if (pdsrc == NULL)
	{
		hDrop = (HDROP)GlobalAlloc(GHND|GMEM_SHARE, 4);
		if (NULL == hDrop)
			return;

		LPBYTE prefCopyData = (LPBYTE)::GlobalLock(hDrop);
		if (NULL == prefCopyData) {
			GlobalFree(hDrop);
			return;
		}

		int	eff = (dwEffect == DROPEFFECT_MOVE ? 2 : 5);
		::ZeroMemory(prefCopyData, 4);
		CopyMemory(prefCopyData, &eff, 1);
		::GlobalUnlock(hDrop);

		/* Init the supported format */
		fmtetc.cfFormat		= ::RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
		/* Init the medium used */
		medium.hGlobal		= hDrop;

		pdobj->SetData(&fmtetc, &medium, TRUE);

		if (::OleSetClipboard(pdobj) == S_OK) {
			::OleFlushClipboard();
		}
	}
	else
	{
		/* Initiate the Drag & Drop */
		DWORD	dwEffectResp;
		::DoDragDrop(pdobj, pdsrc, dwEffect, &dwEffectResp);
	}
}

bool FileList::OnDrop(FORMATETC* pFmtEtc, STGMEDIUM& medium, DWORD *pdwEffect)
{
	LPDROPFILES   hDrop = (LPDROPFILES)::GlobalLock(medium.hGlobal);
	if (NULL == hDrop)
		return false;

	/* get target */
	TCHAR	pszFilesTo[MAX_PATH];
	_tcscpy(pszFilesTo, _pExProp->szCurrentPath);

	/* get position */
	LVHITTESTINFO	hittest			= {0};
	::GetCursorPos(&hittest.pt);
	ScreenToClient(_hSelf, &hittest.pt);
	::SendMessage(_hSelf, LVM_SUBITEMHITTEST, 0, (LPARAM)&hittest);

	if ((UINT)hittest.iItem < _uMaxFolders)
	{
		if (_vFileList[hittest.iItem].bParent == TRUE) {
			::PathRemoveFileSpec(pszFilesTo);
			::PathRemoveFileSpec(pszFilesTo);
		} else {
			::PathAppend(pszFilesTo, _vFileList[hittest.iItem].strNameExt.c_str());
		}
	}

	doPaste(pszFilesTo, hDrop, *pdwEffect);

	return true;
}

bool FileList::doPaste(LPCTSTR pszTo, LPDROPFILES hData, const DWORD & dwEffect)
{
	/* get files from and to, to fill struct */
	UINT		headerSize				= sizeof(DROPFILES);
	UINT		payloadSize				= ::GlobalSize(hData) - headerSize;
	LPVOID		pPld					= (LPBYTE)hData + headerSize;
	LPTSTR		lpszFilesFrom			= NULL;

	if (((LPDROPFILES)hData)->fWide == TRUE) {
		lpszFilesFrom = (LPWSTR)pPld;
	} else {
		lpszFilesFrom = new TCHAR[payloadSize];
		::MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pPld, payloadSize, lpszFilesFrom, payloadSize);
	}

	if (lpszFilesFrom != NULL)
	{
		UINT count = 0;
		UINT length = payloadSize;
		if (((LPDROPFILES)hData)->fWide == TRUE) {
			length = payloadSize / 2;
		}
		for (UINT i = 0; i < length-1; i++) {
			if (lpszFilesFrom[i] == '\0') {
				count++;
			}
		}

		TCHAR	text[MAX_PATH + 32];
		if (dwEffect == DROPEFFECT_MOVE) {
			_stprintf(text, _T("Move %d file(s)/folder(s) to:\n\n%s"), count, pszTo);
		} else if (dwEffect == DROPEFFECT_COPY) {
			_stprintf(text, _T("Copy %d file(s)/folder(s) to:\n\n%s"), count, pszTo);
		}

		if (::MessageBox(_hSelf, text, _T("Explorer"), MB_YESNO) == IDYES)
		{
			// TODO move or copy the file views into other window in dependency to keystate
			SHFILEOPSTRUCT	fileOp	= {0};
			fileOp.hwnd				= _hParent;
			fileOp.pFrom			= lpszFilesFrom;
			fileOp.pTo				= pszTo;
			fileOp.fFlags			= FOF_RENAMEONCOLLISION;
			if (dwEffect == DROPEFFECT_MOVE) {
				fileOp.wFunc		= FO_MOVE;
			} else {
				fileOp.wFunc		= FO_COPY;
			}
			SHFileOperation(&fileOp);

			::KillTimer(_hParent, EXT_UPDATEACTIVATEPATH);
			::SetTimer(_hParent, EXT_UPDATEACTIVATEPATH, 200, NULL);
		}

		if (((LPDROPFILES)hData)->fWide == FALSE) {
			delete [] lpszFilesFrom;
		}
	}
	return true;
}

FileList::FileListComparer::FileListComparer(int column, bool ascending) :_column(column), _ascending(ascending) {}

bool FileList::FileListComparer::operator()(tFileListData a, tFileListData b) const
{
	// Sort first by bParent
	if (a.bParent != b.bParent)
	{
		return a.bParent > b.bParent;
	}
	else
	{
		// Then sort by bFolder
		if (a.bFolder != b.bFolder)
		{
			return a.bFolder > b.bFolder;
		}
		else
		{
			int resultNameExt = _wcsicmp(a.strNameExt.c_str(), b.strNameExt.c_str());
			__int64 sortResult = 0;

			if (a.bFolder)
			{
				// If both are folders, sort by name ascending
				return resultNameExt < 0;
			}
			else
			{
				// If both are files, sort using the _column parameter
				switch (_column)
				{
				case 0:
					// Sort by name
					sortResult = resultNameExt;
					break;
				case 1:
					// Sort by ext, then by name
					sortResult = _wcsicmp(a.strExt.c_str(), b.strExt.c_str());
					break;
				case 2:
					// Sort by size, then by name
					sortResult = a.i64Size - b.i64Size;
					break;
				case 3:
					// Sort by date, then by name
					sortResult = a.i64Date - b.i64Date;
					break;
				default:
					break;
				}

				// If the items' primary sort fields are equal, use nameExt as the secondary sort field
				if (sortResult == 0)
				{
					sortResult = resultNameExt;
				}

				// If we need to sort descending, reverse the order
				if (!_ascending)
				{
					sortResult *= -1;
				}
			}
			return sortResult < 0;
		}
	}
}