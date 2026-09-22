#include "fw.h"
#include <evntrace.h>
#include <evntcons.h>

static const GUID KfwGuid =
    {0xedd08927,0x9cc4,0x4e65,{0xb9,0x70,0xc2,0x56,0x0f,0xb5,0xc2,0x89}};

#define KW_ALL (0x10ull|0x20ull|0x40ull|0x80ull|0x100ull|0x200ull|0x400ull|0x800ull|0x1000ull)
#define SESS_NAME L"FolderWatchSession"

static TRACEHANDLE g_sess;
static TRACEHANDLE g_trace;
static HANDLE g_thread;
static volatile LONG g_stop;
static volatile LONG g_evtCount;
static volatile LONG g_matchCount;

typedef struct Pay {
    const BYTE *p, *end;
    int is32;
} Pay;

static UINT64 Rd(Pay *d, int n)
{
    UINT64 v = 0;
    int i;
    if (d->p + n > d->end) return 0;
    for (i = 0; i < n; i++) v |= (UINT64)d->p[i] << (8 * i);
    d->p += n;
    return v;
}

static UINT64 RdPtr(Pay *d) { return Rd(d, d->is32 ? 4 : 8); }

static void RdStr(Pay *d, wchar_t *out, int cap)
{
    int n = 0;
    out[0] = 0;
    while (d->p + 1 < d->end && n < cap - 1) {
        wchar_t c = (wchar_t)(d->p[0] | (d->p[1] << 8));
        d->p += 2;
        if (!c) break;
        out[n++] = c;
    }
    out[n] = 0;
}

static void Publish(const wchar_t *op, const wchar_t *path, DWORD pid, FILETIME ts)
{
    FwEvent *e;

    if (!path || !path[0] || !FwPathUnder(path))
        return;
    InterlockedIncrement(&g_matchCount);
    e = (FwEvent *)HeapAlloc(GetProcessHeap(), 0, sizeof(FwEvent));
    if (!e) return;
    e->ts = ts;
    e->pid = pid;
    lstrcpynW(e->op, op, FW_OP_MAX);
    lstrcpynW(e->path, path, FW_PATH_MAX);
    {
        const wchar_t *exe = FwPidGet(pid);
        if (exe) lstrcpynW(e->exe, exe, FW_PATH_MAX);
        else wsprintfW(e->exe, L"(PID %u)", pid);
    }
    if (!PostMessageW(UiMainWnd(), WM_APP_EVENT, 0, (LPARAM)e))
        HeapFree(GetProcessHeap(), 0, e);
}

