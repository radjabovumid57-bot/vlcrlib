#ifndef VLCRLIB_VNETWORK_H
#define VLCRLIB_VNETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NetSocket {
    intptr_t handle;
    bool isUDP;
} NetSocket;

typedef struct NetAddress {
    char host[64];
    int port;
} NetAddress;

bool        InitNetwork(void);
void        CloseNetwork(void);

NetSocket   NetCreateTCP(void);
NetSocket   NetCreateUDP(void);
bool        NetConnectTCP(NetSocket *s, const char *host, int port);
bool        NetBindListenTCP(NetSocket *s, int port, int backlog);
NetSocket   NetAcceptTCP(NetSocket *s);
bool        NetBindUDP(NetSocket *s, int port);
int         NetSendTo(NetSocket *s, const NetAddress *address,
                      const void *data, size_t len);
int         NetRecvFrom(NetSocket *s, void *buf, size_t maxLen,
                        NetAddress *address);
int         NetSend(NetSocket *s, const void *data, size_t len);
int         NetRecv(NetSocket *s, void *buf, size_t maxLen);
bool        NetSetUDPPeer(NetSocket *s, const char *host, int port);
void        NetClose(NetSocket *s);
bool        NetSetNonBlocking(NetSocket *s, bool nb);

/* ------------------------------------------------------------------ */
/*  HTTP — очень простой клиент (только http://, GET)                 */
/* ------------------------------------------------------------------ */
typedef struct HttpResponse {
    int   status;       /* HTTP status code (200, 404, ...) — 0 если ошибка */
    char *body;         /* тело ответа (NULL-terminated) */
    size_t bodySize;
} HttpResponse;

HttpResponse HttpGet(const char *url);         /* url = "http://host/path" */
void         FreeHttpResponse(HttpResponse r);

#ifdef __cplusplus
}
#endif
#endif
