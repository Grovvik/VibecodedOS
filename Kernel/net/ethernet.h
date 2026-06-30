#ifndef _KERNEL_NET_ETHERNET_H_
#define _KERNEL_NET_ETHERNET_H_

#include "types.h"
#include "e1000.h"

#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IPv4  0x0800

#define ETH_MAX_PAYLOAD 1500

#pragma pack(push, 1)
typedef struct {
    MacAddr dst;
    MacAddr src;
    u16 type;
} EthHeader;
#pragma pack(pop)

void EthInit(void);
i32  EthInitialized(void);
void EthGetMac(MacAddr* out);
ntstatus EthSend(const MacAddr* dst, u16 type, const void* payload, u16 len);
i32  EthReceive(void* out_payload, u16 max_len, u16* out_type, MacAddr* out_src);

#endif
