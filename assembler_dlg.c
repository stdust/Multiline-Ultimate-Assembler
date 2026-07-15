#include "stdafx.h"
#include "assembler_dlg.h"
#include "tabctrl_ex.h"
struct Sci_CharacterRange {
	long cpMin;
	long cpMax;
};
struct Sci_TextToFind {
	struct Sci_CharacterRange chrg;
	const char *lpstrText;
	struct Sci_CharacterRange chrgText;
};
extern HINSTANCE hDllInst;
extern OPTIONS options;
extern BOOL MyGetColorfromini(HINSTANCE dllinst, TCHAR *key, int *p_val, int def);
#define WriteFileLog(...) ((void)0)
static HACCEL hAccelerators;
static HWND hAsmDlg;
static ASM_DIALOG_PARAM AsmDlgParam;
static WNDPROC wpOrigScintillaProc = NULL;
static int s_backupSelStart = -1;
static int s_backupSelEnd = -1;
static LRESULT CALLBACK ScintillaSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#ifndef SCI_SETCODEPAGE
#define SCI_SETCODEPAGE 2037
#endif
#ifndef SC_CP_UTF8
#define SC_CP_UTF8 65001
#endif
#ifndef SCI_SETEXTRAASCENT
#define SCI_SETEXTRAASCENT 2525
#endif
#ifndef SCI_SETEXTRADESCENT
#define SCI_SETEXTRADESCENT 2526
#endif
#ifndef SCI_SETHSCROLLBAR
#define SCI_SETHSCROLLBAR 2292
#endif
#ifndef SCI_STARTSTYLING
#define SCI_STARTSTYLING 2032
#endif
#ifndef SCI_SETSTYLING
#define SCI_SETSTYLING 2033
#endif
#ifndef SCN_STYLENEEDED
#define SCN_STYLENEEDED (-2006)
#endif
// Custom Sub-Style Numbers
#define SCE_ASM_JMP 16
#define SCE_ASM_CALL 17
#define SCE_ASM_RET 18
#define SCE_ASM_NOP 19
#define SCE_ASM_REG_SPECIAL 20
static BOOL bInStyling = FALSE;
// Custom message for deferred selection collapse after focus transfer.
// Prevents accidental block selection when Ctrl+M switches focus to Scintilla
// while Ctrl key is physically held down.
#define UWM_COLLAPSE_SEL (WM_APP + 100)
static BOOL IsKeyword(const char *word, const char *keywordList)
{
	if (!word || !keywordList || *word == '\0')
		return FALSE;
	char lowerWord[128];
	int i = 0;
	for (; word[i] && i < 127; i++)
	{
		char c = word[i];
		if (c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';
		lowerWord[i] = c;
	}
	lowerWord[i] = '\0';
	const char *p = keywordList;
	int len = i;
	while ((p = strstr(p, lowerWord)) != NULL)
	{
		BOOL startBoundary = (p == keywordList || *(p - 1) == ' ');
		BOOL endBoundary = (*(p + len) == '\0' || *(p + len) == ' ');
		if (startBoundary && endBoundary)
			return TRUE;
		p += len;
	}
	return FALSE;
}
static void DoCustomStyling(HWND hAsmEdit, int startPos, int endPos)
{
	if (!hAsmEdit || bInStyling)
		return;
	bInStyling = TRUE;
	int textLen = (int)SendMessage(hAsmEdit, SCI_GETLENGTH, 0, 0);
	if (textLen <= 0)
	{
		bInStyling = FALSE;
		return;
	}
	WriteFileLog(_T("DoCustomStyling() triggered - textLen: %d, startPos: %d, endPos: %d"), textLen, startPos, endPos);
	char *lpBuf = (char *)HeapAlloc(GetProcessHeap(), 0, textLen + 1);
	if (!lpBuf)
	{
		bInStyling = FALSE;
		return;
	}
	SendMessage(hAsmEdit, SCI_GETTEXT, textLen + 1, (LPARAM)lpBuf);
	// Clear styles and prepare styling
	// SendMessage(hAsmEdit, SCI_CLEARDOCUMENTSTYLE, 0, 0); // Disabled to prevent destroying styling bytes during select/drag repaint
	SendMessage(hAsmEdit, SCI_STARTSTYLING, 0, 0);
	int pos = 0;
	while (pos < textLen)
	{
		char c = lpBuf[pos];
		// 1. Whitespaces and Newlines
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			int start = pos;
			while (pos < textLen && (lpBuf[pos] == ' ' || lpBuf[pos] == '\t' || lpBuf[pos] == '\r' || lpBuf[pos] == '\n'))
			{
				pos++;
			}
			SendMessage(hAsmEdit, SCI_SETSTYLING, pos - start, SCE_ASM_DEFAULT);
			continue;
		}
		// 2. Comments
		if (c == ';')
		{
			int start = pos;
			while (pos < textLen && lpBuf[pos] != '\r' && lpBuf[pos] != '\n')
			{
				pos++;
			}
			SendMessage(hAsmEdit, SCI_SETSTYLING, pos - start, SCE_ASM_COMMENT);
			continue;
		}
		// 3. Strings
		if (c == '"' || c == '\'')
		{
			char quote = c;
			int start = pos;
			pos++;
			while (pos < textLen && lpBuf[pos] != quote && lpBuf[pos] != '\r' && lpBuf[pos] != '\n')
			{
				if (lpBuf[pos] == '\\' && pos + 1 < textLen)
					pos += 2;
				else
					pos++;
			}
			if (pos < textLen && lpBuf[pos] == quote)
				pos++;
			SendMessage(hAsmEdit, SCI_SETSTYLING, pos - start, SCE_ASM_STRING);
			continue;
		}
		// 4. Operators / Symbols
		if (c == '[' || c == ']' || c == ':' || c == ',' || c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')')
		{
			SendMessage(hAsmEdit, SCI_SETSTYLING, 1, SCE_ASM_OPERATOR);
			pos++;
			continue;
		}
		// 5. Numbers
		if (c >= '0' && c <= '9')
		{
			int start = pos;
			while (pos < textLen && (
				(lpBuf[pos] >= '0' && lpBuf[pos] <= '9') ||
				(lpBuf[pos] >= 'a' && lpBuf[pos] <= 'f') ||
				(lpBuf[pos] >= 'A' && lpBuf[pos] <= 'F') ||
				lpBuf[pos] == 'x' || lpBuf[pos] == 'X' ||
				lpBuf[pos] == 'h' || lpBuf[pos] == 'H'
			))
			{
				pos++;
			}
			SendMessage(hAsmEdit, SCI_SETSTYLING, pos - start, SCE_ASM_NUMBER);
			continue;
		}
		// 6. Identifiers / Keywords
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '@' || c == '.')
		{
			int start = pos;
			while (pos < textLen && (
				(lpBuf[pos] >= 'a' && lpBuf[pos] <= 'z') ||
				(lpBuf[pos] >= 'A' && lpBuf[pos] <= 'Z') ||
				(lpBuf[pos] >= '0' && lpBuf[pos] <= '9') ||
				lpBuf[pos] == '_' || lpBuf[pos] == '@' || lpBuf[pos] == '.'
			))
			{
				pos++;
			}
			int len = pos - start;
			char szWord[128];
			if (len > 127) len = 127;
			lstrcpynA(szWord, lpBuf + start, len + 1);
			int style = SCE_ASM_IDENTIFIER;
			if (IsKeyword(szWord, HILITE_ASM_JMP_CMD))
			{
				style = SCE_ASM_JMP;
			}
			else if (IsKeyword(szWord, HILITE_ASM_CALL_CMD))
			{
				style = SCE_ASM_CALL;
			}
			else if (IsKeyword(szWord, HILITE_ASM_RET_CMD))
			{
				style = SCE_ASM_RET;
			}
			else if (IsKeyword(szWord, HILITE_ASM_NOP_CMD))
			{
				style = SCE_ASM_NOP;
			}
			else if (IsKeyword(szWord, HILITE_ASM_CMD_BASIC))
			{
				style = SCE_ASM_CPUINSTRUCTION;
			}
			else if (IsKeyword(szWord, HILITE_ASM_FPU_CMD))
			{
				style = SCE_ASM_MATHINSTRUCTION;
			}
			else if (IsKeyword(szWord, HILITE_ASM_EXT_CMD))
			{
				style = SCE_ASM_EXTINSTRUCTION;
			}
			else if (IsKeyword(szWord, HILITE_REG_SPECIAL))
			{
				style = SCE_ASM_REG_SPECIAL;
			}
			else if (IsKeyword(szWord, HILITE_REG_GP))
			{
				style = SCE_ASM_REGISTER;
			}
			else if (IsKeyword(szWord, HILITE_TYPE))
			{
				style = SCE_ASM_DIRECTIVE;
			}
			else if (IsKeyword(szWord, HILITE_OTHER))
			{
				style = SCE_ASM_DIRECTIVEOPERAND;
			}
			SendMessage(hAsmEdit, SCI_SETSTYLING, len, style);
			continue;
		}
		// 7. Default fallback for other characters
		SendMessage(hAsmEdit, SCI_SETSTYLING, 1, SCE_ASM_DEFAULT);
		pos++;
	}
	HeapFree(GetProcessHeap(), 0, lpBuf);
	bInStyling = FALSE;
}
// JMP/CALL Word highlighting helper functions
static BOOL MyIsAlNum(char c)
{
	return ((c >= 'a' && c <= 'z') || 
	        (c >= 'A' && c <= 'Z') || 
	        (c >= '0' && c <= '9'));
}
#define INDIC_JMP  8
#define INDIC_CALL 9
static void RefreshAllWordHighlights(HWND hAsmEdit)
{
	if (!hAsmEdit)
		return;
	// Prevent horizontal scrollbar from showing up on dynamic text update
	SendMessage(hAsmEdit, SCI_SETHSCROLLBAR, FALSE, 0);
	SendMessage(hAsmEdit, 2030, 1, 0); // SCI_SETSCROLLWIDTH(1) to eliminate horizontal scroll area
	// Prevent flickering and improve performance during scan
	SendMessage(hAsmEdit, WM_SETREDRAW, FALSE, 0);
	int textLen = (int)SendMessage(hAsmEdit, SCI_GETLENGTH, 0, 0);
	if (textLen > 0)
	{
		SendMessage(hAsmEdit, SCI_SETINDICATORCURRENT, INDIC_JMP, 0);
		SendMessage(hAsmEdit, SCI_INDICATORCLEARRANGE, 0, textLen);
		
		SendMessage(hAsmEdit, SCI_SETINDICATORCURRENT, INDIC_CALL, 0);
		SendMessage(hAsmEdit, SCI_INDICATORCLEARRANGE, 0, textLen);
	}
	// Force custom styling execution directly
	DoCustomStyling(hAsmEdit, 0, -1);
	SendMessage(hAsmEdit, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(hAsmEdit, NULL, TRUE);
	// Removed SCI_COLOURISE call - DoCustomStyling already handles all styling.
	// SCI_COLOURISE caused a re-entry cascade that interfered with selection rendering.
}
TCHAR *AssemblerInit()
{
	WriteFileLog(_T("AssemblerInit() - Loading accelerators"));
	hAccelerators = LoadAccelerators(hDllInst, MAKEINTRESOURCE(IDR_MAINACCELERATOR));
	return NULL;
}
void AssemblerExit()
{
	WriteFileLog(_T("AssemblerExit()"));
}
BOOL AssemblerPreTranslateMessage(LPMSG lpMsg)
{
	if(hAsmDlg && IsWindow(hAsmDlg))
	{
		// Ctrl+Enter -> Assemble (IDOK)
		if (lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			if (!(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
			{
				SendMessage(hAsmDlg, WM_COMMAND, MAKEWPARAM(IDOK, 0), 0);
				return TRUE;
			}
		}
		// Block Ctrl+M keydown/char leaks to prevent Carriage Return (ASCII 13) 
		// inside Scintilla which triggers unwanted text selections / newlines.
		if ((lpMsg->message == WM_KEYDOWN || lpMsg->message == WM_CHAR) &&
			lpMsg->wParam == 'M' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
		{
			if (!(GetKeyState(VK_SHIFT) & 0x8000))
			{
				return TRUE; // Consume key message (Block)
			}
		}
		if (lpMsg->message == WM_CHAR && lpMsg->wParam == 13 && (GetKeyState(VK_CONTROL) & 0x8000))
		{
			if (!(GetKeyState(VK_SHIFT) & 0x8000))
			{
				return TRUE; // Consume Carriage Return character (Block)
			}
		}
		HWND hWnd = hAsmDlg;
		HWND hFindReplaceWnd = AsmDlgParam.hFindReplaceWnd;
		if(hFindReplaceWnd && IsWindow(hFindReplaceWnd) && IsDialogMessage(hFindReplaceWnd, lpMsg))
			return TRUE;
		if(GetActiveWindow() == hWnd)
		{
			if(hAccelerators && TranslateAccelerator(hWnd, hAccelerators, lpMsg))
				return TRUE;
		}
		if(IsDialogMessage(hWnd, lpMsg))
			return TRUE;
	}
	return FALSE;
}
void AssemblerShowDlg()
{
	WriteFileLog(_T("AssemblerShowDlg() - Current hAsmDlg = %p"), hAsmDlg);
	if(!hAsmDlg)
	{
		hAsmDlg = CreateAsmDlg();
		WriteFileLog(_T("AssemblerShowDlg() - Created hAsmDlg = %p"), hAsmDlg);
		if(!hAsmDlg)
			MessageBox(hwollymain, _T("Couldn't create dialog"), _T("Multiline Ultimate Assembler error"), MB_ICONHAND);
	}
	else
	{
		HWND hWnd = hAsmDlg;
		// If the dialog window is already active and focused, completely skip 
		// SetFocus / ShowWindow logic to avoid Keyboard/Mouse stuck modifiers leak into the editor window.
		if (GetActiveWindow() == hWnd)
		{
			return;
		}
		if(IsWindowEnabled(hWnd))
		{
			if(IsIconic(hWnd))
				ShowWindow(hWnd, SW_RESTORE);
			// Release stuck keys (Ctrl, Shift, M) and stuck mouse Left Button (LBUTTONUP)
			// to prevent selection/focus drag lock issues especially after a drag operation in disasm.
			keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
			keybd_event('M', 0, KEYEVENTF_KEYUP, 0);
			keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
			mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
			SetFocus(hWnd);
			// Post a deferred selection collapse to prevent block selection
			// that can form when Ctrl key leaks into Scintilla during focus transfer.
			PostMessage(hWnd, UWM_COLLAPSE_SEL, 0, 0);
		}
		else
		{
			HWND hPopupWnd = GetWindow(hWnd, GW_ENABLEDPOPUP);
			if(hPopupWnd)
				SetFocus(hPopupWnd);
		}
	}
}
void AssemblerCloseDlg()
{
	WriteFileLog(_T("AssemblerCloseDlg() - Closing hAsmDlg = %p"), hAsmDlg);
	if(hAsmDlg)
		CloseAsmDlg(hAsmDlg);
}
void AssemblerLoadCode(DWORD_PTR dwAddress, DWORD_PTR dwSize)
{
	AssemblerShowDlg();
	if(hAsmDlg)
		SendMessage(hAsmDlg, UWM_LOADCODE, dwAddress, dwSize);
}
void AssemblerOptionsChanged()
{
	if(hAsmDlg)
		SendMessage(hAsmDlg, UWM_OPTIONSCHANGED, 0, 0);
}
static HWND CreateAsmDlg()
{
	HWND hWnd = CreateDialogParam(hDllInst, MAKEINTRESOURCE(IDD_MAIN), hwollymain,
		(DLGPROC)DlgAsmProc, (LPARAM)&AsmDlgParam);
	if(!hWnd)
		return NULL;
	ShowWindow(hWnd, SW_SHOWNORMAL);
	return hWnd;
}
static void CloseAsmDlg(HWND hWnd)
{
	SendMessage(hWnd, WM_CLOSE, 0, 0);
}
static LRESULT CALLBACK DlgAsmProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ASM_DIALOG_PARAM *p_dialog_param;
	DWORD_PTR dwAddress, dwSize;
	FINDREPLACE *p_findreplace;
	POINT pt;
	RECT rc;
	HDWP hDwp;
	HWND hPopupWnd;
	if(uMsg == WM_INITDIALOG)
	{
		p_dialog_param = (ASM_DIALOG_PARAM *)lParam;
		SetWindowLongPtr(hWnd, DWLP_USER, (LONG_PTR)p_dialog_param);
	}
	else
	{
		p_dialog_param = (ASM_DIALOG_PARAM *)GetWindowLongPtr(hWnd, DWLP_USER);
		if(!p_dialog_param)
			return FALSE;
	}
	switch(uMsg)
	{
	case WM_INITDIALOG:
		WriteFileLog(_T("DlgAsmProc() - WM_INITDIALOG received"));
		p_dialog_param->hSmallIcon = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 16, 16, 0);
		WriteFileLog(_T("DlgAsmProc() - loaded hSmallIcon = %p"), p_dialog_param->hSmallIcon);
		if(p_dialog_param->hSmallIcon)
			SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)p_dialog_param->hSmallIcon);
		p_dialog_param->hLargeIcon = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 32, 32, 0);
		WriteFileLog(_T("DlgAsmProc() - loaded hLargeIcon = %p"), p_dialog_param->hLargeIcon);
		if(p_dialog_param->hLargeIcon)
			SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)p_dialog_param->hLargeIcon);
		WriteFileLog(_T("DlgAsmProc() - calling SetScintillaDesign..."));
		SetScintillaDesign(hWnd, hDllInst, &p_dialog_param->raFont);
		WriteFileLog(_T("DlgAsmProc() - SetScintillaDesign completed"));
		p_dialog_param->hMenu = LoadMenu(hDllInst, MAKEINTRESOURCE(IDR_RIGHTCLICK));
		WriteFileLog(_T("DlgAsmProc() - loaded hMenu = %p"), p_dialog_param->hMenu);
		GetClientRect(hWnd, &rc);
		p_dialog_param->dlg_last_cw = rc.right-rc.left;
		p_dialog_param->dlg_last_ch = rc.bottom-rc.top;
		WriteFileLog(_T("DlgAsmProc() - ClientRect is %d x %d"), p_dialog_param->dlg_last_cw, p_dialog_param->dlg_last_ch);
		WriteFileLog(_T("DlgAsmProc() - calling LoadWindowPos..."));
		LoadWindowPos(hWnd, hDllInst, &p_dialog_param->dlg_min_w, &p_dialog_param->dlg_min_h);
		WriteFileLog(_T("DlgAsmProc() - LoadWindowPos completed, min size: %d x %d"), p_dialog_param->dlg_min_w, p_dialog_param->dlg_min_h);
		WriteFileLog(_T("DlgAsmProc() - calling TabCtrlExInit..."));
		p_dialog_param->bTabCtrlExInitialized = TabCtrlExInit(GetDlgItem(hWnd, IDC_TABS), TCF_EX_REORDER|TCF_EX_LABLEEDIT|TCF_EX_REDUCEFLICKER|TCF_EX_MBUTTONNOFOCUS, UWM_NOTIFY);
		WriteFileLog(_T("DlgAsmProc() - TabCtrlExInit completed, status: %d"), p_dialog_param->bTabCtrlExInitialized);
		if(!p_dialog_param->bTabCtrlExInitialized)
		{
			DestroyWindow(hWnd);
			break;
		}
		WriteFileLog(_T("DlgAsmProc() - calling InitTabs..."));
		InitTabs(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), hDllInst, hWnd, UWM_ERRORMSG);
		WriteFileLog(_T("DlgAsmProc() - InitTabs completed"));
		WriteFileLog(_T("DlgAsmProc() - calling InitFindReplace..."));
		InitFindReplace(hWnd, hDllInst, p_dialog_param);
		WriteFileLog(_T("DlgAsmProc() - InitFindReplace completed"));
		WriteFileLog(_T("DlgAsmProc() - setting focus to Scintilla..."));
		HWND hAsmEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
		if (hAsmEdit)
		{
			wpOrigScintillaProc = (WNDPROC)SetWindowLongPtr(hAsmEdit, GWLP_WNDPROC, (LONG_PTR)ScintillaSubclassProc);
		}
		SetFocus(hAsmEdit);
		WriteFileLog(_T("DlgAsmProc() - focus set, WM_INITDIALOG complete"));
		break;
	case UWM_LOADCODE:
		dwAddress = (DWORD_PTR)wParam;
		dwSize = (DWORD_PTR)lParam;
		if(dwAddress && dwSize)
		{
			if(SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_GETTEXTLENGTH, 0, 0) > 0)
			{
				OnTabChanging(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
				NewTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), NULL);
			}
			LoadCode(hWnd, dwAddress, dwSize);
		}
		break;
	case UWM_OPTIONSCHANGED:
		OptionsChanged(hWnd);
		break;
	case WM_LBUTTONDOWN:
		SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
		break;
	case WM_LBUTTONDBLCLK:
		GetClientRect(GetDlgItem(hWnd, IDC_TABS), &rc);
		pt.x = GET_X_LPARAM(lParam);
		pt.y = GET_Y_LPARAM(lParam);
		if(PtInRect(&rc, pt))
		{
			OnTabChanging(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			NewTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), NULL);
		}
		break;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lParam)->ptMinTrackSize.x = p_dialog_param->dlg_min_w;
		((MINMAXINFO *)lParam)->ptMinTrackSize.y = p_dialog_param->dlg_min_h;
		break;
	case WM_SIZE:
		if(wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
		{
			hDwp = BeginDeferWindowPos(4);
			if(hDwp)
				hDwp = ChildRelativeDeferWindowPos(hDwp, hWnd, IDC_TABS, 
					0, 0, LOWORD(lParam)-p_dialog_param->dlg_last_cw, 0);
			if(hDwp)
				hDwp = ChildRelativeDeferWindowPos(hDwp, hWnd, IDC_ASSEMBLER, 
					0, 0, LOWORD(lParam)-p_dialog_param->dlg_last_cw, HIWORD(lParam)-p_dialog_param->dlg_last_ch);
			if(hDwp)
				hDwp = ChildRelativeDeferWindowPos(hDwp, hWnd, IDOK, 
					0, HIWORD(lParam)-p_dialog_param->dlg_last_ch, 0, 0);
			if(hDwp)
				hDwp = ChildRelativeDeferWindowPos(hDwp, hWnd, IDC_CLOSE, 
					LOWORD(lParam)-p_dialog_param->dlg_last_cw, HIWORD(lParam)-p_dialog_param->dlg_last_ch, 0, 0);
			if(hDwp)
				EndDeferWindowPos(hDwp);
			p_dialog_param->dlg_last_cw = LOWORD(lParam);
			p_dialog_param->dlg_last_ch = HIWORD(lParam);
		}
		break;
	case WM_CONTEXTMENU:
		if((HWND)wParam == GetDlgItem(hWnd, IDC_ASSEMBLER))
		{
			if(lParam == -1)
			{
				GetCaretPos(&pt);
				ClientToScreen((HWND)wParam, &pt);
			}
			else
			{
				pt.x = GET_X_LPARAM(lParam);
				pt.y = GET_Y_LPARAM(lParam);
			}
			UpdateRightClickMenuState(hWnd, GetSubMenu(p_dialog_param->hMenu, 0));
			TrackPopupMenu(GetSubMenu(p_dialog_param->hMenu, 0), TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
		}
		else if((HWND)wParam == hWnd && lParam != -1)
		{
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);
			GetWindowRect(GetDlgItem(hWnd, IDC_TABS), &rc);
			if(PtInRect(&rc, pt))
				TrackPopupMenu(GetSubMenu(p_dialog_param->hMenu, 1), TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
		}
		break;
	case WM_ACTIVATE:
		switch(LOWORD(wParam))
		{
		case WA_INACTIVE:
			SaveFileOfTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			break;
		case WA_ACTIVE:
		case WA_CLICKACTIVE:
			SyncTabs(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			
			// Release stuck keys (Ctrl, Shift, M) and mouse Left Button to prevent selection/drag issues
			keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
			keybd_event('M', 0, KEYEVENTF_KEYUP, 0);
			keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
			mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
			SetFocus(GetDlgItem(hWnd, IDC_ASSEMBLER));
			// Immediately collapse any selection to caret position.
			// Scintilla may create a block selection when it receives focus
			// while Ctrl key is physically held down (from Ctrl+M shortcut).
			{
				HWND hEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
				LRESULT caretPos = SendMessage(hEdit, SCI_GETCURRENTPOS, 0, 0);
				SendMessage(hEdit, SCI_SETSEL, caretPos, caretPos);
			}
			// Also post a deferred collapse to catch any async selection events
			// that may occur after this message handler returns.
			PostMessage(hWnd, UWM_COLLAPSE_SEL, 0, 0);
			break;
		}
		break;
	case WM_NOTIFY:
		switch(((NMHDR *)lParam)->idFrom)
		{
		case IDC_TABS:
			switch(((NMHDR *)lParam)->code)
			{
			case TCN_SELCHANGING:
				OnTabChanging(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
				break;
			case TCN_SELCHANGE:
				OnTabChanged(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
				RefreshAllWordHighlights(GetDlgItem(hWnd, IDC_ASSEMBLER));
				break;
			}
			break;
		case IDC_ASSEMBLER:
			if (((NMHDR *)lParam)->code == SCN_STYLENEEDED)
			{
				DoCustomStyling(((NMHDR *)lParam)->hwndFrom, 0, ((SCNotification *)lParam)->position);
			}
			break;
		}
		break;
	case UWM_NOTIFY:
		if(((UNMTABCTRLEX *)lParam)->hdr.idFrom == IDC_TABS)
		{
			switch(((NMHDR *)lParam)->code)
			{
			case TCN_EX_DRAGDROP:
				OnTabDrag(GetDlgItem(hWnd, IDC_TABS), (int)((UNMTABCTRLEX *)lParam)->wParam, (int)((UNMTABCTRLEX *)lParam)->lParam);
				break;
			case TCN_EX_DBLCLK:
				TabRenameStart(GetDlgItem(hWnd, IDC_TABS));
				break;
			case TCN_EX_MCLICK:
				pt.x = GET_X_LPARAM(((UNMTABCTRLEX *)lParam)->lParam);
				pt.y = GET_Y_LPARAM(((UNMTABCTRLEX *)lParam)->lParam);
				CloseTabOnPoint(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), &pt);
				break;
			case TCN_EX_CONTEXTMENU:
				if(OnContextMenu(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), 
					((UNMTABCTRLEX *)lParam)->lParam, &pt))
					TrackPopupMenu(GetSubMenu(p_dialog_param->hMenu, 1), TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
				break;
			case TCN_EX_BEGINLABELEDIT:
				SetWindowLongPtr(hWnd, DWLP_MSGRESULT, FALSE);
				return TRUE;
			case TCN_EX_ENDLABELEDIT:
				SetWindowLongPtr(hWnd, DWLP_MSGRESULT, 
					TabRenameEnd(GetDlgItem(hWnd, IDC_TABS), (TCHAR *)((UNMTABCTRLEX *)lParam)->lParam));
				return TRUE;
			}
		}
		break;
	case UWM_ERRORMSG:
		AsmDlgMessageBox(hWnd, (TCHAR *)lParam, NULL, (UINT)wParam);
		break;
	case UWM_COLLAPSE_SEL:
		// Deferred handler: collapse any text selection in the Scintilla editor
		// to the current caret position. This prevents the block selection bug
		// that occurs when Ctrl+M transfers focus while Ctrl is physically held.
		{
			HWND hEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
			if (hEdit)
			{
				LRESULT caretPos = SendMessage(hEdit, SCI_GETCURRENTPOS, 0, 0);
				SendMessage(hEdit, SCI_SETSEL, caretPos, caretPos);
			}
		}
		break;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_ASSEMBLER)
		{
			if (HIWORD(wParam) == EN_CHANGE)
			{
				RefreshAllWordHighlights(GetDlgItem(hWnd, IDC_ASSEMBLER));
			}
			break;
		}
		switch(LOWORD(wParam))
		{
		case ID_ACCEL_COMMENT_LINE:
			{
				HWND hAsmEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
				if (hAsmEdit)
				{
					int anchor = (int)SendMessage(hAsmEdit, SCI_GETANCHOR, 0, 0);
					int currentPos = (int)SendMessage(hAsmEdit, SCI_GETCURRENTPOS, 0, 0);
					int selStart = (anchor < currentPos) ? anchor : currentPos;
					int selEnd = (anchor < currentPos) ? currentPos : anchor;
					if (selStart == selEnd && s_backupSelStart != -1 && s_backupSelStart != s_backupSelEnd)
					{
						selStart = s_backupSelStart;
						selEnd = s_backupSelEnd;
					}
					int startLine = (int)SendMessage(hAsmEdit, SCI_LINEFROMPOSITION, selStart, 0);
					int endLine = (int)SendMessage(hAsmEdit, SCI_LINEFROMPOSITION, selEnd, 0);
					int line;
					if (endLine > startLine && (int)SendMessage(hAsmEdit, SCI_POSITIONFROMLINE, endLine, 0) == selEnd)
					{
						endLine--;
					}
					SendMessage(hAsmEdit, SCI_BEGINUNDOACTION, 0, 0);
					for (line = endLine; line >= startLine; line--)
					{
						int lineStartPos = (int)SendMessage(hAsmEdit, SCI_POSITIONFROMLINE, line, 0);
						SendMessage(hAsmEdit, SCI_INSERTTEXT, lineStartPos, (LPARAM)";");
					}
					SendMessage(hAsmEdit, SCI_ENDUNDOACTION, 0, 0);
					// Restore selection to cover the commented lines
					int newSelStart = (int)SendMessage(hAsmEdit, SCI_POSITIONFROMLINE, startLine, 0);
					int newSelEnd = (int)SendMessage(hAsmEdit, SCI_GETLINEENDPOSITION, endLine, 0);
					SendMessage(hAsmEdit, SCI_SETSEL, newSelStart, newSelEnd);
					s_backupSelStart = -1;
					s_backupSelEnd = -1;
				}
			}
			break;
		case ID_ACCEL_UNCOMMENT_LINE:
			{
				HWND hAsmEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
				if (hAsmEdit)
				{
					int anchor = (int)SendMessage(hAsmEdit, SCI_GETANCHOR, 0, 0);
					int currentPos = (int)SendMessage(hAsmEdit, SCI_GETCURRENTPOS, 0, 0);
					int selStart = (anchor < currentPos) ? anchor : currentPos;
					int selEnd = (anchor < currentPos) ? currentPos : anchor;
					if (selStart == selEnd && s_backupSelStart != -1 && s_backupSelStart != s_backupSelEnd)
					{
						selStart = s_backupSelStart;
						selEnd = s_backupSelEnd;
					}
					int startLine = (int)SendMessage(hAsmEdit, SCI_LINEFROMPOSITION, selStart, 0);
					int endLine = (int)SendMessage(hAsmEdit, SCI_LINEFROMPOSITION, selEnd, 0);
					int line;
					if (endLine > startLine && (int)SendMessage(hAsmEdit, SCI_POSITIONFROMLINE, endLine, 0) == selEnd)
					{
						endLine--;
					}
					SendMessage(hAsmEdit, SCI_BEGINUNDOACTION, 0, 0);
					for (line = endLine; line >= startLine; line--)
					{
						int lineStartPos = (int)SendMessage(hAsmEdit, SCI_POSITIONFROMLINE, line, 0);
						int lineEndPos = (int)SendMessage(hAsmEdit, SCI_GETLINEENDPOSITION, line, 0);
						int lineLen = lineEndPos - lineStartPos;
						if (lineLen > 0)
						{
							int fullLineLen = (int)SendMessage(hAsmEdit, SCI_GETLINE, line, 0);
							char *szFullBuf = (char *)HeapAlloc(GetProcessHeap(), 0, fullLineLen + 1);
							if (szFullBuf)
							{
								SendMessage(hAsmEdit, SCI_GETLINE, line, (LPARAM)szFullBuf);
								szFullBuf[fullLineLen] = '\0';
								int idx = 0;
								while (szFullBuf[idx] == ' ' || szFullBuf[idx] == '\t')
								{
									idx++;
								}
								if (szFullBuf[idx] == ';')
								{
									int deletePos = lineStartPos + idx;
									SendMessage(hAsmEdit, SCI_DELETERANGE, deletePos, 1);
								}
								HeapFree(GetProcessHeap(), 0, szFullBuf);
							}
						}
					}
					SendMessage(hAsmEdit, SCI_ENDUNDOACTION, 0, 0);
					// Restore selection to cover the uncommented lines
					int newSelStart = (int)SendMessage(hAsmEdit, SCI_POSITIONFROMLINE, startLine, 0);
					int newSelEnd = (int)SendMessage(hAsmEdit, SCI_GETLINEENDPOSITION, endLine, 0);
					SendMessage(hAsmEdit, SCI_SETSEL, newSelStart, newSelEnd);
					s_backupSelStart = -1;
					s_backupSelEnd = -1;
				}
			}
			break;
		case ID_RCM_UNDO:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_UNDO, 0, 0);
			break;
		case ID_RCM_REDO:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_REDO, 0, 0);
			break;
		case ID_RCM_CUT:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_CUT, 0, 0);
			break;
		case ID_RCM_COPY:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_COPY, 0, 0);
			break;
		case ID_RCM_PASTE:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_PASTE, 0, 0);
			break;
		case ID_RCM_DELETE:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_CLEAR, 0, 0);
			break;
		case ID_RCM_SELECTALL:
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SELECTALL, 0, 0);
			break;
		case ID_TABMENU_NEWTAB:
			OnTabChanging(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			NewTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), NULL);
			break;
		case ID_TABMENU_RENAME:
			TabRenameStart(GetDlgItem(hWnd, IDC_TABS));
			break;
		case ID_TABMENU_CLOSE:
			CloseTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			break;
		case ID_TABMENU_LOADFROMFILE:
			LoadFileFromLibrary(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), hWnd, hDllInst);
			break;
		case ID_TABMENU_SAVETOFILE:
			SaveFileToLibrary(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER), hWnd, hDllInst);
			break;
		case ID_TABSTRIPMENU_CLOSEALLTABS:
			CloseAllTabs(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			break;
		case ID_ACCEL_PREVTAB:
			PrevTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			break;
		case ID_ACCEL_NEXTTAB:
			NextTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			break;
		case ID_ACCEL_FINDWND:
			ShowFindDialog(p_dialog_param);
			break;
		case ID_ACCEL_REPLACEWND:
			ShowReplaceDialog(p_dialog_param);
			break;
		case ID_ACCEL_FINDNEXT:
			if(*p_dialog_param->szFindStr != _T('\0'))
				DoFindCustom(p_dialog_param, FR_DOWN, 0);
			else
				ShowFindDialog(p_dialog_param);
			break;
		case ID_ACCEL_FINDPREV:
			if(*p_dialog_param->szFindStr != _T('\0'))
				DoFindCustom(p_dialog_param, 0, FR_DOWN);
			else
				ShowFindDialog(p_dialog_param);
			break;
		case ID_ACCEL_FOCUS_OLLYDBG:
			SetFocus(hwollymain);
			break;
		case ID_ACCEL_BLOCK_MODE:
			// Block mode is RAEdit specific, disabled or fallback
			break;
		case IDOK:
			SaveFileOfTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
			PatchCode(hWnd);
			break;
		case IDC_CLOSE:
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		}
		break;
	case WM_CLOSE:
		WriteFileLog(_T("DlgAsmProc() - WM_CLOSE received"));
		if(!IsWindowEnabled(hWnd))
		{
			hPopupWnd = GetWindow(hWnd, GW_ENABLEDPOPUP);
			if(hPopupWnd && hPopupWnd != hWnd)
				SendMessage(hPopupWnd, WM_CLOSE, 0, 0);
		}
		if(p_dialog_param->hFindReplaceWnd)
			SendMessage(p_dialog_param->hFindReplaceWnd, WM_CLOSE, 0, 0);
		SaveWindowPos(hWnd, hDllInst);
		SaveFileOfTab(GetDlgItem(hWnd, IDC_TABS), GetDlgItem(hWnd, IDC_ASSEMBLER));
		DestroyWindow(hWnd);
		hAsmDlg = NULL;
		SetWindowLongPtr(hWnd, DWLP_MSGRESULT, 0);
		return TRUE;
	case WM_DESTROY:
		WriteFileLog(_T("DlgAsmProc() - WM_DESTROY received, setting hAsmDlg = NULL"));
		hAsmDlg = NULL;
		{
			HWND hAsmEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
			if (hAsmEdit && wpOrigScintillaProc)
			{
				SetWindowLongPtr(hAsmEdit, GWLP_WNDPROC, (LONG_PTR)wpOrigScintillaProc);
				wpOrigScintillaProc = NULL;
			}
		}
		if(p_dialog_param->hSmallIcon)
		{
			DestroyIcon(p_dialog_param->hSmallIcon);
			p_dialog_param->hSmallIcon = NULL;
		}
		if(p_dialog_param->hLargeIcon)
		{
			DestroyIcon(p_dialog_param->hLargeIcon);
			p_dialog_param->hLargeIcon = NULL;
		}
		if(p_dialog_param->raFont.hFont)
		{
			DeleteObject(p_dialog_param->raFont.hFont);
			p_dialog_param->raFont.hFont = NULL;
		}
		if(p_dialog_param->raFont.hIFont)
		{
			DeleteObject(p_dialog_param->raFont.hIFont);
			p_dialog_param->raFont.hIFont = NULL;
		}
		if(p_dialog_param->raFont.hLnrFont)
		{
			DeleteObject(p_dialog_param->raFont.hLnrFont);
			p_dialog_param->raFont.hLnrFont = NULL;
		}
		if(p_dialog_param->hMenu)
		{
			DestroyMenu(p_dialog_param->hMenu);
			p_dialog_param->hMenu = NULL;
		}
		if(p_dialog_param->bTabCtrlExInitialized)
		{
			TabCtrlExExit(GetDlgItem(hWnd, IDC_TABS));
			p_dialog_param->bTabCtrlExInitialized = FALSE;
		}
		break;
	case WM_NCDESTROY:
		WriteFileLog(_T("DlgAsmProc() - WM_NCDESTROY received"));
		break;
	default:
		if(uMsg == p_dialog_param->uFindReplaceMsg)
		{
			p_findreplace = (FINDREPLACE *)lParam;
			if(p_findreplace->Flags & FR_FINDNEXT)
				DoFind(p_dialog_param);
			else if(p_findreplace->Flags & FR_REPLACE)
				DoReplace(p_dialog_param);
			else if(p_findreplace->Flags & FR_REPLACEALL)
				DoReplaceAll(p_dialog_param);
			else if(p_findreplace->Flags & FR_DIALOGTERM)
				p_dialog_param->hFindReplaceWnd = NULL;
		}
		break;
	}
	return FALSE;
}
static void SetScintillaDesign(HWND hWnd, HINSTANCE hInstance, RAFONT *praFont)
{
	WriteFileLog(_T("SetScintillaDesign() - started"));
	HWND hAsmEdit = GetDlgItem(hWnd, IDC_ASSEMBLER);
	WriteFileLog(_T("SetScintillaDesign() - GetDlgItem hAsmEdit = %p"), hAsmEdit);
	if (!hAsmEdit)
	{
		WriteFileLog(_T("SetScintillaDesign() - hAsmEdit is NULL, returning"));
		return;
	}
	int tabwidth_variants[] = {2, 4, 8};
	COLORREF bckcol, txtcol, selbckcol, seltxtcol, oprcol, strcol, lnrcol;
		COLORREF colorJmpBg, colorCallBg, colorRetBg, colorNopBg, colorRegSpecialBg;
	COLORREF colorFpuBg, colorExtBg, colorTypeBg, colorOtherBg, colorRegBg;
	COLORREF colorJmp;
	COLORREF colorCall;
	COLORREF colorRet;
	COLORREF colorNop;
	COLORREF colorRegSpecial;
	COLORREF colorFpu;
	COLORREF colorExt;
	COLORREF colorType;
	COLORREF colorOther;
	BOOL isDark = FALSE;
	WriteFileLog(_T("SetScintillaDesign() - checking options.bg_use_custom"));
	WriteFileLog(_T("SetScintillaDesign() - options address = %p"), &options);
	
	if (options.bg_use_custom)
	{
		WriteFileLog(_T("SetScintillaDesign() - options.bg_use_custom is true"));
		bckcol = options.bg_color;
		WriteFileLog(_T("SetScintillaDesign() - options.bg_color = %08X"), bckcol);
		// Calculate background luminance
		BYTE r = GetRValue(options.bg_color);
		BYTE g = GetGValue(options.bg_color);
		BYTE b = GetBValue(options.bg_color);
		double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
		if (luminance < 128.0) // Dark Background
		{
			isDark = TRUE;
			txtcol = RGB(240, 240, 240);
			selbckcol = RGB(50, 75, 100);
			seltxtcol = RGB(255, 255, 255);
			oprcol = RGB(220, 220, 220);
			strcol = RGB(230, 110, 110);
			lnrcol = RGB(150, 150, 150);
		}
		else // Light Background
		{
			txtcol = RGB(0, 0, 0);
			selbckcol = RGB(173, 214, 255); // Soft pastel light blue selection background
			seltxtcol = RGB(255, 255, 255);
			oprcol = RGB(0, 0, 160);
			strcol = RGB(160, 0, 0);
			lnrcol = RGB(128, 0, 0);
		}
	}
	else
	{
		WriteFileLog(_T("SetScintillaDesign() - options.bg_use_custom is false"));
		bckcol = RGB(255, 255, 255);
		txtcol = RGB(0, 0, 0);
		selbckcol = RGB(173, 214, 255); // Soft pastel light blue selection background
		seltxtcol = RGB(255, 255, 255);
		oprcol = RGB(0, 0, 160);
		strcol = RGB(160, 0, 0);
		lnrcol = RGB(128, 0, 0);
	}
	WriteFileLog(_T("SetScintillaDesign() - isDark = %d"), isDark);
	int val;
	if (isDark)
	{
		WriteFileLog(_T("SetScintillaDesign() - fetching colors for dark mode"));
		MyGetColorfromini(hInstance, _T("color_jmp"), &val, RGB(255, 110, 110));
		colorJmp = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_call"), &val, RGB(100, 230, 150));
		colorCall = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ret"), &val, RGB(220, 140, 255));
		colorRet = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_nop"), &val, RGB(110, 110, 110));
		colorNop = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_reg_special"), &val, RGB(255, 160, 220));
		colorRegSpecial = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_fpu"), &val, RGB(255, 180, 255));
		colorFpu = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ext"), &val, RGB(255, 130, 130));
		colorExt = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_type"), &val, RGB(140, 200, 255));
		colorType = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_other"), &val, RGB(255, 200, 100));
		colorOther = (COLORREF)val;
		// Load Custom Background colors for Dark Mode (Defaults to bckcol)
		MyGetColorfromini(hInstance, _T("color_jmp_bg"), &val, bckcol);
		colorJmpBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_call_bg"), &val, bckcol);
		colorCallBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ret_bg"), &val, bckcol);
		colorRetBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_nop_bg"), &val, bckcol);
		colorNopBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_reg_special_bg"), &val, bckcol);
		colorRegSpecialBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_fpu_bg"), &val, bckcol);
		colorFpuBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ext_bg"), &val, bckcol);
		colorExtBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_type_bg"), &val, bckcol);
		colorTypeBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_other_bg"), &val, bckcol);
		colorOtherBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_reg_bg"), &val, bckcol);
		colorRegBg = (COLORREF)val;
	}
	else
	{
		WriteFileLog(_T("SetScintillaDesign() - fetching colors for light mode"));
		MyGetColorfromini(hInstance, _T("color_jmp"), &val, RGB(210, 0, 0));
		colorJmp = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_call"), &val, RGB(0, 130, 70));
		colorCall = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ret"), &val, RGB(0, 0, 0));
		colorRet = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_nop"), &val, RGB(160, 160, 160));
		colorNop = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_reg_special"), &val, RGB(180, 0, 100));
		colorRegSpecial = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_fpu"), &val, RGB(160, 0, 160));
		colorFpu = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ext"), &val, RGB(200, 0, 0));
		colorExt = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_type"), &val, RGB(0, 90, 200));
		colorType = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_other"), &val, RGB(150, 75, 0));
		colorOther = (COLORREF)val;
		// Load Custom Background colors for Light Mode (Defaults to bckcol)
		MyGetColorfromini(hInstance, _T("color_jmp_bg"), &val, bckcol);
		colorJmpBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_call_bg"), &val, bckcol);
		colorCallBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ret_bg"), &val, RGB(121, 249, 121));
		colorRetBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_nop_bg"), &val, bckcol);
		colorNopBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_reg_special_bg"), &val, bckcol);
		colorRegSpecialBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_fpu_bg"), &val, bckcol);
		colorFpuBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_ext_bg"), &val, bckcol);
		colorExtBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_type_bg"), &val, bckcol);
		colorTypeBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_other_bg"), &val, bckcol);
		colorOtherBg = (COLORREF)val;
		MyGetColorfromini(hInstance, _T("color_reg_bg"), &val, bckcol);
		colorRegBg = (COLORREF)val;
	}
	COLORREF colorJmpLine;
	COLORREF colorCallLine;
	WriteFileLog(_T("SetScintillaDesign() - setting up line highlighting colors"));
	if (isDark)
	{
		if (options.color_jmp_line_bg == RGB(255, 230, 230))
			colorJmpLine = RGB(40, 25, 25);
		else
			colorJmpLine = options.color_jmp_line_bg;
		if (options.color_call_line_bg == RGB(230, 255, 235))
			colorCallLine = RGB(25, 40, 30);
		else
			colorCallLine = options.color_call_line_bg;
	}
	else
	{
		colorJmpLine = options.color_jmp_line_bg;
		colorCallLine = options.color_call_line_bg;
	}
	WriteFileLog(_T("SetScintillaDesign() - 1. Establish Scintilla Lexer"));
	SendMessage(hAsmEdit, SCI_SETLEXER, SCLEX_CONTAINER, 0);
	SendMessage(hAsmEdit, SCI_SETCODEPAGE, SC_CP_UTF8, 0);
	WriteFileLog(_T("SetScintillaDesign() - 2. Set Default Styles"));
	SendMessage(hAsmEdit, SCI_STYLESETFORE, STYLE_DEFAULT, txtcol);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, STYLE_DEFAULT, bckcol);
	
	WriteFileLog(_T("SetScintillaDesign() - Convert font name to UTF-8"));
	char szFontAnsi[64];
