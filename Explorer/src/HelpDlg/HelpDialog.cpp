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
#include "HelpDialog.h"
#include "Explorer.h"
#include "atlstr.h"
#include "shellapi.h"

#include "ExplorerResource.h"


HelpDlg::HelpDlg() : StaticDialog() {}

void HelpDlg::init(HINSTANCE hInst, NppData nppData)
{
	_nppData = nppData;
	Window::init(hInst, nppData._nppHandle);
}

void HelpDlg::doDialog()
{
    if (!isCreated())
        create(IDD_HELP_DLG);

	goToCenter();
}


INT_PTR CALLBACK HelpDlg::run_dlgProc(UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message) 
	{
        case WM_INITDIALOG :
		{
			setVersionString();

			/* change language */
			NLChangeDialog(_hInst, _nppData._nppHandle, _hSelf, _T("Help"));

			return TRUE;
		}
		case WM_COMMAND : 
		{
			switch (wParam)
			{
				case IDOK :
				case IDCANCEL :
					display(FALSE);
					return TRUE;

				default :
					break;
			}
		}
		case WM_NOTIFY:
		{
			LPNMHDR nmhdr = (LPNMHDR)lParam;
			if ((nmhdr->code == NM_CLICK || nmhdr->code == NM_RETURN) && 
				(nmhdr->idFrom == IDC_SYSLINK_EMAIL || nmhdr->idFrom == IDC_SYSLINK_ORIGURL || nmhdr->idFrom == IDC_SYSLINK_CRTURL))
			{
				PNMLINK pNMLink = (PNMLINK)lParam;
				LITEM   item = pNMLink->item;
				ShellExecute(NULL, _T("open"), item.szUrl, NULL, NULL, SW_SHOWNORMAL);
			}
		}
	}
	return FALSE;
}

void HelpDlg::setVersionString()
{
	TCHAR szFilename[MAX_PATH];
	if (!GetWindowModuleFileName(_hSelf, szFilename, MAX_PATH))
	{
		return;
	}

	DWORD dummy;
	DWORD dwSize = GetFileVersionInfoSize(szFilename, &dummy);
	if (dwSize == 0)
	{
		return;
	}

	std::vector<BYTE> data(dwSize);
	if (!GetFileVersionInfo(szFilename, NULL, dwSize, &data[0]))
	{
		return;
	}

	UINT                uiVerLen = 0;
	VS_FIXEDFILEINFO*   pFixedInfo = 0;
	if (!VerQueryValue(&data[0], TEXT("\\"), (void**)&pFixedInfo, (UINT *)&uiVerLen))
	{
		return;
	}

	CString strProductVersion;

	strProductVersion.Format(_T("v%u.%u.%u.%u"),
		HIWORD(pFixedInfo->dwProductVersionMS),
		LOWORD(pFixedInfo->dwProductVersionMS),
		HIWORD(pFixedInfo->dwProductVersionLS),
		LOWORD(pFixedInfo->dwProductVersionLS));

	HWND versionCtrl = GetDlgItem(_hSelf, IDC_STATIC_VERSION);
	SetWindowText(versionCtrl, strProductVersion);
}