static void OnEvent(PEVENT_RECORD rec)
{
    EVENT_HEADER *h = &rec->EventHeader;
    Pay d;
    UINT64 fo = 0, fk = 0;
    wchar_t name[FW_PATH_MAX];
    BYTE id = h->EventDescriptor.Id;
    BYTE ver = h->EventDescriptor.Version;
    DWORD pid = h->ProcessId;
    FILETIME ts;

    InterlockedIncrement(&g_evtCount);
    d.p = (const BYTE *)rec->UserData;
    d.end = d.p + rec->UserDataLength;
    d.is32 = (h->Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) != 0;
    name[0] = 0;
    ts.dwLowDateTime = h->TimeStamp.LowPart;
    ts.dwHighDateTime = (DWORD)h->TimeStamp.HighPart;

    switch (id) {
    case 10: /* NameCreate */
    case 11: /* NameDelete */
        fo = RdPtr(&d);
        RdStr(&d, name, FW_PATH_MAX);
        if (id == 10 && name[0]) {
            FwPathNormalize(name, name, FW_PATH_MAX);
            if (FwPathUnder(name))
                FwMapSet(fo, name);
        } else if (id == 11) {
            FwMapDel(fo);
        }
        return;
    case 12: /* Create */
        if (ver == 0) {
            RdPtr(&d); RdPtr(&d); fo = RdPtr(&d);
            Rd(&d, 4); Rd(&d, 4); Rd(&d, 4);
        } else {
            RdPtr(&d); fo = RdPtr(&d);
            Rd(&d, 4); Rd(&d, 4); Rd(&d, 4); Rd(&d, 4);
        }
        RdStr(&d, name, FW_PATH_MAX);
        if (name[0]) {
            FwPathNormalize(name, name, FW_PATH_MAX);
            if (FwPathUnder(name)) {
                FwMapSet(fo, name);
                Publish(L"打开", name, pid, ts);
            }
        }
        return;
    case 15: /* Read */
    case 16: /* Write */
        if (ver == 0) {
            Rd(&d, 8); RdPtr(&d); RdPtr(&d);
            fo = RdPtr(&d); fk = RdPtr(&d);
        } else {
            Rd(&d, 8); RdPtr(&d);
            fo = RdPtr(&d); fk = RdPtr(&d); RdPtr(&d);
        }
        {
            const wchar_t *p = FwMapGet(fo);
            if (!p) p = FwMapGet(fk);
            if (p)
                Publish((id == 15) ? L"读取" : L"写入", p, pid, ts);
        }
        return;
    case 17: /* SetInformation */
    case 18: /* SetDelete */
    case 19: /* Rename */
        if (ver == 0) {
            RdPtr(&d); RdPtr(&d); fo = RdPtr(&d); fk = RdPtr(&d);
            RdPtr(&d); Rd(&d, 4);
        } else {
            RdPtr(&d); fo = RdPtr(&d); fk = RdPtr(&d);
            RdPtr(&d); RdPtr(&d); Rd(&d, 4);
        }
        {
            const wchar_t *p = FwMapGet(fo);
            if (!p) p = FwMapGet(fk);
            if (!p) return;
            if (id == 18) Publish(L"删除", p, pid, ts);
            else if (id == 19) Publish(L"重命名", p, pid, ts);
            else Publish(L"修改", p, pid, ts);
        }
        return;
    case 26: /* DeletePath */
    case 27: /* RenamePath */
    case 28: /* SetLinkPath */
        if (ver == 0) {
            RdPtr(&d); RdPtr(&d); fo = RdPtr(&d); fk = RdPtr(&d);
            RdPtr(&d); Rd(&d, 4);
        } else {
            RdPtr(&d); fo = RdPtr(&d); fk = RdPtr(&d);
            RdPtr(&d); RdPtr(&d); Rd(&d, 4);
        }
        RdStr(&d, name, FW_PATH_MAX);
        if (name[0]) {
            FwPathNormalize(name, name, FW_PATH_MAX);
            if (id == 26) Publish(L"删除", name, pid, ts);
            else if (id == 27) Publish(L"重命名", name, pid, ts);
            else Publish(L"修改", name, pid, ts);
        }
        return;
    case 30: /* CreateNewFile */
        if (ver == 0) {
            RdPtr(&d); RdPtr(&d); fo = RdPtr(&d);
            Rd(&d, 4); Rd(&d, 4); Rd(&d, 4);
        } else {
            RdPtr(&d); fo = RdPtr(&d);
            Rd(&d, 4); Rd(&d, 4); Rd(&d, 4); Rd(&d, 4);
        }
        RdStr(&d, name, FW_PATH_MAX);
        if (name[0]) {
            FwPathNormalize(name, name, FW_PATH_MAX);
            if (FwPathUnder(name)) {
                FwMapSet(fo, name);
                Publish(L"新建", name, pid, ts);
            }
        }
        return;
    case 13: /* Cleanup */
    case 14: /* Close */
        if (ver == 0) {
            RdPtr(&d); RdPtr(&d); fo = RdPtr(&d); fk = RdPtr(&d);
        } else {
            RdPtr(&d); fo = RdPtr(&d); RdPtr(&d);
        }
        FwMapDel(fo);
        if (fk) FwMapDel(fk);
        return;
    default:
        return;
    }
}

static void WINAPI EvtCallback(PEVENT_RECORD rec)
{
    if (g_stop) return;
    OnEvent(rec);
}

static DWORD WINAPI TraceThread(LPVOID arg)
{
    EVENT_TRACE_LOGFILEW lf;
    DWORD st;
    (void)arg;
    ZeroMemory(&lf, sizeof(lf));
    lf.LoggerName = (LPWSTR)SESS_NAME;
    lf.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_REAL_TIME;
    lf.EventRecordCallback = EvtCallback;
    g_trace = OpenTraceW(&lf);
    if (g_trace == INVALID_PROCESSTRACE_HANDLE) {
        g_stop = 1;
        return 1;
    }
    st = ProcessTrace(&g_trace, 1, NULL, NULL);
    {
        wchar_t b[120];
        wsprintfW(b, L"ProcessTrace=%u open=%p evt=%d match=%d\r\n",
                  st, (void *)g_trace, (int)g_evtCount, (int)g_matchCount);
        FwDiag(b);
    }
    CloseTrace(g_trace);
    g_trace = INVALID_PROCESSTRACE_HANDLE;
    (void)st;
    return 0;
}

