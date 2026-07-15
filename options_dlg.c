#include "stdafx.h"
#include "options_dlg.h"
#include "plugin.h"
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

extern HINSTANCE hDllInst;
extern OPTIONS options;
extern BOOL MyWriteColortoini(HINSTANCE dllinst, TCHAR *key, int val);

static LRESULT CALLBACK DlgOptionsProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void OptionsToDlg(HWND hWnd);
static void OptionsFromDlg(HWND hWnd);
static void OptionsToIni(HINSTANCE hInst);

// Temporary option copies to revert on Cancel
static OPTIONS temp_options;

#define WriteDebugLog(...) ((void)0)

__declspec(dllexport) void ShowOptionsDlg(HWND hParentWnd, HINSTANCE hInstance)
{
	WriteDebugLog(_T("--- ShowOptionsDlg Start ---"));
	WriteDebugLog(_T("hParentWnd: %p, hInstance: %p, hDllInst: %p"), (void*)hParentWnd, (void*)hInstance, (void*)hDllInst);

	// Try DialogBox
	INT_PTR ret = DialogBox(hInstance, MAKEINTRESOURCE(IDD_OPTIONS), hParentWnd, (DLGPROC)DlgOptionsProc);
	
	WriteDebugLog(_T("DialogBox returned: %Id"), (INT_PTR)ret);

	if (ret == -1)
	{
		DWORD err = GetLastError();
		WriteDebugLog(_T("DialogBox Failed! GetLastError: %lu"), err);

		// Fallback: If hInstance failed, try hDllInst
		if (hInstance != hDllInst)
		{
			WriteDebugLog(_T("Attempting fallback with hDllInst: %p"), (void*)hDllInst);
			ret = DialogBox(hDllInst, MAKEINTRESOURCE(IDD_OPTIONS), hParentWnd, (DLGPROC)DlgOptionsProc);
			WriteDebugLog(_T("Fallback DialogBox returned: %Id"), (INT_PTR)ret);
			if (ret == -1)
			{
				WriteDebugLog(_T("Fallback also failed! GetLastError: %lu"), GetLastError());
			}
		}
	}
	WriteDebugLog(_T("--- ShowOptionsDlg End ---"));
}

static void UpdateFontInfo(HWND hWnd, const OPTIONS *opt)
{
	TCHAR szFontInfo[128];
	wsprintf(szFontInfo, _T("%s, %dpt%s%s"), 
		opt->font_name, 
		opt->font_size, 
		opt->font_bold ? _T(" (Bold)") : _T(""),
		opt->font_italic ? _T(" (Italic)") : _T(""));
	SetDlgItemText(hWnd, IDC_OPT_FONT_INFO, szFontInfo);
}

static BOOL ChooseFont_Dlg(HWND hWnd, OPTIONS *opt)
{
	CHOOSEFONT cf;
	LOGFONT lf;
	HDC hDC;

	ZeroMemory(&cf, sizeof(CHOOSEFONT));
	ZeroMemory(&lf, sizeof(LOGFONT));

	lstrcpyn(lf.lfFaceName, opt->font_name, LF_FACESIZE);
	hDC = GetDC(hWnd);
	lf.lfHeight = -MulDiv(opt->font_size, GetDeviceCaps(hDC, LOGPIXELSY), 72);
	ReleaseDC(hWnd, hDC);

	lf.lfWeight = opt->font_bold ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = (BYTE)opt->font_italic;
	lf.lfCharSet = opt->font_charset;

	cf.lStructSize = sizeof(CHOOSEFONT);
	cf.hwndOwner = hWnd;
	cf.lpLogFont = &lf;
	cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_FORCEFONTEXIST;

	if (ChooseFont(&cf))
	{
		lstrcpyn(opt->font_name, lf.lfFaceName, LF_FACESIZE);
		opt->font_size = cf.iPointSize / 10;
		opt->font_bold = (lf.lfWeight >= FW_BOLD);
		opt->font_italic = (BOOL)lf.lfItalic;
		opt->font_charset = lf.lfCharSet;
		return TRUE;
	}
	return FALSE;
}

static BOOL ChooseColor_Dlg(HWND hWnd, COLORREF *pColor)
{
	CHOOSECOLOR cc;
	static COLORREF acrCustColors[16] = {
		RGB(255, 255, 255), RGB(240, 240, 240), RGB(220, 220, 220), RGB(200, 200, 200),
		RGB(0, 0, 255),     RGB(0, 128, 128),   RGB(0, 128, 0),     RGB(255, 128, 64),
		RGB(0, 0, 0),       RGB(128, 128, 128), RGB(255, 0, 0),     RGB(0, 255, 0),
		RGB(255, 255, 0),   RGB(255, 0, 255),   RGB(0, 255, 255),   RGB(128, 0, 128)
	};

	ZeroMemory(&cc, sizeof(CHOOSECOLOR));
	cc.lStructSize = sizeof(CHOOSECOLOR);
	cc.hwndOwner = hWnd;
	cc.lpCustColors = (LPDWORD)acrCustColors;
	cc.rgbResult = *pColor;
	cc.Flags = CC_FULLOPEN | CC_RGBINIT;

	if (ChooseColor(&cc))
	{
		*pColor = cc.rgbResult;
		return TRUE;
	}
	return FALSE;
}

