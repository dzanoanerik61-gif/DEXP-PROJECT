// DexpOS.cpp — Desktop OS Simulator
// Compile: x86_64-w64-mingw32-g++ DexpOS.cpp -o DexpOS.exe -lgdi32 -lcomctl32 -lmsimg32 -lshlwapi -lwinmm -mwindows -O2 -static

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <process.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <mmsystem.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ─────────────────────────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────────────────────────
#define MAIN_WIDTH      1280
#define MAIN_HEIGHT     720
#define TASKBAR_HEIGHT  42
#define DESKTOP_HEIGHT  (MAIN_HEIGHT - TASKBAR_HEIGHT)
#define ICON_SIZE       80
#define ICON_COLS       6
#define MAX_ICONS       64
#define MAX_WINDOWS     16
#define MAX_TERMINAL_LINES 500
#define MAX_TERM_LINE_LEN  256
#define MAX_NOTES_LEN   8192

// Timer IDs
#define TIMER_SPLASH    1
#define TIMER_CLOCK     2
#define TIMER_ANIMATE   3
#define TIMER_GAME      4

// WM_APP messages
#define WM_OPEN_APP     (WM_APP + 1)
#define WM_CLOSE_APPWND (WM_APP + 2)

// App IDs for Start Menu
#define APP_CALC        1
#define APP_NOTEPAD     2
#define APP_GAME        3
#define APP_TERMINAL    4
#define APP_EXPLORER    5
#define APP_TASKMGR     6
#define APP_SETTINGS    7
#define APP_CMD         8

// Context menu IDs (desktop)
#define CM_NEW_FOLDER   101
#define CM_NEW_FILE     102
#define CM_REFRESH      103
#define CM_SETTINGS     104
// Context menu IDs (icon)
#define CM_OPEN         201
#define CM_RENAME       202
#define CM_DELETE       203
#define CM_COPY         204

// Window state
#define WS_NORMAL   0
#define WS_MINIMIZED 1
#define WS_MAXIMIZED 2

// ─────────────────────────────────────────────────────────────
//  THEME
// ─────────────────────────────────────────────────────────────
struct Theme {
    COLORREF taskbarBg;
    COLORREF taskbarText;
    COLORREF desktopBg1;
    COLORREF desktopBg2;
    COLORREF titleBg;
    COLORREF titleText;
    COLORREF bodyBg;
    COLORREF bodyText;
    COLORREF accent;
    COLORREF buttonBg;
    COLORREF startMenuBg;
    COLORREF trashColor;
    bool     darkMode;
};

static Theme g_themes[4] = {
    // 0: Ocean
    { RGB(10,40,80), RGB(220,240,255),
      RGB(15,55,110), RGB(5,25,60),
      RGB(20,80,160), RGB(255,255,255),
      RGB(8,30,70),   RGB(210,230,255),
      RGB(0,150,255), RGB(30,90,180),
      RGB(5,20,50),   RGB(0,200,220), false },
    // 1: Dark
    { RGB(30,30,30),  RGB(200,200,200),
      RGB(40,40,40),  RGB(20,20,20),
      RGB(50,50,50),  RGB(255,255,255),
      RGB(35,35,35),  RGB(220,220,220),
      RGB(100,100,255), RGB(60,60,60),
      RGB(25,25,25),  RGB(160,160,160), true },
    // 2: Light
    { RGB(220,220,230), RGB(40,40,40),
      RGB(240,245,255), RGB(200,210,230),
      RGB(180,200,230), RGB(20,20,60),
      RGB(250,252,255), RGB(30,30,60),
      RGB(70,130,220),  RGB(190,210,240),
      RGB(235,240,250), RGB(200,100,80), false },
    // 3: Violet
    { RGB(60,0,100),  RGB(240,200,255),
      RGB(80,0,130),  RGB(40,0,70),
      RGB(120,0,200), RGB(255,255,255),
      RGB(55,0,90),   RGB(240,210,255),
      RGB(200,50,255), RGB(130,0,210),
      RGB(45,0,75),   RGB(255,80,180), false },
};
static int  g_themeIdx      = 0;
static bool g_glassEffect   = true;
static bool g_animations    = true;

#define THEME (g_themes[g_themeIdx])

// ─────────────────────────────────────────────────────────────
//  DESKTOP ICON
// ─────────────────────────────────────────────────────────────
struct DesktopIcon {
    wchar_t name[MAX_PATH];
    wchar_t path[MAX_PATH];  // full path on disk
    bool    isFolder;
    bool    isTrash;
    int     x, y;
    bool    selected;
};

// ─────────────────────────────────────────────────────────────
//  APP WINDOW
// ─────────────────────────────────────────────────────────────
struct AppWindow {
    bool   active;
    int    appId;
    wchar_t title[128];
    int    x, y, w, h;
    int    state;          // WS_NORMAL / WS_MINIMIZED / WS_MAXIMIZED
    int    restoreX, restoreY, restoreW, restoreH;
    bool   dragging;
    int    dragOffX, dragOffY;
    bool   resizing;
    int    resizeStartX, resizeStartY, resizeStartW, resizeStartH;
    float  animScale;      // 0→1 on open, 1→0 on close
    bool   closing;

    // Per-app state
    // Notepad
    wchar_t noteText[MAX_NOTES_LEN];
    int     noteCaret;
    // Terminal / CMD
    wchar_t termLines[MAX_TERMINAL_LINES][MAX_TERM_LINE_LEN];
    int     termCount;
    wchar_t termInput[MAX_TERM_LINE_LEN];
    // Calculator
    wchar_t calcDisplay[64];
    double  calcVal;
    double  calcPrev;
    wchar_t calcOp;
    bool    calcNewNum;
    // Game
    int     gameScore;
    int     gameGoal;
    int     gameTimer;
    bool    gameRunning;
    // Explorer
    wchar_t explorerPath[MAX_PATH];
    wchar_t explorerItems[64][MAX_PATH];
    bool    explorerIsDir[64];
    int     explorerCount;
    // Taskmgr / Settings — no extra state needed
    // Settings
    int     settingsThemeHover;
};

// ─────────────────────────────────────────────────────────────
//  TRASH ENTRY
// ─────────────────────────────────────────────────────────────
struct TrashEntry {
    wchar_t origPath[MAX_PATH];
    wchar_t trashPath[MAX_PATH];
    wchar_t name[MAX_PATH];
};

// ─────────────────────────────────────────────────────────────
//  GLOBALS
// ─────────────────────────────────────────────────────────────
static HWND      g_hwnd       = NULL;
static bool      g_splash     = true;
static int       g_splashStep = 0;
static bool      g_startOpen  = false;
static int       g_startHover = -1;

static DesktopIcon g_icons[MAX_ICONS];
static int         g_iconCount = 0;
static int         g_selIcon   = -1;
static int         g_ctxIcon   = -1;  // icon right-clicked on
static wchar_t     g_userDataPath[MAX_PATH];
static wchar_t     g_desktopPath[MAX_PATH];
static wchar_t     g_trashPath[MAX_PATH];
static wchar_t     g_trashInfoPath[MAX_PATH];
static wchar_t     g_dxAppsPath[MAX_PATH];
static wchar_t     g_copyBuffer[MAX_PATH];  // for icon copy

static AppWindow   g_windows[MAX_WINDOWS];
static int         g_topWindow = -1;  // index of focused window

static TrashEntry  g_trash[64];
static int         g_trashCount = 0;

// Rename state
static bool    g_renaming     = false;
static int     g_renameIcon   = -1;
static wchar_t g_renameText[MAX_PATH];
static int     g_renameCaret  = 0;

// ─────────────────────────────────────────────────────────────
//  HELPER MACROS
// ─────────────────────────────────────────────────────────────
#define RGB_R(c) ((c)&0xFF)
#define RGB_G(c) (((c)>>8)&0xFF)
#define RGB_B(c) (((c)>>16)&0xFF)

// ─────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static void LoadDesktopIcons();
static void RenderFrame(HDC hdc);
static void RenderSplash(HDC hdc);
static void RenderDesktop(HDC hdc);
static void RenderTaskbar(HDC hdc);
static void RenderStartMenu(HDC hdc);
static void RenderWindows(HDC hdc);
static void RenderOneWindow(HDC hdc, int idx);
static void RenderCalc(HDC hdc, AppWindow* w, RECT body);
static void RenderNotes(HDC hdc, AppWindow* w, RECT body);
static void RenderGame(HDC hdc, AppWindow* w, RECT body);
static void RenderTerminal(HDC hdc, AppWindow* w, RECT body);
static void RenderExplorer(HDC hdc, AppWindow* w, RECT body);
static void RenderTaskMgr(HDC hdc, AppWindow* w, RECT body);
static void RenderSettings(HDC hdc, AppWindow* w, RECT body);
static void RenderTrash(HDC hdc, AppWindow* w, RECT body);
static void OpenApp(int appId, const wchar_t* extraPath = NULL);
static int  FindWindow_ByApp(int appId);
static void CloseAppWindow(int idx);
static void BringToFront(int idx);
static void LayoutIcons();
static void RefreshIcons();
static void LoadTrash();
static void SaveTrashEntry(const TrashEntry& e);
static void DeleteTrashInfo(const wchar_t* name);
static void CreateNewFolder();
static void CreateNewFile();
static void DeleteIconToTrash(int idx);
static void RestoreTrash(int idx);
static void EmptyTrash();
static bool PtInIcon(int idx, int x, int y);
static int  HitTestIcon(int x, int y);
static void HandleCalcButton(AppWindow* w, int btnIdx);
static void HandleTerminalCommand(AppWindow* w);
static void ExplorerNavigate(AppWindow* w, const wchar_t* path);
static void DrawGradientRect(HDC hdc, RECT r, COLORREF c1, COLORREF c2, bool vert=true);
static void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int rx, COLORREF fill, COLORREF border);
static void TextCentered(HDC hdc, const wchar_t* s, RECT r, COLORREF col, int sz, bool bold=false);
static void TextLeft(HDC hdc, const wchar_t* s, int x, int y, COLORREF col, int sz, bool bold=false, const wchar_t* font=L"Segoe UI");
static void GlassRect(HDC hdc, RECT r, COLORREF col, BYTE alpha);

// ─────────────────────────────────────────────────────────────
//  UTILITY DRAWING
// ─────────────────────────────────────────────────────────────
static void DrawGradientRect(HDC hdc, RECT r, COLORREF c1, COLORREF c2, bool vert) {
    TRIVERTEX tv[2];
    tv[0].x = r.left; tv[0].y = r.top;
    tv[0].Red   = RGB_R(c1)<<8; tv[0].Green = RGB_G(c1)<<8; tv[0].Blue = RGB_B(c1)<<8; tv[0].Alpha = 0;
    tv[1].x = r.right; tv[1].y = r.bottom;
    tv[1].Red   = RGB_R(c2)<<8; tv[1].Green = RGB_G(c2)<<8; tv[1].Blue = RGB_B(c2)<<8; tv[1].Alpha = 0;
    GRADIENT_RECT gr = {0, 1};
    GradientFill(hdc, tv, 2, &gr, 1, vert ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
}

static void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int rx, COLORREF fill, COLORREF border) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN   pn = CreatePen(PS_SOLID, 1, border);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN   op = (HPEN)SelectObject(hdc, pn);
    RoundRect(hdc, x, y, x+w, y+h, rx, rx);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pn);
}