#ifdef UNICODE
	WideCharToMultiByte(CP_UTF8, 0, options.font_name, -1, szFontAnsi, 64, NULL, NULL);
#else
	// options.font_name is CP_ACP. Convert to UTF-8 for Scintilla.
	{
		int wideLen = MultiByteToWideChar(CP_ACP, 0, options.font_name, -1, NULL, 0);
		if (wideLen > 0)
		{
			wchar_t *lpWide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, wideLen * sizeof(wchar_t));
			if (lpWide)
			{
				MultiByteToWideChar(CP_ACP, 0, options.font_name, -1, lpWide, wideLen);
				WideCharToMultiByte(CP_UTF8, 0, lpWide, -1, szFontAnsi, 64, NULL, NULL);
				HeapFree(GetProcessHeap(), 0, lpWide);
			}
			else
			{
				lstrcpynA(szFontAnsi, options.font_name, 64);
			}
		}
		else
		{
			lstrcpynA(szFontAnsi, options.font_name, 64);
		}
	}
#endif
	WriteFileLog(_T("SetScintillaDesign() - font name: %s, size: %d"), szFontAnsi, options.font_size);
	SendMessage(hAsmEdit, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)szFontAnsi);
	SendMessage(hAsmEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, options.font_size);
	SendMessage(hAsmEdit, SCI_STYLESETBOLD, STYLE_DEFAULT, options.font_bold);
	SendMessage(hAsmEdit, SCI_STYLESETITALIC, STYLE_DEFAULT, options.font_italic);
	SendMessage(hAsmEdit, SCI_STYLECLEARALL, 0, 0);
	// Add +3px line spacing (Ascent +2px, Descent +1px)
	SendMessage(hAsmEdit, SCI_SETEXTRAASCENT, 2, 0);
	SendMessage(hAsmEdit, SCI_SETEXTRADESCENT, 1, 0);
	WriteFileLog(_T("SetScintillaDesign() - 3. Set Granular Styling Colors"));
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_DEFAULT, txtcol);
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_IDENTIFIER, txtcol);
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_COMMENT, options.color_cmnt);
	SendMessage(hAsmEdit, SCI_STYLESETITALIC, SCE_ASM_COMMENT, FALSE);
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_NUMBER, options.color_num);
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_STRING, strcol);
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_OPERATOR, oprcol);
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_CPUINSTRUCTION, options.color_cmd);
	COLORREF cmd_bg = options.color_cmd_bg;
	if (cmd_bg == CLR_INVALID) cmd_bg = bckcol;
	if (cmd_bg != bckcol)
		SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_CPUINSTRUCTION, cmd_bg);
	// FPU Commands (Style 7)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_MATHINSTRUCTION, colorFpu);
	if (colorFpuBg != bckcol)
		SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_MATHINSTRUCTION, colorFpuBg);
	// General GP Registers (Style 8)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_REGISTER, options.color_reg);
	if (colorRegBg != bckcol)
		SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_REGISTER, colorRegBg);
	// Type Keywords (Style 9)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_DIRECTIVE, colorType);
	if (colorTypeBg != bckcol)
		SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_DIRECTIVE, colorTypeBg);
	// Auxiliary Keywords (Style 10)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_DIRECTIVEOPERAND, colorOther);
	if (colorOtherBg != bckcol)
		SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_DIRECTIVEOPERAND, colorOtherBg);
	// SSE/AVX Extension Commands (Style 14)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_EXTINSTRUCTION, colorExt);
	if (colorExtBg != bckcol)
		SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_EXTINSTRUCTION, colorExtBg);
	// JMP Commands (Custom Style 16)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_JMP, colorJmp);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_JMP, colorJmpBg);
	// CALL Commands (Custom Style 17)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_CALL, colorCall);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_CALL, colorCallBg);
	// RET Commands (Custom Style 18)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_RET, colorRet);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_RET, colorRetBg);
	// NOP Commands (Custom Style 19)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_NOP, colorNop);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_NOP, colorNopBg);
	// Special Stack & Seg Registers (Custom Style 20)
	SendMessage(hAsmEdit, SCI_STYLESETFORE, SCE_ASM_REG_SPECIAL, colorRegSpecial);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, SCE_ASM_REG_SPECIAL, colorRegSpecialBg);
	WriteFileLog(_T("SetScintillaDesign() - 4. Assign Keyword Lists"));
	SendMessage(hAsmEdit, SCI_SETKEYWORDS, 0, (LPARAM)(HILITE_ASM_CMD_BASIC " " HILITE_ASM_RET_CMD " " HILITE_ASM_NOP_CMD));
	SendMessage(hAsmEdit, SCI_SETKEYWORDS, 1, (LPARAM)(HILITE_REG_GP " " HILITE_REG_SPECIAL));
	SendMessage(hAsmEdit, SCI_SETKEYWORDS, 2, (LPARAM)HILITE_ASM_FPU_CMD);
	SendMessage(hAsmEdit, SCI_SETKEYWORDS, 3, (LPARAM)(HILITE_TYPE " " HILITE_OTHER));
	SendMessage(hAsmEdit, SCI_SETKEYWORDS, 4, (LPARAM)HILITE_ASM_EXT_CMD);
	SendMessage(hAsmEdit, SCI_SETKEYWORDS, 5, (LPARAM)(HILITE_ASM_JMP_CMD " " HILITE_ASM_CALL_CMD));
	WriteFileLog(_T("SetScintillaDesign() - 5. Line numbers setup"));
	SendMessage(hAsmEdit, SCI_SETMARGINTYPEN, 0, 1);
	SendMessage(hAsmEdit, SCI_SETMARGINWIDTHN, 0, 0); 
	SendMessage(hAsmEdit, SCI_STYLESETFORE, STYLE_LINENUMBER, lnrcol);
	SendMessage(hAsmEdit, SCI_STYLESETBACK, STYLE_LINENUMBER, bckcol);
	WriteFileLog(_T("SetScintillaDesign() - 6. Selection colors"));
	// Set Selection Layer to Base (0) so selection is drawn UNDER text.
	// Since explicit bckcol styling has been removed from normal text, this allows normal selection 
	// to show nicely while retaining call/jmp background colors on top!
	SendMessage(hAsmEdit, 2309, 0, 0);             // SCI_SETSELECTIONLAYER(0 = SC_LAYER_BASE)
	SendMessage(hAsmEdit, 2067, FALSE, 0);         // SCI_SETSELFORE(FALSE): keep original styled text colors during selection
	SendMessage(hAsmEdit, 2068, TRUE, selbckcol);  // SCI_SETSELBACK(TRUE, color): set selection background color
	SendMessage(hAsmEdit, 2477, 255, 0);           // SCI_SETSELALPHA(255): set selection opacity to solid opaque under text
	WriteFileLog(_T("SetScintillaDesign() - Disable horizontal scrollbar"));
	SendMessage(hAsmEdit, SCI_SETHSCROLLBAR, FALSE, 0); // Hide horizontal scrollbar
	SendMessage(hAsmEdit, 2030, 1, 0);                 // SCI_SETSCROLLWIDTH(1): eliminate horizontal scroll area
	// Forcefully remove WS_HSCROLL style at the Win32 window layer
	LONG_PTR style = GetWindowLongPtr(hAsmEdit, GWL_STYLE);
	if (style & WS_HSCROLL) {
		style &= ~WS_HSCROLL;
		SetWindowLongPtr(hAsmEdit, GWL_STYLE, style);
		SetWindowPos(hAsmEdit, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}
	WriteFileLog(_T("SetScintillaDesign() - 7. Tab size"));
	// Add safety bound check for options.edit_tabwidth
	int tabIdx = options.edit_tabwidth;
	if (tabIdx < 0 || tabIdx > 2) {
		WriteFileLog(_T("SetScintillaDesign() - options.edit_tabwidth is out of bounds (%d), defaulting to 1 (tab size 4)"), tabIdx);
		tabIdx = 1;
	}
	SendMessage(hAsmEdit, SCI_SETTABWIDTH, tabwidth_variants[tabIdx], 0);
	WriteFileLog(_T("SetScintillaDesign() - 8. Word-level dynamic Indicator setups"));
	SendMessage(hAsmEdit, SCI_INDICSETSTYLE, INDIC_JMP, INDIC_ROUNDBOX);
	SendMessage(hAsmEdit, SCI_INDICSETFORE, INDIC_JMP, colorJmpLine);
	SendMessage(hAsmEdit, SCI_INDICSETALPHA, INDIC_JMP, 80);
	SendMessage(hAsmEdit, SCI_INDICSETOUTLINEALPHA, INDIC_JMP, 120);
	SendMessage(hAsmEdit, SCI_INDICSETSTYLE, INDIC_CALL, INDIC_ROUNDBOX);
	SendMessage(hAsmEdit, SCI_INDICSETFORE, INDIC_CALL, colorCallLine);
	SendMessage(hAsmEdit, SCI_INDICSETALPHA, INDIC_CALL, 80);
	SendMessage(hAsmEdit, SCI_INDICSETOUTLINEALPHA, INDIC_CALL, 120);
	WriteFileLog(_T("SetScintillaDesign() - Refresh highlights"));
	WriteFileLog(_T("SetScintillaDesign() - Clear default Ctrl+M and Ctrl+Shift+M key bindings"));
	SendMessage(hAsmEdit, 2071, (WPARAM)('M' | (2 << 16)), 0);                 // SCI_CLEARCMDKEY: Ctrl + M 해제 (2 = SCMOD_CTRL)
	SendMessage(hAsmEdit, 2071, (WPARAM)('M' | ((2 | 1) << 16)), 0);           // SCI_CLEARCMDKEY: Ctrl + Shift + M 해제 (3 = SCMOD_CTRL | SCMOD_SHIFT)
	
	RefreshAllWordHighlights(hAsmEdit);
	SendMessage(hAsmEdit, 2018, FALSE, 0); // SCI_USEPOPUP(FALSE)
	SendMessage(hAsmEdit, SCI_COLOURISE, 0, -1);
	WriteFileLog(_T("SetScintillaDesign() - completed successfully"));
}
static void UpdateRightClickMenuState(HWND hWnd, HMENU hMenu)
{
	struct Sci_CharacterRange crRange;
	UINT uEnable;
	if(SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_CANUNDO, 0, 0))
		EnableMenuItem(hMenu, ID_RCM_UNDO, MF_ENABLED);
	else
		EnableMenuItem(hMenu, ID_RCM_UNDO, MF_GRAYED);
	if(SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_CANREDO, 0, 0))
		EnableMenuItem(hMenu, ID_RCM_REDO, MF_ENABLED);
	else
		EnableMenuItem(hMenu, ID_RCM_REDO, MF_GRAYED);
	// Paste enabled check
	EnableMenuItem(hMenu, ID_RCM_PASTE, MF_ENABLED);
	crRange.cpMin = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONSTART, 0, 0);
	crRange.cpMax = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONEND, 0, 0);
	
	if(crRange.cpMax - crRange.cpMin > 0)
		uEnable = MF_ENABLED;
	else
		uEnable = MF_GRAYED;
	EnableMenuItem(hMenu, ID_RCM_CUT, uEnable);
	EnableMenuItem(hMenu, ID_RCM_COPY, uEnable);
	EnableMenuItem(hMenu, ID_RCM_DELETE, uEnable);
}
static void LoadWindowPos(HWND hWnd, HINSTANCE hInst, long *p_min_w, long *p_min_h)
{
	long cur_w, cur_h;
	long min_w, min_h;
	int x, y, w, h;
	RECT rc;
	WINDOWPLACEMENT wp;
	GetWindowRect(hWnd, &rc);
	cur_w = rc.right-rc.left;
	cur_h = rc.bottom-rc.top;
	GetWindowRect(GetDlgItem(hWnd, IDOK), &rc);
	min_w = cur_w + rc.right;
	GetWindowRect(GetDlgItem(hWnd, IDC_CLOSE), &rc);
	min_w -= rc.left;
	GetWindowRect(GetDlgItem(hWnd, IDC_ASSEMBLER), &rc);
	min_h = cur_h - (rc.bottom-rc.top);
	*p_min_w = min_w;
	*p_min_h = min_h;
	if(options.edit_savepos)
	{
		if(MyGetintfromini(hInst, _T("pos_x"), &x, 0, 0, 0) && MyGetintfromini(hInst, _T("pos_y"), &y, 0, 0, 0))
		{
			MyGetintfromini(hInst, _T("pos_w"), &w, min_w, INT_MAX, 900);
			MyGetintfromini(hInst, _T("pos_h"), &h, min_h, INT_MAX, 600);
			wp.length = sizeof(WINDOWPLACEMENT);
			wp.flags = 0;
			wp.showCmd = IsWindowVisible(hWnd) ? SW_SHOW : SW_HIDE;
			wp.rcNormalPosition.left = x;
			wp.rcNormalPosition.top = y;
			wp.rcNormalPosition.right = x+w;
			wp.rcNormalPosition.bottom = y+h;
			SetWindowPlacement(hWnd, &wp);
		}
		else
		{
			int scr_w = GetSystemMetrics(SM_CXSCREEN);
			int scr_h = GetSystemMetrics(SM_CYSCREEN);
			x = (scr_w - 900) / 2;
			y = (scr_h - 600) / 2;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			SetWindowPos(hWnd, NULL, x, y, 900, 600, SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
	else
	{
		int scr_w = GetSystemMetrics(SM_CXSCREEN);
		int scr_h = GetSystemMetrics(SM_CYSCREEN);
		x = (scr_w - 900) / 2;
		y = (scr_h - 600) / 2;
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		SetWindowPos(hWnd, NULL, x, y, 900, 600, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}
static void SaveWindowPos(HWND hWnd, HINSTANCE hInst)
{
	WINDOWPLACEMENT wp;
	if(options.edit_savepos)
	{
		wp.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(hWnd, &wp);
		MyWriteinttoini(hInst, _T("pos_x"), wp.rcNormalPosition.left);
		MyWriteinttoini(hInst, _T("pos_y"), wp.rcNormalPosition.top);
		MyWriteinttoini(hInst, _T("pos_w"), wp.rcNormalPosition.right-wp.rcNormalPosition.left);
		MyWriteinttoini(hInst, _T("pos_h"), wp.rcNormalPosition.bottom-wp.rcNormalPosition.top);
	}
}
static HDWP ChildRelativeDeferWindowPos(HDWP hWinPosInfo, HWND hWnd, int nIDDlgItem, int x, int y, int cx, int cy)
{
	HWND hChildWnd;
	RECT rc;
	hChildWnd = GetDlgItem(hWnd, nIDDlgItem);
	GetWindowRect(hChildWnd, &rc);
	MapWindowPoints(NULL, hWnd, (POINT *)&rc, 2);
	return DeferWindowPos(hWinPosInfo, hChildWnd, NULL, 
		rc.left+x, rc.top+y, (rc.right-rc.left)+cx, (rc.bottom-rc.top)+cy, SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
}
static int AsmDlgMessageBox(HWND hWnd, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType)
{
	ASM_DIALOG_PARAM *p_dialog_param;
	HWND hFindReplaceWnd;
	BOOL bOllyEnabled;
	int nRet;
	bOllyEnabled = IsWindowEnabled(hwollymain);
	if(bOllyEnabled)
		EnableWindow(hwollymain, FALSE);
	p_dialog_param = (ASM_DIALOG_PARAM *)GetWindowLongPtr(hWnd, DWLP_USER);
	if(p_dialog_param)
		hFindReplaceWnd = p_dialog_param->hFindReplaceWnd;
	else
		hFindReplaceWnd = NULL;
	if(hFindReplaceWnd)
	{
		if(GetActiveWindow() == hFindReplaceWnd)
		{
			EnableWindow(hWnd, FALSE);
			nRet = MessageBox(hFindReplaceWnd, lpText, lpCaption, uType);
			EnableWindow(hWnd, TRUE);
		}
		else
		{
			EnableWindow(hFindReplaceWnd, FALSE);
			nRet = MessageBox(hWnd, lpText, lpCaption, uType);
			EnableWindow(hFindReplaceWnd, TRUE);
		}
	}
	else
		nRet = MessageBox(hWnd, lpText, lpCaption, uType);
	if(bOllyEnabled)
		EnableWindow(hwollymain, TRUE);
	return nRet;
}
static void InitFindReplace(HWND hWnd, HINSTANCE hInst, ASM_DIALOG_PARAM *p_dialog_param)
{
	p_dialog_param->uFindReplaceMsg = RegisterWindowMessage(FINDMSGSTRING);
	p_dialog_param->hFindReplaceWnd = NULL;
	*p_dialog_param->szFindStr = _T('\0');
	*p_dialog_param->szReplaceStr = _T('\0');
	p_dialog_param->findreplace.lStructSize = sizeof(FINDREPLACE);
	p_dialog_param->findreplace.hwndOwner = hWnd;
	p_dialog_param->findreplace.hInstance = hInst;
	p_dialog_param->findreplace.Flags = FR_DOWN;
	p_dialog_param->findreplace.lpstrFindWhat = p_dialog_param->szFindStr;
	p_dialog_param->findreplace.lpstrReplaceWith = p_dialog_param->szReplaceStr;
	p_dialog_param->findreplace.wFindWhatLen = FIND_REPLACE_TEXT_BUFFER;
	p_dialog_param->findreplace.wReplaceWithLen = FIND_REPLACE_TEXT_BUFFER;
}
static void ShowFindDialog(ASM_DIALOG_PARAM *p_dialog_param)
{
	if(p_dialog_param->hFindReplaceWnd)
		SetFocus(p_dialog_param->hFindReplaceWnd);
	else
		p_dialog_param->hFindReplaceWnd = FindText(&p_dialog_param->findreplace);
}
static void ShowReplaceDialog(ASM_DIALOG_PARAM *p_dialog_param)
{
	if(p_dialog_param->hFindReplaceWnd)
		SetFocus(p_dialog_param->hFindReplaceWnd);
	else
		p_dialog_param->hFindReplaceWnd = ReplaceText(&p_dialog_param->findreplace);
}
static void DoFind(ASM_DIALOG_PARAM *p_dialog_param)
{
	DoFindCustom(p_dialog_param, 0, 0);
}
static void DoFindCustom(ASM_DIALOG_PARAM *p_dialog_param, WPARAM wFlagsSet, WPARAM wFlagsRemove)
{
	FINDREPLACE *p_findreplace;
	HWND hWnd;
	WPARAM wFlagsParam;
	struct Sci_CharacterRange selrange;
	struct Sci_TextToFind findtextex;
#ifdef UNICODE
	char szAnsiFindStr[FIND_REPLACE_TEXT_BUFFER];
#endif
	TCHAR szInfoMsg[sizeof("Cannot find \"\"")-1+FIND_REPLACE_TEXT_BUFFER];
	p_findreplace = &p_dialog_param->findreplace;
	hWnd = p_findreplace->hwndOwner;
	wFlagsParam = p_findreplace->Flags & (FR_DOWN | FR_WHOLEWORD | FR_MATCHCASE);
	wFlagsParam |= wFlagsSet;
	wFlagsParam &= ~wFlagsRemove;
	selrange.cpMin = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONSTART, 0, 0);
	selrange.cpMax = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONEND, 0, 0);
	if(wFlagsParam & FR_DOWN)
	{
		findtextex.chrg.cpMin = selrange.cpMax;
		findtextex.chrg.cpMax = -1;
	}
	else
	{
		findtextex.chrg.cpMin = selrange.cpMin - 1;
		findtextex.chrg.cpMax = 0;
	}
#ifdef UNICODE
	WideCharToMultiByte(CP_UTF8, 0, p_findreplace->lpstrFindWhat, -1, szAnsiFindStr, FIND_REPLACE_TEXT_BUFFER, NULL, NULL);
	findtextex.lpstrText = szAnsiFindStr;
#else // !UNICODE
	findtextex.lpstrText = p_findreplace->lpstrFindWhat;
#endif // UNICODE
	if(findtextex.chrg.cpMin >= 0 && 
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_FINDTEXT, wFlagsParam, (LPARAM)&findtextex) != -1)
	{
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SETSEL, findtextex.chrgText.cpMin, findtextex.chrgText.cpMax);
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SCROLLCARET, 0, 0);
	}
	else
	{
		wsprintf(szInfoMsg, _T("Cannot find \"%s\""), p_findreplace->lpstrFindWhat);
		AsmDlgMessageBox(hWnd, szInfoMsg, _T("Find"), MB_ICONASTERISK);
	}
}
static void DoReplace(ASM_DIALOG_PARAM *p_dialog_param)
{
	FINDREPLACE *p_findreplace;
	HWND hWnd;
	WPARAM wFlagsParam;
	struct Sci_CharacterRange selrange;
	struct Sci_TextToFind findtextex;
#ifdef UNICODE
	char szAnsiFindStr[FIND_REPLACE_TEXT_BUFFER];
#endif
	p_findreplace = &p_dialog_param->findreplace;
	hWnd = p_findreplace->hwndOwner;
	wFlagsParam = p_findreplace->Flags & (FR_DOWN | FR_WHOLEWORD | FR_MATCHCASE);
	selrange.cpMin = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONSTART, 0, 0);
	selrange.cpMax = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONEND, 0, 0);
	findtextex.chrg.cpMin = selrange.cpMin;
	findtextex.chrg.cpMax = selrange.cpMin;
