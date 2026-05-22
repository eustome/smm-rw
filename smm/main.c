#include "stdafx.h"
#include "smm_api.h"

unsigned __int64 __readmsr(unsigned long);
unsigned __int64 __readcr0(void);
void __writecr0(unsigned __int64);
void __cpuid(int[4], int);
void _mm_clflush(void const *);
#pragma intrinsic(__readmsr, __readcr0, __writecr0, __cpuid, _mm_clflush)

EFI_GUID gEfiSmmBase2ProtocolGuid = { 0xf4ccbfb7, 0xf6e0, 0x47fd, { 0x9d, 0xd4, 0x10, 0xa8, 0xf1, 0x50, 0xc1, 0x91 }};
EFI_GUID gEfiSmmCpuProtocolGuid = { 0xeb346b97, 0x975f, 0x4a9f, { 0x8b, 0x22, 0xf8, 0xe9, 0x2b, 0xb3, 0xd5, 0x69 }};
EFI_GUID gSmmVarGuid = { 0xED32D533, 0x99E6, 0x4209, { 0x9C, 0xC0, 0x2D, 0x72, 0xCD, 0xD9, 0x98, 0xA7 }};
EFI_GUID gCommGuid = COMM_GUID_INIT;

typedef struct {
    EFI_GET_VARIABLE            SmmGetVariable;
    EFI_GET_NEXT_VARIABLE_NAME  SmmGetNextVariableName;
    EFI_SET_VARIABLE            SmmSetVariable;
} SMM_VAR_PROTO;

EFI_BOOT_SERVICES     *gBS;
EFI_SMM_SYSTEM_TABLE2 *gSMST;
EFI_SMM_CPU_PROTOCOL  *gCpu;

static QWORD g_tom;
static QWORD g_tom2;
static QWORD g_c_bit;
static volatile long gBusy;

#define H_PSISP   0x367B76E6
#define H_PGPIFN  0xA0AFFDBC
#define H_PGPEPC  0xB6E740E5
#define H_PGPID   0xF58C3ACB
#define H_PGPPEB  0x77EE6188

#define MSR_LSTAR 0xC0000082

static QWORD system_cr3;
static QWORD ntoskrnl;
static QWORD g_psisp;
static DWORD off_ifn;
static DWORD off_epc;
static DWORD off_apl;
static DWORD off_peb;
static BOOLEAN gInit;
static SMM_VAR_PROTO *gVar;

static SMM_COMM_BUFFER g_local;

static QWORD g_shmem_pa;
static QWORD g_shmem_cr3;
static UINT32 g_shmem_last_seq;

static CHAR16 gCmdName[] = L"Cmd";
static CHAR16 gRspName[] = L"Rsp";

static QWORD g_scan_resume;
static QWORD g_tseg_base;
static QWORD g_tseg_size;

static const unsigned int ct[] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
    0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
    0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
    0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
    0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
    0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
    0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
    0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
    0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
    0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
    0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
    0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
    0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
    0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
    0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
    0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
    0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
    0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
    0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
    0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
    0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
    0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
    0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
    0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
    0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
    0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
    0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
    0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
    0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
    0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
    0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
    0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
    0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
    0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
    0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
    0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
    0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
    0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
    0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
    0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
    0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
    0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
    0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
    0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
    0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
    0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
    0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
    0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
    0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
    0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
    0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
    0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
    0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

static DWORD hash_str(const char *s)
{
    DWORD c = 0;
    while (*s) { c = ((c << 8) & 0xFFFFFFFF) ^ ct[((c >> 24) ^ (UINT8)*s) & 0xFF]; s++; }
    return c;
}

static BOOLEAN safe_pa(QWORD pa)
{
    if (pa < 0x1000) return 0;
    if (pa >= 0xA0000 && pa < 0xC0000) return 0;
    if (g_tseg_base && pa >= g_tseg_base && pa < g_tseg_base + g_tseg_size) return 0;
    if (pa < 0x100000) return 1;
    if (g_tom && pa < g_tom) return 1;
    if (g_tom2 && pa >= 0x100000000ULL && pa < g_tom2) return 1;
    return 0;
}

