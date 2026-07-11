#include "tcp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "debug.h"
#include "runtime.h"
#include "error.h"
#include "pit.h"
#include "hal.h"

#define TCP_SOCKET_COUNT  8
#define TCP_BUF_SIZE      4096
#define TCP_MSS           1460
#define TCP_LOCAL_PORT_START 49152

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#pragma pack(push, 1)
typedef struct {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8  offset_flags;
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} TcpHeader;
#pragma pack(pop)

typedef struct {
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
    u8  state;

    u32 snd_una;
    u32 snd_nxt;
    u32 rcv_nxt;
    u32 iss;
    u32 irs;

    u8  send_buf[TCP_BUF_SIZE];
    u32 send_len;       /* total bytes waiting to be acked */
    u32 send_acked;     /* bytes already acked in send_buf */

    u8  recv_buf[TCP_BUF_SIZE];
    u32 recv_len;
    u32 recv_read;

    u16 remote_window;

    u64 retrans_tick;
    u8  retrans_count;

    i32 used;
    u8  pending_close;
} TcpSocket;

static TcpSocket g_tcp_sockets[TCP_SOCKET_COUNT];
static u16 g_next_local_port = TCP_LOCAL_PORT_START;

static u16 NetSwap16(u16 v) {
    return (v >> 8) | (v << 8);
}

static u32 NetSwap32(u32 v) {
    return ((v >> 24) & 0xFF)
         | ((v >> 8) & 0xFF00)
         | ((v << 8) & 0xFF0000)
         | ((v << 24) & 0xFF000000);
}