static void TextCentered(HDC hdc, const wchar_t* s, RECT r, COLORREF col, int sz, bool bold) {
    HFONT f = CreateFont(sz, 0, 0, 0, bold?FW_BOLD:FW_NORMAL, 0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(hdc, f);
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);
    DrawText(hdc, s, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOCLIP);
    SelectObject(hdc, of); DeleteObject(f);
}

static void TextLeft(HDC hdc, const wchar_t* s, int x, int y, COLORREF col, int sz, bool bold, const wchar_t* font) {
    HFONT f = CreateFont(sz, 0, 0, 0, bold?FW_BOLD:FW_NORMAL, 0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, font);
    HFONT of = (HFONT)SelectObject(hdc, f);
    SetTextColor(hdc, col);
    SetBkMode(hdc, TRANSPARENT);
    TextOut(hdc, x, y, s, (int)wcslen(s));
    SelectObject(hdc, of); DeleteObject(f);
}

static void GlassRect(HDC hdc, RECT r, COLORREF col, BYTE alpha) {
    if (!g_glassEffect) {
        HBRUSH br = CreateSolidBrush(col);
        FillRect(hdc, &r, br);
        DeleteObject(br);
        return;
    }
    HDC memDC = CreateCompatibleDC(hdc);
    int w = r.right - r.left, h = r.bottom - r.top;
    HBITMAP bm = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP ob = (HBITMAP)SelectObject(memDC, bm);
    HBRUSH br = CreateSolidBrush(col);
    RECT fill = {0, 0, w, h};
    FillRect(memDC, &fill, br);
    DeleteObject(br);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(hdc, r.left, r.top, w, h, memDC, 0, 0, w, h, bf);
    SelectObject(memDC, ob);
    DeleteObject(bm);
    DeleteDC(memDC);
}

// ─────────────────────────────────────────────────────────────
//  FILE SYSTEM SETUP
// ─────────────────────────────────────────────────────────────
static void SetupDirectories() {
    wchar_t exeDir[MAX_PATH];
    GetModuleFileName(NULL, exeDir, MAX_PATH);
    PathRemoveFileSpec(exeDir);

    swprintf(g_userDataPath, MAX_PATH, L"%s\\DexpOS_UserData", exeDir);
    swprintf(g_desktopPath,  MAX_PATH, L"%s\\Desktop", g_userDataPath);
    swprintf(g_trashPath,    MAX_PATH, L"%s\\Trash", g_userDataPath);
    swprintf(g_trashInfoPath,MAX_PATH, L"%s\\TrashInfo", g_userDataPath);
    swprintf(g_dxAppsPath,   MAX_PATH, L"%s\\DxApps", g_userDataPath);

    CreateDirectory(g_userDataPath, NULL);
    CreateDirectory(g_desktopPath,  NULL);
    CreateDirectory(g_trashPath,    NULL);
    CreateDirectory(g_trashInfoPath,NULL);
    CreateDirectory(g_dxAppsPath,   NULL);
}

// ─────────────────────────────────────────────────────────────
//  ICON LAYOUT & LOADING
// ─────────────────────────────────────────────────────────────
static void LayoutIcons() {
    int cols = 1;
    int startX = 20, startY = 20, padX = 100, padY = 100;
    for (int i = 0; i < g_iconCount; i++) {
        if (!g_icons[i].isTrash) {
            int row = (i) / 1; // column-major: first column only by default
            // Actually layout in a single left column, row by row
            g_icons[i].x = startX + ((i) % ICON_COLS) * padX;  // unused, override below
            g_icons[i].y = startY + ((i) / ICON_COLS) * padY;
            // column-major, single column layout
            g_icons[i].x = startX;
            g_icons[i].y = startY + i * padY;
        }
    }
    // Trash: bottom-left
    for (int i = 0; i < g_iconCount; i++) {
        if (g_icons[i].isTrash) {
            g_icons[i].x = 20;
            g_icons[i].y = DESKTOP_HEIGHT - 110;
        }
    }
}

static void RefreshIcons() {
    g_iconCount = 0;

    // Add trash first
    DesktopIcon& trash = g_icons[g_iconCount++];
    wcscpy(trash.name, L"Корзина");
    wcscpy(trash.path, g_trashPath);
    trash.isFolder = true;
    trash.isTrash  = true;
    trash.selected = false;

    // Read desktop dir
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*", g_desktopPath);
    WIN32_FIND_DATA fd;
    HANDLE hf = FindFirstFile(pattern, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (g_iconCount >= MAX_ICONS) break;
            DesktopIcon& ic = g_icons[g_iconCount++];
            wcscpy(ic.name, fd.cFileName);
            swprintf(ic.path, MAX_PATH, L"%s\\%s", g_desktopPath, fd.cFileName);
            ic.isFolder = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            ic.isTrash  = false;
            ic.selected = false;
        } while (FindNextFile(hf, &fd));
        FindClose(hf);
    }

    LayoutIcons();
}

// ─────────────────────────────────────────────────────────────
//  TRASH
// ─────────────────────────────────────────────────────────────
static void LoadTrash() {
    g_trashCount = 0;
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*.trashinfo", g_trashInfoPath);
    WIN32_FIND_DATA fd;
    HANDLE hf = FindFirstFile(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (g_trashCount >= 64) break;
        wchar_t infoPath[MAX_PATH];
        swprintf(infoPath, MAX_PATH, L"%s\\%s", g_trashInfoPath, fd.cFileName);
        HANDLE fh = CreateFile(infoPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (fh == INVALID_HANDLE_VALUE) continue;
        char buf[MAX_PATH*2];
        DWORD read = 0;
        ReadFile(fh, buf, sizeof(buf)-1, &read, NULL);
        buf[read] = 0;
        CloseHandle(fh);
        // Parse: line1=orig, line2=trash, line3=name
        char* p = buf;
        char orig[MAX_PATH] = {}, trsh[MAX_PATH] = {}, nm[MAX_PATH] = {};
        sscanf(p, "%[^\n]\n%[^\n]\n%[^\n]", orig, trsh, nm);
        TrashEntry& e = g_trash[g_trashCount++];
        MultiByteToWideChar(CP_UTF8, 0, orig, -1, e.origPath, MAX_PATH);
        MultiByteToWideChar(CP_UTF8, 0, trsh, -1, e.trashPath, MAX_PATH);
        MultiByteToWideChar(CP_UTF8, 0, nm,   -1, e.name,     MAX_PATH);
    } while (FindNextFile(hf, &fd));
    FindClose(hf);
}

static void SaveTrashEntry(const TrashEntry& e) {
    wchar_t infoPath[MAX_PATH];
    swprintf(infoPath, MAX_PATH, L"%s\\%s.trashinfo", g_trashInfoPath, e.name);
    HANDLE fh = CreateFile(infoPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (fh == INVALID_HANDLE_VALUE) return;
    char buf[MAX_PATH*6];
    char orig[MAX_PATH], trsh[MAX_PATH], nm[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, e.origPath, -1, orig, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, e.trashPath,-1, trsh, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, e.name,     -1, nm,   MAX_PATH, NULL, NULL);
    int len = sprintf(buf, "%s\n%s\n%s", orig, trsh, nm);
    DWORD written;
    WriteFile(fh, buf, len, &written, NULL);
    CloseHandle(fh);
}

static void DeleteTrashInfo(const wchar_t* name) {
    wchar_t infoPath[MAX_PATH];
    swprintf(infoPath, MAX_PATH, L"%s\\%s.trashinfo", g_trashInfoPath, name);
    DeleteFile(infoPath);
}

// ─────────────────────────────────────────────────────────────
//  DESKTOP ACTIONS
// ─────────────────────────────────────────────────────────────
static void CreateNewFolder() {
    wchar_t path[MAX_PATH];
    int n = 1;
    do {
        swprintf(path, MAX_PATH, L"%s\\Новая папка %d", g_desktopPath, n++);
    } while (PathFileExists(path));
    CreateDirectory(path, NULL);
    RefreshIcons();
}

static void CreateNewFile() {
    wchar_t path[MAX_PATH];
    int n = 1;
    do {
        swprintf(path, MAX_PATH, L"%s\\Новый файл %d.txt", g_desktopPath, n++);
    } while (PathFileExists(path));
    HANDLE fh = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
    RefreshIcons();
}

static void DeleteIconToTrash(int idx) {
    if (idx < 0 || idx >= g_iconCount) return;
    DesktopIcon& ic = g_icons[idx];
    if (ic.isTrash) return;

    TrashEntry e;
    wcscpy(e.origPath, ic.path);
    wcscpy(e.name, ic.name);

    // Move file to trash folder
    wchar_t dest[MAX_PATH];
    swprintf(dest, MAX_PATH, L"%s\\%s", g_trashPath, ic.name);
    wcscpy(e.trashPath, dest);

    if (ic.isFolder) {
        // Simple rename (same drive assumed)
        MoveFile(ic.path, dest);
    } else {
        MoveFile(ic.path, dest);
    }
    SaveTrashEntry(e);
    if (g_trashCount < 64) g_trash[g_trashCount++] = e;
    RefreshIcons();
    LoadTrash();
}

static void RestoreTrash(int idx) {
    if (idx < 0 || idx >= g_trashCount) return;
    TrashEntry& e = g_trash[idx];
    MoveFile(e.trashPath, e.origPath);
    DeleteTrashInfo(e.name);
    LoadTrash();
    RefreshIcons();
}

static void EmptyTrash() {
    for (int i = 0; i < g_trashCount; i++) {
        DeleteFile(g_trash[i].trashPath);
        // For dirs, use recursive delete (simple: just try)
        RemoveDirectory(g_trash[i].trashPath);
        DeleteTrashInfo(g_trash[i].name);
    }
    g_trashCount = 0;
    RefreshIcons();
}

// ─────────────────────────────────────────────────────────────
//  WINDOW MANAGEMENT
// ─────────────────────────────────────────────────────────────
static int FindWindow_ByApp(int appId) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].active && g_windows[i].appId == appId) return i;
    return -1;
}

static void BringToFront(int idx) {
    g_topWindow = idx;
}

static void CloseAppWindow(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    memset(&g_windows[idx], 0, sizeof(AppWindow));
    g_topWindow = -1;
    for (int i = MAX_WINDOWS-1; i >= 0; i--)
        if (g_windows[i].active) { g_topWindow = i; break; }
}

