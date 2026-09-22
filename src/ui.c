#include "fw.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#define IDC_PATH    101
#define IDC_BROWSE  102
#define IDC_START   103
#define IDC_CLEAR   104
#define IDC_LIST    105
#define IDC_COPY    106
#define IDC_OPEN    107
#define IDC_STATUS  108

#define MAX_ROWS 10000

#define COL_TIME 0
#define COL_OP   1
#define COL_CNT  2
#define COL_LAST 3
#define COL_FILE 4
#define COL_EXE  5
#define COL_PID  6

static HWND g_hwnd, g_path, g_browse, g_start, g_clear, g_list, g_copy, g_open, g_status;
static int g_running;
static int g_rows;
static int g_selftest;
static HANDLE g_testThread;
static int g_lastIdx = -1;
static FwEvent g_lastEv;

HWND UiMainWnd(void) { return g_hwnd; }

static void SetStatus(const wchar_t *s)
{
    if (g_status) SetWindowTextW(g_status, s);
}

static void BrowseFolder(void)
{
    wchar_t buf[FW_PATH_MAX];
    BROWSEINFOW bi;
    ITEMIDLIST *idl;
    HWND owner = g_hwnd;

    buf[0] = 0;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.pszDisplayName = buf;
    bi.lpszTitle = L"选择要监控的文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    idl = SHBrowseForFolderW(&bi);
    if (!idl) return;
    if (SHGetPathFromIDListW(idl, buf)) {
        SetWindowTextW(g_path, buf);
        FwSetTarget(buf);
    }
    CoTaskMemFree(idl);
}

static int GetSelPath(wchar_t *exe, int cap, wchar_t *file, int fcap)
{
    int idx;

    if (file) file[0] = 0;
    if (exe) exe[0] = 0;
    idx = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (idx < 0) return -1;
    if (exe) ListView_GetItemText(g_list, idx, COL_EXE, exe, cap);
    if (file) ListView_GetItemText(g_list, idx, COL_FILE, file, fcap);
    return idx;
}

