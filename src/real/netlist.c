/* \real\cmn's network lists: see netlist.h. */
#include "real/netlist.h"
#include <stddef.h>

static uint32_t own_address(void *node, void *arg) { return (uint32_t)(uintptr_t)node; }   /* 0x580420 */

void NetList_Init(NetList *l, NetKeyFn key, void *keyarg)
{
    l->cs = CS_create();   /* 0x531f60 */
    l->count = 0;
    l->flags = 2;
    l->head = l->tail = NULL;
    l->key = key ? key : own_address;
    l->keyarg = keyarg;
}

void NetList_InitShared(NetList *l, NetKeyFn key, void *keyarg, const NetList *with)
{
    l->cs = with->cs;
    l->count = 0;
    l->flags = 0;
    l->head = l->tail = NULL;
    l->key = key ? key : own_address;
    l->keyarg = keyarg;
}

void NetList_Free(NetList *l)
{
    if (l->flags & 2) CS_destroy(l->cs);   /* 0x532040 */
    l->cs = NULL;
}

void NetList_Push(NetList *l, void *node)
{
    CS_enter(l->cs);
    if (node) {
        NetNode *n = node;
        n->next = l->head;
        if (!l->head) l->tail = n;
        l->head = n;
        l->count++;
        l->flags |= 1;
    }
    CS_leave(l->cs);
}

void NetList_Append(NetList *l, void *node)
{
    CS_enter(l->cs);
    if (node) {
        NetNode *n = node;
        n->next = NULL;
        l->count++;
        if (l->tail) l->tail->next = n;
        else l->head = n;
        l->tail = n;
        l->flags |= 1;
    }
    CS_leave(l->cs);
}

void *NetList_Pop(NetList *l)
{
    CS_enter(l->cs);
    NetNode *n = l->head;
    if (n) {
        if (n == l->tail) l->head = l->tail = NULL;
        else l->head = n->next;
        n->next = NULL;
        l->count--;
    }
    l->flags |= 1;
    CS_leave(l->cs);
    return n;
}

/* The tail, found by walking from the head. */
void *NetList_Pull(NetList *l)
{
    CS_enter(l->cs);
    NetNode *n = l->tail;
    if (n) {
        l->count--;
        if (l->head == n) l->head = l->tail = NULL;
        else {
            NetNode *p = l->head;
            while (p->next != n) p = p->next;
            l->tail = p;
            p->next = NULL;
        }
        n->next = NULL;
    }
    l->flags |= 1;
    CS_leave(l->cs);
    return n;
}

/* In key order, after the nodes with smaller keys. */
void NetList_Insert(NetList *l, void *node)
{
    CS_enter(l->cs);
    if (node) {
        uint32_t k = l->key(node, l->keyarg);
        l->count++;
        NetNode *prev = NULL, *at = l->head;
        while (at && l->key(at, l->keyarg) < k) {
            prev = at;
            at = at->next;
        }
        ((NetNode *)node)->next = at;
        if (prev) prev->next = node;
        else l->head = node;
        if (!at) l->tail = node;
        l->flags |= 1;
    }
    CS_leave(l->cs);
}

/* (Without the section: its callers hold it.) */
bool NetList_Unlink(NetList *l, void *node)
{
    NetNode *n = node;
    if (!n || !l->count) return false;
    if (n == l->head) {
        l->count--;
        if (n != l->tail) l->head = n->next;
        else l->head = l->tail = NULL;
        n->next = NULL;
        l->flags |= 1;
        return true;
    }
    NetNode *p = l->head;
    while (p->next && p->next != n) p = p->next;
    if (p->next != n) return false;
    p->next = n->next;
    l->count--;
    if (n == l->tail) l->tail = p;
    n->next = NULL;
    l->flags |= 1;
    return true;
}

int NetList_Count(const NetList *l) { return l->count; }

int NetList_Index(NetList *l, void *node)
{
    int i = -1;
    CS_enter(l->cs);
    if (node) {
        int k = 0;
        for (NetNode *n = l->head; n; n = n->next, k++)
            if (n == node) {
                i = k;
                break;
            }
    }
    CS_leave(l->cs);
    return i;
}

void *NetList_Find(NetList *l, NetMatchFn fn, void *arg)
{
    CS_enter(l->cs);
    NetNode *n = l->head;
    while (n && fn && !fn(n, arg)) n = n->next;
    CS_leave(l->cs);
    return n;
}

/* The i-th node (0x580920 walks i - 1 on from the head's next). */
void *NetList_At(NetList *l, int i)
{
    CS_enter(l->cs);
    NetNode *n = l->head;
    if (n && i) {
        n = n->next;
        for (int k = i - 1; n && k; k--) n = n->next;
    }
    CS_leave(l->cs);
    return n;
}

/* A sorted list's node of exactly that key. */
void *NetList_FindKey(NetList *l, uint32_t key)
{
    CS_enter(l->cs);
    NetNode *n = l->head;
    while (n && l->key(n, l->keyarg) < key) n = n->next;
    if (n && l->key(n, l->keyarg) != key) n = NULL;
    CS_leave(l->cs);
    return n;
}

void *NetList_Take(NetList *l, NetMatchFn fn, void *arg)
{
    CS_enter(l->cs);
    NetNode *n = l->head;
    while (n && fn && !fn(n, arg)) n = n->next;
    if (n && !NetList_Unlink(l, n)) n = NULL;
    CS_leave(l->cs);
    return n;
}

void *NetList_TakeKey(NetList *l, uint32_t key)
{
    CS_enter(l->cs);
    NetNode *n = l->head, *got = NULL;
    while (n && l->key(n, l->keyarg) < key) n = n->next;
    if (n && l->key(n, l->keyarg) == key && NetList_Unlink(l, n)) got = n;
    CS_leave(l->cs);
    return got;
}

/* fn over the nodes until it returns false or the list changes (its count of calls; -1 if it ran to
   the end): the section is let go during the walk, the change flag tells a change. */
int NetList_Walk(NetList *l, NetMatchFn fn, void *arg)
{
    int calls = 0;
    CS_enter(l->cs);
    uint32_t was = l->flags;
    l->flags &= ~1u;
    CS_leave(l->cs);
    for (NetNode *n = l->head; n && !(l->flags & 1); n = n->next, calls++)
        if (!fn(n, arg)) l->flags |= 1;
    CS_enter(l->cs);
    if (!(l->flags & 1)) calls = -1;
    l->flags |= was & 1;
    CS_leave(l->cs);
    return calls;
}
