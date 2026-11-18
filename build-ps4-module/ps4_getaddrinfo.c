/*
 * ps4_getaddrinfo.c - name resolution for Wine's ws2_32 on the PS4.
 *
 * The OpenOrbis libc ships musl's resolver, which reads /etc/resolv.conf and
 * /etc/hosts. Neither exists inside a PS4 app sandbox, so every lookup fails.
 * These definitions take precedence over libc.a's members at link time and
 * resolve through the console's own resolver (libSceNet), which uses the DNS
 * servers from the PS4's network settings. IPv4 only, like the console.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

int sceNetInit(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetResolverCreate(const char *name, int memid, int flags);
int sceNetResolverDestroy(int rid);
int sceNetResolverStartNtoa(int rid, const char *hostname, void *addr,
                            int timeout, int retry, int flags);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_pool = -1;

static int parse_ipv4(const char *s, uint32_t *out)
{
    unsigned v[4];
    int n = 0;
    const char *p = s;
    for (n = 0; n < 4; n++) {
        char *end;
        unsigned long x;
        if (*p < '0' || *p > '9') return 0;
        x = strtoul(p, &end, 10);
        if (x > 255) return 0;
        v[n] = (unsigned)x;
        p = end;
        if (n < 3) { if (*p != '.') return 0; p++; }
    }
    if (*p) return 0;
    *out = htonl((v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3]);
    return 1;
}

/* Resolve one host name to an IPv4 address (network order). 0 on success. */
static int ps4_resolve(const char *host, uint32_t *out)
{
    uint32_t addr = 0;
    int rid, ret;

    if (parse_ipv4(host, out)) return 0;
    if (!strcasecmp(host, "localhost") || !strcasecmp(host, "localhost.")) {
        *out = htonl(INADDR_LOOPBACK);
        return 0;
    }

    pthread_mutex_lock(&g_lock);
    if (g_pool < 0) {
        sceNetInit(); /* already initialised is fine */
        g_pool = sceNetPoolCreate("pcps4_dns", 16 * 1024, 0);
        if (g_pool < 0)
            fprintf(stderr, "[ps4dns] sceNetPoolCreate failed 0x%x\n", g_pool);
    }
    pthread_mutex_unlock(&g_lock);
    if (g_pool < 0) return -1;

    rid = sceNetResolverCreate("pcps4_res", g_pool, 0);
    if (rid < 0) {
        fprintf(stderr, "[ps4dns] sceNetResolverCreate failed 0x%x\n", rid);
        return -1;
    }
    /* timeout in microseconds, retries */
    ret = sceNetResolverStartNtoa(rid, host, &addr, 5 * 1000 * 1000, 2, 0);
    sceNetResolverDestroy(rid);
    if (ret < 0 || !addr) {
        fprintf(stderr, "[ps4dns] %s -> failed 0x%x\n", host, ret);
        return -1;
    }
    fprintf(stderr, "[ps4dns] %s -> %u.%u.%u.%u\n", host,
            ((uint8_t *)&addr)[0], ((uint8_t *)&addr)[1],
            ((uint8_t *)&addr)[2], ((uint8_t *)&addr)[3]);
    *out = addr;
    return 0;
}

static int parse_service(const char *serv, int numeric_only, uint16_t *port)
{
    static const struct { const char *name; uint16_t port; } known[] = {
        { "http", 80 }, { "https", 443 }, { "ftp", 21 }, { "ssh", 22 },
        { "domain", 53 }, { "ntp", 123 }, { "smtp", 25 }, { "imap", 143 },
    };
    char *end;
    unsigned long v;
    size_t i;

    if (!serv) { *port = 0; return 0; }
    v = strtoul(serv, &end, 10);
    if (*serv && !*end) {
        if (v > 65535) return EAI_SERVICE;
        *port = htons((uint16_t)v);
        return 0;
    }
    if (numeric_only) return EAI_NONAME;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (!strcasecmp(serv, known[i].name)) { *port = htons(known[i].port); return 0; }
    return EAI_SERVICE;
}

struct ps4_ai {
    struct addrinfo ai;
    struct sockaddr_in sin;
};

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    int flags = hints ? hints->ai_flags : 0;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    static const int types[2][2] = { { SOCK_STREAM, IPPROTO_TCP },
                                     { SOCK_DGRAM, IPPROTO_UDP } };
    struct addrinfo *head = NULL, **tail = &head;
    uint32_t addr;
    uint16_t port;
    int i, err;

    if (!res) return EAI_FAIL;
    *res = NULL;
    if (!node && !service) return EAI_NONAME;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;

    err = parse_service(service, flags & AI_NUMERICSERV, &port);
    if (err) return err;

    if (!node)
        addr = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
    else if (flags & AI_NUMERICHOST) {
        if (!parse_ipv4(node, &addr)) return EAI_NONAME;
    } else if (ps4_resolve(node, &addr))
        return EAI_NONAME;

    for (i = 0; i < 2; i++) {
        struct ps4_ai *e;
        if (socktype && socktype != types[i][0]) continue;
        if (protocol && protocol != types[i][1]) continue;
        e = calloc(1, sizeof(*e));
        if (!e) { freeaddrinfo(head); return EAI_MEMORY; }
        e->sin.sin_len = sizeof(e->sin);
        e->sin.sin_family = AF_INET;
        e->sin.sin_port = port;
        e->sin.sin_addr.s_addr = addr;
        e->ai.ai_family = AF_INET;
        e->ai.ai_socktype = types[i][0];
        e->ai.ai_protocol = types[i][1];
        e->ai.ai_addrlen = sizeof(e->sin);
        e->ai.ai_addr = (struct sockaddr *)&e->sin;
        if (!head && (flags & AI_CANONNAME) && node)
            e->ai.ai_canonname = strdup(node);
        *tail = &e->ai;
        tail = &e->ai.ai_next;
    }
    if (!head) return EAI_SOCKTYPE;
    *res = head;
    return 0;
}

void freeaddrinfo(struct addrinfo *ai)
{
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai->ai_canonname);
        free(ai); /* ai is the first member of struct ps4_ai */
        ai = next;
    }
}

/* Non-reentrant like the real one; ws2_32 calls it under its own mutex. */
struct hostent *gethostbyname(const char *name)
{
    static struct hostent he;
    static char hname[256];
    static uint32_t haddr;
    static char *addrs[2], *aliases[1];

    if (!name || ps4_resolve(name, &haddr)) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    snprintf(hname, sizeof(hname), "%s", name);
    addrs[0] = (char *)&haddr;
    addrs[1] = NULL;
    aliases[0] = NULL;
    he.h_name = hname;
    he.h_aliases = aliases;
    he.h_addrtype = AF_INET;
    he.h_length = 4;
    he.h_addr_list = addrs;
    return &he;
}
