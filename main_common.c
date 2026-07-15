#include "stdafx.h"
#include "main_common.h"
#include "plugin.h"
#include "assembler_dlg.h"
#include "resource.h"

// DLL DelayLoad 를 사용할 때 IgnoreAllDefaultLibraries 이 활성화되어 있으면,
// delayimp.lib 에서 __load_config_used 심볼을 찾으려고 시도하다 링킹 에러가 발생합니다.
// 이를 방지하기 위해 더미 _load_config_used 구조체를 정의해 줍니다.
#include <windows.h>
#ifdef _WIN64
const IMAGE_LOAD_CONFIG_DIRECTORY64 _load_config_used = {
	sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64)
};
#else
const IMAGE_LOAD_CONFIG_DIRECTORY32 _load_config_used = {
	sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32)
};
#endif

HINSTANCE hDllInst;
OPTIONS options;
static HMODULE hScintillaDLL = NULL;

#define WriteDebugLog(...) ((void)0)
extern BOOL MyGetColorfromini(HINSTANCE dllinst, TCHAR *key, int *p_val, int def);

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch(fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hinstDLL);
		hDllInst = hinstDLL;
		break;
 
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}


static void EnsureIniFileExists(HINSTANCE hInst)
{
	TCHAR szIniPath[MAX_PATH];
	GetModuleFileName(hInst, szIniPath, MAX_PATH);
	TCHAR *p = _tcsrchr(szIniPath, _T('\\'));
	if (p) {
		_tcscpy(p + 1, _T("multiasm.ini"));
	} else {
		_tcscpy(szIniPath, _T("multiasm.ini"));
	}

	DWORD dwAttrib = GetFileAttributes(szIniPath);
	if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
	{
		// INI file does not exist, let's create and write default contents!
		HANDLE hFile = CreateFile(szIniPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			const char *defaultIniData = 
				"[Multiline Ultimate Assembler]\r\n"
				"; =========================================================================\r\n"
				"; Multiline Ultimate Assembler - x64dbg & OllyDbg Syntax Highlighting\r\n"
				"; =========================================================================\r\n"
				"; 이 INI 파일은 어셈블러 에디터의 구문 강조(Syntax Highlighting) 색상을 설정합니다.\r\n"
				";\r\n"
				"; [16진수 RGB 색상 값 설정 가이드]\r\n"
				"; - 직관적인 순방향 16진수 RGB 형식(0xRRGGBB)으로 작성해야 합니다.\r\n"
				"; - 예시:\r\n"
				";   * 빨간색 (Red)   : RGB(255, 0, 0)     => 0xFF0000 (Red=FF, Green=00, Blue=00)\r\n"
				";   * 초록색 (Green) : RGB(0, 255, 0)     => 0x00FF00 (Red=00, Green=FF, Blue=00)\r\n"
				";   * 파란색 (Blue)  : RGB(0, 0, 255)     => 0x0000FF (Red=00, Green=00, Blue=FF)\r\n"
				";   * 노란색 (Yellow): RGB(255, 255, 0)   => 0xFFFF00 (Red=FF, Green=FF, Blue=00)\r\n"
				";   * 흰색 (White)   : RGB(255, 255, 255) => 0xFFFFFF\r\n"
				";   * 검은색 (Black) : RGB(0, 0, 0)       => 0x000000\r\n"
				"; - 텍스트를 굵게 표시하고 싶은 경우, 최상위 바이트에 01을 입력합니다 (예: 0x01RRGGBB).\r\n"
				"; =========================================================================\r\n"
				"; -------------------------------------------------------------------------\r\n"
				"; x64dbg 스크린샷 색상 계열 유지 + 밝은(아이보리) 배경 라이트 테마\r\n"
				";\r\n"
				"; [색상 선택 원칙]\r\n"
				";   스크린샷의 색상 계열(Hue)을 그대로 가져오되,\r\n"
				";   밝은 배경에서 가독성을 위해 채도/명도를 낮춰 어두운 톤으로 조정:\r\n"
				";     - call (시안 계열)   : 밝은 #00FFFF → 어두운 #007799\r\n"
				";     - jmp  (노랑 계열)   : 밝은 #FFFF00 → 어두운 #886600\r\n"
				";     - 숫자 (시안 계열)   : 밝은 #00FFFF → 어두운 #007799\r\n"
				";     - 니모닉 (연두 계열) : 밝은 #80C000 → 어두운 #446600\r\n"
				";     - 레지스터 (오렌지)  : 그대로     → #CC6000\r\n"
				";     - 세그먼트 레지스터  : 밝은 마젠타 → 어두운 #880088\r\n"
				";   call/jmp 키워드 배경 하이라이트는 스크린샷 그대로 형광 시안/노랑 유지\r\n"
				"; -------------------------------------------------------------------------\r\n"
				"\r\n"
				"; --- JMP (점프) ---\r\n"
				"color_jmp          = 0xFF0000   ; jmp/je/js 키워드 글자색 - 어두운 황금색 RGB(136, 102, 0)\r\n"
				"color_jmp_bg       = 0x00FFFF00   ; jmp 키워드 배경 - 형광 노란색 하이라이트 RGB(255, 255, 0)\r\n"
				"\r\n"
				"; --- CALL (함수 호출) ---\r\n"
				"color_call         = 0x00004D66   ; call 키워드 글자색 - 어두운 진청록색 RGB(0, 77, 102)\r\n"
				"color_call_bg      = 0x0000FFFF   ; call 키워드 배경 - 형광 시안 하이라이트 RGB(0, 255, 255)\r\n"
				"\r\n"
				"; --- RET (함수 반환) ---\r\n"
				"color_ret          = 0x00004D66   ; ret 키워드 글자색 - 어두운 진청록색 RGB(0, 77, 102)\r\n"
				"color_ret_bg       = 0x0000FFFF   ; ret 키워드 배경 - 형광 시안 하이라이트 RGB(0, 255, 255)\r\n"
				"\r\n"
				"; --- NOP / INT3 ---\r\n"
				"color_nop          = 0x00808080   ; int3 / NOP 글자색 - 중간 회색 RGB(128, 128, 128)\r\n"
				"color_nop_bg       = 0x00FFFBF0   ; NOP 배경색 - 기본 에디터 배경(아이보리)\r\n"
				"\r\n"
				"; --- 세그먼트 레지스터 (ss, ds, cs, es) ---\r\n"
				"color_reg_special  = 0x800000   ; 세그먼트 레지스터 글자색 - 어두운 마젠타 RGB(136, 0, 136)\r\n"
				"color_reg_special_bg = 0x00FFFBF0 ; 세그먼트 레지스터 배경색 - 기본 에디터 배경\r\n"
				"\r\n"
				"; --- FPU ---\r\n"
				"color_fpu          = 0x00884400   ; FPU 명령어 글자색 - 어두운 갈색-주황 RGB(136, 68, 0)\r\n"
				"color_fpu_bg       = 0x00FFFBF0   ; FPU 배경색 - 기본 에디터 배경\r\n"
				"\r\n"
				"; --- SSE / AVX 확장 명령어 ---\r\n"
				"color_ext          = 0x00880000   ; 확장 명령어 글자색 - 어두운 빨강 RGB(136, 0, 0)\r\n"
				"color_ext_bg       = 0x00FFFBF0   ; 확장 명령어 배경색 - 기본 에디터 배경\r\n"
				"\r\n"
				"; --- 데이터 타입 키워드 (dword, word, byte) ---\r\n"
				"color_type         = 0x00004499   ; 데이터 타입 키워드 글자색 - 어두운 파랑 RGB(0, 68, 153)\r\n"
				"color_type_bg      = 0x00FFFBF0   ; 데이터 타입 배경색 - 기본 에디터 배경\r\n"
				"\r\n"
				"; --- 보조 키워드 (ptr, near, far, short) ---\r\n"
				"color_other        = 0x00886622   ; 보조 키워드 글자색 - 어두운 황갈색 RGB(136, 102, 34)\r\n"
				"color_other_bg     = 0x00FFFBF0   ; 보조 키워드 배경색 - 기본 에디터 배경\r\n"
				"\r\n"
				"; --- 일반 GP 레지스터 배경 ---\r\n"
				"color_reg_bg       = 0x00FFFBF0   ; 일반 GP 레지스터 배경색 - 기본 에디터 배경\r\n"
				"\r\n"
				"; -------------------------------------------------------------------------\r\n"
				"; [에디터 창 위치 및 크기 유지 설정]\r\n"
				"; -------------------------------------------------------------------------\r\n"
				"pos_x              =1061\r\n"
				"pos_y              =71\r\n"
				"pos_w              =900\r\n"
				"pos_h              =1035\r\n"
				"tabs_path          = .\\multiasm\r\n"
				"disasm_rva         =1\r\n"
				"disasm_rva_reloconly =1\r\n"
				"disasm_label       =1\r\n"
				"disasm_extjmp      =1\r\n"
				"disasm_hex         =0\r\n"
				"disasm_labelgen    =0\r\n"
				"asm_comments       =1\r\n"
				"asm_labels         =1\r\n"
				"edit_savepos       =1\r\n"
				"edit_tabwidth      =1\r\n"
				"font_name          =D2Coding\r\n"
				"font_size          =12\r\n"
				"font_bold          =0\r\n"
				"font_italic        =0\r\n"
				"font_charset       =0\r\n"
				"bg_use_custom      =1\r\n"
				"bg_color           =0x00FFFBF0\r\n"
				"color_cmd          =255\r\n"
				"color_cmd_bg       =0x00FFFBF0\r\n"
				"color_reg          =0x00800000\r\n"
				"color_cmnt         =0x003A0000\r\n"
				"color_num          =32768\r\n"
				"color_jmp_line_bg  =0x00FFFFD0\r\n"
				"color_call_line_bg =0x00E0FFFF\r\n";
			
			DWORD dwWritten;
			WriteFile(hFile, defaultIniData, (DWORD)strlen(defaultIniData), &dwWritten, NULL);
			CloseHandle(hFile);
		}
	}
}