static void OpenApp(int appId, const wchar_t* extraPath) {
    // Check if already open (except explorer with different path)
    if (appId != APP_EXPLORER) {
        int ex = FindWindow_ByApp(appId);
        if (ex >= 0) { BringToFront(ex); return; }
    }

    // Find free slot
    int idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) if (!g_windows[i].active) { idx = i; break; }
    if (idx < 0) return;

    AppWindow& w = g_windows[idx];
    memset(&w, 0, sizeof(AppWindow));
    w.active   = true;
    w.appId    = appId;
    w.animScale= g_animations ? 0.01f : 1.0f;
    w.closing  = false;

    // Default position & size
    w.x = 100 + idx * 30; w.y = 60 + idx * 20;
    w.w = 600; w.h = 450;

    // App-specific init
    switch (appId) {
    case APP_CALC:
        wcscpy(w.title, L"Калькулятор");
        w.w = 320; w.h = 460;
        wcscpy(w.calcDisplay, L"0");
        w.calcNewNum = true;
        break;
    case APP_NOTEPAD:
        wcscpy(w.title, L"Заметки");
        w.w = 550; w.h = 420;
        break;
    case APP_GAME:
        wcscpy(w.title, L"Мини-игра");
        w.w = 400; w.h = 380;
        w.gameScore = 0; w.gameGoal = 10; w.gameTimer = 30; w.gameRunning = false;
        break;
    case APP_TERMINAL:
        wcscpy(w.title, L"Терминал");
        w.w = 680; w.h = 420;
        wcscpy(w.termLines[0], L"Dexp OS Terminal v1.0 — введите 'help' для списка команд");
        w.termCount = 1;
        break;
    case APP_CMD:
        wcscpy(w.title, L"CMD");
        w.w = 680; w.h = 420;
        wcscpy(w.termLines[0], L"Dexp OS CMD v1.0 — введите 'help' для списка команд");
        w.termCount = 1;
        break;
    case APP_EXPLORER:
        wcscpy(w.title, L"Проводник");
        w.w = 600; w.h = 430;
        {
            const wchar_t* p = extraPath ? extraPath : g_desktopPath;
            ExplorerNavigate(&w, p);
        }
        break;
    case APP_TASKMGR:
        wcscpy(w.title, L"Диспетчер задач");
        w.w = 520; w.h = 380;
        break;
    case APP_SETTINGS:
        wcscpy(w.title, L"Настройки");
        w.w = 500; w.h = 400;
        w.settingsThemeHover = -1;
        break;
    }

    // Clamp position
    if (w.x + w.w > MAIN_WIDTH)  w.x = MAIN_WIDTH  - w.w - 10;
    if (w.y + w.h > DESKTOP_HEIGHT) w.y = DESKTOP_HEIGHT - w.h - 10;
    if (w.x < 0) w.x = 10;
    if (w.y < 0) w.y = 10;

    w.restoreX = w.x; w.restoreY = w.y; w.restoreW = w.w; w.restoreH = w.h;
    BringToFront(idx);
}

// ─────────────────────────────────────────────────────────────
//  EXPLORER
// ─────────────────────────────────────────────────────────────
static void ExplorerNavigate(AppWindow* w, const wchar_t* path) {
    wcscpy(w->explorerPath, path);
    w->explorerCount = 0;
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%s\\*", path);
    WIN32_FIND_DATA fd;
    HANDLE hf = FindFirstFile(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (w->explorerCount >= 64) break;
        wcscpy(w->explorerItems[w->explorerCount], fd.cFileName);
        w->explorerIsDir[w->explorerCount] = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        w->explorerCount++;
    } while (FindNextFile(hf, &fd));
    FindClose(hf);
}

// ─────────────────────────────────────────────────────────────
//  HIT TESTS
// ─────────────────────────────────────────────────────────────
static bool PtInIcon(int idx, int x, int y) {
    DesktopIcon& ic = g_icons[idx];
    return x >= ic.x && x <= ic.x + ICON_SIZE && y >= ic.y && y <= ic.y + ICON_SIZE + 20;
}

static int HitTestIcon(int x, int y) {
    for (int i = 0; i < g_iconCount; i++)
        if (PtInIcon(i, x, y)) return i;
    return -1;
}

// title bar rect for window idx
static RECT TitleRect(int idx) {
    AppWindow& w = g_windows[idx];
    RECT r = {w.x, w.y, w.x + w.w, w.y + 34};
    return r;
}

