#include "arp.h"
#include "ethernet.h"
#include "debug.h"
#include "runtime.h"
#include "error.h"
#include "pit.h"

#define ARP_CACHE_SIZE 8

typedef struct {
    u32     ip;
    MacAddr mac;
    u64     timestamp;
    i32     valid;
} ArpEntry;

static ArpEntry g_arp_cache[ARP_CACHE_SIZE];
static u32 g_our_ip = 0;

static u16 ArpSwap16(u16 v) {
    return (v >> 8) | (v << 8);
}

void ArpInit(void) {
    RtMemSet(g_arp_cache, 0, sizeof(g_arp_cache));
}

void ArpSetOurIp(u32 ip) {
    g_our_ip = ip;
}

static i32 ArpFindEntry(u32 ip) {
    for (i32 i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) return i;
    }
    return -1;
}

static i32 ArpAllocEntry(void) {
    for (i32 i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) return i;
    }
    /* Evict oldest */
    i32 oldest = 0;
    for (i32 i = 1; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].timestamp < g_arp_cache[oldest].timestamp) oldest = i;
    }
    return oldest;
}

static void ArpUpdateCache(u32 ip, const MacAddr* mac) {
    i32 idx = ArpFindEntry(ip);
    if (idx < 0) idx = ArpAllocEntry();
    g_arp_cache[idx].ip = ip;
    g_arp_cache[idx].mac = *mac;
    g_arp_cache[idx].timestamp = KeGetTickCount();
    g_arp_cache[idx].valid = 1;
}

void ArpHandlePacket(const ArpPacket* pkt) {
    if (!pkt) return;

    u16 htype = ArpSwap16(pkt->htype);
    u16 ptype = ArpSwap16(pkt->ptype);
    u16 oper  = ArpSwap16(pkt->oper);

    if (htype != ARP_HTYPE_ETH || ptype != ARP_PTYPE_IPv4) return;

    if (oper == ARP_OP_REPLY) {
        ArpUpdateCache(pkt->spa, &pkt->sha);
        KdPrintf("[ARP] Cached %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                 (pkt->spa >> 0) & 0xFF, (pkt->spa >> 8) & 0xFF,
                 (pkt->spa >> 16) & 0xFF, (pkt->spa >> 24) & 0xFF,
                 pkt->sha.addr[0], pkt->sha.addr[1], pkt->sha.addr[2],
                 pkt->sha.addr[3], pkt->sha.addr[4], pkt->sha.addr[5]);
    }
    else if (oper == ARP_OP_REQUEST) {
        /* If it's for us, reply */
        if (pkt->tpa == g_our_ip && g_our_ip != 0) {
            ArpPacket reply;
            RtMemSet(&reply, 0, sizeof(reply));
            reply.htype = ArpSwap16(ARP_HTYPE_ETH);
            reply.ptype = ArpSwap16(ARP_PTYPE_IPv4);
            reply.hlen = 6;
            reply.plen = 4;
            reply.oper = ArpSwap16(ARP_OP_REPLY);
            EthGetMac(&reply.sha);
            reply.spa = g_our_ip;
            reply.tha = pkt->sha;
            reply.tpa = pkt->spa;
            EthSend(&pkt->sha, ETH_TYPE_ARP, &reply, sizeof(reply));
            KdPrintf("[ARP] Replied to request for our IP\n");
        }
    }
}

void ArpSendRequest(u32 target_ip) {
    ArpPacket req;
    RtMemSet(&req, 0, sizeof(req));
    req.htype = ArpSwap16(ARP_HTYPE_ETH);
    req.ptype = ArpSwap16(ARP_PTYPE_IPv4);
    req.hlen = 6;
    req.plen = 4;
    req.oper = ArpSwap16(ARP_OP_REQUEST);
    EthGetMac(&req.sha);
    req.spa = g_our_ip;
    RtMemSet(req.tha.addr, 0, 6);
    req.tpa = target_ip;

    MacAddr broadcast;
    RtMemSet(broadcast.addr, 0xFF, 6);
    EthSend(&broadcast, ETH_TYPE_ARP, &req, sizeof(req));
    KdPrintf("[ARP] Sent request for %u.%u.%u.%u\n",
             (target_ip >> 0) & 0xFF, (target_ip >> 8) & 0xFF,
             (target_ip >> 16) & 0xFF, (target_ip >> 24) & 0xFF);
}

i32 ArpResolve(u32 ip, MacAddr* out_mac) {
    if (!out_mac) return -1;

    /* Already in cache? */
    i32 idx = ArpFindEntry(ip);
    if (idx >= 0) {
        *out_mac = g_arp_cache[idx].mac;
        return 0;
    }

    /* Send request */
    ArpSendRequest(ip);
    return -1;
}