#ifdef UNICODE
	WideCharToMultiByte(CP_UTF8, 0, p_findreplace->lpstrFindWhat, -1, szAnsiFindStr, FIND_REPLACE_TEXT_BUFFER, NULL, NULL);
	findtextex.lpstrText = szAnsiFindStr;
#else // !UNICODE
	findtextex.lpstrText = p_findreplace->lpstrFindWhat;
#endif // UNICODE
	if(SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_FINDTEXT, wFlagsParam, (LPARAM)&findtextex) != -1)
	{
		if(findtextex.chrgText.cpMin == selrange.cpMin && findtextex.chrgText.cpMax == selrange.cpMax)
		{
			// Convert replacement string to multi-byte for Scintilla SCI_REPLACESEL
			char szAnsiReplStr[FIND_REPLACE_TEXT_BUFFER];
#ifdef UNICODE
			WideCharToMultiByte(CP_UTF8, 0, p_findreplace->lpstrReplaceWith, -1, szAnsiReplStr, FIND_REPLACE_TEXT_BUFFER, NULL, NULL);
#else
			lstrcpynA(szAnsiReplStr, p_findreplace->lpstrReplaceWith, FIND_REPLACE_TEXT_BUFFER);
#endif
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_REPLACESEL, 0, (LPARAM)szAnsiReplStr);
		}
	}
	DoFind(p_dialog_param);
}
static void DoReplaceAll(ASM_DIALOG_PARAM *p_dialog_param)
{
	FINDREPLACE *p_findreplace;
	HWND hWnd;
	WPARAM wFlagsParam;
	struct Sci_TextToFind findtextex;
#ifdef UNICODE
	char szAnsiFindStr[FIND_REPLACE_TEXT_BUFFER];
#endif
	struct Sci_CharacterRange selrange;
	UINT uReplacedCount;
	TCHAR szInfoMsg[sizeof("4294967295 occurrences were replaced")];
	p_findreplace = &p_dialog_param->findreplace;
	hWnd = p_findreplace->hwndOwner;
	wFlagsParam = p_findreplace->Flags & (FR_DOWN | FR_WHOLEWORD | FR_MATCHCASE);
	findtextex.chrg.cpMin = 0;
	findtextex.chrg.cpMax = -1;
#ifdef UNICODE
	WideCharToMultiByte(CP_UTF8, 0, p_findreplace->lpstrFindWhat, -1, szAnsiFindStr, FIND_REPLACE_TEXT_BUFFER, NULL, NULL);
	findtextex.lpstrText = szAnsiFindStr;
#else // !UNICODE
	findtextex.lpstrText = p_findreplace->lpstrFindWhat;
#endif // UNICODE
	uReplacedCount = 0;
	if(SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_FINDTEXT, wFlagsParam, (LPARAM)&findtextex) != -1)
	{
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_SETREDRAW, FALSE, 0);
		char szAnsiReplStr[FIND_REPLACE_TEXT_BUFFER];