static LRESULT CALLBACK DlgOptionsProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_INITDIALOG:
		WriteDebugLog(_T("DlgOptionsProc WM_INITDIALOG Entered"));

		SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_ADDSTRING, 0, (LPARAM)_T("(disassembler default)"));
		SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_ADDSTRING, 0, (LPARAM)_T("FFFE"));
		SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_ADDSTRING, 0, (LPARAM)_T("0FFFE"));
		SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_ADDSTRING, 0, (LPARAM)_T("0FFFEh"));
		SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_ADDSTRING, 0, (LPARAM)_T("0xFFFE"));

		SendDlgItemMessage(hWnd, IDC_DISASM_LABELGEN, CB_ADDSTRING, 0, (LPARAM)_T("L[counter]"));
		SendDlgItemMessage(hWnd, IDC_DISASM_LABELGEN, CB_ADDSTRING, 0, (LPARAM)_T("L_[address]"));
		SendDlgItemMessage(hWnd, IDC_DISASM_LABELGEN, CB_ADDSTRING, 0, (LPARAM)_T("L_[tab_name]_[counter]"));

		SendDlgItemMessage(hWnd, IDC_EDIT_TABWIDTH, CB_ADDSTRING, 0, (LPARAM)_T("2"));
		SendDlgItemMessage(hWnd, IDC_EDIT_TABWIDTH, CB_ADDSTRING, 0, (LPARAM)_T("4"));
		SendDlgItemMessage(hWnd, IDC_EDIT_TABWIDTH, CB_ADDSTRING, 0, (LPARAM)_T("8"));

		// Keep a backup of original options
		CopyMemory(&temp_options, &options, sizeof(OPTIONS));

		OptionsToDlg(hWnd);
		return TRUE; // Set default focus

	case WM_LBUTTONDOWN:
		SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDC_DISASM_RVA:
			EnableWindow(GetDlgItem(hWnd, IDC_DISASM_RVA_RELOCONLY), IsDlgButtonChecked(hWnd, IDC_DISASM_RVA));
			break;

		case IDC_DISASM_LABEL:
			EnableWindow(GetDlgItem(hWnd, IDC_DISASM_EXTJMP), IsDlgButtonChecked(hWnd, IDC_DISASM_LABEL));
			break;

		case IDC_OPT_BG_USE:
			EnableWindow(GetDlgItem(hWnd, IDC_OPT_BG_BTN), IsDlgButtonChecked(hWnd, IDC_OPT_BG_USE));
			break;

		case IDC_OPT_FONT_BTN:
			if (ChooseFont_Dlg(hWnd, &temp_options))
			{
				UpdateFontInfo(hWnd, &temp_options);
			}
			break;

		case IDC_OPT_BG_BTN:
			ChooseColor_Dlg(hWnd, &temp_options.bg_color);
			break;

		case IDC_OPT_CLR_CMD:
			ChooseColor_Dlg(hWnd, &temp_options.color_cmd);
			break;

		case IDC_OPT_CLR_CMD_BG:
			ChooseColor_Dlg(hWnd, &temp_options.color_cmd_bg);
			break;

		case IDC_OPT_CLR_REG:
			ChooseColor_Dlg(hWnd, &temp_options.color_reg);
			break;

		case IDC_OPT_CLR_CMNT:
			ChooseColor_Dlg(hWnd, &temp_options.color_cmnt);
			break;

		case IDC_OPT_CLR_NUM:
			ChooseColor_Dlg(hWnd, &temp_options.color_num);
			break;

		case IDOK:
			// Copy choices back to main options
			OptionsFromDlg(hWnd);
			CopyMemory(&options, &temp_options, sizeof(OPTIONS));
			OptionsToIni(hDllInst);
			
			// Notify other windows that options changed
			extern void AssemblerOptionsChanged();
			AssemblerOptionsChanged();

			EndDialog(hWnd, 1);
			break;

		case IDCANCEL:
			EndDialog(hWnd, 0);
			break;
		}
		break;
	}

	return FALSE;
}