static void CopyExe(void)
{
    wchar_t exe[FW_PATH_MAX];
    HGLOBAL h;
    wchar_t *p;

    if (GetSelPath(exe, FW_PATH_MAX, NULL, 0) < 0) {
        SetStatus(L"请先选中一行");
        return;
    }
    if (!exe[0]) return;
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    h = GlobalAlloc(GMEM_MOVEABLE, (lstrlenW(exe) + 1) * sizeof(wchar_t));
    if (h) {
        p = (wchar_t *)GlobalLock(h);
        if (p) {
            lstrcpyW(p, exe);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
    SetStatus(L"已复制进程路径");
}

static void OpenExeDir(void)
{
    wchar_t exe[FW_PATH_MAX];
    wchar_t cmd[FW_PATH_MAX + 32];

    if (GetSelPath(exe, FW_PATH_MAX, NULL, 0) < 0) {
        SetStatus(L"请先选中一行");
        return;
    }
    if (!exe[0] || exe[0] == L'(') return;
    wsprintfW(cmd, L"/select,\"%s\"", exe);
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", cmd, NULL, SW_SHOWNORMAL);
}

static void FormatTime(const FILETIME *ts, wchar_t *buf, int cap)
{
    SYSTEMTIME st;
    FILETIME lt;

    FileTimeToLocalFileTime(ts, &lt);
    FileTimeToSystemTime(&lt, &st);
    wsprintfW(buf, L"%02u:%02u:%02u.%03u",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    (void)cap;
}

static int SameBatch(const FwEvent *a, const FwEvent *b)
{
    return a->pid == b->pid &&
           a->op[0] &&
           lstrcmpiW(a->op, b->op) == 0 &&
           lstrcmpiW(a->path, b->path) == 0 &&
           lstrcmpiW(a->exe, b->exe) == 0;
}

void UiAddEvent(const FwEvent *e)
{
    LVITEMW li;
    wchar_t timeBuf[64], cntBuf[16];
    int idx;

    if (!g_list) return;

    if (g_lastIdx >= 0 && g_lastIdx < g_rows && SameBatch(&g_lastEv, e)) {
        int cnt = 0;
        wchar_t old[16];
        old[0] = 0;
        ListView_GetItemText(g_list, g_lastIdx, COL_CNT, old, 16);
        cnt = 0;
        {
            const wchar_t *p = old;
            while (*p >= L'0' && *p <= L'9') {
                cnt = cnt * 10 + (*p - L'0');
                p++;
            }
        }
        if (cnt < 1) cnt = 1;
        cnt++;
        wsprintfW(cntBuf, L"%d", cnt);
        ListView_SetItemText(g_list, g_lastIdx, COL_CNT, cntBuf);
        FormatTime(&e->ts, timeBuf, 64);
        ListView_SetItemText(g_list, g_lastIdx, COL_LAST, timeBuf);
        if (CompareFileTime(&e->ts, &g_lastEv.ts) > 0)
            g_lastEv.ts = e->ts;
        ListView_EnsureVisible(g_list, g_lastIdx, FALSE);
        return;
    }

    if (g_rows >= MAX_ROWS) {
        ListView_DeleteItem(g_list, 0);
        g_rows--;
        if (g_lastIdx > 0) g_lastIdx--;
        else if (g_lastIdx == 0) g_lastIdx = -1;
    }

    FormatTime(&e->ts, timeBuf, 64);
    ZeroMemory(&li, sizeof(li));
    li.mask = LVIF_TEXT;
    li.iItem = g_rows;
    li.pszText = timeBuf;
    idx = ListView_InsertItem(g_list, &li);
    if (idx < 0) return;
    ListView_SetItemText(g_list, idx, COL_OP, (LPWSTR)e->op);
    ListView_SetItemText(g_list, idx, COL_CNT, L"1");
    {
        wchar_t lastBuf[64];
        FormatTime(&e->ts, lastBuf, 64);
        ListView_SetItemText(g_list, idx, COL_LAST, lastBuf);
    }
    ListView_SetItemText(g_list, idx, COL_FILE, (LPWSTR)e->path);
    ListView_SetItemText(g_list, idx, COL_EXE, (LPWSTR)e->exe);
    wsprintfW(cntBuf, L"%u", e->pid);
    ListView_SetItemText(g_list, idx, COL_PID, cntBuf);

    g_rows++;
    g_lastIdx = idx;
    g_lastEv = *e;
    ListView_EnsureVisible(g_list, idx, FALSE);
}

static void ToggleRun(void); /* fwd */

static DWORD WINAPI SelfTestThread(LPVOID arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 16; i++) {
        Sleep(500);
        if (!g_running) break;
    }
    if (g_running)
        ToggleRun();
    FwDiag(L"selftest finished\r\n");
    PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    return 0;
}

static void ToggleRun(void)
{
    wchar_t folder[FW_PATH_MAX];
    DWORD err;

    if (!g_running) {
        GetWindowTextW(g_path, folder, FW_PATH_MAX);
        if (!folder[0]) {
            SetStatus(L"请先选择文件夹");
            return;
        }
        FwSetTarget(folder);
        err = EtwStart();
        if (err) {
            wchar_t msg[192];
            wsprintfW(msg, L"启动 ETW 失败: %u (需管理员)\r\n", err);
            SetStatus(msg);
            if (g_selftest) FwDiag(msg);
            if (g_selftest) {
                PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            }
            return;
        }
        g_running = 1;
        SetWindowTextW(g_start, L"停止");
        SetStatus(L"监控中…");
        if (g_selftest) FwDiag(L"ETW started\r\n");
        if (g_selftest && !g_testThread) {
            g_testThread = CreateThread(NULL, 0, SelfTestThread, NULL, 0, NULL);
        }
    } else {
        EtwStop();
        g_running = 0;
        SetWindowTextW(g_start, L"开始");
        SetStatus(L"已停止");
    }
}

static void ClearList(void)
{
    ListView_DeleteAllItems(g_list);
    g_rows = 0;
    g_lastIdx = -1;
    g_lastEv.pid = 0;
    g_lastEv.op[0] = 0;
    g_lastEv.path[0] = 0;
    g_lastEv.exe[0] = 0;
}

static void Layout(void)
{
    RECT rc;
    int w, top, listH;
    HDWP d;

    if (!g_hwnd) return;
    GetClientRect(g_hwnd, &rc);
    w = rc.right - rc.left;
    top = 8;
    d = BeginDeferWindowPos(8);
    d = DeferWindowPos(d, g_path, NULL, 8, top, w - 200, 24, SWP_NOZORDER);
    d = DeferWindowPos(d, g_browse, NULL, w - 188, top, 80, 24, SWP_NOZORDER);
    d = DeferWindowPos(d, g_start, NULL, w - 104, top, 48, 24, SWP_NOZORDER);
    d = DeferWindowPos(d, g_clear, NULL, w - 52, top, 44, 24, SWP_NOZORDER);
    top += 32;
    listH = rc.bottom - top - 64;
    if (listH < 40) listH = 40;
    d = DeferWindowPos(d, g_list, NULL, 8, top, w - 16, listH, SWP_NOZORDER);
    top += listH + 8;
    d = DeferWindowPos(d, g_copy, NULL, 8, top, 110, 26, SWP_NOZORDER);
    d = DeferWindowPos(d, g_open, NULL, 124, top, 120, 26, SWP_NOZORDER);
    d = DeferWindowPos(d, g_status, NULL, 252, top, w - 260, 26, SWP_NOZORDER);
    EndDeferWindowPos(d);
}

static void AddCol(int i, const wchar_t *t, int w)
{
    LVCOLUMNW c;
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = (LPWSTR)t;
    c.cx = w;
    c.iSubItem = i;
    ListView_InsertColumn(g_list, i, &c);
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_LISTVIEW_CLASSES };
        LV_COLUMNW col;
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        g_hwnd = wnd;
        InitCommonControlsEx(&ic);
        g_path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_PATH, cs->hInstance, NULL);
        g_browse = CreateWindowExW(0, L"BUTTON", L"浏览",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_BROWSE, cs->hInstance, NULL);
        g_start = CreateWindowExW(0, L"BUTTON", L"开始",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_START, cs->hInstance, NULL);
        g_clear = CreateWindowExW(0, L"BUTTON", L"清空",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_CLEAR, cs->hInstance, NULL);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_LIST, cs->hInstance, NULL);
        ListView_SetExtendedListViewStyle(g_list,
            LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_HEADERDRAGDROP);
        AddCol(COL_TIME, L"时间", 110);
        AddCol(COL_OP, L"操作", 70);
        AddCol(COL_CNT, L"次数", 56);
        AddCol(COL_LAST, L"最后操作时间", 110);
        AddCol(COL_FILE, L"文件路径", 300);
        AddCol(COL_EXE, L"进程路径", 300);
        AddCol(COL_PID, L"PID", 70);
        (void)col;
        g_copy = CreateWindowExW(0, L"BUTTON", L"复制进程路径",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_COPY, cs->hInstance, NULL);
        g_open = CreateWindowExW(0, L"BUTTON", L"打开进程目录",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_OPEN, cs->hInstance, NULL);
        g_status = CreateWindowExW(0, L"STATIC", L"选择文件夹后点开始",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, wnd, (HMENU)(UINT_PTR)IDC_STATUS, cs->hInstance, NULL);
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE: BrowseFolder(); return 0;
        case IDC_START: ToggleRun(); return 0;
        case IDC_CLEAR: ClearList(); return 0;
        case IDC_COPY: CopyExe(); return 0;
        case IDC_OPEN: OpenExeDir(); return 0;
        }
        break;
    case WM_APP_EVENT: {
        FwEvent *e = (FwEvent *)lp;
        if (e) {
            UiAddEvent(e);
            HeapFree(GetProcessHeap(), 0, e);
        }
        return 0;
    }
    case WM_CLOSE:
        if (g_running) {
            EtwStop();
            g_running = 0;
        }
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSEXW wc;
    MSG msg;
    HWND hwnd;
    const wchar_t *cls = L"FolderWatchClass";
    int argc = 0;
    LPWSTR *argv;
    (void)prev; (void)cmd;

    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && lstrcmpiW(argv[1], L"-selftest") == 0) {
        g_selftest = 1;
        show = SW_HIDE;
    }
    if (argv) LocalFree(argv);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(0, cls, L"FolderWatch - 文件夹监控",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 600,
        NULL, NULL, inst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, g_selftest ? SW_HIDE : show);
    UpdateWindow(hwnd);
    if (g_selftest) {
        argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 3) {
            SetWindowTextW(g_path, argv[2]);
            FwSetTarget(argv[2]);
            ToggleRun();
        }
        if (argv) LocalFree(argv);
    }
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}

void Entry(void)
{
    int r;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && lstrcmpiW(argv[1], L"-selftest") == 0)
        g_selftest = 1;
    if (argv) LocalFree(argv);
    r = wWinMain(GetModuleHandleW(NULL), NULL, GetCommandLineW(),
                 g_selftest ? SW_HIDE : SW_SHOWDEFAULT);
    ExitProcess((UINT)r);
}
