#ifndef _KERNEL_NET_NET_H_
#define _KERNEL_NET_NET_H_

#include "types.h"

void NetInit(u32 our_ip, u32 gateway, u32 netmask);
void NetPoll(void);
i32  NetInitialized(void);

#endif