static bool PtInRect2(RECT r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// ─────────────────────────────────────────────────────────────
//  CALCULATOR LOGIC
// ─────────────────────────────────────────────────────────────
// Buttons: C ⌫ % ÷  /  7 8 9 × / 4 5 6 − / 1 2 3 + / ± 0 . =
static const wchar_t* CALC_LABELS[20] = {
    L"C", L"⌫", L"%", L"÷",
    L"7", L"8", L"9", L"×",
    L"4", L"5", L"6", L"−",
    L"1", L"2", L"3", L"+",
    L"±", L"0", L".", L"="
};

static void HandleCalcButton(AppWindow* w, int btn) {
    const wchar_t* lbl = CALC_LABELS[btn];
    if (wcscmp(lbl, L"C") == 0) {
        wcscpy(w->calcDisplay, L"0");
        w->calcVal = 0; w->calcPrev = 0; w->calcOp = 0; w->calcNewNum = true;
    } else if (wcscmp(lbl, L"⌫") == 0) {
        int len = (int)wcslen(w->calcDisplay);
        if (len > 1) w->calcDisplay[len-1] = 0;
        else wcscpy(w->calcDisplay, L"0");
    } else if (wcscmp(lbl, L"±") == 0) {
        double v = _wtof(w->calcDisplay);
        swprintf(w->calcDisplay, 64, L"%g", -v);
    } else if (wcscmp(lbl, L"%") == 0) {
        double v = _wtof(w->calcDisplay);
        swprintf(w->calcDisplay, 64, L"%g", v / 100.0);
    } else if (wcscmp(lbl, L"÷")==0 || wcscmp(lbl, L"×")==0 || wcscmp(lbl, L"−")==0 || wcscmp(lbl, L"+")==0) {
        w->calcPrev = _wtof(w->calcDisplay);
        w->calcOp   = lbl[0];
        w->calcNewNum = true;
    } else if (wcscmp(lbl, L"=") == 0) {
        double cur = _wtof(w->calcDisplay);
        double res = w->calcPrev;
        if      (w->calcOp == L'+') res = w->calcPrev + cur;
        else if (w->calcOp == L'−') res = w->calcPrev - cur;
        else if (w->calcOp == L'×') res = w->calcPrev * cur;
        else if (w->calcOp == L'÷') res = (cur != 0) ? w->calcPrev / cur : 0;
        swprintf(w->calcDisplay, 64, L"%g", res);
        w->calcOp = 0; w->calcNewNum = true;
    } else {
        // digit or dot
        if (w->calcNewNum) {
            wcscpy(w->calcDisplay, lbl);
            w->calcNewNum = false;
        } else {
            if (wcscmp(lbl, L".") == 0 && wcschr(w->calcDisplay, L'.')) return;
            int len = (int)wcslen(w->calcDisplay);
            if (len < 16) wcscat(w->calcDisplay, lbl);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  TERMINAL COMMAND HANDLER
// ─────────────────────────────────────────────────────────────
static void TermAddLine(AppWindow* w, const wchar_t* line) {
    if (w->termCount >= MAX_TERMINAL_LINES) {
        memmove(w->termLines, w->termLines+1, sizeof(w->termLines[0])*(MAX_TERMINAL_LINES-1));
        w->termCount = MAX_TERMINAL_LINES - 1;
    }
    wcsncpy(w->termLines[w->termCount++], line, MAX_TERM_LINE_LEN-1);
}

static void HandleTerminalCommand(AppWindow* w) {
    wchar_t cmd[MAX_TERM_LINE_LEN];
    wcscpy(cmd, w->termInput);
    // echo
    wchar_t echo[MAX_TERM_LINE_LEN + 4];
    swprintf(echo, MAX_TERM_LINE_LEN + 4, L"> %s", cmd);
    TermAddLine(w, echo);

    // Trim
    while (cmd[0] == L' ') memmove(cmd, cmd+1, (wcslen(cmd))*sizeof(wchar_t));
    // lowercase compare
    wchar_t low[MAX_TERM_LINE_LEN];
    wcscpy(low, cmd);
    for (int i = 0; low[i]; i++) low[i] = towlower(low[i]);

    if (wcslen(low) == 0) {
    } else if (wcscmp(low, L"help") == 0) {
        TermAddLine(w, L"Команды: help, cls, time, ver, calc, notepad, game, explorer, taskmgr, settings, exit");
    } else if (wcscmp(low, L"cls") == 0) {
        w->termCount = 0;
    } else if (wcscmp(low, L"time") == 0) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t buf[64];
        swprintf(buf, 64, L"Время: %02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
        TermAddLine(w, buf);
    } else if (wcscmp(low, L"ver") == 0) {
        TermAddLine(w, L"Dexp OS Version 1.0.0 (Build 2025)");
    } else if (wcscmp(low, L"calc")     == 0) { PostMessage(g_hwnd, WM_OPEN_APP, APP_CALC, 0);
    } else if (wcscmp(low, L"notepad")  == 0) { PostMessage(g_hwnd, WM_OPEN_APP, APP_NOTEPAD, 0);
    } else if (wcscmp(low, L"game")     == 0) { PostMessage(g_hwnd, WM_OPEN_APP, APP_GAME, 0);
    } else if (wcscmp(low, L"explorer") == 0) { PostMessage(g_hwnd, WM_OPEN_APP, APP_EXPLORER, 0);
    } else if (wcscmp(low, L"taskmgr")  == 0) { PostMessage(g_hwnd, WM_OPEN_APP, APP_TASKMGR, 0);
    } else if (wcscmp(low, L"settings") == 0) { PostMessage(g_hwnd, WM_OPEN_APP, APP_SETTINGS, 0);
    } else if (wcscmp(low, L"exit")     == 0) {
        // find and close this window
        for (int i = 0; i < MAX_WINDOWS; i++)
            if (g_windows[i].active && &g_windows[i] == w) { CloseAppWindow(i); break; }
        return;
    } else {
        wchar_t err[128];
        swprintf(err, 128, L"Команда не найдена: %s", cmd);
        TermAddLine(w, err);
    }

    w->termInput[0] = 0;
}

// ─────────────────────────────────────────────────────────────
//  RENDERING — SPLASH
// ─────────────────────────────────────────────────────────────
static void RenderSplash(HDC hdc) {
    RECT full = {0, 0, MAIN_WIDTH, MAIN_HEIGHT};
    DrawGradientRect(hdc, full, RGB(0,0,30), RGB(0,0,60));

    // Logo
    int cx = MAIN_WIDTH / 2, cy = MAIN_HEIGHT / 2 - 40;
    wchar_t logo[] = L"DEXP OS";
    RECT rLogo = {cx-200, cy-40, cx+200, cy+40};
    TextCentered(hdc, logo, rLogo, RGB(0,180,255), 48, true);

    wchar_t sub[] = L"Загрузка системы...";
    RECT rSub = {cx-200, cy+50, cx+200, cy+80};
    TextCentered(hdc, sub, rSub, RGB(150,200,255), 18, false);

    // Progress bar
    int barW = 400, barH = 16;
    int bx = cx - barW/2, by = cy + 100;
    // Background
    HBRUSH brBack = CreateSolidBrush(RGB(30,30,60));
    RECT rBar = {bx, by, bx+barW, by+barH};
    FillRect(hdc, &rBar, brBack);
    DeleteObject(brBack);
    // Fill
    int fill = (g_splashStep * barW) / 100;
    HBRUSH brFill = CreateSolidBrush(RGB(0,150,255));
    RECT rFill = {bx, by, bx+fill, by+barH};
    FillRect(hdc, &rFill, brFill);
    DeleteObject(brFill);

    // Border
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0,100,200));
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, bx, by, bx+barW, by+barH);
    SelectObject(hdc, op); SelectObject(hdc, ob);
    DeleteObject(pen);

    wchar_t pct[16];
    swprintf(pct, 16, L"%d%%", g_splashStep);
    RECT rPct = {bx, by+20, bx+barW, by+40};
    TextCentered(hdc, pct, rPct, RGB(100,160,255), 14);
}

// ─────────────────────────────────────────────────────────────
//  RENDERING — DESKTOP
// ─────────────────────────────────────────────────────────────
static void RenderDesktop(HDC hdc) {
    RECT full = {0, 0, MAIN_WIDTH, DESKTOP_HEIGHT};
    DrawGradientRect(hdc, full, THEME.desktopBg1, THEME.desktopBg2);

    // Draw icons
    for (int i = 0; i < g_iconCount; i++) {
        DesktopIcon& ic = g_icons[i];
        int ix = ic.x, iy = ic.y;

        // Selection highlight
        if (ic.selected) {
            RECT sr = {ix-4, iy-4, ix+ICON_SIZE+4, iy+ICON_SIZE+24};
            HBRUSH selBr = CreateSolidBrush(RGB(0,120,215));
            FillRect(hdc, &sr, selBr);
            DeleteObject(selBr);
        }

        // Icon body
        if (ic.isTrash) {
            // Trash can
            HBRUSH br = CreateSolidBrush(g_trashCount > 0 ? RGB(220,80,40) : THEME.trashColor);
            RECT tr = {ix+10, iy+15, ix+ICON_SIZE-10, iy+ICON_SIZE-5};
            FillRect(hdc, &tr, br);
            DeleteObject(br);
            // Lid
            HBRUSH lid = CreateSolidBrush(g_trashCount > 0 ? RGB(255,100,60) : RGB(150,180,200));
            RECT lr = {ix+5, iy+8, ix+ICON_SIZE-5, iy+20};
            FillRect(hdc, &lr, lid);
            DeleteObject(lid);
            // Lines on trash
            for (int l = 0; l < 3; l++) {
                int lx = ix + 24 + l*12;
                MoveToEx(hdc, lx, iy+24, NULL);
                LineTo(hdc, lx, iy+ICON_SIZE-10);
            }
        } else if (ic.isFolder) {
            // Folder
            HBRUSH fBr = CreateSolidBrush(RGB(255,200,60));
            RECT fb = {ix+5, iy+18, ix+ICON_SIZE-5, iy+ICON_SIZE-5};
            FillRect(hdc, &fb, fBr);
            // Tab
            RECT tab = {ix+5, iy+10, ix+35, iy+22};
            FillRect(hdc, &tab, fBr);
            DeleteObject(fBr);
        } else {
            // File
            HBRUSH fBr = CreateSolidBrush(RGB(240,245,255));
            RECT fb = {ix+12, iy+5, ix+ICON_SIZE-12, iy+ICON_SIZE-5};
            FillRect(hdc, &fb, fBr);
            DeleteObject(fBr);
            // Folded corner
            HBRUSH corBr = CreateSolidBrush(RGB(180,195,220));
            POINT pts[3] = {{ix+ICON_SIZE-26, iy+5},{ix+ICON_SIZE-12, iy+19},{ix+ICON_SIZE-26, iy+19}};
            HPEN np = CreatePen(PS_SOLID, 1, RGB(150,165,190));
            HPEN nop = (HPEN)SelectObject(hdc, np);
            HBRUSH nob = (HBRUSH)SelectObject(hdc, corBr);
            Polygon(hdc, pts, 3);
            SelectObject(hdc, nop); SelectObject(hdc, nob);
            DeleteObject(corBr); DeleteObject(np);
            // Lines
            HPEN lp = CreatePen(PS_SOLID, 1, RGB(170,185,210));
            HPEN lop = (HPEN)SelectObject(hdc, lp);
            for (int l = 0; l < 4; l++) {
                int ly = iy+28+l*10;
                MoveToEx(hdc, ix+16, ly, NULL);
                LineTo(hdc, ix+ICON_SIZE-16, ly);
            }
            SelectObject(hdc, lop); DeleteObject(lp);
        }

        // Label (or rename box)
        if (g_renaming && g_renameIcon == i) {
            // Draw text input box
            RECT rr = {ix, iy+ICON_SIZE+2, ix+ICON_SIZE, iy+ICON_SIZE+22};
            HBRUSH editBr = CreateSolidBrush(RGB(255,255,255));
            FillRect(hdc, &rr, editBr);
            DeleteObject(editBr);
            HPEN ep = CreatePen(PS_SOLID, 2, THEME.accent);
            HPEN eop = (HPEN)SelectObject(hdc, ep);
            HBRUSH eob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rr.left, rr.top, rr.right, rr.bottom);
            SelectObject(hdc, eop); SelectObject(hdc, eob);
            DeleteObject(ep);
            TextCentered(hdc, g_renameText, rr, RGB(0,0,0), 13);
        } else {
            RECT lr = {ix, iy+ICON_SIZE+2, ix+ICON_SIZE, iy+ICON_SIZE+22};
            // Shadow
            RECT lrs = lr; lrs.left+=1; lrs.top+=1; lrs.right+=1; lrs.bottom+=1;
            TextCentered(hdc, ic.name, lrs, RGB(0,0,0), 13);
            TextCentered(hdc, ic.name, lr, RGB(255,255,255), 13);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  RENDERING — TASKBAR
// ─────────────────────────────────────────────────────────────
static void RenderTaskbar(HDC hdc) {
    RECT tr = {0, DESKTOP_HEIGHT, MAIN_WIDTH, MAIN_HEIGHT};
    GlassRect(hdc, tr, THEME.taskbarBg, 220);

    // Start button
    RECT sr = {0, DESKTOP_HEIGHT, 90, MAIN_HEIGHT};
    HBRUSH sbr = CreateSolidBrush(g_startOpen ? THEME.accent : THEME.buttonBg);
    FillRect(hdc, &sr, sbr);
    DeleteObject(sbr);
    TextCentered(hdc, L"⊞ Пуск", sr, THEME.taskbarText, 15, true);

    // Window buttons
    int bx = 94;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].active) continue;
        if (bx + 140 > MAIN_WIDTH - 120) break;
        RECT wr = {bx, DESKTOP_HEIGHT+4, bx+136, MAIN_HEIGHT-4};
        bool isTop = (i == g_topWindow);
        HBRUSH wbr = CreateSolidBrush(isTop ? THEME.accent : THEME.buttonBg);
        FillRect(hdc, &wr, wbr);
        DeleteObject(wbr);
        // Truncate title
        wchar_t ttl[40];
        wcsncpy(ttl, g_windows[i].title, 18);
        ttl[18] = 0;
        if (wcslen(g_windows[i].title) > 18) wcscat(ttl, L"…");
        TextCentered(hdc, ttl, wr, THEME.taskbarText, 13);
        bx += 140;
    }

    // Clock & date
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t timeBuf[16], dateBuf[20];
    swprintf(timeBuf, 16, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    swprintf(dateBuf, 20, L"%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear);
    RECT cr = {MAIN_WIDTH-130, DESKTOP_HEIGHT, MAIN_WIDTH, MAIN_HEIGHT};
    RECT cr2 = {MAIN_WIDTH-130, DESKTOP_HEIGHT, MAIN_WIDTH, DESKTOP_HEIGHT + TASKBAR_HEIGHT/2};
    RECT cr3 = {MAIN_WIDTH-130, DESKTOP_HEIGHT + TASKBAR_HEIGHT/2, MAIN_WIDTH, MAIN_HEIGHT};
    TextCentered(hdc, timeBuf, cr2, THEME.taskbarText, 14, true);
    TextCentered(hdc, dateBuf, cr3, THEME.taskbarText, 12);
}

// ─────────────────────────────────────────────────────────────
//  RENDERING — START MENU
// ─────────────────────────────────────────────────────────────
static const wchar_t* START_ITEMS[] = {
    L"🖩  Калькулятор",
    L"📝  Заметки",
    L"🎮  Мини-игра",
    L"💻  Терминал",
    L"📁  Проводник",
    L"📊  Диспетчер задач",
    L"⚙️  Настройки",
    L"⬛  CMD",
    L"🧩  Мои .dx приложения",
};
static int START_APP_IDS[] = {APP_CALC, APP_NOTEPAD, APP_GAME, APP_TERMINAL, APP_EXPLORER, APP_TASKMGR, APP_SETTINGS, APP_CMD, 0};
#define START_COUNT 9

static void RenderStartMenu(HDC hdc) {
    if (!g_startOpen) return;
    int mw = 320, mh = 480;
    int mx = 0, my = DESKTOP_HEIGHT - mh;

    GlassRect(hdc, {mx, my, mx+mw, my+mh}, THEME.startMenuBg, 235);

    // Header
    RECT hdr = {mx, my, mx+mw, my+50};
    HBRUSH hdrBr = CreateSolidBrush(THEME.accent);
    FillRect(hdc, &hdr, hdrBr);
    DeleteObject(hdrBr);
    TextCentered(hdc, L"DEXP OS", hdr, RGB(255,255,255), 20, true);

    int itemH = 42;
    for (int i = 0; i < START_COUNT; i++) {
        int iy = my + 54 + i * itemH;
        RECT ir = {mx, iy, mx+mw, iy+itemH};
        if (i == g_startHover) {
            HBRUSH hov = CreateSolidBrush(THEME.accent);
            FillRect(hdc, &ir, hov);
            DeleteObject(hov);
        }
        // separator line
        HPEN sep = CreatePen(PS_SOLID, 1, RGB(80,80,80));
        HPEN sep_old = (HPEN)SelectObject(hdc, sep);
        MoveToEx(hdc, mx+10, iy+itemH-1, NULL);
        LineTo(hdc, mx+mw-10, iy+itemH-1);
        SelectObject(hdc, sep_old); DeleteObject(sep);

        TextLeft(hdc, START_ITEMS[i], mx+16, iy+12, THEME.bodyText, 15);
    }
}

// ─────────────────────────────────────────────────────────────
//  RENDERING — WINDOW CHROME
// ─────────────────────────────────────────────────────────────
static void DrawWindowChrome(HDC hdc, int idx) {
    AppWindow& w = g_windows[idx];
    bool isTop = (idx == g_topWindow);

    // Apply animation scale around center
    float sc = w.animScale;
    int origX = w.x, origY = w.y, origW = w.w, origH = w.h;
    if (sc < 0.999f) {
        int cx = w.x + w.w/2, cy = w.y + w.h/2;
        int sw = (int)(w.w * sc), sh = (int)(w.h * sc);
        w.x = cx - sw/2; w.y = cy - sh/2; w.w = sw; w.h = sh;
    }

    // Shadow
    if (isTop) {
        for (int s = 6; s > 0; s--) {
            BYTE a = (BYTE)(10 * s);
            RECT sr = {w.x-s, w.y-s, w.x+w.w+s, w.y+w.h+s};
            GlassRect(hdc, sr, RGB(0,0,0), a);
        }
    }

    // Window body
    RECT body = {w.x, w.y + 34, w.x + w.w, w.y + w.h};
    GlassRect(hdc, body, THEME.bodyBg, g_glassEffect ? 200 : 255);

    // Title bar
    RECT title = {w.x, w.y, w.x + w.w, w.y + 34};
    HBRUSH tbr = CreateSolidBrush(isTop ? THEME.titleBg : RGB(
        (RGB_R(THEME.titleBg)+RGB_R(THEME.bodyBg))/2,
        (RGB_G(THEME.titleBg)+RGB_G(THEME.bodyBg))/2,
        (RGB_B(THEME.titleBg)+RGB_B(THEME.bodyBg))/2));
    FillRect(hdc, &title, tbr);
    DeleteObject(tbr);

    // Title text
    RECT ttr = {w.x + 10, w.y, w.x + w.w - 105, w.y + 34};
    TextCentered(hdc, w.title, ttr, THEME.titleText, 14, true);

    // Control buttons
    int bw = 32, by = w.y + 1, bh = 32;
    // Minimize
    RECT minR = {w.x + w.w - 99, by, w.x + w.w - 67, by + bh};
    HBRUSH minBr = CreateSolidBrush(RGB(255,190,0));
    FillRect(hdc, &minR, minBr); DeleteObject(minBr);
    TextCentered(hdc, L"─", minR, RGB(80,50,0), 14);
    // Maximize
    RECT maxR = {w.x + w.w - 66, by, w.x + w.w - 34, by + bh};
    HBRUSH maxBr = CreateSolidBrush(RGB(0,200,80));
    FillRect(hdc, &maxR, maxBr); DeleteObject(maxBr);
    TextCentered(hdc, w.state == WS_MAXIMIZED ? L"❐" : L"☐", maxR, RGB(0,50,10), 14);
    // Close
    RECT clsR = {w.x + w.w - 33, by, w.x + w.w - 1, by + bh};
    HBRUSH clsBr = CreateSolidBrush(RGB(220,50,40));
    FillRect(hdc, &clsR, clsBr); DeleteObject(clsBr);
    TextCentered(hdc, L"✕", clsR, RGB(255,255,255), 14);

    // Border
    HPEN border = CreatePen(PS_SOLID, 1, isTop ? THEME.accent : RGB(60,70,90));
    HPEN bop = (HPEN)SelectObject(hdc, border);
    HBRUSH bnull = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, w.x, w.y, w.x+w.w, w.y+w.h);
    SelectObject(hdc, bop); SelectObject(hdc, bnull);
    DeleteObject(border);

    // Resize handle (bottom-right triangle)
    if (w.state == WS_NORMAL) {
        HPEN rp = CreatePen(PS_SOLID, 1, THEME.accent);
        HPEN rop = (HPEN)SelectObject(hdc, rp);
        for (int d = 0; d < 3; d++) {
            MoveToEx(hdc, w.x+w.w-4-d*5, w.y+w.h-2, NULL);
            LineTo(hdc, w.x+w.w-2, w.y+w.h-4-d*5);
        }
        SelectObject(hdc, rop); DeleteObject(rp);
    }

    // Restore position
    w.x = origX; w.y = origY; w.w = origW; w.h = origH;
}

// ─────────────────────────────────────────────────────────────
//  APP BODY RENDERING
// ─────────────────────────────────────────────────────────────
static void RenderCalc(HDC hdc, AppWindow* w, RECT body) {
    // Display
    RECT dispR = {body.left+8, body.top+8, body.right-8, body.top+60};
    HBRUSH dispBr = CreateSolidBrush(THEME.darkMode ? RGB(20,20,20) : RGB(240,245,255));
    FillRect(hdc, &dispR, dispBr);
    DeleteObject(dispBr);
    // Right-align display
    HFONT df = CreateFont(28,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");
    HFONT odf = (HFONT)SelectObject(hdc, df);
    SetTextColor(hdc, THEME.bodyText);
    SetBkMode(hdc, TRANSPARENT);
    RECT dr2 = dispR; dr2.right -= 8;
    DrawText(hdc, w->calcDisplay, -1, &dr2, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(hdc, odf); DeleteObject(df);

    // Buttons grid 4x5
    int cols = 4, rows = 5;
    int bw = (body.right - body.left - 16) / cols;
    int bh = (body.bottom - body.top - 80) / rows;
    int startX = body.left + 8, startY = body.top + 68;
    for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
        int bi = r * cols + c;
        int bx = startX + c * bw, by2 = startY + r * bh;
        const wchar_t* lbl = CALC_LABELS[bi];
        bool isOp = (wcscmp(lbl,L"÷")==0||wcscmp(lbl,L"×")==0||wcscmp(lbl,L"−")==0||wcscmp(lbl,L"+")==0||wcscmp(lbl,L"=")==0);
        bool isFn = (wcscmp(lbl,L"C")==0||wcscmp(lbl,L"⌫")==0||wcscmp(lbl,L"%")==0||wcscmp(lbl,L"±")==0);
        COLORREF bgc = isOp ? THEME.accent : (isFn ? RGB(180,100,50) : THEME.buttonBg);
        DrawRoundRect(hdc, bx+2, by2+2, bw-4, bh-4, 6, bgc, THEME.accent);
        RECT br2 = {bx, by2, bx+bw, by2+bh};
        TextCentered(hdc, lbl, br2, RGB(255,255,255), 16, isOp);
    }
}

static void RenderNotes(HDC hdc, AppWindow* w, RECT body) {
    HBRUSH nb = CreateSolidBrush(THEME.darkMode ? RGB(25,25,35) : RGB(255,255,250));
    FillRect(hdc, &body, nb);
    DeleteObject(nb);

    // Render text with word wrap
    HFONT nf = CreateFont(16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    HFONT onf = (HFONT)SelectObject(hdc, nf);
    SetTextColor(hdc, THEME.bodyText);
    SetBkMode(hdc, TRANSPARENT);
    RECT tr = {body.left+8, body.top+8, body.right-8, body.bottom-8};
    DrawText(hdc, w->noteText, -1, &tr, DT_LEFT|DT_TOP|DT_WORDBREAK|DT_EDITCONTROL);

    // Cursor blink (simple — always show)
    // Find caret position by measuring text
    RECT cr2 = tr; cr2.right = cr2.left;
    DrawText(hdc, w->noteText, w->noteCaret, &cr2, DT_LEFT|DT_TOP|DT_WORDBREAK|DT_EDITCONTROL|DT_CALCRECT);
    int caretX = body.left + 8;
    int caretY = body.top + 8 + cr2.bottom - cr2.top;
    MoveToEx(hdc, caretX + cr2.right, caretY - 16, NULL);
    LineTo(hdc, caretX + cr2.right, caretY);

    SelectObject(hdc, onf); DeleteObject(nf);
}

static void RenderGame(HDC hdc, AppWindow* w, RECT body) {
    // Background
    HBRUSH gb = CreateSolidBrush(THEME.bodyBg);
    FillRect(hdc, &body, gb);
    DeleteObject(gb);

    int cx = (body.left + body.right) / 2;

    // Title
    RECT titR = {body.left, body.top+10, body.right, body.top+40};
    TextCentered(hdc, L"🎮 Кликер!", titR, THEME.accent, 22, true);

    // Score & goal
    wchar_t scoreTxt[64];
    swprintf(scoreTxt, 64, L"Счёт: %d  /  Цель: %d", w->gameScore, w->gameGoal);
    RECT scR = {body.left, body.top+45, body.right, body.top+70};
    TextCentered(hdc, scoreTxt, scR, THEME.bodyText, 16);

    // Timer
    wchar_t timeTxt[32];
    if (w->gameRunning) swprintf(timeTxt, 32, L"⏱ Время: %d сек", w->gameTimer);
    else if (w->gameTimer <= 0) wcscpy(timeTxt, L"⏱ Время вышло!");
    else wcscpy(timeTxt, L"Нажми кнопку чтобы начать!");
    RECT tmR = {body.left, body.top+72, body.right, body.top+96};
    TextCentered(hdc, timeTxt, tmR, w->gameTimer<=0 ? RGB(220,60,40) : THEME.bodyText, 15);

    // Big button
    int bw = 200, bh = 70;
    int bx = cx - bw/2, by2 = body.top + 110;
    COLORREF btnColor = w->gameRunning ? THEME.accent : RGB(100,100,120);
    DrawRoundRect(hdc, bx, by2, bw, bh, 14, btnColor, THEME.accent);
    RECT btnR = {bx, by2, bx+bw, by2+bh};
    TextCentered(hdc, L"НАЖМИ МЕНЯ!", btnR, RGB(255,255,255), 18, true);

    // Progress to goal
    if (w->gameRunning) {
        int pbarW = body.right - body.left - 40;
        int pbarX = body.left + 20;
        int pbarY = body.top + 200;
        RECT pb = {pbarX, pbarY, pbarX+pbarW, pbarY+14};
        HBRUSH pbb = CreateSolidBrush(RGB(50,50,70));
        FillRect(hdc, &pb, pbb); DeleteObject(pbb);
        int prog = w->gameGoal > 0 ? (w->gameScore * pbarW / w->gameGoal) : 0;
        if (prog > pbarW) prog = pbarW;
        RECT pf = {pbarX, pbarY, pbarX+prog, pbarY+14};
        HBRUSH pfb = CreateSolidBrush(THEME.accent);
        FillRect(hdc, &pf, pfb); DeleteObject(pfb);
    }

    // Restart hint
    if (w->gameTimer <= 0 && !w->gameRunning) {
        RECT rh = {body.left, body.top+230, body.right, body.top+260};
        TextCentered(hdc, L"Нажми кнопку для новой игры", rh, THEME.bodyText, 14);
    }
}

static void RenderTerminal(HDC hdc, AppWindow* w, RECT body) {
    HBRUSH tb = CreateSolidBrush(RGB(12,12,18));
    FillRect(hdc, &body, tb);
    DeleteObject(tb);

    HFONT tf = CreateFont(15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH, L"Consolas");
    HFONT otf = (HFONT)SelectObject(hdc, tf);
    SetBkMode(hdc, TRANSPARENT);

    int lineH = 18;
    int visLines = (body.bottom - body.top - 32) / lineH;
    int start = w->termCount > visLines ? w->termCount - visLines : 0;

    for (int i = start; i < w->termCount; i++) {
        int ly = body.top + 6 + (i - start) * lineH;
        // Color for prompt lines
        COLORREF lc = RGB(180,220,180);
        if (w->termLines[i][0] == L'>') lc = RGB(100,200,255);
        else if (wcsstr(w->termLines[i], L"не найдена")) lc = RGB(255,100,100);
        SetTextColor(hdc, lc);
        TextOut(hdc, body.left+8, ly, w->termLines[i], (int)wcslen(w->termLines[i]));
    }

    // Input line
    RECT inputR = {body.left, body.bottom - 28, body.right, body.bottom};
    HBRUSH ib = CreateSolidBrush(RGB(20,30,20));
    FillRect(hdc, &inputR, ib);
    DeleteObject(ib);

    wchar_t prompt[MAX_TERM_LINE_LEN + 4];
    swprintf(prompt, MAX_TERM_LINE_LEN + 4, L"$ %s_", w->termInput);
    SetTextColor(hdc, RGB(0,255,100));
    TextOut(hdc, body.left+8, body.bottom-24, prompt, (int)wcslen(prompt));

    SelectObject(hdc, otf); DeleteObject(tf);
}

static void RenderExplorer(HDC hdc, AppWindow* w, RECT body) {
    HBRUSH eb = CreateSolidBrush(THEME.darkMode ? RGB(20,22,30) : RGB(245,248,255));
    FillRect(hdc, &body, eb);
    DeleteObject(eb);

    // Path bar
    RECT pathR = {body.left, body.top, body.right, body.top+30};
    HBRUSH pb = CreateSolidBrush(THEME.darkMode ? RGB(30,35,50) : RGB(225,232,250));
    FillRect(hdc, &pathR, pb);
    DeleteObject(pb);
    TextLeft(hdc, w->explorerPath, body.left+8, body.top+7, THEME.bodyText, 13);

    // Items
    int itemH = 28;
    for (int i = 0; i < w->explorerCount; i++) {
        int iy = body.top + 36 + i * itemH;
        if (iy + itemH > body.bottom) break;
        if (i % 2 == 0) {
            RECT stripe = {body.left, iy, body.right, iy+itemH};
            HBRUSH sb = CreateSolidBrush(THEME.darkMode ? RGB(28,30,42) : RGB(238,243,255));
            FillRect(hdc, &stripe, sb);
            DeleteObject(sb);
        }
        wchar_t label[MAX_PATH + 4];
        swprintf(label, MAX_PATH+4, L"%s %s", w->explorerIsDir[i] ? L"📁" : L"📄", w->explorerItems[i]);
        TextLeft(hdc, label, body.left+10, iy+6, THEME.bodyText, 14);
    }
    if (w->explorerCount == 0) {
        RECT empty = body;
        TextCentered(hdc, L"Папка пуста", empty, THEME.bodyText, 16);
    }
}

static void RenderTaskMgr(HDC hdc, AppWindow* w, RECT body) {
    HBRUSH bg = CreateSolidBrush(THEME.bodyBg);
    FillRect(hdc, &body, bg);
    DeleteObject(bg);

    // Header
    RECT hdr = {body.left, body.top, body.right, body.top+26};
    HBRUSH hb = CreateSolidBrush(THEME.titleBg);
    FillRect(hdc, &hdr, hb); DeleteObject(hb);
    TextLeft(hdc, L"Имя процесса", body.left+10, body.top+5, THEME.titleText, 13, true);
    TextLeft(hdc, L"PID",    body.left+200, body.top+5, THEME.titleText, 13, true);
    TextLeft(hdc, L"RAM",    body.left+280, body.top+5, THEME.titleText, 13, true);
    TextLeft(hdc, L"CPU",    body.left+370, body.top+5, THEME.titleText, 13, true);

    static int fakePIDs[MAX_WINDOWS] = {0};
    static int fakeRAM[MAX_WINDOWS]  = {0};
    srand(42);
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!fakePIDs[i]) fakePIDs[i] = 1000 + rand()%8000;
        if (!fakeRAM[i])  fakeRAM[i]  = 12 + rand()%120;
    }

    int row = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_windows[i].active) continue;
        int iy = body.top + 30 + row * 24;
        if (iy + 24 > body.bottom) break;
        if (row % 2 == 0) {
            RECT sr = {body.left, iy, body.right, iy+24};
            HBRUSH sb = CreateSolidBrush(THEME.darkMode ? RGB(35,38,52) : RGB(242,246,255));
            FillRect(hdc, &sr, sb); DeleteObject(sb);
        }
        wchar_t buf[64];
        TextLeft(hdc, g_windows[i].title, body.left+10, iy+4, THEME.bodyText, 13);
        swprintf(buf, 64, L"%d", fakePIDs[i]);
        TextLeft(hdc, buf, body.left+200, iy+4, THEME.bodyText, 13);
        swprintf(buf, 64, L"%d MB", fakeRAM[i]);
        TextLeft(hdc, buf, body.left+280, iy+4, THEME.bodyText, 13);
        swprintf(buf, 64, L"%.1f%%", (fakePIDs[i] % 50) * 0.1f);
        TextLeft(hdc, buf, body.left+370, iy+4, THEME.bodyText, 13);
        row++;
    }
    if (row == 0) {
        RECT er = body;
        TextCentered(hdc, L"Нет активных процессов", er, THEME.bodyText, 15);
    }
}

