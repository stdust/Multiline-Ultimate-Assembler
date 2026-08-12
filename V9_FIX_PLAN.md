# v9 수정 계획 (v8 코드 리뷰 결과)

이 파일은 v8 전체를 읽고 확인한 실제 결함 목록이다. v9에서 **해당 지점만 국소 수정**한다.
전체 파일 재작성 금지 — 파서/디스어셈블 경로는 컴파일 검증 없이는 손대면 위험하다.

작업 순서: CRITICAL → IMPORTANT → MINOR. 각 항목 수정 후 해당 파일만 다시 읽어 확인.

---

## CRITICAL

### C1. `read_asm.c` / `CreateAndSetLabels` — 스택 버퍼 오버플로우 (호출자 버퍼)

`options.disasm_labelgen == 2` 분기:

```c
for(i=0; i<32 && pLabelPrefix[i] != _T('\0'); i++) { ... }

if(i == 32)
    pLabelPrefix[i] = _T('\0');   // <-- 인덱스 32. 호출자 배열은 32칸.
```

호출자: `assembler_dlg.c` `LoadCode()` 의 `TCHAR szLabelPrefix[32];` → `GetTabName(..., szLabelPrefix, 32)`.

**수정**: 호출자 버퍼를 변형하지 말고 지역 복사본을 쓴다.

```c
TCHAR szPrefix[LABEL_PREFIX_MAX_LEN];   // 32

if(options.disasm_labelgen == 2)
{
    lstrcpyn(szPrefix, pLabelPrefix, LABEL_PREFIX_MAX_LEN);  // 항상 NUL 종료

    for(i = 0; szPrefix[i] != _T('\0'); i++)
    {
        if((szPrefix[i] < _T('0') || szPrefix[i] > _T('9')) &&
           (szPrefix[i] < _T('A') || szPrefix[i] > _T('Z')) &&
           (szPrefix[i] < _T('a') || szPrefix[i] > _T('z')) &&
           szPrefix[i] != _T('_'))
            szPrefix[i] = _T('_');
    }

    pLabelPrefix = szPrefix;   // 이후 wsprintf 는 이걸 사용
}
```

`LABEL_PREFIX_MAX_LEN` 은 `read_asm.h` 에 `32` 로 정의해 매직넘버 제거.
`wsprintf(pLabel, _T("L_%s_%08u"), pLabelPrefix, n)` 최대 길이 = 2+31+1+8+1 = 43 ≤ `LABEL_MAX_LEN`(256). OK.

### C2. `plugin_odbg.c` / `SimpleDisasm` — 초기화되지 않은 `t_disasm.error` 판독

```c
t_disasm disasm;
DWORD dwCommandSize = Disasm(cmd, cmdsize, ip, dec, &disasm,
                             bSizeOnly ? DISASM_SIZE : DISASM_FILE, 0);
if(disasm.error)      // DISASM_SIZE 에서는 OllyDbg 가 구조체를 채우지 않음
    return 0;
```

크기 전용 호출(`read_asm.c` `ProcessData`, `write_asm.c`)이 스택 쓰레기값에 따라 성공/실패한다.

**수정**:

```c
t_disasm disasm;
DWORD dwCommandSize;

disasm.error = 0;
dwCommandSize = Disasm(cmd, cmdsize, ip, dec, &disasm,
                       bSizeOnly ? DISASM_SIZE : DISASM_FILE, 0);
if(dwCommandSize == 0 || disasm.error)
    return 0;
```

### C3. `assembler_dlg_tabs.c` / `GetFileLastWriteTime` — 잘못된 핸들 검사

```c
hFind = FindFirstFile(pFilePath, &find_data);
if(!hFind)            // 실패값은 INVALID_HANDLE_VALUE (-1), 0 이 아님
```

파일이 없을 때 초기화되지 않은 `find_data.ftLastWriteTime` 을 복사하고 `FindClose(-1)` 을 호출한다.
`SyncTabs` 의 변경 감지가 오염되어 편집 중 내용이 재로드로 날아갈 수 있다.

**수정**: `if(hFind == INVALID_HANDLE_VALUE)`.

