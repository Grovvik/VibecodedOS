#ifndef _SHARED_ERROR_H_
#define _SHARED_ERROR_H_

#include "types.h"

#define STATUS_SUCCESS              ((ntstatus)0x00000000)
#define STATUS_UNSUCCESSFUL        ((ntstatus)0xC0000001)
#define STATUS_NOT_FOUND           ((ntstatus)0xC0000225)
#define STATUS_OUT_OF_MEMORY       ((ntstatus)0xC0000017)
#define STATUS_ACCESS_DENIED       ((ntstatus)0xC0000022)
#define STATUS_INVALID_PARAMETER   ((ntstatus)0xC000000D)
#define STATUS_NO_SUCH_FILE        ((ntstatus)0xC000000F)
#define STATUS_DISK_FULL           ((ntstatus)0xC000007F)
#define STATUS_END_OF_FILE         ((ntstatus)0xC0000011)

#define NT_SUCCESS(Status) (((ntstatus)(Status) & 0x80000000) == 0)
#define NT_ERROR(Status)   (((ntstatus)(Status) & 0x80000000) != 0)

#endif