static const wchar_t* THEME_NAMES[4] = {L"🌊 Океан", L"🌑 Тёмная", L"☀️ Светлая", L"💜 Фиолет"};

static void RenderSettings(HDC hdc, AppWindow* w, RECT body) {
    HBRUSH bg = CreateSolidBrush(THEME.bodyBg);
    FillRect(hdc, &body, bg);
    DeleteObject(bg);

    TextLeft(hdc, L"Тема оформления", body.left+16, body.top+14, THEME.bodyText, 16, true);

    // Theme buttons 2x2
    int tw = 160, th = 50, margin = 12;
    for (int i = 0; i < 4; i++) {
        int col = i % 2, row = i / 2;
        int tx = body.left + margin + col * (tw + margin);
        int ty = body.top + 44 + row * (th + margin);
        bool sel = (i == g_themeIdx);
        bool hov = (i == w->settingsThemeHover);
        COLORREF bc = sel ? THEME.accent : (hov ? THEME.buttonBg : THEME.darkMode ? RGB(50,52,65) : RGB(210,218,240));
        DrawRoundRect(hdc, tx, ty, tw, th, 10, bc, THEME.accent);
        RECT tr2 = {tx, ty, tx+tw, ty+th};
        TextCentered(hdc, THEME_NAMES[i], tr2, sel ? RGB(255,255,255) : THEME.bodyText, 14, sel);
    }

    // Toggles
    int ty2 = body.top + 180;
    TextLeft(hdc, L"Стеклянный эффект:", body.left+16, ty2, THEME.bodyText, 14);
    RECT glassBtn = {body.left+220, ty2-4, body.left+320, ty2+26};
    DrawRoundRect(hdc, glassBtn.left, glassBtn.top, 100, 30, 8,
        g_glassEffect ? RGB(0,200,80) : RGB(180,50,50), THEME.accent);
    TextCentered(hdc, g_glassEffect ? L"Вкл" : L"Выкл", glassBtn, RGB(255,255,255), 14, true);

    ty2 += 44;
    TextLeft(hdc, L"Анимации:", body.left+16, ty2, THEME.bodyText, 14);
    RECT animBtn = {body.left+220, ty2-4, body.left+320, ty2+26};
    DrawRoundRect(hdc, animBtn.left, animBtn.top, 100, 30, 8,
        g_animations ? RGB(0,200,80) : RGB(180,50,50), THEME.accent);
    TextCentered(hdc, g_animations ? L"Вкл" : L"Выкл", animBtn, RGB(255,255,255), 14, true);

    ty2 += 60;
    TextLeft(hdc, L"О системе:", body.left+16, ty2, THEME.bodyText, 14, true);
    ty2 += 22;
    TextLeft(hdc, L"Dexp OS v1.0  •  WinAPI/GDI  •  64-bit", body.left+16, ty2, THEME.bodyText, 13);
}