TCHAR *PluginInit(HINSTANCE hInst)
{
	EnsureIniFileExists(hInst);
	WriteDebugLog(_T("--- Multiline Ultimate Assembler PluginInit Start (Scintilla version) ---"));
	WriteDebugLog(_T("hInst (DLL instance handle): %p"), (void*)hInst);

	INITCOMMONCONTROLSEX icex;
	TCHAR *pError;

	// Ensure that the common control DLL is loaded.
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_TAB_CLASSES;
	InitCommonControlsEx(&icex);

	// For drag'n'drop support
	if(FAILED(OleInitialize(NULL)))
		return _T("OleInitialize() failed");

	// Load Scintilla.dll
	// 1. Try loading from the plugin's own directory first
	TCHAR szDllPath[MAX_PATH];
	szDllPath[0] = _T('\0');
	DWORD dwLen = GetModuleFileName(hInst, szDllPath, MAX_PATH);
	if (dwLen > 0 && dwLen < MAX_PATH)
	{
		TCHAR *pSlash = _tcsrchr(szDllPath, _T('\\'));
		if (pSlash)
		{
			*(pSlash + 1) = _T('\0');
		}
		lstrcat(szDllPath, _T("Scintilla.dll"));
		WriteDebugLog(_T("Attempting to load Scintilla from plugin directory: %s"), szDllPath);
		hScintillaDLL = LoadLibrary(szDllPath);
	}

	// 2. Fallback to standard path if loading from the plugin directory failed
	if (!hScintillaDLL)
	{
		WriteDebugLog(_T("Failed to load Scintilla from plugin directory, trying standard path..."));
		hScintillaDLL = LoadLibrary(_T("Scintilla.dll"));
	}

	if (!hScintillaDLL)
	{
		WriteDebugLog(_T("Failed to load Scintilla.dll"));
		OleUninitialize();
		return _T("Failed to load Scintilla.dll");
	}

	// Init stuff
	pError = AssemblerInit();
	if(pError)
	{
		FreeLibrary(hScintillaDLL);
		hScintillaDLL = NULL;
		OleUninitialize();
		return pError;
	}

	// Load options
	MyGetintfromini(hInst, _T("disasm_rva"), &options.disasm_rva, 0, 0, 1);
	MyGetintfromini(hInst, _T("disasm_rva_reloconly"), &options.disasm_rva_reloconly, 0, 0, 1);
	MyGetintfromini(hInst, _T("disasm_label"), &options.disasm_label, 0, 0, 1);
	MyGetintfromini(hInst, _T("disasm_extjmp"), &options.disasm_extjmp, 0, 0, 1);
	MyGetintfromini(hInst, _T("disasm_hex"), &options.disasm_hex, 0, 4, 0);
	MyGetintfromini(hInst, _T("disasm_labelgen"), &options.disasm_labelgen, 0, 2, 0);
	MyGetintfromini(hInst, _T("asm_comments"), &options.asm_comments, 0, 0, 1);
	MyGetintfromini(hInst, _T("asm_labels"), &options.asm_labels, 0, 0, 1);
	MyGetintfromini(hInst, _T("edit_savepos"), &options.edit_savepos, 0, 0, 1);
	MyGetintfromini(hInst, _T("edit_tabwidth"), &options.edit_tabwidth, 0, 2, 1);

	// New appearance settings load
	if (!MyGetstringfromini(hInst, _T("font_name"), options.font_name, 32))
	{
		lstrcpy(options.font_name, _T("Lucida Console"));
	}
	MyGetintfromini(hInst, _T("font_size"), &options.font_size, 4, 72, 12);
	MyGetintfromini(hInst, _T("font_bold"), &options.font_bold, 0, 1, 0);
	MyGetintfromini(hInst, _T("font_italic"), &options.font_italic, 0, 1, 0);
	
	int font_charset_int;
	MyGetintfromini(hInst, _T("font_charset"), &font_charset_int, 0, 255, DEFAULT_CHARSET);
	options.font_charset = (BYTE)font_charset_int;

	MyGetintfromini(hInst, _T("bg_use_custom"), &options.bg_use_custom, 0, 1, 0);
	
	int bg_color_int;
	MyGetColorfromini(hInst, _T("bg_color"), &bg_color_int, RGB(255, 255, 255));
	options.bg_color = (COLORREF)bg_color_int;

	int val;
	MyGetColorfromini(hInst, _T("color_cmd"), &val, RGB(0, 0, 128));
	options.color_cmd = (COLORREF)val;

	MyGetColorfromini(hInst, _T("color_cmd_bg"), &val, CLR_INVALID);
	options.color_cmd_bg = (COLORREF)val;

	MyGetColorfromini(hInst, _T("color_reg"), &val, RGB(0, 128, 128));
	options.color_reg = (COLORREF)val;

	MyGetColorfromini(hInst, _T("color_cmnt"), &val, RGB(0, 128, 0));
	options.color_cmnt = (COLORREF)val;

	MyGetColorfromini(hInst, _T("color_num"), &val, RGB(0, 0, 255));
	options.color_num = (COLORREF)val;

	// Load Alternative Line Highlighting colors
	MyGetColorfromini(hInst, _T("color_jmp_line_bg"), &val, RGB(255, 230, 230));
	options.color_jmp_line_bg = (COLORREF)val;

	MyGetColorfromini(hInst, _T("color_call_line_bg"), &val, RGB(230, 255, 235));
	options.color_call_line_bg = (COLORREF)val;

	WriteDebugLog(_T("--- Multiline Ultimate Assembler PluginInit End (Success) ---"));
	return NULL;
}