static QWORD pa4k(QWORD e) { return (e & ~g_c_bit) & 0xFFFFFFFFF000ULL; }
static QWORD pa2m(QWORD e) { return (e & ~g_c_bit) & 0xFFFFFFE00000ULL; }
static QWORD pa1g(QWORD e) { return (e & ~g_c_bit) & 0xFFFFC0000000ULL; }

static void pm_read(QWORD pa, void *buf, QWORD len)
{
    if (!len) return;
    if (!safe_pa(pa) || !safe_pa(pa + len - 1)) return;
    volatile UINT8 *s = (volatile UINT8*)pa;
    for (QWORD i = 0; i < len; i++) ((UINT8*)buf)[i] = s[i];
}

static void pm_write(QWORD pa, void *buf, QWORD len)
{
    if (!len) return;
    if (!safe_pa(pa) || !safe_pa(pa + len - 1)) return;
    volatile UINT8 *d = (volatile UINT8*)pa;
    for (QWORD i = 0; i < len; i++) d[i] = ((UINT8*)buf)[i];
    for (QWORD i = 0; i < len; i += 64) _mm_clflush((void const*)(pa+i));
}

static BOOLEAN pm_read_ok(QWORD pa, void *buf, QWORD len)
{
    if (!len) return 0;
    if (!safe_pa(pa) || !safe_pa(pa + len - 1)) return 0;
    volatile UINT8 *s = (volatile UINT8*)pa;
    for (QWORD i = 0; i < len; i++) ((UINT8*)buf)[i] = s[i];
    return 1;
}

static QWORD pm_read64(QWORD a) { QWORD r=0; pm_read(a,&r,8); return r; }
static UINT32 pm_read32(QWORD a) { UINT32 r=0; pm_read(a,&r,4); return r; }
static UINT16 pm_read16(QWORD a) { UINT16 r=0; pm_read(a,&r,2); return r; }

static QWORD pm_translate(QWORD dir, QWORD va)
{
    QWORD v2 = pm_read64(8 * ((va >> 39) & 0x1FF) + dir);
    if (!v2 || !(v2 & 1)) return 0;

    QWORD v3 = pm_read64(pa4k(v2) + 8 * ((va >> 30) & 0x1FF));
    if (!v3 || !(v3 & 1)) return 0;
    if (v3 & 0x80) return (va & 0x3FFFFFFF) + pa1g(v3);

    QWORD v5 = pm_read64(pa4k(v3) + 8 * ((va >> 21) & 0x1FF));
    if (!v5 || !(v5 & 1)) return 0;
    if (v5 & 0x80) return (va & 0x1FFFFF) + pa2m(v5);

    QWORD v6 = pm_read64(pa4k(v5) + 8 * ((va >> 12) & 0x1FF));
    if (v6 && (v6 & 1)) return (va & 0xFFF) + pa4k(v6);
    return 0;
}

static BOOLEAN vm_read(QWORD cr3, QWORD address, VOID *buffer, QWORD length)
{
    QWORD offset = 0;
    while (offset < length) {
        QWORD pa = pm_translate(cr3, address + offset);
        QWORD chunk = 0x1000 - ((address + offset) & 0xFFF);
        if (chunk > length - offset) chunk = length - offset;
        if (!pa) {
            for (QWORD i = 0; i < chunk; i++) ((UINT8*)buffer)[offset+i] = 0;
        } else {
            pm_read(pa, (UINT8*)buffer + offset, chunk);
        }
        offset += chunk;
    }
    return 1;
}

static void vm_write(QWORD cr3, QWORD address, VOID *buffer, QWORD length)
{
    QWORD offset = 0;
    while (offset < length) {
        QWORD pa = pm_translate(cr3, address + offset);
        QWORD chunk = 0x1000 - ((address + offset) & 0xFFF);
        if (chunk > length - offset) chunk = length - offset;
        if (pa) pm_write(pa, (UINT8*)buffer + offset, chunk);
        offset += chunk;
    }
}

