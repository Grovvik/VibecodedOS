#ifndef _KERNEL_NET_ARP_H_
#define _KERNEL_NET_ARP_H_

#include "types.h"
#include "e1000.h"

#define ARP_HTYPE_ETH   1
#define ARP_PTYPE_IPv4  0x0800
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#pragma pack(push, 1)
typedef struct {
    u16 htype;
    u16 ptype;
    u8  hlen;
    u8  plen;
    u16 oper;
    MacAddr sha;
    u32 spa;
    MacAddr tha;
    u32 tpa;
} ArpPacket;
#pragma pack(pop)

void ArpInit(void);
void ArpSetOurIp(u32 ip);
void ArpHandlePacket(const ArpPacket* pkt);
i32  ArpResolve(u32 ip, MacAddr* out_mac);
void ArpSendRequest(u32 target_ip);

#endif
