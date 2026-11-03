/* The address providers' registry and the connections on them: see netprov.h. */
#include "real/netprov.h"
#include "real/thread.h"
#include <stdlib.h>
#include <string.h>

const NetProvider *g_net_providers[NET_MAXPROVIDERS];
const NetTransport *g_net_transports[NET_MAXPROVIDERS];
int g_net_nproviders;
int g_net_resend;
NetList g_net_conns;
CritSec *g_net_sendcs;
static bool conns_ready;   /* 0x6abd80 */

/* (Each registration, 0x560580 / 0x560840 / 0x581ff0, appends its pair as one.) */
void NET_addprovider(const NetProvider *p, const NetTransport *transport)
{
    if (g_net_nproviders >= NET_MAXPROVIDERS) return;
    g_net_providers[g_net_nproviders] = p;
    g_net_transports[g_net_nproviders] = transport;
    g_net_nproviders++;
}

/* The same GUID. 0x580080 */
static bool same_guid(const uint8_t *a, const uint8_t *b) { return !memcmp(a, b, 16); }

static void closeall_atexit(void) { NET_closeall(); }

int NET_open(NetConn *c, int (*joinfilter)(NetConn *, const NetAddr *), const uint8_t *game, const uint8_t *provider,
             int port, const char *name, const char *extra)
{
    int r = 0;
    if (!conns_ready) {
        NetList_Init(&g_net_conns, NULL, NULL);
        atexit(closeall_atexit);   /* 0x5570e0 */
        if (!g_net_resend) g_net_resend = (TIMER_RATE + 9) / 10;
        conns_ready = true;
    }
    if (NetList_Index(&g_net_conns, c) != -1) NET_close(c);
    void *keep = c->owner;
    memset(c, 0, sizeof *c);
    c->owner = keep;
    for (int i = 0; i < g_net_nproviders && !r; i++) {
        const NetTransport *t = g_net_transports[i];
        const NetProvider *p = g_net_providers[i];
        c->joinfilter = joinfilter;
        c->provider = p;
        c->transport = t;
        if (t && (t->anyprovider || same_guid(p->guid, provider))) r = t->open(c, game, provider, port, name, extra);
    }
    if (r) NetList_Push(&g_net_conns, c);
    return r;
}

int NET_close(NetConn *c)
{
    int r = 0;
    if (NetList_Index(&g_net_conns, c) != -1) {
        r = c->transport->close(c);
        NetList_Remove(&g_net_conns, c);
    }
    return r;
}

int NET_leave(NetConn *c)
{
    if (NetList_Index(&g_net_conns, c) != -1) return c->transport->leave(c);
    return 1;
}

int NET_find(NetConn *c, NetGameFn fn, int ticks, int user) { return c->transport->find(c, fn, ticks, user); }
int NET_join(NetConn *c, const NetAddr *host, const char *password) { return c->transport->join(c, host, password); }
bool NET_query(NetConn *c, const NetAddr *host, NetGameInfo *out) { return c->transport->query(c, host, out); }
int NET_host(NetConn *c, NetGameInfo *info, const char *password, NetPlayerFn joinfn)
{
    return c->transport->host(c, info, password, joinfn);
}
int NET_setinfo(NetConn *c, const NetGameInfo *info) { return c->transport->setinfo(c, info); }
int NET_hide(NetConn *c, int free_it) { return c->transport->hide(c, free_it); }
int NET_show(NetConn *c) { return c->transport->show(c); }
unsigned NET_players(NetConn *c, const NetAddr *host, NetPlayerFn fn) { return c->transport->players(c, host, fn); }
int NET_kill(NetConn *c, const NetAddr *who) { return c->transport->kill(c, who); }
int NET_config(NetConn *c, const void *cfg) { return c->transport->config ? c->transport->config(c, cfg) : 0; }

int NET_setmessage(NetConn *c, void (*fn)(NetConn *, const uint8_t *, int))
{
    c->onmessage = fn;
    return c->transport->setmessage ? c->transport->setmessage(c, fn) : 0;
}

void NET_closeall(void)
{
    if (!conns_ready) return;
    for (NetConn *c; (c = NetList_At(&g_net_conns, 0));) NET_close(c);
    NetList_Free(&g_net_conns);
    conns_ready = false;
}

bool NETCONN_same(NetConn *c, const NetAddr *a, const NetAddr *b)
{
    return c->provider->same(c->socket, a, b);
}

int NETCONN_parse(NetConn *c, const char *text, int port, NetAddr *out)
{
    return c->provider->parse ? c->provider->parse(c->socket, text, port, out) : 0;
}

int NETCONN_totext(NetConn *c, const NetAddr *a, char *text, uint32_t *port)
{
    return c->provider->totext ? c->provider->totext(c->socket, a, text, port) : 0;
}

int NETCONN_port(NetConn *c)
{
    return c->provider->port ? c->provider->port(c->socket) : 0;
}
