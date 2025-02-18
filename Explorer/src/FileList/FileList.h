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


#ifndef FILELIST_DEFINE_H
#define FILELIST_DEFINE_H

#include "Explorer.h"
#include "ToolBar.h"
#include "window.h"
#include "DragDropImpl.h"
#pragma warning(push)
#pragma warning(disable: 4091)
#include <shlobj.h>
#pragma warning(pop)

typedef struct {
	wstring			strPath;
	vector<wstring>	vStrItems;
} tStaInfo;


/* pattern for column resize by mouse */
static const WORD DotPattern[] = 
{
	0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF
};

typedef struct {
	BOOL		bParent;
	INT			iIcon;
	INT			iOverlay;
	BOOL		bHidden;
	BOOL		bFolder;
	wstring		strName;
	wstring		strExt;
	wstring		strSize;
	wstring		strDate;
	/* not visible, only for sorting */
	wstring		strNameExt;
	__int64		i64Size;
	__int64		i64Date;
	/* not visible, remember state */
	UINT		state;
} tFileListData;


class FileList : public Window, public CIDropTarget
{
public:
	FileList(void);
	~FileList(void);
	
	void init(HINSTANCE hInst, HWND hParent, HWND hParentList);
	void initProp(tExProp* prop);
	void initFont();

	void viewPath(LPCTSTR currentPath, BOOL redraw = FALSE);

	BOOL notify(WPARAM wParam, LPARAM lParam);

	void filterFiles(LPCTSTR currentFilter);
	void SelectCurFile(void);
	void SelectFolder(LPTSTR selFolder);

	virtual void destroy() override;
	virtual void redraw() {
		initFont();
		_hImlListSys = GetSmallImageList(_pExProp->bUseSystemIcons);
		ListView_SetImageList(_hSelf, _hImlListSys, LVSIL_SMALL);
		SetColumns();
		Window::redraw();
	}

	void ToggleStackRec(void);					// enables/disable trace of viewed directories
	void ResetDirStack(void);					// resets the stack
	void SetToolBarInfo(ToolBar *ToolBar, UINT idRedo, UINT idUndo);	// set dependency to a toolbar element
	bool GetPrevDir(LPTSTR pszPath, vector<wstring> & vStrItems);			// get previous directory
	bool GetNextDir(LPTSTR pszPath, vector<wstring> & vStrItems);			// get next directory
	INT  GetPrevDirs(LPTSTR *pszPathes);		// get previous directorys
	INT  GetNextDirs(LPTSTR *pszPathes);		// get next directorys
	void OffsetItr(INT offsetItr, vector<wstring> & vStrItems);			// get offset directory
	void UpdateSelItems(void);
	void SetItems(vector<wstring> vStrItems);

	void UpdateOverlayIcon(void);

public:
	virtual bool OnDrop(FORMATETC* pFmtEtc, STGMEDIUM& medium, DWORD *pdwEffect);

protected:

