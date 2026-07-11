#include "udp.h"
#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "debug.h"
#include "runtime.h"
#include "error.h"
#include "pit.h"
#include "hal.h"

/* -------------------------------------------------------------------------
 * UDP header
 * ---------------------------------------------------------------------- */
#pragma pack(push, 1)
typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} UdpHeader;
#pragma pack(pop)

#define IP_PROTO_UDP 17

/* -------------------------------------------------------------------------
 * Minimal UDP receive ring: one slot per port (port -> one pending datagram)
 * ---------------------------------------------------------------------- */
#define UDP_RECV_PORT  5353      /* only slot we statically track (DNS replies) */
#define UDP_RECV_BUF   512

static u8  g_udp_buf[UDP_RECV_BUF];
static u16 g_udp_len    = 0;
static u16 g_udp_port   = 0;   /* which local port this arrived on */
static u32 g_udp_src_ip = 0;

static u16 UdpSwap16(u16 v) { return (v >> 8) | (v << 8); }

/* -------------------------------------------------------------------------
 * UDP checksum (optional — zero means "not computed")
 * ---------------------------------------------------------------------- */
static u16 UdpChecksum(u32 src_ip, u32 dst_ip,
                       const UdpHeader* hdr, const void* data, u16 data_len) {
    struct { u32 src; u32 dst; u8 zero; u8 proto; u16 len; } ph;
    ph.src   = src_ip;
    ph.dst   = dst_ip;
    ph.zero  = 0;
    ph.proto = IP_PROTO_UDP;
    u16 total = (u16)(sizeof(UdpHeader) + data_len);
    ph.len   = UdpSwap16(total);

    u32 sum = 0;
    const u16* p = (const u16*)&ph;
    for (u32 i = 0; i < sizeof(ph) / 2; i++) sum += p[i];
    p = (const u16*)hdr;
    for (u32 i = 0; i < sizeof(UdpHeader) / 2; i++) sum += p[i];
    p = (const u16*)data;
    u16 dlen = data_len;
    while (dlen > 1) { sum += *p++; dlen -= 2; }
    if (dlen) sum += *(const u8*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

/* -------------------------------------------------------------------------
 * UdpSend
 * ---------------------------------------------------------------------- */
ntstatus UdpSend(u32 dst_ip, u16 dst_port, u16 src_port,
                 const void* data, u16 len) {
    u8 pkt[sizeof(UdpHeader) + 512];
    if (len > 512) return STATUS_INVALID_PARAMETER;

    UdpHeader* hdr = (UdpHeader*)pkt;
    hdr->src_port = UdpSwap16(src_port);
    hdr->dst_port = UdpSwap16(dst_port);
    u16 total = (u16)(sizeof(UdpHeader) + len);
    hdr->length   = UdpSwap16(total);
    hdr->checksum = 0;  /* optional, leave as 0 */

    RtMemCopy(pkt + sizeof(UdpHeader), data, len);

    return Ipv4Send(dst_ip, IP_PROTO_UDP, pkt, total);
}

/* -------------------------------------------------------------------------
 * UdpHandlePacket — called by IPv4 dispatch
 * ---------------------------------------------------------------------- */
void UdpHandlePacket(u32 src_ip, const void* data, u16 len) {
    if (len < sizeof(UdpHeader)) return;
    const UdpHeader* hdr = (const UdpHeader*)data;
    u16 dst_port = UdpSwap16(hdr->dst_port);
    u16 payload_len = UdpSwap16(hdr->length);
    if (payload_len < sizeof(UdpHeader)) return;
    payload_len -= sizeof(UdpHeader);
    const u8* payload = (const u8*)data + sizeof(UdpHeader);

    /* Store in the single receive slot (overwrite if not consumed yet) */
    if (payload_len > UDP_RECV_BUF) payload_len = UDP_RECV_BUF;
    RtMemCopy(g_udp_buf, payload, payload_len);
    g_udp_len    = payload_len;
    g_udp_port   = dst_port;
    g_udp_src_ip = src_ip;
}

/* -------------------------------------------------------------------------
 * UdpRecv — spin-wait for a datagram on local_port
 * ---------------------------------------------------------------------- */
i32 UdpRecv(u16 local_port, void* buf, u16 max_len,
            u32 timeout_ms, u32* src_ip) {
    g_udp_len  = 0;  /* clear any stale data */
    g_udp_port = 0;

    u64 deadline = KeGetTickCount() + (timeout_ms / 10);
    while (KeGetTickCount() < deadline) {
        if (g_udp_len > 0 && g_udp_port == local_port) {
            u16 copy = g_udp_len < max_len ? g_udp_len : max_len;
            RtMemCopy(buf, g_udp_buf, copy);
            if (src_ip) *src_ip = g_udp_src_ip;
            g_udp_len = 0;
            return (i32)copy;
        }
        HalHlt();
    }
    return 0;
}

/* =========================================================================
 * DNS resolver
 * =====================================================================*/

#define DNS_SERVER_IP   0x0302000A  /* 10.0.2.3 little-endian */
#define DNS_PORT        53
#define DNS_SRC_PORT    5353
#define DNS_BUF_SIZE    512
#define DNS_TIMEOUT_MS  3000

#pragma pack(push, 1)
typedef struct {
    u16 id;
    u16 flags;
    u16 qdcount;
    u16 ancount;
    u16 nscount;
    u16 arcount;
} DnsHeader;
#pragma pack(pop)

/* Encode a DNS name: "www.example.com" -> \x03www\x07example\x03com\x00 */
static int DnsEncodeName(const char* name, u8* out) {
    int total = 0;
    while (*name) {
        const char* dot = name;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - name);
        if (label_len == 0 || label_len > 63) return -1;
        out[total++] = (u8)label_len;
        for (int i = 0; i < label_len; i++) out[total++] = (u8)name[i];
        name = dot;
        if (*name == '.') name++;
    }
    out[total++] = 0;
    return total;
}

/* Skip a DNS name (handles compression pointers) */
static int DnsSkipName(const u8* buf, int offset, int len) {
    while (offset < len) {
        u8 c = buf[offset];
        if (c == 0) { return offset + 1; }
        if ((c & 0xC0) == 0xC0) { return offset + 2; }  /* pointer */
        offset += c + 1;
    }
    return offset;
}

u32 DnsResolve(const char* hostname) {
    /* Fast path: if hostname is already an IP address */
    {
        u32 ip = 0; int octet = 0, dots = 0; const char* p = hostname;
        int valid = 1;
        while (*p && valid) {
            if (*p >= '0' && *p <= '9') {
                octet = octet * 10 + (*p - '0');
                if (octet > 255) valid = 0;
            } else if (*p == '.') {
                ip |= ((u32)octet << (dots * 8));
                octet = 0; dots++;
                if (dots > 3) valid = 0;
            } else { valid = 0; }
            p++;
        }
        if (valid && dots == 3) {
            ip |= ((u32)octet << 24);
            return ip;
        }
    }

    /* Build DNS query */
    u8 query[DNS_BUF_SIZE];
    DnsHeader* hdr = (DnsHeader*)query;
    hdr->id      = UdpSwap16(0xAB12);
    hdr->flags   = UdpSwap16(0x0100); /* RD=1 */
    hdr->qdcount = UdpSwap16(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;

    int off = sizeof(DnsHeader);
    int name_len = DnsEncodeName(hostname, query + off);
    if (name_len < 0) return 0;
    off += name_len;
    query[off++] = 0x00; query[off++] = 0x01; /* QTYPE = A */
    query[off++] = 0x00; query[off++] = 0x01; /* QCLASS = IN */

    /* Make sure gateway is resolved first (ARP may need warming) */
    UdpSend(DNS_SERVER_IP, DNS_PORT, DNS_SRC_PORT, query, (u16)off);

    /* Wait for reply */
    u8 reply[DNS_BUF_SIZE];
    u32 src_ip = 0;
    i32 rlen = UdpRecv(DNS_SRC_PORT, reply, DNS_BUF_SIZE, DNS_TIMEOUT_MS, &src_ip);
    if (rlen < (i32)sizeof(DnsHeader)) {
        /* Retry once */
        UdpSend(DNS_SERVER_IP, DNS_PORT, DNS_SRC_PORT, query, (u16)off);
        rlen = UdpRecv(DNS_SRC_PORT, reply, DNS_BUF_SIZE, DNS_TIMEOUT_MS, &src_ip);
        if (rlen < (i32)sizeof(DnsHeader)) return 0;
    }

    DnsHeader* rhdr = (DnsHeader*)reply;
    u16 ancount = UdpSwap16(rhdr->ancount);
    if (ancount == 0) return 0;

    /* Skip question section */
    int pos = sizeof(DnsHeader);
    u16 qdcount = UdpSwap16(rhdr->qdcount);
    for (u16 q = 0; q < qdcount && pos < rlen; q++) {
        pos = DnsSkipName(reply, pos, rlen);
        pos += 4; /* QTYPE + QCLASS */
    }

    /* Parse answer section — find first A record */
    for (u16 a = 0; a < ancount && pos + 10 < rlen; a++) {
        pos = DnsSkipName(reply, pos, rlen);
        if (pos + 10 > rlen) break;
        u16 rtype  = ((u16)reply[pos] << 8) | reply[pos+1];
        /* u16 rclass = ... */
        u16 rdlen  = ((u16)reply[pos+8] << 8) | reply[pos+9];
        pos += 10;
        if (rtype == 1 && rdlen == 4 && pos + 4 <= rlen) {
            /* A record — return as little-endian u32 */
            u32 ip = ((u32)reply[pos+0])
                   | ((u32)reply[pos+1] << 8)
                   | ((u32)reply[pos+2] << 16)
                   | ((u32)reply[pos+3] << 24);
            KdPrintf("[DNS] %s -> %u.%u.%u.%u\n", hostname,
                     reply[pos+0], reply[pos+1], reply[pos+2], reply[pos+3]);
            return ip;
        }
        pos += rdlen;
    }
    return 0;
}