### C4. `assembler_dlg_tabs.c` / `LoadFileFromLibrary` — 스택 버퍼 오버플로우

```c
lstrcpy(szFilePath, pOfnBuffer);   // szFilePath[MAX_PATH], pOfnBuffer 는 10240 TCHAR
```

`GetOpenFileName` 다중 선택 시 디렉터리 접두부가 MAX_PATH 를 넘을 수 있다.
이어지는 `lstrcpy(pFileNameDst, pFileNameSrc)` 도 무경계.

**수정**: 복사 전 길이 검사. `ofn.nFileOffset` + 각 파일명 길이 + `1`(구분자) 이
`MAX_PATH` 미만인지 확인하고, 초과 시 그 파일만 건너뛰고 오류를 보고한다.

### C5. `assembler_dlg_tabs.c` / `NewTab` — 경로 탈출

`InitLoadTabs`, `SyncTabs`, `TabRenameEnd` 는 `MakeTabLabelValid()` 를 호출하지만
`NewTab()` 은 호출하지 않는다. `LoadFileFromLibrary` 가 파일 열기 대화상자에서 온
문자열을 그대로 넘기고, 그 값이 `tabs.ini` 에 저장되어 이후
`SaveFileOfTab`(생성) / `CloseTabByIndex`(삭제) 대상 경로가 된다 → `tabs_path` 밖 파일 조작.

**수정**: `NewTab()` 진입부에서 `pTabLabel` 이 NULL 이 아니면 `MakeTabLabelValid()` 적용
(호출자 버퍼를 바꾸지 않도록 지역 복사 후 검사).

### C6. `MakeTabLabelValid` 보강

현재 금지문자 `\ / : * ? " < > |` 만 치환한다. 추가로 막아야 하는 것:

