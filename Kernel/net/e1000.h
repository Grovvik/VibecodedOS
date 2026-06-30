#ifndef _KERNEL_NET_E1000_H_
#define _KERNEL_NET_E1000_H_

#include "types.h"

#define NET_MAC_SIZE 6

typedef struct {
    u8 addr[NET_MAC_SIZE];
} MacAddr;

void     E1000Init(void);
i32      E1000Initialized(void);
ntstatus E1000SendFrame(const void* data, u16 len);
i32      E1000ReceiveFrame(void* out_buf, u16 max_len);
void     E1000GetMac(MacAddr* out);

#endif
