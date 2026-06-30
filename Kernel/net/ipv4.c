#include "ipv4.h"
#include "ethernet.h"
#include "arp.h"
#include "tcp.h"
#include "debug.h"
#include "runtime.h"
#include "error.h"
#include "pit.h"

static u32 g_our_ip = 0;
static u32 g_gateway = 0;
static u32 g_netmask = 0;

static u16 g_ip_id = 1;

void Ipv4Init(u32 our_ip, u32 gateway, u32 netmask) {
    g_our_ip = our_ip;
    g_gateway = gateway;
    g_netmask = netmask;
    ArpSetOurIp(our_ip);
    KdPrintf("[IPv4] IP=%u.%u.%u.%u GW=%u.%u.%u.%u NM=%u.%u.%u.%u\n",
             (our_ip >> 0) & 0xFF, (our_ip >> 8) & 0xFF,
             (our_ip >> 16) & 0xFF, (our_ip >> 24) & 0xFF,
             (gateway >> 0) & 0xFF, (gateway >> 8) & 0xFF,
             (gateway >> 16) & 0xFF, (gateway >> 24) & 0xFF,
             (netmask >> 0) & 0xFF, (netmask >> 8) & 0xFF,
             (netmask >> 16) & 0xFF, (netmask >> 24) & 0xFF);
}

void Ipv4SetOurIp(u32 ip) {
    g_our_ip = ip;
    ArpSetOurIp(ip);
}

u32 Ipv4OurIp(void) {
    return g_our_ip;
}

static u16 Ipv4Swap16(u16 v) {
    return (v >> 8) | (v << 8);
}

u16 Ipv4Checksum(const void* data, u16 len) {
    const u16* ptr = (const u16*)data;
    u32 sum = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len) sum += *(const u8*)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

ntstatus Ipv4Send(u32 dst_ip, u8 proto, const void* payload, u16 len) {
    if (!g_our_ip) return STATUS_UNSUCCESSFUL;

    u8 packet[2048];
    Ipv4Header* hdr = (Ipv4Header*)packet;
    hdr->ihl_version = 0x45;
    hdr->tos = 0;
    u16 total = sizeof(Ipv4Header) + len;
    hdr->total_len = Ipv4Swap16(total);
    hdr->id = Ipv4Swap16(g_ip_id++);
    hdr->flags_frag = Ipv4Swap16(0x4000); /* Don't fragment */
    hdr->ttl = 64;
    hdr->protocol = proto;
    hdr->checksum = 0;
    hdr->src_ip = g_our_ip;
    hdr->dst_ip = dst_ip;

    hdr->checksum = Ipv4Checksum(hdr, sizeof(Ipv4Header));
    RtMemCopy(packet + sizeof(Ipv4Header), payload, len);

    u32 target = dst_ip;
    if ((dst_ip & g_netmask) != (g_our_ip & g_netmask)) {
        target = g_gateway;
    }

    MacAddr mac;
    if (ArpResolve(target, &mac) < 0) {
        /* Will need to retry after ARP reply */
        return STATUS_UNSUCCESSFUL;
    }

    return EthSend(&mac, ETH_TYPE_IPv4, packet, total);
}

void Ipv4HandlePacket(const Ipv4Header* hdr, u16 len) {
    if (!hdr || len < sizeof(Ipv4Header)) return;

    if (hdr->ihl_version != 0x45) return;

    u16 hdr_len = (hdr->ihl_version & 0x0F) * 4;
    u16 total_len = Ipv4Swap16(hdr->total_len);
    if (total_len < hdr_len || total_len > len) return;

    if (hdr->dst_ip != g_our_ip && g_our_ip != 0) return;

    u16 payload_len = total_len - hdr_len;
    const void* payload = (const u8*)hdr + hdr_len;

    KdPrintf("[IPv4] packet from %u.%u.%u.%u proto=%u len=%u\n",
             (hdr->src_ip >> 0) & 0xFF, (hdr->src_ip >> 8) & 0xFF,
             (hdr->src_ip >> 16) & 0xFF, (hdr->src_ip >> 24) & 0xFF,
             hdr->protocol, payload_len);

    switch (hdr->protocol) {
    case IP_PROTO_TCP:
        TcpHandlePacket(hdr->src_ip, hdr->dst_ip, payload, payload_len);
        break;
    case IP_PROTO_ICMP:
        /* Not implemented for now */
        break;
    case IP_PROTO_UDP:
        /* Not implemented for now */
        break;
    default:
        break;
    }
}
