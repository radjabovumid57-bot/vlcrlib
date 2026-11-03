#include "vnetwork.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET sock_t;
#  define INV_SOCK INVALID_SOCKET
#  define CLOSE_SOCK closesocket
#  define SOCK_ERR   SOCKET_ERROR
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <signal.h>
   typedef int sock_t;
#  define INV_SOCK (-1)
#  define CLOSE_SOCK close
#  define SOCK_ERR   (-1)
#endif

static bool g_netInit = false;

bool InitNetwork(void) {
    if (g_netInit) return true;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false;
#else
    /* A peer closing a TCP socket must not terminate the host process. */
    signal(SIGPIPE, SIG_IGN);
#endif
    g_netInit = true;
    return true;
}

void CloseNetwork(void) {
#ifdef _WIN32
    if (g_netInit) WSACleanup();
#endif
    g_netInit = false;
}

/* ------------------------------------------------------------------ */

NetSocket NetCreateTCP(void) {
    NetSocket s = {0};
    sock_t handle = socket(AF_INET, SOCK_STREAM, 0);
    if (handle != INV_SOCK) s.handle = (intptr_t)handle;
    return s;
}

NetSocket NetCreateUDP(void) {
    NetSocket s = {0};
    sock_t handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (handle != INV_SOCK) s.handle = (intptr_t)handle;
    s.isUDP  = true;
    return s;
}

static bool hostToAddr(const char *host, int port, struct sockaddr_in *out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) return true;

    struct hostent *he = gethostbyname(host);
    if (!he) return false;
    memcpy(&out->sin_addr, he->h_addr_list[0], he->h_length);
    return true;
}

bool NetBindUDP(NetSocket *s, int port) {
    if (!s || !s->handle || !s->isUDP || port < 1 || port > 65535) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((unsigned short)port);
    int yes = 1;
    setsockopt((sock_t)s->handle, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&yes, sizeof(yes));
    return bind((sock_t)s->handle, (struct sockaddr *)&a, sizeof(a)) == 0;
}

int NetSendTo(NetSocket *s, const NetAddress *address,
              const void *data, size_t len) {
    if (!s || !s->handle || !s->isUDP || !address || !data) return -1;
    struct sockaddr_in a;
    if (!hostToAddr(address->host, address->port, &a)) return -1;
    return (int)sendto((sock_t)s->handle, (const char *)data, (int)len, 0,
                       (struct sockaddr *)&a, sizeof(a));
}

int NetRecvFrom(NetSocket *s, void *buf, size_t maxLen, NetAddress *address) {
    if (!s || !s->handle || !s->isUDP || !buf || !address) return -1;
    struct sockaddr_in a;
#ifdef _WIN32
    int alen = sizeof(a);
#else
    socklen_t alen = sizeof(a);
#endif
    int got = (int)recvfrom((sock_t)s->handle, (char *)buf, (int)maxLen, 0,
                            (struct sockaddr *)&a, &alen);
    if (got < 0) return got;
    if (!inet_ntop(AF_INET, &a.sin_addr, address->host, sizeof(address->host)))
        address->host[0] = 0;
    address->port = (int)ntohs(a.sin_port);
    return got;
}

bool NetConnectTCP(NetSocket *s, const char *host, int port) {
    if (!s || !s->handle) return false;
    struct sockaddr_in a;
    if (!hostToAddr(host, port, &a)) return false;
    return connect((sock_t)s->handle, (struct sockaddr *)&a, sizeof(a)) == 0;
}

bool NetBindListenTCP(NetSocket *s, int port, int backlog) {
    if (!s || !s->handle) return false;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((unsigned short)port);

    int yes = 1;
    setsockopt((sock_t)s->handle, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&yes, sizeof(yes));

    if (bind((sock_t)s->handle, (struct sockaddr *)&a, sizeof(a)) != 0) return false;
    if (listen((sock_t)s->handle, backlog) != 0) return false;
    return true;
}

NetSocket NetAcceptTCP(NetSocket *s) {
    NetSocket out = {0};
    if (!s || !s->handle) return out;
    struct sockaddr_in a;
#ifdef _WIN32
    int alen = sizeof(a);
#else
    socklen_t alen = sizeof(a);
#endif
    sock_t c = accept((sock_t)s->handle, (struct sockaddr *)&a, &alen);
    if (c == INV_SOCK) return out;
    out.handle = (intptr_t)c;
    return out;
}

