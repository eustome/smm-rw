#pragma once

#define CMD_PING       0
#define CMD_INIT       1
#define CMD_READ_VIRT  2
#define CMD_READ_PHYS  3
#define CMD_WRITE_VIRT 4
#define CMD_WRITE_PHYS 5
#define CMD_FIND_PROC  6
#define CMD_GET_CR3    7
#define CMD_GET_BASE   8
#define CMD_SETUP_SHMEM 9

#define SMM_STATUS_IDLE    0
#define SMM_STATUS_PENDING 1
#define SMM_STATUS_OK      2
#define SMM_STATUS_ERROR   3

#define SHARED_MAGIC  0x7A3F9B2E4D1C8A56ULL

#define COMM_GUID_INIT { 0x78876464, 0x9753, 0xa77a, { 0x76, 0x07, 0x65, 0x25, 0x33, 0x89, 0x12, 0x31 } }
#define COMM_VAR_ATTR  0x07

#pragma pack(push, 1)
typedef struct {
    unsigned __int64 magic;
    unsigned int   seq;
    unsigned int   cmd;
    unsigned int   status;
    unsigned int   data_size;
    unsigned __int64 param1;
    unsigned __int64 param2;
    unsigned __int64 param3;
    unsigned __int64 result;
    unsigned char  data[4040];
} SMM_COMM_BUFFER;
#pragma pack(pop)

#define SMM_DATA_SIZE 4040
#define SMM_HDR_SIZE  56
