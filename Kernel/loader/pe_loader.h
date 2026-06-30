#ifndef _KERNEL_LOADER_PE_H_
#define _KERNEL_LOADER_PE_H_

#include "types.h"
#include "error.h"

struct KProcess;

ntstatus PeLoadProgram(const char* path, struct KProcess** out_process);

#endif