int NetSend(NetSocket *s, const void *data, size_t len) {
    if (!s || !s->handle) return -1;
    return (int)send((sock_t)s->handle, (const char *)data, (int)len, 0);
}

int NetRecv(NetSocket *s, void *buf, size_t maxLen) {
    if (!s || !s->handle) return -1;
    return (int)recv((sock_t)s->handle, (char *)buf, (int)maxLen, 0);
}

bool NetSetUDPPeer(NetSocket *s, const char *host, int port) {
    if (!s || !s->handle) return false;
    struct sockaddr_in a;
    if (!hostToAddr(host, port, &a)) return false;
    return connect((sock_t)s->handle, (struct sockaddr *)&a, sizeof(a)) == 0;
}

void NetClose(NetSocket *s) {
    if (!s || !s->handle) return;
    CLOSE_SOCK((sock_t)s->handle);
    s->handle = 0;
}

bool NetSetNonBlocking(NetSocket *s, bool nb) {
    if (!s || !s->handle) return false;
#ifdef _WIN32
    u_long v = nb ? 1 : 0;
    return ioctlsocket((sock_t)s->handle, FIONBIO, &v) == 0;
#else
    int flags = fcntl((sock_t)s->handle, F_GETFL, 0);
    if (flags < 0) return false;
    if (nb) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
    return fcntl((sock_t)s->handle, F_SETFL, flags) == 0;
#endif
}

/* ------------------------------------------------------------------ */
/*  HTTP                                                              */
/* ------------------------------------------------------------------ */

static bool parseUrl(const char *url, char *host, size_t hsz,
                     char *path, size_t psz, int *port)
{
    const char *p = url;
    if (strncmp(p, "http://", 7) != 0) return false;
    p += 7;

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t hostLen;
    if (colon && (!slash || colon < slash)) {
        hostLen = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else if (slash) {
        hostLen = (size_t)(slash - p);
        *port = 80;
    } else {
        hostLen = strlen(p);
        *port = 80;
    }
    if (hostLen + 1 > hsz) return false;
    memcpy(host, p, hostLen);
    host[hostLen] = 0;

    if (slash) {
        strncpy(path, slash, psz - 1);
        path[psz - 1] = 0;
    } else {
        strcpy(path, "/");
    }
    return true;
}

HttpResponse HttpGet(const char *url) {
    HttpResponse r = {0};

    char host[256], path[512];
    int  port = 80;
    if (!parseUrl(url, host, sizeof(host), path, sizeof(path), &port)) {
        fprintf(stderr, "[vlcrlib] HttpGet: bad url\n");
        return r;
    }

    NetSocket s = NetCreateTCP();
    if (!s.handle) return r;
    if (!NetConnectTCP(&s, host, port)) { NetClose(&s); return r; }

    char req[1024];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: vlcrlib/1.0\r\n"
             "Connection: close\r\n"
             "\r\n", path, host);
    NetSend(&s, req, strlen(req));

    /* читаем всё до EOF */
    size_t cap = 8192, total = 0;
    char *buf = malloc(cap);
    if (!buf) { NetClose(&s); return r; }
    for (;;) {
        if (total + 4096 > cap) {
            if (cap > (size_t)-1 / 2) {
                free(buf);
                NetClose(&s);
                return r;
            }
            size_t newCap = cap * 2;
            char *newBuf = realloc(buf, newCap);
            if (!newBuf) {
                free(buf);
                NetClose(&s);
                return r;
            }
            buf = newBuf;
            cap = newCap;
        }
        int got = NetRecv(&s, buf + total, cap - total - 1);
        if (got <= 0) break;
        total += (size_t)got;
    }
    buf[total] = 0;
    NetClose(&s);

    /* парсим статус */
    int status = 0;
    if (sscanf(buf, "HTTP/%*d.%*d %d", &status) == 1) r.status = status;

    /* пропускаем заголовки до \r\n\r\n */
    char *body = strstr(buf, "\r\n\r\n");
    if (body) body += 4;
    else body = buf;

    size_t bodyLen = (size_t)(buf + total - body);
    r.body = malloc(bodyLen + 1);
    if (!r.body) {
        free(buf);
        return r;
    }
    memcpy(r.body, body, bodyLen);
    r.body[bodyLen] = 0;
    r.bodySize = bodyLen;

    free(buf);
    return r;
}

void FreeHttpResponse(HttpResponse r) {
    if (r.body) free(r.body);
}
