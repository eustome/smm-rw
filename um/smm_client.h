#pragma once
#include <windows.h>
#include "smm_api.h"

typedef long NTSTATUS;

typedef struct {
    unsigned short Length;
    unsigned short MaximumLength;
    wchar_t *Buffer;
} USTRING;

typedef NTSTATUS (NTAPI *pNtSetSysEnvEx)(USTRING*, GUID*, void*, unsigned long, unsigned long);
typedef NTSTATUS (NTAPI *pNtQuerySysEnvEx)(USTRING*, GUID*, void*, unsigned long*, unsigned long*);

static pNtSetSysEnvEx   g_SetVar;
static pNtQuerySysEnvEx g_GetVar;
static unsigned int     g_seq = 1;
static GUID g_comm_guid = COMM_GUID_INIT;

static wchar_t g_cmd_name[] = L"Cmd";
static wchar_t g_rsp_name[] = L"Rsp";
static USTRING g_us_cmd = { 12, 14, g_cmd_name };
static USTRING g_us_rsp = { 12, 14, g_rsp_name };

static volatile SMM_COMM_BUFFER *g_shmem;
static int g_use_shmem;

static BOOL smm_enable_priv(const char *priv)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return FALSE;
    LookupPrivilegeValueA(NULL, priv, &tp.Privileges[0].Luid);
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
    DWORD err = GetLastError();
    CloseHandle(tok);
    return err == ERROR_SUCCESS;
}

static int smm_open(void)
{
    if (g_SetVar) return 1;
    smm_enable_priv("SeDebugPrivilege");
    smm_enable_priv("SeSystemEnvironmentPrivilege");
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    g_SetVar = (pNtSetSysEnvEx)GetProcAddress(ntdll, "NtSetSystemEnvironmentValueEx");
    g_GetVar = (pNtQuerySysEnvEx)GetProcAddress(ntdll, "NtQuerySystemEnvironmentValueEx");
    if (!g_SetVar || !g_GetVar) { g_SetVar = 0; g_GetVar = 0; return 0; }

    SMM_COMM_BUFFER cleanup;
    unsigned long csz = sizeof(cleanup);
    unsigned long cattr = 0;
    if (g_GetVar(&g_us_rsp, &g_comm_guid, &cleanup, &csz, &cattr) == 0)
        g_SetVar(&g_us_rsp, &g_comm_guid, 0, 0, 0);
    csz = sizeof(cleanup);
    if (g_GetVar(&g_us_cmd, &g_comm_guid, &cleanup, &csz, &cattr) == 0)
        g_SetVar(&g_us_cmd, &g_comm_guid, 0, 0, 0);

    return 1;
}

static void smm_close(void)
{
    if (g_shmem) {
        g_shmem->magic = 0;
        VirtualFree((void*)g_shmem, 0, MEM_RELEASE);
        g_shmem = 0;
    }
    if (!g_SetVar) return;
    g_SetVar(&g_us_rsp, &g_comm_guid, 0, 0, 0);
    g_SetVar(&g_us_cmd, &g_comm_guid, 0, 0, 0);
}

static int smm_call_nv(SMM_COMM_BUFFER *buf, int timeout_ms)
{
    if (!g_SetVar) return 0;

    buf->magic = SHARED_MAGIC;
    buf->status = SMM_STATUS_PENDING;
    buf->seq = g_seq++;

    unsigned int seq = buf->seq;
    unsigned long cmd_size = SMM_HDR_SIZE + buf->data_size;
    if (cmd_size > sizeof(SMM_COMM_BUFFER)) cmd_size = sizeof(SMM_COMM_BUFFER);

    NTSTATUS st = g_SetVar(&g_us_cmd, &g_comm_guid, buf, cmd_size, COMM_VAR_ATTR);
    if (st != 0) return 0;

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        SMM_COMM_BUFFER rsp;
        unsigned long rsz = sizeof(rsp);
        unsigned long rattr = 0;

        st = g_GetVar(&g_us_rsp, &g_comm_guid, &rsp, &rsz, &rattr);
        if (st == 0 && rsz >= SMM_HDR_SIZE) {
            if (rsp.magic == SHARED_MAGIC && rsp.seq == seq) {
                memcpy(buf, &rsp, rsz);
                if (rsz < sizeof(SMM_COMM_BUFFER))
                    memset((unsigned char*)buf + rsz, 0, sizeof(SMM_COMM_BUFFER) - rsz);
                return buf->status == SMM_STATUS_OK;
            }
        }

        if (elapsed == 0) { elapsed++; continue; }
        Sleep(1);
        elapsed++;
    }
    return 0;
}

static int smm_call_shmem(SMM_COMM_BUFFER *buf, int timeout_ms)
{
    buf->magic = SHARED_MAGIC;
    buf->seq = g_seq++;
    buf->status = SMM_STATUS_PENDING;

    unsigned long size = SMM_HDR_SIZE + buf->data_size;
    if (size > sizeof(SMM_COMM_BUFFER)) size = sizeof(SMM_COMM_BUFFER);

    memcpy((void*)g_shmem, buf, size);
    MemoryBarrier();

    g_SetVar(&g_us_cmd, &g_comm_guid, 0, 0, COMM_VAR_ATTR);

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        MemoryBarrier();
        unsigned int st = ((volatile SMM_COMM_BUFFER*)g_shmem)->status;
        if (st == SMM_STATUS_OK || st == SMM_STATUS_ERROR) {
            unsigned long rsz = SMM_HDR_SIZE + g_shmem->data_size;
            if (rsz > sizeof(SMM_COMM_BUFFER)) rsz = sizeof(SMM_COMM_BUFFER);
            memcpy(buf, (void*)g_shmem, rsz);
            if (rsz < sizeof(SMM_COMM_BUFFER))
                memset((unsigned char*)buf + rsz, 0, sizeof(SMM_COMM_BUFFER) - rsz);
            return buf->status == SMM_STATUS_OK;
        }
        Sleep(1);
        elapsed++;
    }
    return 0;
}