#ifdef UNICODE
		WideCharToMultiByte(CP_UTF8, 0, p_findreplace->lpstrReplaceWith, -1, szAnsiReplStr, FIND_REPLACE_TEXT_BUFFER, NULL, NULL);
#else
		lstrcpynA(szAnsiReplStr, p_findreplace->lpstrReplaceWith, FIND_REPLACE_TEXT_BUFFER);
#endif
		do
		{
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SETSEL, findtextex.chrgText.cpMin, findtextex.chrgText.cpMax);
			SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_REPLACESEL, 0, (LPARAM)szAnsiReplStr);
			uReplacedCount++;
			selrange.cpMin = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONSTART, 0, 0);
			selrange.cpMax = (long)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETSELECTIONEND, 0, 0);
			findtextex.chrg.cpMin = selrange.cpMax;
		}
		while(SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_FINDTEXT, wFlagsParam, (LPARAM)&findtextex) != -1);
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(GetDlgItem(hWnd, IDC_ASSEMBLER), NULL, TRUE);
	}
	switch(uReplacedCount)
	{
	case 0:
		lstrcpy(szInfoMsg, _T("No occurrences were replaced"));
		break;
	case 1:
		lstrcpy(szInfoMsg, _T("1 occurrence was replaced"));
		break;
	default:
		wsprintf(szInfoMsg, _T("%u occurrences were replaced"), uReplacedCount);
		break;
	}
	AsmDlgMessageBox(hWnd, szInfoMsg, _T("Replace All"), MB_ICONINFORMATION);
}
static void OptionsChanged(HWND hWnd)
{
	ASM_DIALOG_PARAM *p_dialog_param = (ASM_DIALOG_PARAM *)GetWindowLongPtr(hWnd, DWLP_USER);
	if (!p_dialog_param)
		return;
	// Clean GDI fonts in RAFONT
	if(p_dialog_param->raFont.hFont)
	{
		DeleteObject(p_dialog_param->raFont.hFont);
		p_dialog_param->raFont.hFont = NULL;
	}
	if(p_dialog_param->raFont.hIFont)
	{
		DeleteObject(p_dialog_param->raFont.hIFont);
		p_dialog_param->raFont.hIFont = NULL;
	}
	if(p_dialog_param->raFont.hLnrFont)
	{
		DeleteObject(p_dialog_param->raFont.hLnrFont);
		p_dialog_param->raFont.hLnrFont = NULL;
	}
	// Reapply styling and fonts cleanly
	SetScintillaDesign(hWnd, hDllInst, &p_dialog_param->raFont);
	SendMessage(GetDlgItem(hWnd, IDC_ASSEMBLER), SCI_COLOURISE, 0, -1);
}
static BOOL LoadCode(HWND hWnd, DWORD_PTR dwAddress, DWORD_PTR dwSize)
{
	TCHAR szLabelPrefix[32];
	TCHAR szError[1024+1];
	TCHAR *lpText;
	if(!GetTabName(GetDlgItem(hWnd, IDC_TABS), szLabelPrefix, 32))
		*szLabelPrefix = _T('\0');
	SuspendAllThreads();
	lpText = ReadAsm(dwAddress, dwSize, szLabelPrefix, szError);
	ResumeAllThreads();
	if(!lpText)
	{
		AsmDlgMessageBox(hWnd, szError, NULL, MB_ICONHAND);
		return FALSE;
	}
	// Wide char to Multibyte UTF-8/Ansi mapping for Scintilla WM_SETTEXT
#ifdef UNICODE
	char *lpTextAnsi;
	int textLen = lstrlen(lpText);
	int ansiLen = WideCharToMultiByte(CP_UTF8, 0, lpText, textLen, NULL, 0, NULL, NULL);
	lpTextAnsi = (char *)HeapAlloc(GetProcessHeap(), 0, ansiLen + 1);
	if (lpTextAnsi)
	{
		WideCharToMultiByte(CP_UTF8, 0, lpText, textLen, lpTextAnsi, ansiLen, NULL, NULL);
		lpTextAnsi[ansiLen] = '\0';
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SETTEXT, 0, (LPARAM)lpTextAnsi);
		HeapFree(GetProcessHeap(), 0, lpTextAnsi);
	}
