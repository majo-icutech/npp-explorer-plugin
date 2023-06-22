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
#include "OptionDialog.h"
#include "Explorer.h"
#include <Commctrl.h>
#include <shlobj.h>


// Set a call back with the handle after init to set the path.
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/shellcc/platform/shell/reference/callbackfunctions/browsecallbackproc.asp
static int __stdcall BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM, LPARAM pData)
{
	if (uMsg == BFFM_INITIALIZED)
		::SendMessage(hwnd, BFFM_SETSELECTION, TRUE, pData);
	return 0;
};


UINT OptionDlg::doDialog(tExProp *prop)
{
	_pProp = prop;
	return ::DialogBoxParam(_hInst, MAKEINTRESOURCE(IDD_OPTION_DLG), _hParent,  (DLGPROC)dlgProc, (LPARAM)this);
}


INT_PTR CALLBACK OptionDlg::run_dlgProc(UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message) 
	{
		case WM_INITDIALOG:
		{
			goToCenter();

			for (LPTSTR strSizeFmt : pszSizeFmt)
			{
				::SendDlgItemMessage(_hSelf, IDC_COMBO_SIZE_FORMAT, CB_ADDSTRING, 0, (LPARAM)strSizeFmt);
			}
			for (LPTSTR strDateFmt : pszDateFmt)
			{
				::SendDlgItemMessage(_hSelf, IDC_COMBO_DATE_FORMAT, CB_ADDSTRING, 0, (LPARAM)strDateFmt);
			}
			for (LPTSTR strFontSize: pszFontSize)
			{
				::SendDlgItemMessage(_hSelf, IDC_COMBO_FONT_SIZE, CB_ADDSTRING, 0, (LPARAM)strFontSize);
			}
			::SendDlgItemMessage(_hSelf, IDC_EDIT_TIMEOUT, EM_LIMITTEXT, 5, 0);
			
			SetParams();
			LongUpdate();

			break;
		}
		case WM_COMMAND : 
		{
			switch (LOWORD(wParam))
			{
				case IDC_CHECK_LONG:
				{
					LongUpdate();
					return TRUE;
				}
				case IDC_BTN_OPENDLG:
				{
					// This code was copied and slightly modifed from:
					// http://www.bcbdev.com/faqs/faq62.htm

					// SHBrowseForFolder returns a PIDL. The memory for the PIDL is
					// allocated by the shell. Eventually, we will need to free this
					// memory, so we need to get a pointer to the shell malloc COM
					// object that will free the PIDL later on.
					LPMALLOC pShellMalloc = 0;
					if (::SHGetMalloc(&pShellMalloc) == NO_ERROR)
					{
						// If we were able to get the shell malloc object,
						// then proceed by initializing the BROWSEINFO stuct
						TCHAR displayName[MAX_PATH];
						BROWSEINFO info;
						ZeroMemory(&info, sizeof(info));						
						info.hwndOwner			= _hParent;
						info.pidlRoot			= NULL;
						info.pszDisplayName		= displayName;
						info.lpszTitle			= _T("Select a folder:");
						info.ulFlags			= BIF_RETURNONLYFSDIRS;
						info.lpfn				= BrowseCallbackProc;
						info.lParam				= (LPARAM)_pProp->nppExecProp.szScriptPath;

						// Execute the browsing dialog.
						LPITEMIDLIST pidl = ::SHBrowseForFolder(&info);

						// pidl will be null if they cancel the browse dialog.
						// pidl will be not null when they select a folder.
						if (pidl) 
						{
							// Try to convert the pidl to a display string.
							// Return is true if success.
							if (::SHGetPathFromIDList(pidl, _pProp->nppExecProp.szScriptPath))
							{
								// Set edit control to the directory path.
								::SetWindowText(::GetDlgItem(_hSelf, IDC_EDIT_SCRIPTPATH), _pProp->nppExecProp.szScriptPath);
							}
							pShellMalloc->Free(pidl);
						}
						pShellMalloc->Release();
					}
					break;
				}
				case IDC_BTN_EXAMPLE_FILE:
				{
					BYTE	szBOM[]			= {0xFF, 0xFE};
					DWORD	dwByteWritten	= 0;
					TCHAR	szExampleScriptPath[MAX_PATH];

					if (_pProp->nppExecProp.szScriptPath[0] == '.')
					{
						/* module path of notepad */
						GetModuleFileName(_hInst, szExampleScriptPath, sizeof(szExampleScriptPath));
						PathRemoveFileSpec(szExampleScriptPath);
						PathAppend(szExampleScriptPath, _pProp->nppExecProp.szScriptPath);
					} else {
						_tcscpy(szExampleScriptPath, _pProp->nppExecProp.szScriptPath);
					}
					::PathAppend(szExampleScriptPath, _T("Goto path.exec"));

					HANDLE	hFile = ::CreateFile(szExampleScriptPath, 
						GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 
						NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

					::WriteFile(hFile, szBOM, sizeof(szBOM), &dwByteWritten, NULL);
					for (INT i = 0; i < MAX_NPP_EXAMPLE_LINE; i++)
						::WriteFile(hFile, szExampleScript[i], _tcslen(szExampleScript[i]) * sizeof(TCHAR), &dwByteWritten, NULL);

					::CloseHandle(hFile);
					break;
				}
				case IDCANCEL:
					::EndDialog(_hSelf, IDCANCEL);
					return TRUE;
				case IDOK:
				{
					if (GetParams() == FALSE)
					{
						return FALSE;
					}
					::EndDialog(_hSelf, IDOK);
					return TRUE;
				}
				default:
					return FALSE;
			}
			break;
		}
		default:
			break;
	}
	return FALSE;
}