static QWORD vm_read_i64(QWORD cr3, QWORD a) { QWORD r=0; vm_read(cr3,a,&r,8); return r; }

static int to_lower(int c) { return (c>='A'&&c<='Z') ? c+32 : c; }
static int scmp(const char *s1, const char *s2)
{
    while (*s1 && (to_lower(*s1)==to_lower(*s2))) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static QWORD vm_get_export_h(QWORD cr3, QWORD module, DWORD target)
{
    QWORD a0;
    DWORD a1[4];
    char a2[120];

    QWORD pe_pa = pm_translate(cr3, module + 0x3C);
    if (!pe_pa) return 0;
    a0 = module + pm_read16(pe_pa);

    QWORD exp_pa = pm_translate(cr3, a0 + 0x88);
    if (!exp_pa) return 0;
    a0 = module + pm_read32(exp_pa);

    a1[0] = 0;
    QWORD pa18 = pm_translate(cr3, a0 + 0x18);
    if (!pa18) return 0;
    pm_read(pa18, &a1[0], 8);

    QWORD pa20 = pm_translate(cr3, a0 + 0x20);
    if (!pa20) return 0;
    pm_read(pa20, &a1[2], 8);

    if (a1[0] > 0x10000) a1[0] = 0x10000;

    while (a1[0]--) {
        QWORD rva_pa = pm_translate(cr3, module + a1[2] + (a1[0]*4));
        if (!rva_pa) continue;
        QWORD rva = pm_read32(rva_pa);
        QWORD name_pa = pm_translate(cr3, module + rva);
        if (!name_pa) continue;
        pm_read(name_pa, a2, 119);
        a2[119] = 0;
        if (hash_str(a2) == target) {
            QWORD ord_pa = pm_translate(cr3, module + a1[3] + (a1[0]*2));
            if (!ord_pa) return 0;
            WORD ord = pm_read16(ord_pa);
            QWORD fn_pa = pm_translate(cr3, module + a1[1] + (ord * 4));
            if (!fn_pa) return 0;
            return module + pm_read32(fn_pa);
        }
    }
    return 0;
}

static BOOLEAN do_init(QWORD ntos_base, QWORD cr3)
{
    if (!cr3 || !ntos_base) return 0;
    QWORD pa = pm_translate(cr3, ntos_base);
    if (!pa) return 0;
    if (pm_read16(pa) != 0x5A4D) return 0;

    ntoskrnl = ntos_base;

    g_psisp = vm_get_export_h(cr3, ntoskrnl, H_PSISP);
    if (!g_psisp) return 0;

    QWORD fn, tpa;

    fn = vm_get_export_h(cr3, ntoskrnl, H_PGPIFN);
    if (!fn) return 0;
    tpa = pm_translate(cr3, fn + 3);
    if (!tpa) return 0;
    off_ifn = pm_read32(tpa);

    fn = vm_get_export_h(cr3, ntoskrnl, H_PGPEPC);
    if (!fn) return 0;
    tpa = pm_translate(cr3, fn + 2);
    if (!tpa) return 0;
    off_epc = pm_read32(tpa);

    fn = vm_get_export_h(cr3, ntoskrnl, H_PGPID);
    if (!fn) return 0;
    tpa = pm_translate(cr3, fn + 3);
    if (!tpa) return 0;
    off_apl = pm_read32(tpa) + 8;

    fn = vm_get_export_h(cr3, ntoskrnl, H_PGPPEB);
    if (!fn) return 0;
    tpa = pm_translate(cr3, fn + 3);
    if (!tpa) return 0;
    off_peb = pm_read32(tpa);

    QWORD sys_pa = pm_translate(cr3, g_psisp);
    if (!sys_pa) return 0;
    QWORD sys = pm_read64(sys_pa);
    if (!sys) return 0;

    QWORD scr3_pa = pm_translate(cr3, sys + 0x28);
    if (!scr3_pa) return 0;
    system_cr3 = pm_read64(scr3_pa) & 0xFFFFFFFFF000ULL;
    if (!system_cr3) return 0;

    gInit = 1;
    return 1;
}

static QWORD find_process(const char *name)
{
    if (!gInit) return 0;
    char pname[16] = {0};
    QWORD proc_pa = pm_translate(system_cr3, g_psisp);
    if (!proc_pa) return 0;
    QWORD proc = pm_read64(proc_pa);
    if (!proc) return 0;
    QWORD entry = proc;
    int limit = 2000;
    do {
        QWORD pa = pm_translate(system_cr3, entry + off_ifn);
        if (!pa) break;
        pm_read(pa, pname, 15);
        QWORD epa = pm_translate(system_cr3, entry + off_epc);
        if (!epa) break;
        if (!((pm_read32(epa) >> 2) & 1) && !scmp(pname, name))
            return entry;
        QWORD npa = pm_translate(system_cr3, entry + off_apl);
        if (!npa) break;
        entry = pm_read64(npa);
        if (!entry) break;
        entry -= off_apl;
    } while (entry != proc && --limit > 0);
    return 0;
}

static QWORD find_module(QWORD cr3, QWORD eprocess, const unsigned char *name)
{
    if (!gInit || !eprocess || !cr3) return 0;
    QWORD peb_pa = pm_translate(system_cr3, eprocess + off_peb);
    if (!peb_pa) return 0;
    QWORD peb = pm_read64(peb_pa);
    if (!peb) return 0;
    QWORD ldr = vm_read_i64(cr3, peb + 0x18);
    if (!ldr) return 0;
    QWORD first = vm_read_i64(cr3, ldr + 0x20);
    if (!first) return 0;
    QWORD entry = first;
    int limit = 500;
    do {
        unsigned short mn[64] = {0};
        QWORD np = vm_read_i64(cr3, entry + 0x50);
        if (np) vm_read(cr3, np, mn, 128);
        int match = 1;
        for (int i = 0; name[i]; i++) {
            int a = mn[i], b = name[i];
            if (a>='A'&&a<='Z') a+=32;
            if (b>='A'&&b<='Z') b+=32;
            if (a!=b) { match=0; break; }
        }
        if (match) return vm_read_i64(cr3, entry + 0x20);
        entry = vm_read_i64(cr3, entry);
    } while (entry && entry != first && --limit > 0);
    return 0;
}

static VOID process_command(SMM_COMM_BUFFER *buf)
{
    DWORD in_data_size = buf->data_size;
    buf->data_size = 0;
    switch (buf->cmd) {
    case CMD_PING:
        buf->result = 1;
        buf->status = SMM_STATUS_OK;
        break;
    case CMD_INIT:
        buf->status = gInit ? SMM_STATUS_OK : SMM_STATUS_ERROR;
        if (gInit) buf->result = system_cr3;
        break;
    case CMD_READ_VIRT: {
        DWORD sz = (DWORD)buf->param3;
        if (sz > SMM_DATA_SIZE) sz = SMM_DATA_SIZE;
        if (buf->param1 && buf->param2 && vm_read(buf->param1, buf->param2, buf->data, sz)) {
            buf->data_size = sz; buf->status = SMM_STATUS_OK;
        } else buf->status = SMM_STATUS_ERROR;
        break;
    }
    case CMD_READ_PHYS: {
        DWORD sz = (DWORD)buf->param2;
        if (sz > SMM_DATA_SIZE) sz = SMM_DATA_SIZE;
        if (pm_read_ok(buf->param1, buf->data, sz)) {
            buf->data_size = sz; buf->status = SMM_STATUS_OK;
        } else buf->status = SMM_STATUS_ERROR;
        break;
    }
    case CMD_WRITE_VIRT: {
        DWORD sz = in_data_size;
        if (sz > SMM_DATA_SIZE) sz = SMM_DATA_SIZE;
        if (buf->param1 && buf->param2) {
            vm_write(buf->param1, buf->param2, buf->data, sz);
            buf->status = SMM_STATUS_OK;
        } else buf->status = SMM_STATUS_ERROR;
        break;
    }
    case CMD_WRITE_PHYS:
        if (buf->param1) {
            DWORD sz = in_data_size;
            if (sz > SMM_DATA_SIZE) sz = SMM_DATA_SIZE;
            pm_write(buf->param1, buf->data, sz);
            buf->status = SMM_STATUS_OK;
        } else buf->status = SMM_STATUS_ERROR;
        break;
    case CMD_FIND_PROC:
        buf->data[63] = 0;
        buf->result = find_process((const char*)buf->data);
        buf->status = buf->result ? SMM_STATUS_OK : SMM_STATUS_ERROR;
        break;
    case CMD_GET_CR3:
        if (gInit && buf->param1) {
            QWORD pa = pm_translate(system_cr3, buf->param1 + 0x28);
            if (pa) { buf->result = pm_read64(pa) & 0xFFFFFFFFF000ULL; buf->status = SMM_STATUS_OK; }
            else buf->status = SMM_STATUS_ERROR;
        } else buf->status = SMM_STATUS_ERROR;
        break;
    case CMD_GET_BASE:
        buf->data[127] = 0;
        buf->result = find_module(buf->param1, buf->param2, buf->data);
        buf->status = buf->result ? SMM_STATUS_OK : SMM_STATUS_ERROR;
        break;
    case CMD_SETUP_SHMEM: {
        buf->data[63] = 0;
        QWORD proc = find_process((const char*)buf->data);
        if (!proc) { buf->status = SMM_STATUS_ERROR; break; }
        QWORD cr3_pa = pm_translate(system_cr3, proc + 0x28);
        if (!cr3_pa) { buf->status = SMM_STATUS_ERROR; break; }
        QWORD cr3 = pm_read64(cr3_pa) & 0xFFFFFFFFF000ULL;
        if (!cr3) { buf->status = SMM_STATUS_ERROR; break; }
        QWORD va = buf->param1;
        QWORD pa = pm_translate(cr3, va);
        if (!pa) { buf->status = SMM_STATUS_ERROR; break; }
        QWORD mg = pm_read64(pa);
        if (mg != SHARED_MAGIC) { buf->status = SMM_STATUS_ERROR; break; }
        g_shmem_pa = pa;
        g_shmem_cr3 = cr3;
        g_shmem_last_seq = 0;
        buf->result = pa;
        buf->status = SMM_STATUS_OK;
        break;
    }
    default:
        buf->status = SMM_STATUS_ERROR;
    }
}

#define KERN_VA_START 0xFFFFF80000000000ULL
#define KERN_VA_END   0xFFFFF80080000000ULL
#define KERN_VA_STEP  0x200000ULL

static BOOLEAN is_kernel_pml4(QWORD cr3)
{
    QWORD pml4e = pm_read64(cr3 + ((KERN_VA_START >> 39) & 0x1FF) * 8);
    if (!(pml4e & 1)) return 0;
    if (pa4k(pml4e) < 0x100000) return 0;
    if (pa4k(pml4e) >= 0x10000000000ULL) return 0;

    int populated = 0;
    for (int i = 256; i < 512; i++) {
        QWORD e = pm_read64(cr3 + i * 8);
        if (e & 1) populated++;
    }
    if (populated < 4) return 0;

    return 1;
}

static QWORD find_ntos_via_lstar(QWORD cr3)
{
    QWORD lstar = __readmsr(MSR_LSTAR);
    if (lstar < 0xFFFFF80000000000ULL) return 0;

    QWORD page = lstar & ~0xFFFULL;
    for (QWORD i = 0; i < 0x2000; i++) {
        QWORD va = page - (i << 12);
        if (va < 0xFFFFF80000000000ULL) break;
        QWORD pa = pm_translate(cr3, va);
        if (!pa) continue;
        if (pm_read16(pa) != 0x5A4D) continue;
        DWORD pe_off = pm_read32(pa + 0x3C);
        if (pe_off >= 0x1000) continue;
        if (pm_read32(pa + pe_off) != 0x00004550) continue;
        DWORD soi = pm_read32(pa + pe_off + 0x50);
        if (soi < 0x800000) continue;
        return va;
    }
    return 0;
}

static BOOLEAN try_cr3_scan(QWORD cr3, QWORD start_va, QWORD budget)
{
    QWORD count = 0;
    for (QWORD va = start_va; va < KERN_VA_END; va += KERN_VA_STEP) {
        if (++count > budget) {
            g_scan_resume = va;
            return 0;
        }
        QWORD pa = pm_translate(cr3, va);
        if (!pa || pa < 0x100000) continue;
        if (pm_read16(pa) != 0x5A4D) continue;
        DWORD pe_off = pm_read32(pa + 0x3C);
        if (pe_off >= 0x1000) continue;
        if (pm_read32(pa + pe_off) != 0x00004550) continue;
        DWORD soi = pm_read32(pa + pe_off + 0x50);
        if (soi < 0x800000) continue;
        if (do_init(va, cr3))
            return 1;
    }
    g_scan_resume = 0;
    return 0;
}

static BOOLEAN auto_init(void)
{
    if (!gCpu) return 0;

    QWORD cr3 = 0;

    QWORD boot_cr3 = pm_read64(0x10A0) & 0xFFFFFFFFF000ULL;
    if (boot_cr3 && is_kernel_pml4(boot_cr3)) {
        cr3 = boot_cr3;
        QWORD ntos = find_ntos_via_lstar(cr3);
        if (ntos && do_init(ntos, cr3))
            return 1;
    }

    for (UINTN cpu = 0; cpu < gSMST->NumberOfCpus; cpu++) {
        cr3 = 0;
        EFI_STATUS st = gCpu->ReadSaveState(gCpu, 8, EFI_SMM_SAVE_STATE_REGISTER_CR3, cpu, &cr3);
        if (EFI_ERROR(st)) continue;
        cr3 = cr3 & 0xFFFFFFFFF000ULL;
        if (!cr3) continue;

        if (!is_kernel_pml4(cr3)) continue;

        QWORD ntos = find_ntos_via_lstar(cr3);
        if (ntos && do_init(ntos, cr3))
            return 1;

        QWORD start = g_scan_resume ? g_scan_resume : KERN_VA_START;
        if (try_cr3_scan(cr3, start, 256))
            return 1;
    }

    return 0;
}

EFI_STATUS EFIAPI SmmHandler(
    IN EFI_HANDLE DispatchHandle,
    IN CONST VOID *Context OPTIONAL,
    IN OUT VOID *CommBuffer OPTIONAL,
    IN OUT UINTN *CommBufferSize OPTIONAL)
{
    if (_InterlockedCompareExchange(&gBusy, 1, 0) != 0)
        return EFI_SUCCESS;

    unsigned __int64 old_cr0 = __readcr0();
    __writecr0(old_cr0 & ~(1ULL << 16));

    if (!gInit)
        auto_init();

    if (g_shmem_pa && gInit) {
        QWORD mg = pm_read64(g_shmem_pa);
        UINT32 st = pm_read32(g_shmem_pa + 16);
        UINT32 sq = pm_read32(g_shmem_pa + 8);
        if (mg == SHARED_MAGIC && st == SMM_STATUS_PENDING && sq != g_shmem_last_seq) {
            pm_read(g_shmem_pa, &g_local, sizeof(g_local));
            g_shmem_last_seq = g_local.seq;
            process_command(&g_local);
            g_local.magic = SHARED_MAGIC;
            pm_write(g_shmem_pa, &g_local, SMM_HDR_SIZE + g_local.data_size);
        } else if (mg != SHARED_MAGIC) {
            g_shmem_pa = 0;
        }
    }

    if (!gVar)
        gSMST->SmmLocateProtocol(&gSmmVarGuid, 0, (void**)&gVar);

    if (gVar) {
        UINTN sz = sizeof(g_local);
        EFI_STATUS vs = gVar->SmmGetVariable(gCmdName, &gCommGuid, 0, &sz, &g_local);
        if (vs == 0) {
            gVar->SmmSetVariable(gCmdName, &gCommGuid, 0, 0, 0);
            if (sz >= SMM_HDR_SIZE && g_local.magic == SHARED_MAGIC && g_local.status == SMM_STATUS_PENDING) {
                process_command(&g_local);
                UINT32 rsp_sz = SMM_HDR_SIZE + g_local.data_size;
                gVar->SmmSetVariable(gRspName, &gCommGuid, COMM_VAR_ATTR, rsp_sz, &g_local);
            }
        }
    }

    __writecr0(old_cr0);
    _InterlockedExchange(&gBusy, 0);
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI EfiMain(IN EFI_LOADED_IMAGE *LoadedImage, IN EFI_SYSTEM_TABLE *SystemTable)
{
    gBS = SystemTable->BootServices;

    EFI_SMM_BASE2_PROTOCOL *SmmBase2 = 0;
    if (EFI_ERROR(gBS->LocateProtocol(&gEfiSmmBase2ProtocolGuid, 0, (void**)&SmmBase2)))
        return 0;

    if (EFI_ERROR(SmmBase2->GetSmstLocation(SmmBase2, &gSMST)))
        return 0;

    {
        int ci[4] = {0};
        __cpuid(ci, 0);
        if (ci[1] == 0x68747541) {
            g_tom = __readmsr(0xC001001A);
            g_tom2 = __readmsr(0xC001001D);
        } else {
            __cpuid(ci, 0x80000008);
            int phys_bits = ci[0] & 0xFF;
            if (phys_bits < 36) phys_bits = 36;
            if (phys_bits > 52) phys_bits = 52;
            g_tom = 1ULL << phys_bits;
            g_tom2 = 0;
        }
        if (!g_tom) g_tom = 0x100000000ULL;
    }

    {
        int ci[4] = {0};
        __cpuid(ci, 0x80000000);
        if ((unsigned int)ci[0] >= 0x8000001F) {
            __cpuid(ci, 0x8000001F);
            if (ci[0] & 1) g_c_bit = 1ULL << (ci[1] & 0x3F);
        }
    }

    {
        int ci[4] = {0};
        __cpuid(ci, 0);
        if (ci[1] == 0x68747541) {
            QWORD smmaddr = __readmsr(0xC0010112);
            QWORD smmmask = __readmsr(0xC0010113);
            if (smmmask & 1) {
                g_tseg_base = smmaddr & 0xFFFFF000;
                QWORD mask_bits = smmmask & 0xFFFFF000;
                g_tseg_size = (~mask_bits & 0xFFFFFFFF) + 1;
            }
        } else {
            QWORD smrr_base = __readmsr(0x1F2);
            QWORD smrr_mask = __readmsr(0x1F3);
            if (smrr_mask & 0x800) {
                g_tseg_base = smrr_base & 0xFFFFF000;
                QWORD mask_bits = smrr_mask & 0xFFFFF000;
                g_tseg_size = (~mask_bits & 0xFFFFFFFF) + 1;
            }
        }
    }

    gSMST->SmmLocateProtocol(&gEfiSmmCpuProtocolGuid, 0, (void**)&gCpu);
    gSMST->SmmLocateProtocol(&gSmmVarGuid, 0, (void**)&gVar);

    EFI_HANDLE handle = 0;
    gSMST->SmiHandlerRegister(SmmHandler, 0, &handle);

    return 0;
}
