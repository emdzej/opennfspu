/* The network library's lists (\real\cmn, 0x5803e0..0x580b70): singly linked through each node's first
   word, with a count, a head and a tail, a key function for the sorted ones (default: the node's
   address, 0x580420) and a critical section of their own. A push goes in at the head and a pop takes
   the head (a stack); with pull, which takes the tail, a push makes a queue. */
#pragma once
#include "real/thread.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct NetNode { struct NetNode *next; } NetNode;
typedef uint32_t (*NetKeyFn)(void *node, void *arg);
typedef bool (*NetMatchFn)(void *node, void *arg);

typedef struct NetList {
    int count;
    uint32_t flags;        /* 1: changed (a walk in progress stops), 2: the section is the list's own */
    NetNode *head, *tail;
    NetKeyFn key;
    void *keyarg;
    CritSec *cs;
} NetList;

void NetList_Init(NetList *l, NetKeyFn key, void *keyarg);                         /* 0x5803e0 */
void NetList_InitShared(NetList *l, NetKeyFn key, void *keyarg, const NetList *with);   /* 0x580430 */
void NetList_Free(NetList *l);                                                     /* 0x580470 */
void NetList_Push(NetList *l, void *node);                                         /* 0x580490 */
void NetList_Append(NetList *l, void *node);                                       /* 0x5804e0 */
void *NetList_Pop(NetList *l);                                                     /* 0x580540 */
void *NetList_Pull(NetList *l);                                                    /* 0x580590 */
void NetList_Insert(NetList *l, void *node);                                       /* 0x580600: by key */
bool NetList_Remove(NetList *l, void *node);                                       /* 0x5806a0: Unlink in the section */
bool NetList_Unlink(NetList *l, void *node);                                       /* 0x5806e0 */
int NetList_Count(const NetList *l);                                               /* 0x5807a0 */
int NetList_Index(NetList *l, void *node);                                         /* 0x5807b0 */
void *NetList_Find(NetList *l, NetMatchFn fn, void *arg);                          /* 0x580880 */
void *NetList_At(NetList *l, int i);                                               /* 0x5808d0 */
void *NetList_FindKey(NetList *l, uint32_t key);                                   /* 0x580940 */
void *NetList_Take(NetList *l, NetMatchFn fn, void *arg);                          /* 0x5809c0 */
void *NetList_TakeKey(NetList *l, uint32_t key);                                   /* 0x580af0 */
int NetList_Walk(NetList *l, NetMatchFn fn, void *arg);                            /* 0x580b70 */
