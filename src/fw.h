#pragma once
#include <windows.h>

#define WM_APP_EVENT (WM_APP + 1)
#define FW_PATH_MAX  1024
#define FW_OP_MAX    16

typedef struct FwEvent {
    FILETIME ts;
    DWORD    pid;
    wchar_t  op[FW_OP_MAX];
    wchar_t  path[FW_PATH_MAX];
    wchar_t  exe[FW_PATH_MAX];
} FwEvent;

int  FwCmpPrefixI(const wchar_t *s, const wchar_t *prefix, int n);
void FwPathNormalize(const wchar_t *in, wchar_t *out, int cap);
void FwSetTarget(const wchar_t *folder);
const wchar_t *FwGetTarget(void);
int  FwPathUnder(const wchar_t *path);

void FwMapSet(UINT64 key, const wchar_t *path);
const wchar_t *FwMapGet(UINT64 key);
void FwMapDel(UINT64 key);
void FwMapClear(void);
const wchar_t *FwPidGet(DWORD pid);
void FwPidClear(void);

DWORD EtwStart(void);
void  EtwStop(void);
void  FwDiag(const wchar_t *msg);

void UiAddEvent(const FwEvent *e);
HWND UiMainWnd(void);