static int smm_call(SMM_COMM_BUFFER *buf, int timeout_ms)
{
    if (g_use_shmem && g_shmem)
        return smm_call_shmem(buf, timeout_ms);
    return smm_call_nv(buf, timeout_ms);
}

static int smm_setup_shmem(void)
{
    for (int i = 0; i < 20; i++) {
        unsigned char k = 0x42;
        g_SetVar(&g_us_cmd, &g_comm_guid, &k, 1, COMM_VAR_ATTR);
    }

    g_shmem = (volatile SMM_COMM_BUFFER*)VirtualAlloc(NULL, sizeof(SMM_COMM_BUFFER),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_shmem) return 0;

    if (!VirtualLock((void*)g_shmem, sizeof(SMM_COMM_BUFFER))) {
        VirtualFree((void*)g_shmem, 0, MEM_RELEASE);
        g_shmem = 0;
        return 0;
    }

    g_shmem->magic = SHARED_MAGIC;
    g_shmem->status = SMM_STATUS_IDLE;

    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_SETUP_SHMEM;
    buf.param1 = (unsigned __int64)g_shmem;
    {
        char path[260] = {0};
        GetModuleFileNameA(NULL, path, 260);
        char *p = path;
        for (char *s = path; *s; s++) { if (*s == '\\' || *s == '/') p = s + 1; }
        int len = 0;
        while (p[len] && len < 14) { buf.data[len] = p[len]; len++; }
        buf.data[len] = 0;
        buf.data_size = len + 1;
    }

    buf.magic = SHARED_MAGIC;
    buf.status = SMM_STATUS_PENDING;
    buf.seq = g_seq++;
    unsigned long sz = SMM_HDR_SIZE + buf.data_size;
    if (sz > sizeof(SMM_COMM_BUFFER)) sz = sizeof(SMM_COMM_BUFFER);
    g_SetVar(&g_us_cmd, &g_comm_guid, &buf, sz, COMM_VAR_ATTR);

    g_SetVar(&g_us_cmd, &g_comm_guid, 0, 0, 0);
    g_SetVar(&g_us_rsp, &g_comm_guid, 0, 0, 0);

    g_use_shmem = 1;

    for (int i = 0; i < 10; i++) {
        SMM_COMM_BUFFER ping = {0};
        ping.cmd = CMD_PING;
        if (smm_call_shmem(&ping, 3000))
            return 1;
        Sleep(500);
    }

    g_use_shmem = 0;
    VirtualFree((void*)g_shmem, 0, MEM_RELEASE);
    g_shmem = 0;
    return 0;
}

static int smm_ping(void)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_PING;
    return smm_call(&buf, 5000);
}

static int smm_init(void)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_INIT;
    return smm_call(&buf, 5000);
}

static unsigned __int64 smm_find_process(const char *name)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_FIND_PROC;
    int len = 0;
    while (name[len] && len < 62) { buf.data[len] = name[len]; len++; }
    buf.data[len] = 0;
    buf.data_size = len + 1;
    if (smm_call(&buf, 2000))
        return buf.result;
    return 0;
}

static unsigned __int64 smm_get_cr3(unsigned __int64 eprocess)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_GET_CR3;
    buf.param1 = eprocess;
    if (smm_call(&buf, 2000))
        return buf.result;
    return 0;
}

static unsigned __int64 smm_get_module_base(unsigned __int64 cr3, unsigned __int64 eprocess, const char *name)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_GET_BASE;
    buf.param1 = cr3;
    buf.param2 = eprocess;
    int len = 0;
    while (name[len] && len < 126) { buf.data[len] = name[len]; len++; }
    buf.data[len] = 0;
    buf.data_size = len + 1;
    if (smm_call(&buf, 2000))
        return buf.result;
    return 0;
}

static int smm_read_virt(unsigned __int64 cr3, unsigned __int64 addr, void *out, unsigned int size)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_READ_VIRT;
    buf.param1 = cr3;
    buf.param2 = addr;
    buf.param3 = size > SMM_DATA_SIZE ? SMM_DATA_SIZE : size;
    if (smm_call(&buf, 2000)) {
        memcpy(out, buf.data, buf.data_size);
        return 1;
    }
    return 0;
}

static int smm_write_virt(unsigned __int64 cr3, unsigned __int64 addr, void *in, unsigned int size)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_WRITE_VIRT;
    buf.param1 = cr3;
    buf.param2 = addr;
    buf.data_size = size > SMM_DATA_SIZE ? SMM_DATA_SIZE : size;
    memcpy(buf.data, in, buf.data_size);
    return smm_call(&buf, 2000);
}

static int smm_read_phys(unsigned __int64 addr, void *out, unsigned int size)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_READ_PHYS;
    buf.param1 = addr;
    buf.param2 = size > SMM_DATA_SIZE ? SMM_DATA_SIZE : size;
    if (smm_call(&buf, 2000)) {
        memcpy(out, buf.data, buf.data_size);
        return 1;
    }
    return 0;
}

static int smm_write_phys(unsigned __int64 addr, void *in, unsigned int size)
{
    SMM_COMM_BUFFER buf = {0};
    buf.cmd = CMD_WRITE_PHYS;
    buf.param1 = addr;
    buf.data_size = size > SMM_DATA_SIZE ? SMM_DATA_SIZE : size;
    memcpy(buf.data, in, buf.data_size);
    return smm_call(&buf, 2000);
}
