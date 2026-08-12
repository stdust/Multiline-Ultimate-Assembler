#ifndef _OPTIONS_DEF_V4_H_
#define _OPTIONS_DEF_V4_H_

#include <windows.h>

typedef struct {
	int disasm_rva;
	int disasm_rva_reloconly;
	int disasm_label;
	int disasm_extjmp;
	int disasm_hex;
	int disasm_labelgen;
	int asm_comments;
	int asm_labels;
	int edit_savepos;
	int edit_tabwidth;

	// Appearance options (Font)
	TCHAR font_name[32];
	int font_size;
	BOOL font_bold;
	BOOL font_italic;
	BYTE font_charset;

	// Appearance options (Background)
	BOOL bg_use_custom;
	COLORREF bg_color;

	// Appearance options (Syntax Highlight Colors)
	COLORREF color_cmd;
	COLORREF color_cmd_bg; // New Command Highlight Background color
	COLORREF color_reg;
	COLORREF color_cmnt;
	COLORREF color_num;

	// Alternative Line Highlighting Colors
	COLORREF color_jmp_line_bg;
	COLORREF color_call_line_bg;
} OPTIONS;

// Number of entries in the IDC_DISASM_HEX / IDC_DISASM_LABELGEN /
// IDC_EDIT_TABWIDTH combo boxes. Kept here so that the ini loader
// (main_common.c) and the options dialog (options_dlg.c) cannot drift apart.
#define OPT_DISASM_HEX_MAX       4
#define OPT_DISASM_LABELGEN_MAX  2
#define OPT_EDIT_TABWIDTH_MAX    2

#endif // _OPTIONS_DEF_V4_H_