#else // !UNICODE
#ifdef TARGET_ODBG
	// OllyDbg API returns ANSI (system codepage) strings. Convert to UTF-8 for Scintilla.
	{
		int textLen = lstrlenA(lpText);
		int wideLen = MultiByteToWideChar(CP_ACP, 0, lpText, textLen, NULL, 0);
		if (wideLen > 0)
		{
			wchar_t *lpWide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (wideLen + 1) * sizeof(wchar_t));
			if (lpWide)
			{
				MultiByteToWideChar(CP_ACP, 0, lpText, textLen, lpWide, wideLen);
				lpWide[wideLen] = L'\0';
				int utf8Len = WideCharToMultiByte(CP_UTF8, 0, lpWide, wideLen, NULL, 0, NULL, NULL);
				char *lpUtf8 = (char *)HeapAlloc(GetProcessHeap(), 0, utf8Len + 1);
				if (lpUtf8)
				{
					WideCharToMultiByte(CP_UTF8, 0, lpWide, wideLen, lpUtf8, utf8Len, NULL, NULL);
					lpUtf8[utf8Len] = '\0';
					SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SETTEXT, 0, (LPARAM)lpUtf8);
					HeapFree(GetProcessHeap(), 0, lpUtf8);
				}
				HeapFree(GetProcessHeap(), 0, lpWide);
			}
		}
	}
