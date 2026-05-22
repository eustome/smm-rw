#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winternl.h>
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "shell32.lib")
#include "smm_api.h"
#include "smm_client.h"
#include <stdio.h>
#include <string.h>

typedef unsigned __int64 u64;

static BOOL is_elevated(void)
{
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
        return FALSE;
    TOKEN_ELEVATION elev = {0};
    DWORD sz = sizeof(elev);
    GetTokenInformation(tok, TokenElevation, &elev, sizeof(elev), &sz);
    CloseHandle(tok);
    return elev.TokenIsElevated;
}

int main(void)
{
    if (!is_elevated()) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        ShellExecuteA(NULL, "runas", path, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    if (!smm_open()) {
        printf("channel open failed\n");
        return 1;
    }
    printf("channel open ok\n");

    printf("trying sharedmem\n");
    for (int a = 0; a < 30; a++) {
        if (smm_setup_shmem()) break;
        Sleep(2000);
    }

    if (g_use_shmem)
        printf("shared mem ok\n");
    else
        printf("shared mem failed, nv fallback\n");

    printf("trying init\n");
    int inited = 0;
    for (int i = 0; i < 30; i++) {
        if (smm_init()) { inited = 1; break; }
        Sleep(1000);
    }

    if (inited)
        printf("init ok\n");
    else {
        printf("init failed\n");
        return 1;
    }

    u64 sys = smm_find_process("System");
    if (!sys) {
        printf("system process not found\n");
        return 1;
    }
    u64 sys_cr3 = smm_get_cr3(sys);
    printf("system eprocess = 0x%llx ; cr3 = 0x%llx\n", sys, sys_cr3);

    return 0;
}
