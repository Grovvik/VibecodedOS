#ifndef _KERNEL_NET_TCP_H_
#define _KERNEL_NET_TCP_H_

#include "types.h"

#define TCP_STATE_CLOSED       0
#define TCP_STATE_LISTEN       1
#define TCP_STATE_SYN_SENT     2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED  4
#define TCP_STATE_FIN_WAIT_1   5
#define TCP_STATE_FIN_WAIT_2   6
#define TCP_STATE_CLOSE_WAIT   7
#define TCP_STATE_LAST_ACK     8
#define TCP_STATE_CLOSING      9
#define TCP_STATE_TIME_WAIT    10

void TcpInit(void);
i32  TcpSocketCreate(void);
ntstatus TcpConnect(i32 sock, u32 dst_ip, u16 dst_port);
i32  TcpSend(i32 sock, const void* data, u16 len);
i32  TcpRecv(i32 sock, void* buf, u16 max_len, u32 timeout_ms);
ntstatus TcpClose(i32 sock);
void TcpHandlePacket(u32 src_ip, u32 dst_ip, const void* tcp_data, u16 len);
void TcpPoll(void);

#endif