static void RenderTrash(HDC hdc, AppWindow* w, RECT body) {
    HBRUSH bg = CreateSolidBrush(THEME.bodyBg);
    FillRect(hdc, &body, bg);
    DeleteObject(bg);

    TextLeft(hdc, L"Корзина", body.left+16, body.top+10, THEME.bodyText, 16, true);

    if (g_trashCount == 0) {
        RECT er = {body.left, body.top+50, body.right, body.bottom};
        TextCentered(hdc, L"Корзина пуста", er, THEME.bodyText, 15);
    } else {
        for (int i = 0; i < g_trashCount; i++) {
            int iy = body.top + 40 + i * 28;
            if (iy + 28 > body.bottom - 40) break;
            TextLeft(hdc, g_trash[i].name, body.left+10, iy+4, THEME.bodyText, 13);
            // Restore button
            RECT rb = {body.right-180, iy, body.right-90, iy+24};
            DrawRoundRect(hdc, rb.left, rb.top, 90, 24, 6, RGB(0,150,80), RGB(0,200,100));
            TextCentered(hdc, L"Восстановить", rb, RGB(255,255,255), 12);
        }
    }

    // Empty Trash button
    RECT eb = {body.left+16, body.bottom-36, body.left+180, body.bottom-8};
    DrawRoundRect(hdc, eb.left, eb.top, 164, 28, 8, RGB(200,50,40), RGB(255,80,60));
    TextCentered(hdc, L"🗑 Очистить корзину", eb, RGB(255,255,255), 13, true);
}

// ─────────────────────────────────────────────────────────────
//  WINDOW RENDERING DISPATCH
// ─────────────────────────────────────────────────────────────
static void RenderOneWindow(HDC hdc, int idx) {
    AppWindow& w = g_windows[idx];
    if (!w.active) return;
    if (w.state == WS_MINIMIZED) return;

    // Scale for animation
    float sc = w.animScale;
    int ox = w.x, oy = w.y, ow = w.w, oh = w.h;
    if (sc < 0.999f) {
        int cx = w.x + w.w/2, cy = w.y + w.h/2;
        w.x = cx - (int)(w.w*sc/2); w.y = cy - (int)(w.h*sc/2);
        w.w = (int)(w.w*sc); w.h = (int)(w.h*sc);
    }

    DrawWindowChrome(hdc, idx);

    RECT body = {w.x, w.y + 34, w.x + w.w, w.y + w.h};

    if (sc >= 0.5f) {
        // Clip content to body
        HRGN clip = CreateRectRgn(body.left, body.top, body.right, body.bottom);
        SelectClipRgn(hdc, clip);

        switch (w.appId) {
        case APP_CALC:     RenderCalc(hdc, &w, body);     break;
        case APP_NOTEPAD:  RenderNotes(hdc, &w, body);    break;
        case APP_GAME:     RenderGame(hdc, &w, body);     break;
        case APP_TERMINAL:
        case APP_CMD:      RenderTerminal(hdc, &w, body); break;
        case APP_EXPLORER: RenderExplorer(hdc, &w, body); break;
        case APP_TASKMGR:  RenderTaskMgr(hdc, &w, body); break;
        case APP_SETTINGS: RenderSettings(hdc, &w, body); break;
        default:
            // Trash window
            RenderTrash(hdc, &w, body);
            break;
        }

        SelectClipRgn(hdc, NULL);
        DeleteObject(clip);
    }

    // Restore
    w.x = ox; w.y = oy; w.w = ow; w.h = oh;
}

static void RenderWindows(HDC hdc) {
    // Draw non-top windows first, then top
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].active && i != g_topWindow) RenderOneWindow(hdc, i);
    if (g_topWindow >= 0 && g_windows[g_topWindow].active) RenderOneWindow(hdc, g_topWindow);
}

// ─────────────────────────────────────────────────────────────
//  FULL FRAME
// ─────────────────────────────────────────────────────────────
static void RenderFrame(HDC hdc) {
    // Off-screen buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, MAIN_WIDTH, MAIN_HEIGHT);
    HBITMAP ob = (HBITMAP)SelectObject(memDC, bm);

    if (g_splash) {
        RenderSplash(memDC);
    } else {
        RenderDesktop(memDC);
        RenderWindows(memDC);
        RenderTaskbar(memDC);
        RenderStartMenu(memDC);
    }

    BitBlt(hdc, 0, 0, MAIN_WIDTH, MAIN_HEIGHT, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, ob);
    DeleteObject(bm);
    DeleteDC(memDC);
}

