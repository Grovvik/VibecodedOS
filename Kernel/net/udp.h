#ifndef _KERNEL_NET_UDP_H_
#define _KERNEL_NET_UDP_H_

#include "types.h"

/* Send a single UDP datagram to dst_ip:dst_port from src_port */
ntstatus UdpSend(u32 dst_ip, u16 dst_port, u16 src_port,
                 const void* data, u16 len);

/* Receive a UDP datagram on local_port, with timeout.
   Returns number of bytes copied, 0 on timeout, -1 on error.
   src_ip is filled with sender IP if not NULL. */
i32 UdpRecv(u16 local_port, void* buf, u16 max_len,
            u32 timeout_ms, u32* src_ip);

/* Called from IPv4 dispatch for proto=17 packets */
void UdpHandlePacket(u32 src_ip, const void* data, u16 len);

/* Resolve hostname via DNS (QEMU DNS at 10.0.2.3).
   Returns IPv4 address in network byte order, 0 on failure. */
u32 DnsResolve(const char* hostname);

#endif
