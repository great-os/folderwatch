#include "fw.h"

#define FO_SLOTS 1024
#define PID_SLOTS 256

typedef struct FoSlot {
    UINT64  key;
    int     used;
    wchar_t *path;
} FoSlot;

typedef struct PidSlot {
    DWORD  pid;
    int    used;
    int    failed;
    wchar_t *path;
} PidSlot;

static FoSlot  g_fo[FO_SLOTS];
static PidSlot g_pid[PID_SLOTS];

static unsigned HashU64(UINT64 x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (unsigned)(x & (FO_SLOTS - 1));
}

static unsigned HashDword(DWORD x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    return (unsigned)(x & (PID_SLOTS - 1));
}

static wchar_t *DupStr(const wchar_t *s)
{
    int n = lstrlenW(s);
    wchar_t *p = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(wchar_t));
    if (p) CopyMemory(p, s, (n + 1) * sizeof(wchar_t));
    return p;
}

void FwMapSet(UINT64 key, const wchar_t *path)
{
    unsigned i, start;
    int firstEmpty = -1;

    if (!key || !path || !path[0])
        return;
    start = i = HashU64(key);
    for (;;) {
        if (!g_fo[i].used) {
            if (firstEmpty < 0)
                firstEmpty = (int)i;
            break;
        }
        if (g_fo[i].key == key) {
            if (g_fo[i].path) HeapFree(GetProcessHeap(), 0, g_fo[i].path);
            g_fo[i].path = DupStr(path);
            return;
        }
        i = (i + 1) & (FO_SLOTS - 1);
        if (i == start)
            break;
    }
    if (firstEmpty < 0)
        return;
    g_fo[firstEmpty].key = key;
    g_fo[firstEmpty].used = 1;
    g_fo[firstEmpty].path = DupStr(path);
}

const wchar_t *FwMapGet(UINT64 key)
{
    unsigned i, start;

    if (!key)
        return NULL;
    start = i = HashU64(key);
    for (;;) {
        if (!g_fo[i].used)
            return NULL;
        if (g_fo[i].key == key)
            return g_fo[i].path;
        i = (i + 1) & (FO_SLOTS - 1);
        if (i == start)
            return NULL;
    }
}

void FwMapDel(UINT64 key)
{
    unsigned i, start;

    if (!key)
        return;
    start = i = HashU64(key);
    for (;;) {
        if (!g_fo[i].used)
            return;
        if (g_fo[i].key == key) {
            if (g_fo[i].path) {
                HeapFree(GetProcessHeap(), 0, g_fo[i].path);
                g_fo[i].path = NULL;
            }
            g_fo[i].used = 0;
            g_fo[i].key = 0;
            return;
        }
        i = (i + 1) & (FO_SLOTS - 1);
        if (i == start)
            return;
    }
}

void FwMapClear(void)
{
    unsigned i;
    for (i = 0; i < FO_SLOTS; i++) {
        if (g_fo[i].path)
            HeapFree(GetProcessHeap(), 0, g_fo[i].path);
        g_fo[i].path = NULL;
        g_fo[i].used = 0;
        g_fo[i].key = 0;
    }
}

const wchar_t *FwPidGet(DWORD pid)
{
    unsigned i, start;
    HANDLE h;
    int firstEmpty = -1;
    wchar_t buf[FW_PATH_MAX];
    DWORD cap;

    if (!pid)
        return NULL;
    start = i = HashDword(pid);
    for (;;) {
        if (!g_pid[i].used) {
            if (firstEmpty < 0)
                firstEmpty = (int)i;
            break;
        }
        if (g_pid[i].pid == pid)
            return g_pid[i].path;
        i = (i + 1) & (PID_SLOTS - 1);
        if (i == start)
            break;
    }
    if (firstEmpty < 0)
        return NULL;

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        g_pid[firstEmpty].pid = pid;
        g_pid[firstEmpty].used = 1;
        g_pid[firstEmpty].failed = 1;
        g_pid[firstEmpty].path = NULL;
        return NULL;
    }
    cap = FW_PATH_MAX;
    buf[0] = 0;
    if (!QueryFullProcessImageNameW(h, 0, buf, &cap))
        buf[0] = 0;
    CloseHandle(h);
    g_pid[firstEmpty].pid = pid;
    g_pid[firstEmpty].used = 1;
    if (!buf[0]) {
        g_pid[firstEmpty].failed = 1;
        g_pid[firstEmpty].path = NULL;
        return NULL;
    }
    g_pid[firstEmpty].failed = 0;
    g_pid[firstEmpty].path = DupStr(buf);
    return g_pid[firstEmpty].path;
}

void FwPidClear(void)
{
    unsigned i;
    for (i = 0; i < PID_SLOTS; i++) {
        if (g_pid[i].path)
            HeapFree(GetProcessHeap(), 0, g_pid[i].path);
        g_pid[i].path = NULL;
        g_pid[i].used = 0;
        g_pid[i].pid = 0;
        g_pid[i].failed = 0;
    }
}
