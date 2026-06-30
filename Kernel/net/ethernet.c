#include "ethernet.h"
#include "e1000.h"
#include "debug.h"
#include "runtime.h"
#include "error.h"

static i32 g_eth_initialized = 0;

void EthInit(void) {
    E1000Init();
    g_eth_initialized = E1000Initialized();
}

i32 EthInitialized(void) {
    return g_eth_initialized;
}

void EthGetMac(MacAddr* out) {
    E1000GetMac(out);
}

ntstatus EthSend(const MacAddr* dst, u16 type, const void* payload, u16 len) {
    if (!g_eth_initialized) return STATUS_UNSUCCESSFUL;
    if (!dst || !payload || len > ETH_MAX_PAYLOAD) return STATUS_INVALID_PARAMETER;

    u8 frame[1518];
    EthHeader* hdr = (EthHeader*)frame;
    hdr->dst = *dst;
    EthGetMac(&hdr->src);
    hdr->type = (type >> 8) | (type << 8); /* byte swap for big-endian wire format */

    RtMemCopy(frame + sizeof(EthHeader), payload, len);

    u16 total = sizeof(EthHeader) + len;
    if (total < 60) total = 60; /* minimum Ethernet frame size */

    return E1000SendFrame(frame, total);
}

i32 EthReceive(void* out_payload, u16 max_len, u16* out_type, MacAddr* out_src) {
    if (!g_eth_initialized) return -1;

    u8 frame[1518];
    i32 len = E1000ReceiveFrame(frame, sizeof(frame));
    if (len <= 0) return 0;
    if (len < (i32)sizeof(EthHeader)) return 0;

    EthHeader* hdr = (EthHeader*)frame;
    u16 type = (hdr->type >> 8) | (hdr->type << 8);

    if (out_src) *out_src = hdr->src;
    if (out_type) *out_type = type;

    u16 payload_len = (u16)(len - sizeof(EthHeader));
    if (payload_len > max_len) payload_len = max_len;

    if (out_payload && payload_len > 0) {
        RtMemCopy(out_payload, frame + sizeof(EthHeader), payload_len);
    }

    return (i32)payload_len;
}
