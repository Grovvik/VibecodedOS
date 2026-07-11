#include "userlib.h"
#include "wolfssl/ssl.h"
#include <intrin.h>

#define SYS_SYSTEM 22

/* embedded Let's Encrypt ISRG Root X1 certificate for trust chain verification */
static const char g_root_ca[] = 
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

struct tls_session {
    int sock;
    WOLFSSL_CTX* ctx;
    WOLFSSL* ssl;
};

static u64 g_rng_state = 0x8A9B1C2D3E4F5A6BUL;

/* custom entropy generator using cpu cycles and kernel ticks */
int GenerateEntropy(unsigned char* output, int sz) {
    u64 ticks = syscall1(SYS_SYSTEM, 2);
    u64 seed = __rdtsc() ^ ticks;
    g_rng_state ^= seed;

    for (int i = 0; i < sz; i++) {
        g_rng_state ^= g_rng_state >> 12;
        g_rng_state ^= g_rng_state << 25;
        g_rng_state ^= g_rng_state >> 27;
        u64 val = g_rng_state * 0x2545F4914F6CDD1DULL;
        output[i] = (unsigned char)(val & 0xFF);
    }
    return 0;
}

/* custom low res timer */
unsigned long MyTime(unsigned long* t) {
    unsigned long sec = (unsigned long)(syscall1(SYS_SYSTEM, 2) / 100);
    sec += 1700000000UL; /* fake starting time: somewhere in 2023 */
    if (t) *t = sec;
    return sec;
}

/* custom wolfSSL I/O send callback */
static int MyIOSend(WOLFSSL* ssl, char* buf, int sz, void* context) {
    int sock = (int)(size_t)context;
    if (sz <= 0) return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    /* kernel TCP takes u16 len — clamp to 32KB chunks */
    if (sz > 32767) sz = 32767;
    int ret = net_send(sock, buf, (u16)sz);
    if (ret < 0) {
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }
    if (ret == 0) {
        return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }
    return ret;
}

/* custom wolfSSL I/O receive callback */
static int MyIORecv(WOLFSSL* ssl, char* buf, int sz, void* context) {
    int sock = (int)(size_t)context;
    if (sz <= 0) return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    /* kernel TCP takes u16 len — clamp to 32KB */
    if (sz > 32767) sz = 32767;
    /* Block with a 10-second timeout to allow the server to transmit */
    int ret = net_recv(sock, buf, (u16)sz, 10000);
    if (ret < 0) {
        return WOLFSSL_CBIO_ERR_CONN_CLOSE;
    }
    if (ret == 0) {
        return WOLFSSL_CBIO_ERR_WANT_READ;
    }
    return ret;
}

int tls_library_init(void) {
    int ret = wolfSSL_Init();
    return (ret == WOLFSSL_SUCCESS) ? 0 : -1;
}

tls_session_t* tls_session_create(int sock, const char* host) {
    /* wolfSSLv23 auto-negotiates TLS 1.2/1.3 — required because TLS 1.3
       servers use TLS 1.2 record-layer framing for backwards compatibility,
       which the strict TLS 1.3-only mode rejects with VERSION_ERROR -326 */
    WOLFSSL_METHOD* method = wolfSSLv23_client_method();
    if (!method) {
        return NULL;
    }

    WOLFSSL_CTX* ctx = wolfSSL_CTX_new(method);
    if (!ctx) {
        return NULL;
    }

    /* NOTE: for initial stack validation we skip CA loading and disable
       peer verification. Once end-to-end works, we will re-enable it with
       the correct Root CA for the target server (badssl.com uses DigiCert,
       not ISRG Root X1). */
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);

    /* IMPORTANT: register I/O callbacks on ctx BEFORE wolfSSL_new()
       because wolfSSL copies ctx callbacks into the ssl object at creation */
    wolfSSL_SetIOSend(ctx, MyIOSend);
    wolfSSL_SetIORecv(ctx, MyIORecv);

    WOLFSSL* ssl = wolfSSL_new(ctx);
    if (!ssl) {
        wolfSSL_CTX_free(ctx);
        return NULL;
    }

    /* Set the socket as I/O context on the ssl object */
    wolfSSL_SetIOWriteCtx(ssl, (void*)(size_t)sock);
    wolfSSL_SetIOReadCtx(ssl, (void*)(size_t)sock);

    /* SNI (Server Name Indication) is crucial for modern HTTPS hosting */
    int sni_res = wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, host, (unsigned short)strlen(host));
    if (sni_res != WOLFSSL_SUCCESS) {
        printf("[TLS] Failed to set SNI: %d\n", sni_res);
    }

    tls_session_t* session = (tls_session_t*)malloc(sizeof(tls_session_t));
    if (!session) {
        wolfSSL_free(ssl);
        wolfSSL_CTX_free(ctx);
        return NULL;
    }

    session->sock = sock;
    session->ctx = ctx;
    session->ssl = ssl;
    return session;
}

int tls_handshake(tls_session_t* session) {
    if (!session || !session->ssl) {
        return -1;
    }
    int ret = wolfSSL_connect(session->ssl);
    if (ret != WOLFSSL_SUCCESS) {
        int err = wolfSSL_get_error(session->ssl, ret);
        printf("[TLS] Handshake failed, wolfSSL error: %d\n", err);
        return err;
    }
    return 0;
}

int tls_write(tls_session_t* session, const void* buf, int len) {
    if (!session || !session->ssl) {
        return -1;
    }
    return wolfSSL_write(session->ssl, buf, len);
}

int tls_read(tls_session_t* session, void* buf, int len) {
    if (!session || !session->ssl) {
        return -1;
    }
    return wolfSSL_read(session->ssl, buf, len);
}

#include "time.h"

void tls_session_free(tls_session_t* session) {
    if (session) {
        if (session->ssl) wolfSSL_free(session->ssl);
        if (session->ctx) wolfSSL_CTX_free(session->ctx);
        free(session);
    }
}

/* Custom LowResTimer needed by wolfSSL when USER_TICKS is defined */
#ifdef __cplusplus
extern "C" {
#endif
unsigned int LowResTimer(void) {
    return (unsigned int)syscall1(SYS_SYSTEM, 2);
}

struct tm* MyGmTime(const unsigned long* timer, struct tm* tmp) {
    (void)timer;
    if (!tmp) return NULL;
    tmp->tm_sec = 0;
    tmp->tm_min = 0;
    tmp->tm_hour = 12;
    tmp->tm_mday = 1;
    tmp->tm_mon = 0;
    tmp->tm_year = 126; /* 2026 */
    tmp->tm_wday = 1;
    tmp->tm_yday = 0;
    tmp->tm_isdst = 0;
    return tmp;
}
#ifdef __cplusplus
}
#endif