// ─────────────────────────────────────────────────────────────
//  MOUSE HIT TESTS (windows)
// ─────────────────────────────────────────────────────────────
static int HitTestWindow(int x, int y) {
    // Top window first
    if (g_topWindow >= 0) {
        AppWindow& w = g_windows[g_topWindow];
        if (w.active && w.state != WS_MINIMIZED &&
            x >= w.x && x <= w.x+w.w && y >= w.y && y <= w.y+w.h) return g_topWindow;
    }
    for (int i = MAX_WINDOWS-1; i >= 0; i--) {
        if (!g_windows[i].active || g_windows[i].state == WS_MINIMIZED) continue;
        AppWindow& w = g_windows[i];
        if (x >= w.x && x <= w.x+w.w && y >= w.y && y <= w.y+w.h) return i;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────
//  MOUSE DOWN HANDLER
// ─────────────────────────────────────────────────────────────
static void HandleLButtonDown(int x, int y, bool dbl) {
    // Close start menu if clicking elsewhere
    if (g_startOpen) {
        // Check if in start menu
        int mw = 320, mh = 480, mx = 0, my = DESKTOP_HEIGHT - mh;
        if (x >= mx && x <= mx+mw && y >= my && y <= my+mh) {
            // Clicked start item?
            if (y >= my+54) {
                int itemH = 42;
                int idx = (y - (my+54)) / itemH;
                if (idx >= 0 && idx < START_COUNT) {
                    g_startOpen = false;
                    if (START_APP_IDS[idx] != 0) OpenApp(START_APP_IDS[idx]);
                    InvalidateRect(g_hwnd, NULL, FALSE);
                }
            }
            return;
        }
        g_startOpen = false;
    }

    // Start button
    if (x >= 0 && x <= 90 && y >= DESKTOP_HEIGHT && y <= MAIN_HEIGHT) {
        g_startOpen = !g_startOpen;
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    // Taskbar window buttons
    if (y >= DESKTOP_HEIGHT && y < MAIN_HEIGHT) {
        int bx = 94;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!g_windows[i].active) continue;
            if (x >= bx && x <= bx+136) {
                AppWindow& w = g_windows[i];
                if (w.state == WS_MINIMIZED) {
                    w.state = WS_NORMAL;
                    BringToFront(i);
                } else if (i == g_topWindow) {
                    w.state = WS_MINIMIZED;
                } else {
                    BringToFront(i);
                }
                InvalidateRect(g_hwnd, NULL, FALSE);
                return;
            }
            bx += 140;
        }
        return;
    }

    // Desktop area — hit test windows
    int wIdx = HitTestWindow(x, y);
    if (wIdx >= 0) {
        AppWindow& w = g_windows[wIdx];
        BringToFront(wIdx);

        // Title bar buttons
        int bby = w.y + 1;
        RECT minR = {w.x + w.w - 99, bby, w.x + w.w - 67, bby + 32};
        RECT maxR = {w.x + w.w - 66, bby, w.x + w.w - 34, bby + 32};
        RECT clsR = {w.x + w.w - 33, bby, w.x + w.w - 1,  bby + 32};

        if (PtInRect2(clsR, x, y)) {
            if (g_animations) { w.closing = true; w.animScale = 1.0f; }
            else CloseAppWindow(wIdx);
        } else if (PtInRect2(minR, x, y)) {
            w.state = WS_MINIMIZED;
        } else if (PtInRect2(maxR, x, y)) {
            if (w.state == WS_MAXIMIZED) {
                w.state = WS_NORMAL;
                w.x = w.restoreX; w.y = w.restoreY; w.w = w.restoreW; w.h = w.restoreH;
            } else {
                w.restoreX = w.x; w.restoreY = w.y; w.restoreW = w.w; w.restoreH = w.h;
                w.x = 0; w.y = 0; w.w = MAIN_WIDTH; w.h = DESKTOP_HEIGHT;
                w.state = WS_MAXIMIZED;
            }
        } else {
            // Title bar drag
            RECT title = {w.x, w.y, w.x + w.w - 100, w.y + 34};
            if (PtInRect2(title, x, y) && w.state != WS_MAXIMIZED) {
                w.dragging = true;
                w.dragOffX = x - w.x;
                w.dragOffY = y - w.y;
                SetCapture(g_hwnd);
            }
            // Resize handle (bottom-right 18x18)
            RECT resR = {w.x+w.w-18, w.y+w.h-18, w.x+w.w, w.y+w.h};
            if (PtInRect2(resR, x, y) && w.state == WS_NORMAL) {
                w.resizing = true;
                w.resizeStartX = x; w.resizeStartY = y;
                w.resizeStartW = w.w; w.resizeStartH = w.h;
                SetCapture(g_hwnd);
            }

            // App-specific click handling
            RECT body = {w.x, w.y+34, w.x+w.w, w.y+w.h};

            if (w.appId == APP_CALC) {
                int cols = 4, rows = 5;
                int bw2 = (w.w - 16) / cols;
                int bh2 = (w.h - 80) / rows;
                int sX = w.x + 8, sY = w.y + 68 + 34;
                if (x >= sX && x <= sX + cols*bw2 && y >= sY && y <= sY + rows*bh2) {
                    int col = (x - sX) / bw2;
                    int row2 = (y - sY) / bh2;
                    HandleCalcButton(&w, row2*4 + col);
                }
            } else if (w.appId == APP_GAME) {
                // Big button click
                int cx2 = (body.left + body.right)/2;
                int bw3 = 200, bh3 = 70, bx2 = cx2-100, by3 = body.top+110;
                RECT btnR = {bx2, by3, bx2+bw3, by3+bh3};
                if (PtInRect2(btnR, x, y)) {
                    if (!w.gameRunning || w.gameTimer <= 0) {
                        // Start/restart
                        w.gameScore = 0; w.gameGoal = 10; w.gameTimer = 30; w.gameRunning = true;
                        SetTimer(g_hwnd, TIMER_GAME, 1000, NULL);
                    } else {
                        w.gameScore++;
                        if (w.gameScore >= w.gameGoal) {
                            w.gameGoal += 10;
                            w.gameTimer += 10;
                        }
                    }
                }
            } else if (w.appId == APP_EXPLORER && dbl) {
                // Double click on item
                int itemH = 28;
                int idx2 = (y - body.top - 36) / itemH;
                if (idx2 >= 0 && idx2 < w.explorerCount) {
                    wchar_t fullPath[MAX_PATH];
                    swprintf(fullPath, MAX_PATH, L"%s\\%s", w.explorerPath, w.explorerItems[idx2]);
                    if (w.explorerIsDir[idx2]) {
                        ExplorerNavigate(&w, fullPath);
                    } else {
                        wchar_t cmdLine[MAX_PATH];
swprintf(cmdLine, MAX_PATH, L"cmd.exe /c start \"\" \"%s\"", fullPath);
_wspawnl(_P_NOWAIT, L"cmd.exe", L"cmd.exe", L"/c", L"start", L"", fullPath, NULL);
                    }
                }
            } else if (w.appId == APP_SETTINGS) {
                // Theme buttons
                int tw = 160, th = 50, margin = 12;
                for (int i = 0; i < 4; i++) {
                    int col = i%2, row2 = i/2;
                    int tx = body.left + margin + col*(tw+margin);
                    int ty = body.top + 44 + row2*(th+margin);
                    RECT tr2 = {tx, ty, tx+tw, ty+th};
                    if (PtInRect2(tr2, x, y)) { g_themeIdx = i; }
                }
                // Glass toggle
                RECT glassBtn = {body.left+220, body.top+176, body.left+320, body.top+206};
                if (PtInRect2(glassBtn, x, y)) g_glassEffect = !g_glassEffect;
                // Anim toggle
                RECT animBtn = {body.left+220, body.top+220, body.left+320, body.top+250};
                if (PtInRect2(animBtn, x, y)) g_animations = !g_animations;
            } else if (w.appId == 0 || (w.appId != APP_CALC && w.appId != APP_NOTEPAD &&
                       w.appId != APP_GAME && w.appId != APP_TERMINAL && w.appId != APP_CMD &&
                       w.appId != APP_EXPLORER && w.appId != APP_TASKMGR && w.appId != APP_SETTINGS)) {
                // Trash window
                // Restore buttons
                int bby2 = body.top + 40;
                for (int i = 0; i < g_trashCount; i++) {
                    int iy = bby2 + i * 28;
                    RECT rb = {body.right-180, iy, body.right-90, iy+24};
                    if (PtInRect2(rb, x, y)) { RestoreTrash(i); break; }
                }
                // Empty trash
                RECT eb = {body.left+16, body.bottom-36, body.left+180, body.bottom-8};
                if (PtInRect2(eb, x, y)) EmptyTrash();
            }
        }
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    }

    // Desktop icon click
    if (!g_renaming) {
        // Deselect all
        for (int i = 0; i < g_iconCount; i++) g_icons[i].selected = false;
        int icon = HitTestIcon(x, y);
        if (icon >= 0) {
            g_icons[icon].selected = true;
            g_selIcon = icon;
            if (dbl) {
                // Open
                DesktopIcon& ic = g_icons[icon];
                if (ic.isTrash) {
                    // Open trash window
                    // Reuse APP_EXPLORER slot with special title
                    int ti = -1;
                    for (int i = 0; i < MAX_WINDOWS; i++) if (!g_windows[i].active) { ti = i; break; }
                    if (ti >= 0) {
                        AppWindow& tw2 = g_windows[ti];
                        memset(&tw2, 0, sizeof(AppWindow));
                        tw2.active = true;
                        tw2.appId  = 99; // trash
                        wcscpy(tw2.title, L"Корзина");
                        tw2.x = 200; tw2.y = 100; tw2.w = 520; tw2.h = 380;
                        tw2.animScale = g_animations ? 0.01f : 1.0f;
                        tw2.restoreX = tw2.x; tw2.restoreY = tw2.y;
                        tw2.restoreW = tw2.w; tw2.restoreH = tw2.h;
                        BringToFront(ti);
                        LoadTrash();
                    }
                } else if (ic.isFolder) {
                    OpenApp(APP_EXPLORER, ic.path);
                } else {
                    // Open file in notepad
                    int ni = FindWindow_ByApp(APP_NOTEPAD);
                    if (ni < 0) {
                        OpenApp(APP_NOTEPAD);
                        ni = FindWindow_ByApp(APP_NOTEPAD);
                    }
                    if (ni >= 0) {
                        // Load file content
                        HANDLE fh = CreateFile(ic.path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                        if (fh != INVALID_HANDLE_VALUE) {
                            char buf[MAX_NOTES_LEN];
                            DWORD read = 0;
                            ReadFile(fh, buf, MAX_NOTES_LEN-1, &read, NULL);
                            buf[read] = 0;
                            CloseHandle(fh);
                            MultiByteToWideChar(CP_UTF8, 0, buf, -1, g_windows[ni].noteText, MAX_NOTES_LEN);
                        }
                        BringToFront(ni);
                    }
                }
            }
        } else {
            g_selIcon = -1;
        }
        InvalidateRect(g_hwnd, NULL, FALSE);
    } else {
        // Commit rename
        if (g_renameIcon >= 0 && g_renameIcon < g_iconCount) {
            wchar_t newPath[MAX_PATH];
            swprintf(newPath, MAX_PATH, L"%s\\%s", g_desktopPath, g_renameText);
            MoveFile(g_icons[g_renameIcon].path, newPath);
            RefreshIcons();
        }
        g_renaming = false;
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

// ─────────────────────────────────────────────────────────────
//  MAIN WINDOW PROC
// ─────────────────────────────────────────────────────────────
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, TIMER_SPLASH, 20, NULL);  // Splash progress
        SetTimer(hwnd, TIMER_CLOCK,  1000, NULL); // Clock
        SetTimer(hwnd, TIMER_ANIMATE, 16, NULL);  // 60fps animation
        return 0;

    case WM_TIMER:
        if (wp == TIMER_SPLASH && g_splash) {
            g_splashStep += 2;
            if (g_splashStep >= 100) {
                g_splash = false;
                KillTimer(hwnd, TIMER_SPLASH);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == TIMER_CLOCK) {
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == TIMER_ANIMATE) {
            bool needRedraw = false;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (!g_windows[i].active) continue;
                AppWindow& w = g_windows[i];
                if (!w.closing && w.animScale < 1.0f) {
                    w.animScale += 0.08f;
                    if (w.animScale > 1.0f) w.animScale = 1.0f;
                    needRedraw = true;
                }
                if (w.closing) {
                    w.animScale -= 0.12f;
                    if (w.animScale <= 0.0f) { CloseAppWindow(i); }
                    needRedraw = true;
                }
            }
            if (needRedraw) InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == TIMER_GAME) {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (!g_windows[i].active || g_windows[i].appId != APP_GAME) continue;
                AppWindow& w = g_windows[i];
                if (w.gameRunning && w.gameTimer > 0) {
                    w.gameTimer--;
                    if (w.gameTimer <= 0) { w.gameRunning = false; KillTimer(hwnd, TIMER_GAME); }
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RenderFrame(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        HandleLButtonDown(x, y, false);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        HandleLButtonDown(x, y, true);
        return 0;
    }

    case WM_LBUTTONUP:
        for (int i = 0; i < MAX_WINDOWS; i++) {
            g_windows[i].dragging = false;
            g_windows[i].resizing = false;
        }
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        bool moved = false;

        // Dragging
        for (int i = 0; i < MAX_WINDOWS; i++) {
            AppWindow& w = g_windows[i];
            if (!w.active) continue;
            if (w.dragging) {
                int nx = x - w.dragOffX, ny = y - w.dragOffY;
                // Clamp
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx + w.w > MAIN_WIDTH) nx = MAIN_WIDTH - w.w;
                if (ny + w.h > DESKTOP_HEIGHT) ny = DESKTOP_HEIGHT - w.h;
                w.x = nx; w.y = ny;
                w.restoreX = nx; w.restoreY = ny;
                moved = true;
            }
            if (w.resizing) {
                int dx = x - w.resizeStartX, dy = y - w.resizeStartY;
                int nw = w.resizeStartW + dx, nh = w.resizeStartH + dy;
                if (nw < 200) nw = 200;
                if (nh < 150) nh = 150;
                if (w.x + nw > MAIN_WIDTH) nw = MAIN_WIDTH - w.x;
                if (w.y + nh > DESKTOP_HEIGHT) nh = DESKTOP_HEIGHT - w.y;
                w.w = nw; w.h = nh;
                w.restoreW = nw; w.restoreH = nh;
                moved = true;
            }
        }

        // Start menu hover
        if (g_startOpen) {
            int mh = 480, mx2 = 0, my2 = DESKTOP_HEIGHT - mh;
            if (x >= mx2 && x <= mx2 + 320 && y >= my2 + 54) {
                int itemH = 42;
                int hi = (y - (my2+54)) / itemH;
                if (hi >= 0 && hi < START_COUNT) {
                    if (g_startHover != hi) { g_startHover = hi; moved = true; }
                } else { g_startHover = -1; }
            } else { g_startHover = -1; }
        }

        // Settings hover
        int wIdx = HitTestWindow(x, y);
        if (wIdx >= 0 && g_windows[wIdx].appId == APP_SETTINGS) {
            AppWindow& sw = g_windows[wIdx];
            RECT body = {sw.x, sw.y+34, sw.x+sw.w, sw.y+sw.h};
            int tw = 160, th = 50, margin = 12;
            int hov = -1;
            for (int i = 0; i < 4; i++) {
                int col = i%2, row2 = i/2;
                int tx = body.left + margin + col*(tw+margin);
                int ty2 = body.top + 44 + row2*(th+margin);
                RECT tr2 = {tx, ty2, tx+tw, ty2+th};
                if (PtInRect2(tr2, x, y)) { hov = i; break; }
            }
            if (sw.settingsThemeHover != hov) { sw.settingsThemeHover = hov; moved = true; }
        }

        if (moved) InvalidateRect(g_hwnd, NULL, FALSE);
        return 0;
    }

    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (g_splash) return 0;

        // Stop rename if active
        if (g_renaming) { g_renaming = false; }

        // Check if over window
        int wIdx = HitTestWindow(x, y);
        if (wIdx >= 0) return 0;

        // Check if over icon
        int icon = HitTestIcon(x, y);
        g_ctxIcon = icon;

        HMENU menu = CreatePopupMenu();
        if (icon < 0) {
            AppendMenu(menu, MF_STRING, CM_NEW_FOLDER, L"📁 Создать папку");
            AppendMenu(menu, MF_STRING, CM_NEW_FILE,   L"📄 Создать текстовый файл");
            AppendMenu(menu, MF_SEPARATOR, 0, NULL);
            AppendMenu(menu, MF_STRING, CM_REFRESH,    L"🔄 Обновить");
            AppendMenu(menu, MF_STRING, CM_SETTINGS,   L"⚙️ Настройки");
        } else {
            AppendMenu(menu, MF_STRING, CM_OPEN,   L"📂 Открыть");
            AppendMenu(menu, MF_STRING, CM_RENAME, L"✏️ Переименовать");
            if (!g_icons[icon].isTrash) {
                AppendMenu(menu, MF_STRING, CM_DELETE, L"🗑 Удалить в корзину");
                AppendMenu(menu, MF_STRING, CM_COPY,   L"📋 Копировать");
            }
        }

        POINT pt = {x, y};
        ClientToScreen(hwnd, &pt);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(menu);

        switch (cmd) {
        case CM_NEW_FOLDER: CreateNewFolder(); break;
        case CM_NEW_FILE:   CreateNewFile();   break;
        case CM_REFRESH:    RefreshIcons();    break;
        case CM_SETTINGS:   OpenApp(APP_SETTINGS); break;
        case CM_OPEN:
            if (g_ctxIcon >= 0 && g_ctxIcon < g_iconCount) {
                DesktopIcon& ic = g_icons[g_ctxIcon];
                if (ic.isTrash) {
                    // open trash window (reuse double-click logic)
                } else if (ic.isFolder) {
                    OpenApp(APP_EXPLORER, ic.path);
                } else {
                    int ni = FindWindow_ByApp(APP_NOTEPAD);
                    if (ni < 0) { OpenApp(APP_NOTEPAD); ni = FindWindow_ByApp(APP_NOTEPAD); }
                    if (ni >= 0) BringToFront(ni);
                }
            }
            break;
        case CM_RENAME:
            if (g_ctxIcon >= 0) {
                g_renaming = true;
                g_renameIcon = g_ctxIcon;
                wcscpy(g_renameText, g_icons[g_ctxIcon].name);
                g_renameCaret = (int)wcslen(g_renameText);
            }
            break;
        case CM_DELETE:
            if (g_ctxIcon >= 0) DeleteIconToTrash(g_ctxIcon);
            break;
        case CM_COPY:
            if (g_ctxIcon >= 0) wcscpy(g_copyBuffer, g_icons[g_ctxIcon].path);
            break;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wp;
        // Rename input
        if (g_renaming) {
            if (ch == VK_BACK || ch == 8) {
                int len = (int)wcslen(g_renameText);
                if (len > 0) g_renameText[len-1] = 0;
            } else if (ch >= 32) {
                int len = (int)wcslen(g_renameText);
                if (len < MAX_PATH-1) {
                    g_renameText[len] = ch;
                    g_renameText[len+1] = 0;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Focused window text input
        if (g_topWindow >= 0) {
            AppWindow& w = g_windows[g_topWindow];
            if (w.appId == APP_NOTEPAD) {
                if (ch == 8) { // backspace
                    if (w.noteCaret > 0) {
                        memmove(w.noteText + w.noteCaret - 1, w.noteText + w.noteCaret,
                            (wcslen(w.noteText) - w.noteCaret + 1) * sizeof(wchar_t));
                        w.noteCaret--;
                    }
                } else if (ch == 13) { // enter
                    int len = (int)wcslen(w.noteText);
                    if (len < MAX_NOTES_LEN-1) {
                        memmove(w.noteText + w.noteCaret + 1, w.noteText + w.noteCaret,
                            (len - w.noteCaret + 1) * sizeof(wchar_t));
                        w.noteText[w.noteCaret] = L'\n';
                        w.noteCaret++;
                    }
                } else if (ch >= 32) {
                    int len = (int)wcslen(w.noteText);
                    if (len < MAX_NOTES_LEN-1) {
                        memmove(w.noteText + w.noteCaret + 1, w.noteText + w.noteCaret,
                            (len - w.noteCaret + 1) * sizeof(wchar_t));
                        w.noteText[w.noteCaret] = ch;
                        w.noteCaret++;
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (w.appId == APP_TERMINAL || w.appId == APP_CMD) {
                if (ch == 8) {
                    int len = (int)wcslen(w.termInput);
                    if (len > 0) w.termInput[len-1] = 0;
                } else if (ch == 13) {
                    HandleTerminalCommand(&w);
                } else if (ch >= 32) {
                    int len = (int)wcslen(w.termInput);
                    if (len < MAX_TERM_LINE_LEN-1) {
                        w.termInput[len] = ch;
                        w.termInput[len+1] = 0;
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (g_renaming && wp == VK_RETURN) {
            // Commit rename
            if (g_renameIcon >= 0 && g_renameIcon < g_iconCount) {
                wchar_t newPath[MAX_PATH];
                swprintf(newPath, MAX_PATH, L"%s\\%s", g_desktopPath, g_renameText);
                MoveFile(g_icons[g_renameIcon].path, newPath);
                RefreshIcons();
            }
            g_renaming = false;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (g_renaming && wp == VK_ESCAPE) {
            g_renaming = false;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        // Notepad arrow keys
        if (g_topWindow >= 0) {
            AppWindow& w = g_windows[g_topWindow];
            if (w.appId == APP_NOTEPAD) {
                int len = (int)wcslen(w.noteText);
                if (wp == VK_LEFT  && w.noteCaret > 0)   w.noteCaret--;
                if (wp == VK_RIGHT && w.noteCaret < len)  w.noteCaret++;
                if (wp == VK_DELETE && w.noteCaret < len) {
                    memmove(w.noteText + w.noteCaret, w.noteText + w.noteCaret + 1,
                        (len - w.noteCaret) * sizeof(wchar_t));
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_OPEN_APP:
        OpenApp((int)wp);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_CLOCK);
        KillTimer(hwnd, TIMER_ANIMATE);
        KillTimer(hwnd, TIMER_GAME);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────
//  WINMAIN
// ─────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Init common controls
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    // Setup FS
    SetupDirectories();
    RefreshIcons();
    LoadTrash();

    // Register window class
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"DexpOSMain";
    RegisterClassEx(&wc);

    // Center window on screen
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int wx = (sw - MAIN_WIDTH)  / 2;
    int wy = (sh - MAIN_HEIGHT) / 2;

    g_hwnd = CreateWindowEx(0, L"DexpOSMain", L"Dexp OS",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        wx, wy, MAIN_WIDTH, MAIN_HEIGHT, NULL, NULL, hInst, NULL);

    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