- 제어문자 (`< 0x20`)
- `.` 와 `..`
- 앞뒤 공백 / 끝의 `.`
- DOS 예약 이름 (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`)
- 결과가 빈 문자열이면 대체 이름 사용

### C7. `GetIniFilePath` — `plugin_x64dbg.c` 와 `plugin_odbg.c` 양쪽 동일

```c
GetModuleFileName(dllinst, pszIniPath, MAX_PATH);   // 반환값 미검사
TCHAR *p = _tcsrchr(pszIniPath, _T('\\'));
if (p) { _tcscpy(p + 1, _T("multiasm.ini")); }      // 무경계
```

반환 0 이면 미초기화 버퍼를 스캔, 반환 `MAX_PATH` 면 (구버전 OS) NUL 종료 안 됨,
디렉터리부가 `MAX_PATH-13` 초과 시 `szIniPath[MAX_PATH]` 밖으로 쓴다.
`MyGetintfromini` / `MyWriteinttoini` / `MyGetstringfromini` / `MyWritestringtoini` 4개 모두 영향.

**수정**: `BOOL` 반환으로 바꾸고 경계 검사. 실패 시 호출자는 기본값 사용 / `FALSE` 반환.
`main_common.c` 의 `BuildPathNextToModule()` 와 동일한 형태로 통일한다.

---

## IMPORTANT

### I1. `MyGetintfromini` — 범위 검증이 사실상 비활성

```c
if (min && max && (val < min || val > max))
```

`min` 이나 `max` 가 0 이면 검증 자체가 건너뛰어진다. `main_common.c` 의
`disasm_hex(0,4)`, `disasm_labelgen(0,2)`, `edit_tabwidth(0,2)`, `font_bold(0,1)`,
`bg_use_custom(0,1)`, 불리언 전부가 미검증. 실제로 `assembler_dlg.c` 에
`edit_tabwidth` 방어 clamp 가 이미 들어가 있다.

**수정**: `if (min != max && (val < min || val > max))`.

`MyGetColorfromini` 는 스타일 바이트(예 `0x01FF6E6E`) 보존을 위해 의도적으로
`(0, 0)` 을 넘긴다 → `min == max` 이므로 계속 미검증. 이 관용구를 반드시 유지하고
`plugin.h` 에 주석으로 규약을 명시한다.

`main_common.c` 는 불리언 옵션을 `(0, 1)` 로 바꾼다.

### I2. `main_x64dbg.c` / `GetPluginVersion` — 디버그 잔여물

```c
if(c >= '0' && c <= '9')
{
_plugin_logputs("");    // 자릿수마다 빈 줄 로그
    nVersion *= 10;
```

**수정**: 해당 줄 삭제.

### I3. `main_x64dbg.c` / `CmdShow` — 등록된 명령이 아무 동작 안 함

`multiasm_show` 본문이 `// GuiExecuteOnGuiThread(AssemblerShowDlg);` 로 주석 처리됨.

**수정**: 주석 해제. 대화상자는 GUI 스레드에서 만들어야 하므로
`GuiExecuteOnGuiThread(AssemblerShowDlg);` 형태 유지.

### I4. `write_asm.c` — `"codecave:"` 접두 비교가 문자열 끝을 넘어 읽음

두 곳:

- `ParseAddress`: `BOOL bIsCavePrefix = (memcmp(p, szPrefix, nPrefixLen * sizeof(TCHAR)) == 0);`
- `ReplaceLabelsFromList`: `if (memcmp(pszCaveName, szPrefix, nPrefixLen * sizeof(TCHAR)) == 0)`

`p` 가 9글자보다 짧으면 버퍼 밖을 읽는다.

**수정**: 길이 안전한 헬퍼 하나를 추가해 양쪽에서 사용.

```c
static BOOL IsCaveNamePrefix(const TCHAR *p)
{
    int i;
    for(i = 0; i < CAVE_NAME_PREFIX_LEN; i++)
    {
        if(p[i] == _T('\0') || p[i] != CAVE_NAME_PREFIX[i])
            return FALSE;
    }
    return TRUE;
}
```

`write_asm.h` 에 `CAVE_NAME_PREFIX` / `CAVE_NAME_PREFIX_LEN` / `CAVE_NAME_MAX_LEN`(64) 정의.
`szCaveName[64]` 와 `lstrcpyn(..., 64)` 의 하드코딩 64 도 이 상수로 교체.

### I5. `read_asm.c` / `ProcessCode` — 미초기화 변수 + 잘못된 반환형

```c
DWORD dwCommandSize;          // switch 가 모든 경우를 덮지 않으면 미초기화
switch(nCommandType) { ... }  // default 없음
if(dwCommandSize == 0) return FALSE;
...
*pCode = 1;
ZeroMemory(pCode+1, dwCommandSize-1);   // dwCommandSize > nSize 면 힙 오버런
...
lstrcpy(lpError, _T("Allocation failed"));
return 0;                     // BOOL 함수인데 0
```

**수정**: `dwCommandSize = 0;` 초기화, `default:` 추가(오류 메시지 설정 후 `FALSE`),
`if(dwCommandSize > nSize)` 방어, `return 0` → `return FALSE`.

### I6. `assembler_dlg.c` / `IsKeyword` — 매치 누락

```c
while ((p = strstr(p, lowerWord)) != NULL)
{
    ... 경계 검사 ...
    p += len;      // 경계 불일치 시 유효한 다음 매치를 건너뛸 수 있음
}
```

**수정**: 경계 불일치 시 `p += 1`.

### I7. `assembler_dlg.c` / `LoadCode` — 실패를 조용히 삼킴

`WideCharToMultiByte` / `HeapAlloc` 실패 시 `SCI_SETTEXT` 를 아예 호출하지 않고
`return TRUE` 로 빠진다. 사용자에게는 디스어셈블 결과가 빈 화면으로 보인다.

**수정**: 변환/할당 실패 시 `AsmDlgMessageBox(...)` 로 보고하고 `FALSE` 반환.
UNICODE / `TARGET_ODBG` / x64dbg 세 분기 모두.

### I8. `options_dlg.c` / `OptionsFromDlg` — `CB_ERR` 저장

```c
temp_options.disasm_hex = (int)SendDlgItemMessage(hWnd, IDC_DISASM_HEX, CB_GETCURSEL, 0, 0);
```

선택이 없으면 `-1`(`CB_ERR`)이 그대로 저장되고 ini 에 기록된다.
(`OptionsToDlg` 가 미검증 ini 값으로 `CB_SETCURSEL` 하므로 도달 가능 — I1 과 연결)

**수정**: 세 콤보(`disasm_hex`, `disasm_labelgen`, `edit_tabwidth`) 모두
`if(n < 0) n = 0;` 또는 기존 값 유지. `options_def.h` 의 `OPT_*_MAX` 로 상한도 검사.

### I9. `options_dlg.c` / `ChooseColor_Dlg` — 볼드 플래그 소실

`MyGetColorfromini` 가 보존하는 상위 스타일 바이트를 `cc.rgbResult` 왕복에서 잃는다.
`CLR_INVALID`(`0xFFFFFFFF`)를 그대로 넣는 문제도 있다.

**수정**: 호출 전 상위 바이트를 저장하고 `cc.rgbResult = *pColor & 0x00FFFFFF;` 로 전달,
성공 시 `*pColor = (저장한 상위 바이트 << 24) | (cc.rgbResult & 0x00FFFFFF);`
`*pColor == CLR_INVALID` 이면 기본색으로 초기화.

### I10. `tabctrl_ex.c` / `TabEndEditLabel` — 재진입 이중 해제

`DestroyWindow(hEditCtrlWnd)` → `WM_KILLFOCUS` → `TabCtrl_Ex_EndEditLabelNow` 재진입 →
`UnhookWindowsHookEx` / `DestroyWindow` 가 두 번 실행된다.

**수정**: `TabEndEditLabel` 진입부에서 `g_hEditCtrlWnd = NULL` 로 먼저 지우고,
`EndEditLabelNow` 는 `g_hEditCtrlWnd` 가 NULL 이면 즉시 반환. 서브클래스 프로시저를
원복한 뒤에 `DestroyWindow` 를 호출한다.

### I11. `plugin_odbg.c` — OllyDbg `TEXTLEN` 초과

```c
BOOL QuickInsertLabel(...)   { char szAnsi[1024]; ... }
BOOL QuickInsertComment(...) { char szAnsi[2048]; ... }
```

OllyDbg 의 `TEXTLEN` 은 256. `Quickinsertname` 내부 레코드를 넘어 쓸 수 있다.

**수정**: 두 버퍼 모두 `TEXTLEN` 으로. 그리고 `ConvertUtf8ToAnsi` 는
`WideCharToMultiByte` 반환값을 검사해 실패 시 `szAnsi[0] = '\0'` 로 NUL 종료를 보장.

### I12. `plugin_odbg.c` / `FindModuleByName` — NULL 역참조

```c
table = (t_table *)Plugingetvalue(VAL_MODULES);
sorted = &table->data;      // table NULL 검사 없음
```

**수정**: `if(!table) return NULL;`

### I13. `plugin_odbg.c` / `EnsureMemoryBackup` — 백업 없이 패치될 수 있음

`t_dump dump;` 를 5개 필드만 채우고(나머지 미초기화) `Dumpbackup` 반환값을 버린다.
백업 생성 실패 시 사용자는 되돌릴 수 없다.

**수정**: `ZeroMemory(&dump, sizeof(dump));` 선행, `Dumpbackup` 결과 확인.

### I14. `ShowStatus` — 양쪽 백엔드 공통

`WideCharToMultiByte(CP_ACP, 0, msg, -1, szAnsi, 1024, NULL, NULL)` 반환값 미검사 →
실패 시 미종료 버퍼를 `GuiAddStatusBarMessage` / `Flash` 에 넘긴다.

**수정**: 반환값 0 이면 `szAnsi[0] = '\0'`.

### I15. `plugin_x64dbg.c` / `AssembleWithGivenSize` — NOP 채우기 상한 없음

```c
while(size < nReqSize)
    bBuffer[size++] = 0x90;
```

호출자는 모두 `BYTE bCode[MAXCMDSIZE]`(16)를 넘기고 `nReqSize` 는
`cmd_node->nCodeSize` 에서 검증 없이 온다.

**수정**: 진입부에서 `if(nReqSize > MAXCMDSIZE) { lstrcpy(lpError, ...); return 0; }`

### I16. `write_asm.c` / `ScanMemoryForCave` — 정수 오버플로우

`SIZE_T nScanSize = nRequiredSize + 8;` 및 `dwAddr + nBytesToRead` 계산.

**수정**: `nRequiredSize` 가 비정상적으로 클 때 조기 반환.

---

## MINOR (여유 있으면)

- `assembler_dlg_tabs.c`: 탭 전환 시 `SCI_EMPTYUNDOBUFFER` 미호출 → 새 탭에서 Ctrl+Z 로
  이전 탭 텍스트가 나오고 그대로 저장될 수 있음. (`scintilla.h` 에 상수 추가해 둠)
- `assembler_dlg_tabs.c`: `LoadFileOfTab` 이 읽기 실패 후에도 타임스탬프와 save point 를
  갱신 → 데이터 손실. 실패 시 갱신 생략.
- `assembler_dlg_tabs.c`: `nMaxLabelLen` / `cchTextMax` 가 0 또는 음수가 될 수 있는데
  `DWORD`/`int` 카운트로 넘어간다. 하한 검사 추가.
- `assembler_dlg_tabs.c`: `CloseTabByIndex` 가 `nTabIndex == -1` 로 진행 가능.
  경로 버퍼를 라벨 버퍼로 재사용해 전체 경로가 라벨로 `tabs.ini` 에 기록될 수 있음.
- `assembler_dlg_tabs.c`: 부분 `ReadFile`/`WriteFile` 을 성공으로 처리.
- `assembler_dlg_tabs.c`: 읽기 전용 열기에 `OPEN_ALWAYS` → 0바이트 파일 생성.
- `assembler_dlg_tabs.c`: `sizeof("...")` 를 TCHAR 개수로 사용(UNICODE 빌드에서 틀림).
- `tabctrl_ex.c`: `WM_NCDESTROY` 처리 없음 → prop + 힙 누수. 재정렬 드래그 후
  `SetCurSel` 누락으로 선택이 사라짐(그 뒤 인덱스 -1 이 흘러감).
- `options_dlg.c`: ini 쓰기 24곳 전부 반환값 무시 → 읽기 전용 ini 에서 설정이 조용히 사라짐.
- `options_dlg.c`: `WriteDebugLog` 가 `((void)0)` 이라 `DialogBox` 실패 경로 전체가
  죽은 코드. `DWORD err` 도 미사용. 실패 시 사용자에게 알리도록 정리.
- `plugin_x64dbg.c`: `IsModuleWithRelocations` 가 읽기 실패와 "재배치 없음" 을 구분 못 함.
  `e_lfanew` 미검증.
- `plugin_x64dbg.c`: `basicinfo.instruction[COMMAND_MAX_LEN-1-6] = '\0'` 는 여유 0 으로
  딱 맞는다. 주석으로 근거를 남길 것.
- `plugin_x64dbg.c`: `GetModuleName` 이 `ModNameFromAddr(..., FALSE)` 로 확장자를 떼어
  RVA 텍스트 → `ModBaseFromName` 왕복이 깨질 소지. 동작 변경 위험이 있어 별도 검토.
- `read_asm.h` / `write_asm.h`: 헤더에 `static` 함수 선언. 각 헤더가 단일 .c 에서만
  포함되므로 동작은 문제없다. 정리는 선택.

---

## 검증 한계

이 환경에서는 셸이 동작하지 않아 **빌드/테스트를 실행할 수 없다**.
수정 후 반드시 사용자 환경에서 다음 구성을 빌드해 확인해야 한다:

- `Release_x64dbg|Win32`, `Release_x64dbg|x64`
- `Release_odbg|Win32`, `Release_odbg_v2|Win32`, `Release_immdbg|Win32`

참고: `Debug_x64dbg|Win32` 와 `Debug_x64dbg|x64` 의 `ResourceCompile` 이
`TARGET_ODBG` 를 정의한다(다른 x64dbg 구성과 불일치). `TARGET_X64DBG` 로 고쳐야 한다.
