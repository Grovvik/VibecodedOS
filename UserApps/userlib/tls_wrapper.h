#ifndef _TLS_WRAPPER_H_
#define _TLS_WRAPPER_H_

typedef struct tls_session tls_session_t;

int             tls_library_init(void);
tls_session_t*  tls_session_create(int sock, const char* host);
int             tls_handshake(tls_session_t* session);
int             tls_write(tls_session_t* session, const void* buf, int len);
int             tls_read(tls_session_t* session, void* buf, int len);
void            tls_session_free(tls_session_t* session);

/* Custom entropy + time — exposed so wolfSSL callbacks can find them */
int             GenerateEntropy(unsigned char* output, int sz);
unsigned long   MyTime(unsigned long* t);

#endif /* _TLS_WRAPPER_H_ */
