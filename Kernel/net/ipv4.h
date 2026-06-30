#ifndef _KERNEL_NET_IPV4_H_
#define _KERNEL_NET_IPV4_H_

#include "types.h"
#include "e1000.h"

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

#pragma pack(push, 1)
typedef struct {
    u8  ihl_version;
    u8  tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} Ipv4Header;
#pragma pack(pop)

void Ipv4Init(u32 our_ip, u32 gateway, u32 netmask);
void Ipv4SetOurIp(u32 ip);
u32  Ipv4OurIp(void);
ntstatus Ipv4Send(u32 dst_ip, u8 proto, const void* payload, u16 len);
void Ipv4HandlePacket(const Ipv4Header* hdr, u16 len);
u16 Ipv4Checksum(const void* data, u16 len);

#endif