static u32 TcpChecksum(const TcpSocket* s, const TcpHeader* hdr,
                       const void* data, u16 len, u32 src_ip, u32 dst_ip) {
    struct {
        u32 src;
        u32 dst;
        u8  zero;
        u8  proto;
        u16 len;
    } ph;
    ph.src = src_ip;
    ph.dst = dst_ip;
    ph.zero = 0;
    ph.proto = IP_PROTO_TCP;
    ph.len = NetSwap16((u16)(sizeof(TcpHeader) + len));

    u32 sum = 0;
    const u16* ptr = (const u16*)&ph;
    for (u32 i = 0; i < sizeof(ph) / 2; i++) sum += ptr[i];

    ptr = (const u16*)hdr;
    u32 hdr_words = sizeof(TcpHeader) / 2;
    for (u32 i = 0; i < hdr_words; i++) sum += ptr[i];

    ptr = (const u16*)data;
    u32 data_words = len / 2;
    for (u32 i = 0; i < data_words; i++) sum += ptr[i];
    if (len & 1) sum += ((const u8*)data)[len - 1];

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

static TcpSocket* TcpFindSocket(u16 local_port, u32 remote_ip, u16 remote_port) {
    for (i32 i = 0; i < TCP_SOCKET_COUNT; i++) {
        TcpSocket* s = &g_tcp_sockets[i];
        if (!s->used) continue;
        if (s->local_port != local_port) continue;
        if (s->state == TCP_STATE_LISTEN) {
            if (remote_port == 0) return s;
            continue;
        }
        if (s->remote_ip == remote_ip && s->remote_port == remote_port) return s;
    }
    return NULL;
}

static TcpSocket* TcpAllocSocket(void) {
    for (i32 i = 0; i < TCP_SOCKET_COUNT; i++) {
        if (!g_tcp_sockets[i].used) {
            RtMemSet(&g_tcp_sockets[i], 0, sizeof(TcpSocket));
            g_tcp_sockets[i].used = 1;
            return &g_tcp_sockets[i];
        }
    }
    return NULL;
}

static void TcpFreeSocket(TcpSocket* s) {
    if (s) RtMemSet(s, 0, sizeof(TcpSocket));
}

void TcpInit(void) {
    RtMemSet(g_tcp_sockets, 0, sizeof(g_tcp_sockets));
}

i32 TcpSocketCreate(void) {
    TcpSocket* s = TcpAllocSocket();
    if (!s) return -1;
    s->state = TCP_STATE_CLOSED;
    return (i32)(s - g_tcp_sockets);
}

static void TcpBuildHeader(TcpSocket* s, TcpHeader* hdr, u8 flags,
                           u32 seq, u32 ack, const void* payload, u16 payload_len) {
    hdr->src_port = NetSwap16(s->local_port);
    hdr->dst_port = NetSwap16(s->remote_port);
    hdr->seq = NetSwap32(seq);
    hdr->ack = NetSwap32(ack);
    hdr->offset_flags = (5 << 4); /* 20 bytes header, no options */
    hdr->flags = flags;
    hdr->window = NetSwap16(TCP_BUF_SIZE - s->recv_len);
    hdr->checksum = 0;
    hdr->urgent = 0;
    hdr->checksum = (u16)TcpChecksum(s, hdr, payload, payload_len,
                                     Ipv4OurIp(), s->remote_ip);
}

static ntstatus TcpSendSegment(TcpSocket* s, u8 flags,
                                const void* payload, u16 payload_len,
                                u32 seq, u32 ack) {
    u8 packet[2048];
    TcpHeader* hdr = (TcpHeader*)packet;
    TcpBuildHeader(s, hdr, flags, seq, ack, payload, payload_len);
    if (payload_len > 0) {
        RtMemCopy(packet + sizeof(TcpHeader), payload, payload_len);
    }
    return Ipv4Send(s->remote_ip, IP_PROTO_TCP, packet,
                    sizeof(TcpHeader) + payload_len);
}

ntstatus TcpConnect(i32 sock, u32 dst_ip, u16 dst_port) {
    if (sock < 0 || sock >= TCP_SOCKET_COUNT) return STATUS_INVALID_PARAMETER;
    TcpSocket* s = &g_tcp_sockets[sock];
    if (!s->used || s->state != TCP_STATE_CLOSED) return STATUS_INVALID_PARAMETER;

    s->remote_ip = dst_ip;
    s->remote_port = dst_port;
    s->local_port = g_next_local_port++;
    if (g_next_local_port < TCP_LOCAL_PORT_START) g_next_local_port = TCP_LOCAL_PORT_START;

    s->iss = (u32)(KeGetTickCount() * 0x12345678);
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1;
    s->rcv_nxt = 0;
    s->state = TCP_STATE_SYN_SENT;
    s->remote_window = TCP_BUF_SIZE;
    s->retrans_tick = KeGetTickCount() + 20;
    s->retrans_count = 0;

    KdPrintf("[TCP] Connecting %d -> %u.%u.%u.%u:%u\n", sock,
             (dst_ip >> 0) & 0xFF, (dst_ip >> 8) & 0xFF,
             (dst_ip >> 16) & 0xFF, (dst_ip >> 24) & 0xFF, dst_port);

    /* Send SYN (ignore immediate failure, retransmit timer will retry) */
    TcpSendSegment(s, TCP_SYN, NULL, 0, s->iss, 0);

    /* Wait for SYN+ACK */
    u64 timeout = KeGetTickCount() + 500; /* 5 seconds */
    while (KeGetTickCount() < timeout) {
        if (s->state == TCP_STATE_ESTABLISHED) {
            KdPrintf("[TCP] Connected %d\n", sock);
            return STATUS_SUCCESS;
        }
        HalHlt();
    }

    s->state = TCP_STATE_CLOSED;
    KdPrintf("[TCP] Connect timeout %d\n", sock);
    return STATUS_UNSUCCESSFUL;
}

i32 TcpSend(i32 sock, const void* data, u16 len) {
    if (sock < 0 || sock >= TCP_SOCKET_COUNT) return -1;
    TcpSocket* s = &g_tcp_sockets[sock];
    if (!s->used || s->state != TCP_STATE_ESTABLISHED) return -1;
    if (!data || len == 0) return -1;

    const u8* src = (const u8*)data;
    u32 sent = 0;

    while (sent < len) {
        /* Wait for window space */
        u64 timeout = KeGetTickCount() + 500;
        while (s->snd_nxt - s->snd_una >= s->remote_window) {
            if (KeGetTickCount() >= timeout) return (i32)sent;
            HalHlt();
        }

        u16 chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        if (chunk > s->remote_window) chunk = (u16)s->remote_window;
        if (chunk == 0) { HalHlt(); continue; }

        ntstatus st = TcpSendSegment(s, TCP_PSH | TCP_ACK,
                                      src + sent, chunk,
                                      s->snd_nxt, s->rcv_nxt);
        if (NT_ERROR(st)) return (i32)sent;

        s->snd_nxt += chunk;
        sent += chunk;
        s->retrans_tick = KeGetTickCount() + 20;
        s->retrans_count = 0;

        /* Wait for ACK (or proceed if window allows) */
        if (s->remote_window <= chunk) {
            timeout = KeGetTickCount() + 500;
            while (s->snd_una < s->snd_nxt && KeGetTickCount() < timeout) {
                HalHlt();
            }
        }
    }

    return (i32)sent;
}

i32 TcpRecv(i32 sock, void* buf, u16 max_len, u32 timeout_ms) {
    if (sock < 0 || sock >= TCP_SOCKET_COUNT) return -1;
    TcpSocket* s = &g_tcp_sockets[sock];
    if (!s->used) return -1;

    // Check if we already have data in the buffer
    if (s->recv_len > 0) {
        u16 copy = max_len;
        if (copy > s->recv_len) copy = (u16)s->recv_len;
        RtMemCopy(buf, s->recv_buf, copy);
        /* Shift remaining */
        if (copy < s->recv_len) {
            RtMemMove(s->recv_buf, s->recv_buf + copy, s->recv_len - copy);
        }
        s->recv_len -= copy;
        return (i32)copy;
    }

    // If timeout_ms is 0, do not block and return immediately
    if (timeout_ms == 0) {
        return 0;
    }

    u64 deadline = KeGetTickCount() + (timeout_ms / 10);

    while (1) {
        if (s->recv_len > 0) {
            u16 copy = max_len;
            if (copy > s->recv_len) copy = (u16)s->recv_len;
            RtMemCopy(buf, s->recv_buf, copy);
            /* Shift remaining */
            if (copy < s->recv_len) {
                RtMemMove(s->recv_buf, s->recv_buf + copy, s->recv_len - copy);
            }
            s->recv_len -= copy;
            return (i32)copy;
        }

        if (s->state == TCP_STATE_CLOSE_WAIT ||
            s->state == TCP_STATE_CLOSED ||
            s->state == TCP_STATE_FIN_WAIT_1 ||
            s->state == TCP_STATE_FIN_WAIT_2) {
            return 0; /* EOF / closed */
        }

        if (KeGetTickCount() >= deadline) return 0;
        HalHlt();
    }
}

ntstatus TcpClose(i32 sock) {
    if (sock < 0 || sock >= TCP_SOCKET_COUNT) return STATUS_INVALID_PARAMETER;
    TcpSocket* s = &g_tcp_sockets[sock];
    if (!s->used) return STATUS_INVALID_PARAMETER;

    if (s->state == TCP_STATE_ESTABLISHED) {
        s->state = TCP_STATE_FIN_WAIT_1;
        s->pending_close = 1;
        TcpSendSegment(s, TCP_FIN | TCP_ACK, NULL, 0, s->snd_nxt, s->rcv_nxt);
        s->snd_nxt++;

        u64 timeout = KeGetTickCount() + 500;
        while (KeGetTickCount() < timeout) {
            if (s->state == TCP_STATE_CLOSED) break;
            HalHlt();
        }
    }

    TcpFreeSocket(s);
    KdPrintf("[TCP] Socket %d closed\n", sock);
    return STATUS_SUCCESS;
}

void TcpHandlePacket(u32 src_ip, u32 dst_ip, const void* tcp_data, u16 len) {
    (void)dst_ip;
    if (!tcp_data || len < sizeof(TcpHeader)) return;

    const TcpHeader* hdr = (const TcpHeader*)tcp_data;
    u16 src_port = NetSwap16(hdr->src_port);
    u16 dst_port = NetSwap16(hdr->dst_port);
    u32 seq = NetSwap32(hdr->seq);
    u32 ack = NetSwap32(hdr->ack);
    u8 flags = hdr->flags;
    u8 data_offset = (hdr->offset_flags >> 4) * 4;
    u16 payload_len = 0;
    if (len > data_offset) payload_len = len - data_offset;

    TcpSocket* s = TcpFindSocket(dst_port, src_ip, src_port);
    if (!s) return;

    s->remote_window = NetSwap16(hdr->window);

    /* Validate checksum? Skip for performance in hobby OS */

    KdPrintf("[TCP] sock=%d flags=0x%02x seq=%u ack=%u len=%u state=%u\n",
             (i32)(s - g_tcp_sockets), flags, seq, ack, payload_len, s->state);

    /* Handle SYN_SENT -> SYN_RECEIVED/ESTABLISHED */
    if (s->state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            if (ack == s->snd_nxt) {
                s->rcv_nxt = seq + 1;
                s->irs = seq;
                s->snd_una = ack;
                TcpSendSegment(s, TCP_ACK, NULL, 0, s->snd_nxt, s->rcv_nxt);
                s->state = TCP_STATE_ESTABLISHED;
            }
        }
        return;
    }

    /* Check sequence for established states */
    if (s->state == TCP_STATE_ESTABLISHED ||
        s->state == TCP_STATE_FIN_WAIT_1 ||
        s->state == TCP_STATE_FIN_WAIT_2 ||
        s->state == TCP_STATE_CLOSE_WAIT) {

        /* Accept data if seq matches rcv_nxt */
        if (payload_len > 0 && seq == s->rcv_nxt) {
            u32 space = TCP_BUF_SIZE - s->recv_len;
            if (payload_len > space) payload_len = (u16)space;
            if (payload_len > 0) {
                RtMemCopy(s->recv_buf + s->recv_len,
                          (const u8*)tcp_data + data_offset, payload_len);
                s->recv_len += payload_len;
                s->rcv_nxt += payload_len;
            }
        }

        /* Process ACK */
        if (flags & TCP_ACK) {
            if (ack > s->snd_una && ack <= s->snd_nxt) {
                s->snd_una = ack;
            }
        }

        /* Send ACK for data or FIN */
        TcpSendSegment(s, TCP_ACK, NULL, 0, s->snd_nxt, s->rcv_nxt);

        /* Handle incoming FIN */
        if (flags & TCP_FIN) {
            s->rcv_nxt++;
            TcpSendSegment(s, TCP_ACK, NULL, 0, s->snd_nxt, s->rcv_nxt);
            if (s->state == TCP_STATE_ESTABLISHED) {
                s->state = TCP_STATE_CLOSE_WAIT;
            } else if (s->state == TCP_STATE_FIN_WAIT_1) {
                if (ack == s->snd_nxt) s->state = TCP_STATE_TIME_WAIT;
                else s->state = TCP_STATE_CLOSING;
            } else if (s->state == TCP_STATE_FIN_WAIT_2) {
                s->state = TCP_STATE_TIME_WAIT;
            }
        }

        /* If we have pending close and all data acked, send FIN */
        if (s->state == TCP_STATE_CLOSE_WAIT && s->pending_close) {
            s->state = TCP_STATE_LAST_ACK;
            TcpSendSegment(s, TCP_FIN | TCP_ACK, NULL, 0, s->snd_nxt, s->rcv_nxt);
            s->snd_nxt++;
        }

        /* If LAST_ACK and our FIN is acked, close */
        if (s->state == TCP_STATE_LAST_ACK && (flags & TCP_ACK)) {
            if (ack >= s->snd_nxt) {
                s->state = TCP_STATE_CLOSED;
            }
        }

        /* Handle CLOSING -> TIME_WAIT */
        if (s->state == TCP_STATE_CLOSING && (flags & TCP_ACK)) {
            if (ack >= s->snd_nxt) {
                s->state = TCP_STATE_TIME_WAIT;
            }
        }
    }

    /* FIN_WAIT_1 -> FIN_WAIT_2 (our FIN acked but no peer FIN yet) */
    if (s->state == TCP_STATE_FIN_WAIT_1 && (flags & TCP_ACK)) {
        if (ack >= s->snd_nxt) {
            s->state = TCP_STATE_FIN_WAIT_2;
        }
    }
}

