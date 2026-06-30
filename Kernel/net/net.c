#include "net.h"
#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "tcp.h"
#include "debug.h"
#include "runtime.h"

static i32 g_net_initialized = 0;

void NetInit(u32 our_ip, u32 gateway, u32 netmask) {
    KdPrintf("[NET] Initializing network stack...\n");
    EthInit();
    if (!EthInitialized()) {
        KdPrintf("[NET] Ethernet not available\n");
        return;
    }
    ArpInit();
    Ipv4Init(our_ip, gateway, netmask);
    TcpInit();
    g_net_initialized = 1;
    KdPrintf("[NET] Stack ready\n");
}

i32 NetInitialized(void) {
    return g_net_initialized;
}

void NetPoll(void) {
    if (!g_net_initialized) return;

    u8 payload[2048];
    MacAddr src;
    u16 type;
    i32 len;

    /* Drain all received frames */
    while ((len = EthReceive(payload, sizeof(payload), &type, &src)) > 0) {
        if (type == ETH_TYPE_ARP) {
            ArpHandlePacket((ArpPacket*)payload);
        } else if (type == ETH_TYPE_IPv4) {
            Ipv4HandlePacket((Ipv4Header*)payload, (u16)len);
        }
    }

    /* TCP timers and pending sends */
    TcpPoll();
}