#else
	// x64dbg API already returns UTF-8 strings. Pass directly to Scintilla.
	SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SETTEXT, 0, (LPARAM)lpText);
#endif
#endif // UNICODE
	
	HeapFree(GetProcessHeap(), 0, lpText);
	// Trigger dynamic highlights
	RefreshAllWordHighlights(GetDlgItem(hWnd, IDC_ASSEMBLER));
	return TRUE;
}
static BOOL PatchCode(HWND hWnd)
{
	TCHAR szError[1024+1];
	TCHAR *lpText;
	int nTextSize;
	LONG_PTR result;
	if(!IsProcessLoaded())
	{
		AsmDlgMessageBox(hWnd, _T("No process is loaded"), NULL, MB_ICONASTERISK);
		return FALSE;
	}
	nTextSize = (int)SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETLENGTH, 0, 0);
	char *lpTextAnsi = (char *)HeapAlloc(GetProcessHeap(), 0, nTextSize + 1);
	if(!lpTextAnsi)
	{
		AsmDlgMessageBox(hWnd, _T("Allocation failed"), NULL, MB_ICONHAND);
		return FALSE;
	}
	SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_GETTEXT, nTextSize + 1, (LPARAM)lpTextAnsi);
#ifdef UNICODE
	int wideLen = MultiByteToWideChar(CP_UTF8, 0, lpTextAnsi, nTextSize, NULL, 0);
	lpText = (TCHAR *)HeapAlloc(GetProcessHeap(), 0, (wideLen + 1) * sizeof(TCHAR));
	if (lpText)
	{
		MultiByteToWideChar(CP_UTF8, 0, lpTextAnsi, nTextSize, lpText, wideLen);
		lpText[wideLen] = _T('\0');
	}
	HeapFree(GetProcessHeap(), 0, lpTextAnsi);