void TcpPoll(void) {
    for (i32 i = 0; i < TCP_SOCKET_COUNT; i++) {
        TcpSocket* s = &g_tcp_sockets[i];
        if (!s->used) continue;
        if (s->state == TCP_STATE_SYN_SENT || s->state == TCP_STATE_ESTABLISHED) {
            if (KeGetTickCount() >= s->retrans_tick && s->retrans_count < 5) {
                s->retrans_count++;
                s->retrans_tick = KeGetTickCount() + 20;

                if (s->state == TCP_STATE_SYN_SENT) {
                    TcpSendSegment(s, TCP_SYN, NULL, 0, s->iss, 0);
                } else if (s->snd_una < s->snd_nxt) {
                    /* Retransmit unacked data */
                    u32 unacked = s->snd_nxt - s->snd_una;
                    if (unacked > TCP_MSS) unacked = TCP_MSS;
                    TcpSendSegment(s, TCP_PSH | TCP_ACK,
                                   s->send_buf, (u16)unacked,
                                   s->snd_una, s->rcv_nxt);
                }
            }
        }

        if (s->state == TCP_STATE_TIME_WAIT || s->state == TCP_STATE_CLOSED) {
            /* Clean up closed sockets after some time or immediately */
            if (s->state == TCP_STATE_CLOSED) {
                TcpFreeSocket(s);
            }
        }
    }
}