	/* Subclassing list control */
	static const int _idSubclassListProc = 43;
	LRESULT runListProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK wndDefaultListProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
		FileList* lpThis = (FileList*)dwRefData;
		return lpThis->runListProc(hwnd, Message, wParam, lParam);
	}

	/* Subclassing header control */
	static const int _idSubclassHeaderProc = 43;
	LRESULT runHeaderProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK wndDefaultHeaderProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
		FileList* lpThis = (FileList*)dwRefData;
		return lpThis->runHeaderProc(hwnd, Message, wParam, lParam);
	}

	void ReadIconToList(UINT iItem, LPINT piIcon, LPINT piOverlayed);
	void ReadArrayToList(LPTSTR szItem, INT iItem ,INT iSubItem);

	void DrawDivider(UINT x);
	void UpdateList(void);
	void SetColumns(void);
	void SetOrder(void);
	void SetFilter(LPCTSTR pszNewFilter);

	BOOL FindNextItemInList(UINT maxFolder, UINT maxData, LPUINT puPos);

	void onRMouseBtn();
	void onLMouseBtnDbl();

	void onSelectItem(TCHAR charkey);
	void onSelectAll(void);
	void onDelete(bool immediate = false);
	void onCopy(void);
	void onPaste(void);
	void onCut(void);

	void FolderExChange(CIDropSource* pdsrc, CIDataObject* pdobj, UINT dwEffect);
	bool doPaste(LPCTSTR pszTo, LPDROPFILES hData, const DWORD & dwEffect);

	void PushDir(LPCTSTR str);
	void UpdateToolBarElements(void);

	void SetFocusItem(INT item) {
		/* select first entry */
		INT	dataSize	= _uMaxElements;

		/* at first unselect all */
		for (INT iItem = 0; iItem < dataSize; iItem++)
			ListView_SetItemState(_hSelf, iItem, 0, 0xFF);

		ListView_SetItemState(_hSelf, item, LVIS_SELECTED|LVIS_FOCUSED, 0xFF);
		ListView_EnsureVisible(_hSelf, item, TRUE);
		ListView_SetSelectionMark(_hSelf, item);
	}

	void GetSize(__int64 size, wstring& str);
	void GetDate(FILETIME ftLastWriteTime, wstring& str);

	void GetFilterLists(LPTSTR pszFilter, LPTSTR pszExFilter);
	BOOL DoFilter(LPCTSTR pszFileName, LPTSTR pszFilter, LPTSTR pszExFilter);
	INT  WildCmp(LPCTSTR wstring, LPCTSTR wild);

private:	/* for thread */

	void LIST_LOCK(void) {
		while (_hSemaphore) {
			if (::WaitForSingleObject(_hSemaphore, 50) == WAIT_OBJECT_0)
				return;
		}
	};
	void LIST_UNLOCK(void) {
		if (_hSemaphore) {
			::ReleaseSemaphore(_hSemaphore, 1, NULL);
		}
	};

private:
	HWND						_hHeader;
	HIMAGELIST					_hImlListSys;

	tExProp*					_pExProp;

	/* file list comparer */
	class FileListComparer {
	public:
		FileListComparer(int column, bool ascending);
		bool operator()(tFileListData a, tFileListData b) const;
	private:
		int _column;
		bool _ascending;
	};

	/* file list owner drawn */
	HFONT						_hFont;
	HFONT						_hFontUnder;
	HIMAGELIST					_hImlParent;

	enum eOverThEv { FL_EVT_EXIT, FL_EVT_INT, FL_EVT_START, FL_EVT_NEXT, FL_EVT_MAX };
	HANDLE						_hEvent[FL_EVT_MAX];
	HANDLE						_hOverThread;
	HANDLE						_hSemaphore;

	/* header values */
	HBITMAP						_bmpSortUp;
	HBITMAP						_bmpSortDown;
	INT							_iMouseTrackItem;
	LONG						_lMouseTrackPos;
	INT							_iBltPos;

	/* Character used for separating decimals and thousands */
	TCHAR						_szDecimalSeparator[8];
	TCHAR						_szThousandsSeparator[8];
	NUMBERFMT					_numberFmt;

	/* current file filter */
	TCHAR						_szFileFilter[MAX_PATH];

	/* stores the path here for sorting		*/
	/* Note: _vFolder will not be sorted    */
	UINT						_uMaxFolders;
	UINT						_uMaxElements;
	UINT						_uMaxElementsOld;
	vector<tFileListData>		_vFileList;

	/* search in list by typing of characters */
	BOOL						_bSearchFile;
	TCHAR						_strSearchFile[MAX_PATH];

	BOOL						_bOldAddExtToName;
	BOOL						_bOldViewLong;

	/* stack for prev and next dir */
	BOOL						_isStackRec;
	vector<tStaInfo>			_vDirStack;
	vector<tStaInfo>::iterator	_itrPos;
    
	ToolBar*					_pToolBar;
	UINT						_idRedo;
	UINT						_idUndo;

	/* scrolling on DnD */
	BOOL						_isScrolling;
	BOOL						_isDnDStarted;
};


#endif	//	FILELIST_DEFINE_H