void FwDiag(const wchar_t *msg)
{
    wchar_t path[FW_PATH_MAX];
    DWORD n = 0;
    HANDLE f;
    GetModuleFileNameW(NULL, path, FW_PATH_MAX);
    {
        wchar_t *slash = path;
        wchar_t *p = path;
        while (*p) {
            if (*p == L'\\') slash = p;
            p++;
        }
        slash[1] = 0;
    }
    lstrcatW(path, L"fw_diag.log");
    f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        SetFilePointer(f, 0, NULL, FILE_END);
        WriteFile(f, msg, lstrlenW(msg) * sizeof(wchar_t), &n, NULL);
        CloseHandle(f);
    }
}

static void FreeProps(EVENT_TRACE_PROPERTIES *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

static EVENT_TRACE_PROPERTIES *MakeProps(ULONG *outSize)
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 1024 + 1024;
    EVENT_TRACE_PROPERTIES *p = (EVENT_TRACE_PROPERTIES *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!p) return NULL;
    p->Wnode.BufferSize = size;
    p->Wnode.ClientContext = 1;
    p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE;
    p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    p->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 512;
    *outSize = size;
    return p;
}

DWORD EtwStart(void)
{
    ULONG size = 0, st;
    EVENT_TRACE_PROPERTIES *p;
    DWORD err;

    if (g_sess) return ERROR_ALREADY_EXISTS;
    FwMapClear();
    FwPidClear();
    g_stop = 0;
    p = MakeProps(&size);
    if (!p) return ERROR_OUTOFMEMORY;
    st = StartTraceW(&g_sess, SESS_NAME, p);
    FreeProps(p);
    {
        wchar_t b[80];
        wsprintfW(b, L"StartTrace(system)=%u\r\n", st);
        FwDiag(b);
    }
    if (st != ERROR_SUCCESS && st != ERROR_ALREADY_EXISTS) {
        /* retry as private session without system logger flag */
        p = MakeProps(&size);
        if (p) {
            p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
            p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
            st = StartTraceW(&g_sess, SESS_NAME, p);
            FreeProps(p);
        }
        {
            wchar_t b[80];
            wsprintfW(b, L"StartTrace(private)=%u\r\n", st);
            FwDiag(b);
        }
    }
    if (st != ERROR_SUCCESS && st != ERROR_ALREADY_EXISTS) {
        g_sess = 0;
        return st;
    }
    if (st == ERROR_ALREADY_EXISTS) {
        p = MakeProps(&size);
        ControlTraceW(0, SESS_NAME, p, EVENT_TRACE_CONTROL_STOP);
        FreeProps(p);
        g_sess = 0;
        p = MakeProps(&size);
        st = StartTraceW(&g_sess, SESS_NAME, p);
        FreeProps(p);
        {
            wchar_t b[80];
            wsprintfW(b, L"StartTrace(restart)=%u\r\n", st);
            FwDiag(b);
        }
        if (st != ERROR_SUCCESS) {
            g_sess = 0;
            return st;
        }
    }
    err = EnableTraceEx2(g_sess, &KfwGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                         TRACE_LEVEL_INFORMATION, KW_ALL, 0, 0, NULL);
    {
        wchar_t b[80];
        wsprintfW(b, L"EnableTraceEx2=%u\r\n", err);
        FwDiag(b);
    }
    if (err != ERROR_SUCCESS) {
        p = MakeProps(&size);
        ControlTraceW(g_sess, SESS_NAME, p, EVENT_TRACE_CONTROL_STOP);
        FreeProps(p);
        g_sess = 0;
        return err;
    }
    g_thread = CreateThread(NULL, 0, TraceThread, NULL, 0, NULL);
    FwDiag(g_thread ? L"TraceThread ok\r\n" : L"TraceThread FAIL\r\n");
    if (!g_thread) {
        p = MakeProps(&size);
        ControlTraceW(g_sess, SESS_NAME, p, EVENT_TRACE_CONTROL_STOP);
        FreeProps(p);
        g_sess = 0;
        return GetLastError();
    }
    /* give ProcessTrace a moment to open */
    Sleep(50);
    return 0;
}

void EtwStop(void)
{
    ULONG size = 0;
    EVENT_TRACE_PROPERTIES *p;

    g_stop = 1;
    if (g_sess) {
        p = MakeProps(&size);
        if (p) {
            ControlTraceW(g_sess, SESS_NAME, p, EVENT_TRACE_CONTROL_STOP);
            FreeProps(p);
        }
        g_sess = 0;
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_trace != INVALID_PROCESSTRACE_HANDLE && g_trace != 0) {
        CloseTrace(g_trace);
        g_trace = INVALID_PROCESSTRACE_HANDLE;
    }
}