void PluginExit()
{
	AssemblerExit();
	if (hScintillaDLL)
	{
		FreeLibrary(hScintillaDLL);
		hScintillaDLL = NULL;
	}
	OleUninitialize();
}

BOOL OpenHelp(HWND hWnd, HINSTANCE hInst)
{
	TCHAR szFilePath[MAX_PATH];
	DWORD dwPathLen;

	dwPathLen = GetModuleFileName(hInst, szFilePath, MAX_PATH);
	if(dwPathLen == 0)
		return FALSE;

	do
	{
		dwPathLen--;

		if(dwPathLen == 0)
			return FALSE;
	}
	while(szFilePath[dwPathLen] != _T('\\'));

	dwPathLen++;
	szFilePath[dwPathLen] = _T('\0');

	dwPathLen += sizeof("multiasm.chm") - 1;
	if(dwPathLen > MAX_PATH - 1)
		return FALSE;

	lstrcat(szFilePath, _T("multiasm.chm"));

	return !((int)(UINT_PTR)ShellExecute(hWnd, NULL, szFilePath, NULL, NULL, SW_SHOWNORMAL) <= 32);
}

#if !(defined(TARGET_ODBG) || defined(TARGET_IMMDBG) || defined(TARGET_ODBG2))
void OpenUrl(HWND hWnd, PCWSTR url) {
	if((INT_PTR)ShellExecuteW(hWnd, L"open", url, NULL, NULL, SW_SHOWNORMAL) <= 32) {
		MessageBox(hWnd, _T("Failed to open link"), NULL, MB_ICONHAND);
	}
}

