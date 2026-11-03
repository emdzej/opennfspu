/* The address providers' registry and a connection's calls into its provider: see netprov.h. */
#include "real/netprov.h"
#include <stddef.h>

const NetProvider *g_net_providers[NET_MAXPROVIDERS];
const void *g_net_transports[NET_MAXPROVIDERS];
int g_net_nproviders;

/* (Each registration, 0x560580 / 0x560840 / 0x581ff0, appends its pair as one.) */
void NET_addprovider(const NetProvider *p, const void *transport)
{
    if (g_net_nproviders >= NET_MAXPROVIDERS) return;
    g_net_providers[g_net_nproviders] = p;
    g_net_transports[g_net_nproviders] = transport;
    g_net_nproviders++;
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