static void OptionsToDlg(HWND hWnd)
{
	if(temp_options.disasm_rva)
		CheckDlgButton(hWnd, IDC_DISASM_RVA, BST_CHECKED);
	else
		EnableWindow(GetDlgItem(hWnd, IDC_DISASM_RVA_RELOCONLY), FALSE);

	if(temp_options.disasm_rva_reloconly)
		CheckDlgButton(hWnd, IDC_DISASM_RVA_RELOCONLY, BST_CHECKED);

	if(temp_options.disasm_label)
		CheckDlgButton(hWnd, IDC_DISASM_LABEL, BST_CHECKED);
	else
		EnableWindow(GetDlgItem(hWnd, IDC_DISASM_EXTJMP), FALSE);

	if(temp_options.disasm_extjmp)
		CheckDlgButton(hWnd, IDC_DISASM_EXTJMP, BST_CHECKED);

	SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_SETCURSEL, temp_options.disasm_hex, 0);

	SendDlgItemMessage(hWnd, IDC_DISASM_LABELGEN, CB_SETCURSEL, temp_options.disasm_labelgen, 0);

	if(temp_options.asm_comments)
		CheckDlgButton(hWnd, IDC_ASM_COMMENTS, BST_CHECKED);

	if(temp_options.asm_labels)
		CheckDlgButton(hWnd, IDC_ASM_LABELS, BST_CHECKED);

	if(temp_options.edit_savepos)
		CheckDlgButton(hWnd, IDC_EDIT_SAVEPOS, BST_CHECKED);

	SendDlgItemMessage(hWnd, IDC_EDIT_TABWIDTH, CB_SETCURSEL, temp_options.edit_tabwidth, 0);

	// Custom Styling
	UpdateFontInfo(hWnd, &temp_options);

	if(temp_options.bg_use_custom)
		CheckDlgButton(hWnd, IDC_OPT_BG_USE, BST_CHECKED);
	else
		EnableWindow(GetDlgItem(hWnd, IDC_OPT_BG_BTN), FALSE);
}

static void OptionsFromDlg(HWND hWnd)
{
	temp_options.disasm_rva = IsDlgButtonChecked(hWnd, IDC_DISASM_RVA) == BST_CHECKED;
	temp_options.disasm_rva_reloconly = IsDlgButtonChecked(hWnd, IDC_DISASM_RVA_RELOCONLY) == BST_CHECKED;
	temp_options.disasm_label = IsDlgButtonChecked(hWnd, IDC_DISASM_LABEL) == BST_CHECKED;
	temp_options.disasm_extjmp = IsDlgButtonChecked(hWnd, IDC_DISASM_EXTJMP) == BST_CHECKED;
	temp_options.disasm_hex = (int)SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_GETCURSEL, 0, 0);
	temp_options.disasm_labelgen = (int)SendDlgItemMessage(hWnd, IDC_DISASM_LABELGEN, CB_GETCURSEL, 0, 0);
	temp_options.asm_comments = IsDlgButtonChecked(hWnd, IDC_ASM_COMMENTS) == BST_CHECKED;
	temp_options.asm_labels = IsDlgButtonChecked(hWnd, IDC_ASM_LABELS) == BST_CHECKED;
	temp_options.edit_savepos = IsDlgButtonChecked(hWnd, IDC_EDIT_SAVEPOS) == BST_CHECKED;
	temp_options.edit_tabwidth = (int)SendDlgItemMessage(hWnd, IDC_EDIT_TABWIDTH, CB_GETCURSEL, 0, 0);

	temp_options.bg_use_custom = IsDlgButtonChecked(hWnd, IDC_OPT_BG_USE) == BST_CHECKED;
}

static void OptionsToIni(HINSTANCE hInst)
{
	MyWriteinttoini(hInst, _T("disasm_rva"), options.disasm_rva);
	MyWriteinttoini(hInst, _T("disasm_rva_reloconly"), options.disasm_rva_reloconly);
	MyWriteinttoini(hInst, _T("disasm_label"), options.disasm_label);
	MyWriteinttoini(hInst, _T("disasm_extjmp"), options.disasm_extjmp);
	MyWriteinttoini(hInst, _T("disasm_hex"), options.disasm_hex);
	MyWriteinttoini(hInst, _T("disasm_labelgen"), options.disasm_labelgen);
	MyWriteinttoini(hInst, _T("asm_comments"), options.asm_comments);
	MyWriteinttoini(hInst, _T("asm_labels"), options.asm_labels);
	MyWriteinttoini(hInst, _T("edit_savepos"), options.edit_savepos);
	MyWriteinttoini(hInst, _T("edit_tabwidth"), options.edit_tabwidth);

	// Styling options
	MyWritestringtoini(hInst, _T("font_name"), options.font_name);
	MyWriteinttoini(hInst, _T("font_size"), options.font_size);
	MyWriteinttoini(hInst, _T("font_bold"), options.font_bold);
	MyWriteinttoini(hInst, _T("font_italic"), options.font_italic);
	MyWriteinttoini(hInst, _T("font_charset"), (int)options.font_charset);

	MyWriteinttoini(hInst, _T("bg_use_custom"), options.bg_use_custom);
	MyWriteColortoini(hInst, _T("bg_color"), (int)options.bg_color);

	MyWriteColortoini(hInst, _T("color_cmd"), (int)options.color_cmd);
	MyWriteColortoini(hInst, _T("color_cmd_bg"), (int)options.color_cmd_bg);
	MyWriteColortoini(hInst, _T("color_reg"), (int)options.color_reg);
	MyWriteColortoini(hInst, _T("color_cmnt"), (int)options.color_cmnt);
	MyWriteColortoini(hInst, _T("color_num"), (int)options.color_num);

	// Alternative Line Highlighting colors
	MyWriteColortoini(hInst, _T("color_jmp_line_bg"), (int)options.color_jmp_line_bg);
	MyWriteColortoini(hInst, _T("color_call_line_bg"), (int)options.color_call_line_bg);
}