void OptionDlg::LongUpdate(void)
{
	BOOL	bViewLong = FALSE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_LONG, BM_GETCHECK, 0, 0) == BST_CHECKED)
		bViewLong = TRUE;

	::EnableWindow(::GetDlgItem(_hSelf, IDC_COMBO_SIZE_FORMAT), bViewLong);
	::EnableWindow(::GetDlgItem(_hSelf, IDC_COMBO_DATE_FORMAT), bViewLong);
}


void OptionDlg::SetParams(void)
{
	::SendDlgItemMessage(_hSelf, IDC_CHECK_LONG, BM_SETCHECK, _pProp->bViewLong?BST_CHECKED:BST_UNCHECKED, 0);
	::SendDlgItemMessage(_hSelf, IDC_COMBO_SIZE_FORMAT, CB_SETCURSEL, (WPARAM)_pProp->fmtSize, 0);
	::SendDlgItemMessage(_hSelf, IDC_COMBO_DATE_FORMAT, CB_SETCURSEL, (WPARAM)_pProp->fmtDate, 0);

	::SendDlgItemMessage(_hSelf, IDC_CHECK_SEPEXT, BM_SETCHECK, _pProp->bAddExtToName?BST_UNCHECKED:BST_CHECKED, 0);
	::SendDlgItemMessage(_hSelf, IDC_CHECK_BRACES, BM_SETCHECK, _pProp->bViewBraces?BST_CHECKED:BST_UNCHECKED, 0);
	::SendDlgItemMessage(_hSelf, IDC_CHECK_AUTO, BM_SETCHECK, _pProp->bAutoUpdate?BST_CHECKED:BST_UNCHECKED, 0);
	::SendDlgItemMessage(_hSelf, IDC_CHECK_HIDDEN, BM_SETCHECK, _pProp->bShowHidden?BST_CHECKED:BST_UNCHECKED, 0);
	::SendDlgItemMessage(_hSelf, IDC_CHECK_USEICON, BM_SETCHECK, _pProp->bUseSystemIcons?BST_CHECKED:BST_UNCHECKED, 0);
	::SendDlgItemMessage(_hSelf, IDC_CHECK_USEFULLTREE, BM_SETCHECK, _pProp->bUseFullTree?BST_CHECKED:BST_UNCHECKED, 0);

	TCHAR temp[MAX_PATH];
	if (_pProp->iFontSize > 0) 
	{
		::SetDlgItemInt(_hSelf, IDC_COMBO_FONT_SIZE, _pProp->iFontSize, FALSE);
	}
	else 
	{
		::SendDlgItemMessage(_hSelf, IDC_COMBO_FONT_SIZE, CB_SETCURSEL, 0, 0);
	}

	::SetDlgItemText(_hSelf, IDC_EDIT_EXECNAME, _pProp->nppExecProp.szAppName);
	::SetDlgItemText(_hSelf, IDC_EDIT_SCRIPTPATH, _pProp->nppExecProp.szScriptPath);

	::SetDlgItemInt(_hSelf, IDC_EDIT_TIMEOUT, _pProp->uTimeout, FALSE);
}


BOOL OptionDlg::GetParams(void)
{
	BOOL		bRet	= TRUE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_LONG, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bViewLong = TRUE;
	else
		_pProp->bViewLong = FALSE;

	_pProp->fmtSize = (eSizeFmt)::SendDlgItemMessage(_hSelf, IDC_COMBO_SIZE_FORMAT, CB_GETCURSEL, 0, 0);
	_pProp->fmtDate = (eDateFmt)::SendDlgItemMessage(_hSelf, IDC_COMBO_DATE_FORMAT, CB_GETCURSEL, 0, 0);

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_SEPEXT, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bAddExtToName = FALSE;
	else
		_pProp->bAddExtToName = TRUE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_AUTO, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bAutoUpdate = TRUE;
	else
		_pProp->bAutoUpdate = FALSE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_BRACES, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bViewBraces = TRUE;
	else
		_pProp->bViewBraces = FALSE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_USEFULLTREE, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bUseFullTree = TRUE;
	else
		_pProp->bUseFullTree = FALSE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_HIDDEN, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bShowHidden = TRUE;
	else
		_pProp->bShowHidden = FALSE;

	if (::SendDlgItemMessage(_hSelf, IDC_CHECK_USEICON, BM_GETCHECK, 0, 0) == BST_CHECKED)
		_pProp->bUseSystemIcons = TRUE;
	else
		_pProp->bUseSystemIcons = FALSE;

	TCHAR	TEMP[MAX_PATH];
	::GetDlgItemText(_hSelf, IDC_EDIT_TIMEOUT, TEMP, MAX_PATH);
	_pProp->uTimeout = (UINT)_ttoi(TEMP);

	::GetDlgItemText(_hSelf, IDC_EDIT_EXECNAME, _pProp->nppExecProp.szAppName, MAX_PATH);
	::GetDlgItemText(_hSelf, IDC_EDIT_SCRIPTPATH, _pProp->nppExecProp.szScriptPath, MAX_PATH);

	::GetDlgItemText(_hSelf, IDC_COMBO_FONT_SIZE, TEMP, MAX_PATH);
	_pProp->iFontSize = (UINT)_ttoi(TEMP);

	return bRet;
}