#else // !UNICODE
#ifdef TARGET_ODBG
	// Scintilla holds UTF-8. Convert back to ANSI (system codepage) for OllyDbg WriteAsm.
	if (nTextSize == 0)
	{
		lpText = (char *)HeapAlloc(GetProcessHeap(), 0, 1);
		if (lpText) lpText[0] = '\0';
		HeapFree(GetProcessHeap(), 0, lpTextAnsi);
	}
	else
	{
		int wideLen = MultiByteToWideChar(CP_UTF8, 0, lpTextAnsi, nTextSize, NULL, 0);
		if (wideLen > 0)
		{
			wchar_t *lpWide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (wideLen + 1) * sizeof(wchar_t));
			if (lpWide)
			{
				MultiByteToWideChar(CP_UTF8, 0, lpTextAnsi, nTextSize, lpWide, wideLen);
				lpWide[wideLen] = L'\0';
				int ansiLen = WideCharToMultiByte(CP_ACP, 0, lpWide, wideLen, NULL, 0, NULL, NULL);
				lpText = (char *)HeapAlloc(GetProcessHeap(), 0, ansiLen + 1);
				if (lpText)
				{
					WideCharToMultiByte(CP_ACP, 0, lpWide, wideLen, lpText, ansiLen, NULL, NULL);
					lpText[ansiLen] = '\0';
				}
				HeapFree(GetProcessHeap(), 0, lpWide);
			}
			else
				lpText = NULL;
		}
		else
			lpText = NULL;
		HeapFree(GetProcessHeap(), 0, lpTextAnsi);
	}