HRESULT CALLBACK AboutMessageBoxCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData) {
	switch(msg) {
	case TDN_HYPERLINK_CLICKED:
		OpenUrl(hwnd, (PCWSTR)lParam);
		break;
	}

	return S_OK;
}
#endif // !(defined(TARGET_ODBG) || defined(TARGET_IMMDBG) || defined(TARGET_ODBG2))

int AboutMessageBox(HWND hWnd, HINSTANCE hInst)
{
	// OllyDbg doesn't use visual styles, so TaskDialogIndirect isn't available.
#if defined(TARGET_ODBG) || defined(TARGET_IMMDBG) || defined(TARGET_ODBG2)
	PCTSTR content =
		DEF_PLUGINNAME _T(" v") DEF_VERSION _T("\n")
		_T("By m417z (Ramen Software)\n")
		_T("\n")
		_T("Source code:\n")
		_T("https://github.com/m417z/Multiline-Ultimate-Assembler");

	MSGBOXPARAMS mbpMsgBoxParams;

	ZeroMemory(&mbpMsgBoxParams, sizeof(MSGBOXPARAMS));

	mbpMsgBoxParams.cbSize = sizeof(MSGBOXPARAMS);
	mbpMsgBoxParams.hwndOwner = hWnd;
	mbpMsgBoxParams.hInstance = hInst;
	mbpMsgBoxParams.lpszText = content;
	mbpMsgBoxParams.lpszCaption = _T("About");
	mbpMsgBoxParams.dwStyle = MB_USERICON;
	mbpMsgBoxParams.lpszIcon = MAKEINTRESOURCE(IDI_MAIN);

	return MessageBoxIndirect(&mbpMsgBoxParams);
#else
	PCWSTR content =
		DEF_PLUGINNAME L" v" DEF_VERSION L"\n"
		L"By m417z (<A HREF=\"https://ramensoftware.com/\">Ramen Software</A>)\n"
		L"\n"
		L"Source code:\n"
		L"<A HREF=\"https://github.com/m417z/Multiline-Ultimate-Assembler\">https://github.com/m417z/Multiline-Ultimate-Assembler</a>";

	TASKDIALOGCONFIG taskDialogConfig;

	ZeroMemory(&taskDialogConfig, sizeof(TASKDIALOGCONFIG));

	taskDialogConfig.cbSize = sizeof(taskDialogConfig);
	taskDialogConfig.hwndParent = hWnd;
	taskDialogConfig.hInstance = hInst;
	taskDialogConfig.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION;
	taskDialogConfig.pszWindowTitle = L"About";
	taskDialogConfig.pszMainIcon = MAKEINTRESOURCEW(IDI_MAIN);
	taskDialogConfig.pszContent = content;
	taskDialogConfig.pfCallback = AboutMessageBoxCallback;

	return TaskDialogIndirect(&taskDialogConfig, NULL, NULL, NULL);
#endif
}
