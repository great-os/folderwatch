#include "fw.h"

static wchar_t g_target[FW_PATH_MAX];

int FwCmpPrefixI(const wchar_t *s, const wchar_t *prefix, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        wchar_t a = s[i], b = prefix[i];
        if (a >= L'A' && a <= L'Z') a += 32;
        if (b >= L'A' && b <= L'Z') b += 32;
        if (a != b) return (int)a - (int)b;
        if (!a) return 0;
    }
    return 0;
}

static void StripUncPrefix(const wchar_t *in, wchar_t *out, int cap)
{
    if (in[0] == L'\\' && in[1] == L'?' && in[2] == L'?' && in[3] == L'\\') {
        in += 4;
    }
    lstrcpynW(out, in, cap);
}

static int DeviceToDos(const wchar_t *in, wchar_t *out, int cap)
{
    wchar_t drive[8], dev[512];
    wchar_t c;
    int n;

    if (in[0] != L'\\')
        return 0;
    for (c = L'A'; c <= L'Z'; c++) {
        drive[0] = c;
        drive[1] = L':';
        drive[2] = 0;
        if (!QueryDosDeviceW(drive, dev, 512))
            continue;
        n = lstrlenW(dev);
        if (FwCmpPrefixI(in, dev, n) != 0)
            continue;
        if (in[n] != 0 && in[n] != L'\\')
            continue;
        if (n + lstrlenW(in + n) + 3 > cap)
            return 0;
        out[0] = c;
        out[1] = L':';
        lstrcpyW(out + 2, in + n);
        if (!out[2]) {
            out[2] = L'\\';
            out[3] = 0;
        }
        return 1;
    }
    return 0;
}

void FwPathNormalize(const wchar_t *in, wchar_t *out, int cap)
{
    wchar_t tmp[FW_PATH_MAX];
    int i, n;

    if (!in || !in[0]) {
        out[0] = 0;
        return;
    }
    StripUncPrefix(in, tmp, FW_PATH_MAX);
    if (tmp[0] == L'\\') {
        wchar_t dos[FW_PATH_MAX];
        if (DeviceToDos(tmp, dos, FW_PATH_MAX))
            lstrcpynW(tmp, dos, FW_PATH_MAX);
    }
    n = lstrlenW(tmp);
    if (n >= cap)
        n = cap - 1;
    for (i = 0; i < n; i++) {
        wchar_t ch = tmp[i];
        if (ch == L'/')
            ch = L'\\';
        out[i] = ch;
    }
    out[n] = 0;
    while (n > 3 && out[n - 1] == L'\\') {
        out[--n] = 0;
    }
}

void FwSetTarget(const wchar_t *folder)
{
    FwPathNormalize(folder, g_target, FW_PATH_MAX);
}

const wchar_t *FwGetTarget(void)
{
    return g_target;
}

int FwPathUnder(const wchar_t *path)
{
    int n, len;

    if (!g_target[0] || !path || !path[0])
        return 0;
    n = lstrlenW(g_target);
    len = lstrlenW(path);
    if (len < n)
        return 0;
    if (FwCmpPrefixI(path, g_target, n) != 0)
        return 0;
    if (len == n)
        return 1;
    if (g_target[n - 1] == L'\\')
        return 1;
    return path[n] == L'\\';
}