#else
	// x64dbg: Scintilla UTF-8 text is already compatible with WriteAsm.
	lpText = lpTextAnsi;
#endif
#endif // UNICODE
	if (!lpText)
	{
		AsmDlgMessageBox(hWnd, _T("Allocation failed"), NULL, MB_ICONHAND);
		return FALSE;
	}
	SuspendAllThreads();
	result = WriteAsm(lpText, szError);
	ResumeAllThreads();
	RefreshAllWordHighlights(GetDlgItem(hWnd, IDC_ASSEMBLER));
	HeapFree(GetProcessHeap(), 0, lpText);
	if(result <= 0)
	{
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SETSEL, -result, -result);
		SendDlgItemMessage(hWnd, IDC_ASSEMBLER, SCI_SCROLLCARET, 0, 0);
		AsmDlgMessageBox(hWnd, szError, NULL, MB_ICONHAND);
		SetFocus(GetDlgItem(hWnd, IDC_ASSEMBLER));
		return FALSE;
	}
	ShowStatus(_T("Assembly Succeeded!"));
	return TRUE;
}
static LRESULT CALLBACK ScintillaSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static BOOL s_bInterceptRButton = FALSE;
	if (uMsg == WM_RBUTTONDOWN)
	{
		s_bInterceptRButton = FALSE;
		int anchor = (int)SendMessage(hWnd, SCI_GETANCHOR, 0, 0);
		int currentPos = (int)SendMessage(hWnd, SCI_GETCURRENTPOS, 0, 0);

		if (anchor != currentPos)
		{
			s_bInterceptRButton = TRUE;
			s_backupSelStart = (anchor < currentPos) ? anchor : currentPos;
			s_backupSelEnd = (anchor < currentPos) ? currentPos : anchor;
			return 0; // Prevent Scintilla from clearing selection
		}
		else
		{
			s_backupSelStart = -1;
			s_backupSelEnd = -1;
		}
	}
	else if (uMsg == WM_LBUTTONDOWN)
	{
		s_backupSelStart = -1;
		s_backupSelEnd = -1;
	}
	else if (uMsg == WM_RBUTTONUP)
	{
		if (s_bInterceptRButton)
		{
			s_bInterceptRButton = FALSE;
			POINT pt;
			GetCursorPos(&pt);
			LPARAM lCtxParam = MAKELPARAM(pt.x, pt.y);
			PostMessage(hWnd, WM_CONTEXTMENU, (WPARAM)hWnd, lCtxParam);
			return 0;
		}
	}
	if (uMsg == WM_CONTEXTMENU)
	{
		SendMessage(GetParent(hWnd), WM_CONTEXTMENU, (WPARAM)hWnd, lParam);
		return 0;
	}
	if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN)
	{
		BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		BOOL alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
		WPARAM key = wParam;
		// 1. Comment / Uncomment block (Ctrl + : / Alt + ;)
		if (key == VK_OEM_1) // Semicolon/Colon key
		{
			if (ctrl && !shift && !alt)
			{
				SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40022, 0), 0); // ID_ACCEL_COMMENT_LINE
				return 0;
			}
			if (ctrl && shift && !alt)
			{
				SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40023, 0), 0); // ID_ACCEL_UNCOMMENT_LINE
				return 0;
			}
		}
		// 2. Find / Replace / Find Next / Find Prev
		if (key == 'F' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40016, 0), 0); // ID_ACCEL_FINDWND
			return 0;
		}
		if (key == 'H' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40017, 0), 0); // ID_ACCEL_REPLACEWND
			return 0;
		}
		if (key == VK_F3 && !ctrl && !alt)
		{
			if (shift)
				SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40019, 0), 0); // ID_ACCEL_FINDPREV
			else
				SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40018, 0), 0); // ID_ACCEL_FINDNEXT
			return 0;
		}
		// 3. Tab operations
		if (key == 'N' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40008, 0), 0); // ID_TABMENU_NEWTAB
			return 0;
		}
		if (key == 'T' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40008, 0), 0); // ID_TABMENU_NEWTAB
			return 0;
		}
		if (key == 'W' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40010, 0), 0); // ID_TABMENU_CLOSE
			return 0;
		}
		if (key == VK_F4 && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40010, 0), 0); // ID_TABMENU_CLOSE
			return 0;
		}
		if (key == 'R' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40009, 0), 0); // ID_TABMENU_RENAME
			return 0;
		}
		if (key == VK_F2 && !ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40009, 0), 0); // ID_TABMENU_RENAME
			return 0;
		}
		if (key == 'W' && ctrl && shift && alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40013, 0), 0); // ID_TABSTRIPMENU_CLOSEALLTABS
			return 0;
		}
		// 4. File Load / Save
		if (key == 'O' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40011, 0), 0); // ID_TABMENU_LOADFROMFILE
			return 0;
		}
		if (key == 'S' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40012, 0), 0); // ID_TABMENU_SAVETOFILE
			return 0;
		}
		// 5. Next Tab / Prev Tab
		if (key == VK_NEXT && ctrl && !shift && !alt) // Page Down
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40015, 0), 0); // ID_ACCEL_NEXTTAB
			return 0;
		}
		if (key == VK_PRIOR && ctrl && !shift && !alt) // Page Up
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40014, 0), 0); // ID_ACCEL_PREVTAB
			return 0;
		}
		if (key == VK_TAB && ctrl && !alt)
		{
			if (shift)
				SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40014, 0), 0); // ID_ACCEL_PREVTAB
			else
				SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40015, 0), 0); // ID_ACCEL_NEXTTAB
			return 0;
		}
		// 6. Focus OllyDbg
		if (key == 'D' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40020, 0), 0); // ID_ACCEL_FOCUS_OLLYDBG
			return 0;
		}
		// 7. Block Mode
		if (key == 'B' && ctrl && !shift && !alt)
		{
			SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(40021, 0), 0); // ID_ACCEL_BLOCK_MODE
			return 0;
		}
	}
	else if (uMsg == WM_CHAR)
	{
		BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		BOOL alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
		if (ctrl || alt)
		{
			if (wParam == ';' || wParam == ':' || wParam == 27 || wParam == 10 || wParam == 13)
			{
				return 0; // Consume char to avoid typing issues on shortcut trigger
			}
		}
	}
	return CallWindowProc(wpOrigScintillaProc, hWnd, uMsg, wParam, lParam);
}
