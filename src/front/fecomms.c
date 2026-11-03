/* The network game screens (see fecomms.h). */
#include "front/fecomms.h"
#include "exe.h"
#include "front/fe_parts.h"
#include "front/feaccess.h"
#include "front/feapp.h"
#include "front/fesave.h"
#include "game/car.h"
#include "game/net.h"
#include "game/option_rec.h"
#include "pending.h"
#include "platform.h"
#include "real/systask.h"
#include "real/thread.h"
#include "render/render.h"
#include "support/crt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The descriptions' lobby fields (0x65808c + 0x580 a player's number): COLOUR, FEMULTIPLAYERCARNAME,
   FEMULTIPLAYERCARID, READY. */
enum { D_COLOUR = 0x10, D_CARNAME = 0x534, D_CARID = 0x574, D_READY = 0x578 };
static int32_t desc_i(int n, int off)
{
    int32_t v = 0;
    if (0 <= n && n < CAR_MAX) memcpy(&v, g_cardesc[n] + off, 4);
    return v;
}
static void desc_set(int n, int off, int32_t v)
{
    if (0 <= n && n < CAR_MAX) memcpy(g_cardesc[n] + off, &v, 4);
}

/* The session's states as the screens test them: connected (+8, not lost), in a game (+0xbe), seated
   (+0xdc). */
static bool live(void) { return Net_Live(); }
static bool in_game(void) { return live() && g_net->in; }
static bool seated(void) { return Net_InGame(); }
static int me(void) { return live() ? g_net->me : 0; }   /* 0x4ed360 (0 out of a session) */

bool FEComms_InGame(void) { return seated(); }

static void lock(void) { if (g_net && g_net->cs) CS_enter(*g_net->cs); }
static void unlock(void) { if (g_net && g_net->cs) CS_leave(*g_net->cs); }

/* My READY to everyone (the screens' 0x4ed290(0x255) record and Net_AddOptions). */
static void send_ready(int v)
{
    if (!in_game()) return;
    int32_t recs[4], *p = recs;
    if (0 <= me()) Option_PutCar(&p, 0x255, me(), v);
    *p = 0;
    Net_AddOptions(recs);
}

/* The car chosen, its records to everyone (zMergedList.Car's slot 1, FEMergedCarListAccessor_v1
   0x4f3f10, with my READY first). */
static void send_car(int ready)
{
    int32_t recs[0x40], *p = recs;
    if (0 <= me()) Option_PutCar(&p, 0x255, me(), ready);
    p = FEData_CarRecords(p, me());
    *p = 0;
    Net_AddOptions(recs);
}

/* ---------------------------------------------------------------- the error handler */

static bool s_on;                /* 0x699dd0 */
static bool s_signon;            /* FEMainScreen's +0x294: SIGNON's, till the session's state 4 */
static int s_conn = -1;          /* 0x699ddc: the connecting box (0x331, its cancel) */
static int s_persona = -1;       /* 0x699dd8: the name taken box (0x335, a new name) */
static char s_address[0x80];     /* 0x699d80: the registry's Primary Address (6) */

void FEComms_Errors(bool on)
{
    s_on = on;
    if (!on) {
        FEApp_DialogClose(s_conn);
        FEApp_DialogClose(s_persona);
        s_conn = s_persona = -1;
    }
}

static const char *primary_address(void)   /* 0x509060 */
{
    RRegistry_GetString(6, s_address, sizeof s_address);
    return s_address;
}

static const char *player_name(void)   /* 0x500850 */
{
    Save *sv = FESave_Current();
    return sv ? sv->player2 : "";
}

static void set_nick(const char *name)   /* (the session's +0xa8) */
{
    if (g_net) snprintf(g_net->nick, sizeof g_net->nick, "%s", name);
}

/* Leaving the session's game after a message: a server's game left for its lobby (+0xc6: state 4),
   another ended. */
static void drop(void)
{
    if (!g_net) return;
    if (g_net->inroom) g_net->state = 4;
    else Net_End();
}

static void back(void);

static void errors(void)   /* FECommsErrorHandler_v1 0x508c10 */
{
    if (!s_on) return;
    int st = Net_State();
    if (st == 3) {
        if (FEApp_DialogState(s_conn) < 0) s_conn = FEApp_DialogOpen(0, FEStrings_Get(0x331), 0x2bf, 0, 0);
        if (0 < FEApp_DialogState(s_conn)) {
            FEApp_DialogClose(s_conn);
            s_conn = -1;
            if (g_net) {
                Net_End();
                drop();
            }
        }
    } else if (0 <= s_conn) {
        FEApp_DialogClose(s_conn);
        s_conn = -1;
    }
    switch (st) {
    case 5:
    case 6:
    case 7:
        FEApp_Message(st == 5 ? 0x33e : st == 6 ? 0x33f : 0x333);
        if (g_net) Net_End();
        break;
    case 8: {
        /* the name taken: another asked for (eight letters, the player's name to start from) */
        FEApp_DialogClose(s_persona);
        s_persona = FEApp_DialogOpen(2, FEStrings_Get(0x335), 0, 0, 8);
        snprintf(FEApp_DialogInput(), 32, "%s", player_name());
        if (g_net) Net_End();
        break;
    }
    case 9:
    case 10:
    case 0xb:
        FEApp_Message(st == 9 ? 0x332 : st == 10 ? 0x336 : 0x334);
        drop();
        back();
        break;
    case 0xc:
        FEApp_Message(0x33e);
        if (g_net) Net_End();
        back();
        break;
    default:
        break;
    }
    if (0 <= s_persona && FEApp_DialogState(s_persona) == 1) {
        const char *in = FEApp_DialogInput();
        if (!in[0]) {
            /* (an empty name: the box again) */
            s_persona = FEApp_DialogOpen(2, FEStrings_Get(0x335), 0, 0, 8);
            return;
        }
        Save *sv = FESave_Current();
        if (sv) snprintf(sv->player2, sizeof sv->player2, "%s", in);   /* 0x500860 */
        set_nick(player_name());
        s_persona = -1;
        Net_JoinGame(1, 0, primary_address());
    }
}

/* ---------------------------------------------------------------- the screens */

typedef enum { S_NONE, S_CREATE, S_JOIN, S_LAN, S_WEB, S_MODEM, S_PEER, S_SERVER } Kind;
static struct {
    Kind kind;
    bool was_in;      /* create, join +0x284: seated once */
    bool sent;        /* +0x285: the car's records sent */
    bool leaving;     /* create +0x286, join +0x291: its back ran */
    bool lobby;       /* games +0x28c: in the lobby once (its list again) */
    int name_box;     /* games +0x298: CREATE's game name box */
    bool connecting;  /* peer +0x288, modem +0x294 */
    bool went;        /* peer +0x289, modem +0x295 */
    int nplayers;     /* join +0x288 */
    bool playing;     /* the base list's +0x278: PLAYGAME under way */
} S;

static Kind kind_of(const char *cbl)
{
    static const struct { const char *n; Kind k; } names[] = {
        {"ID_COMMSCREATESCREEN", S_CREATE}, {"ID_COMMSJOINSCREEN", S_JOIN}, {"ID_COMMSLANGAMESSCREEN", S_LAN},
        {"ID_COMMSWEBGAMESSCREEN", S_WEB},  {"ID_COMMSMODEMSCREEN", S_MODEM}, {"ID_PEERTOPEERIP", S_PEER},
        {"ID_COMMSSERVERSCREEN", S_SERVER},
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(cbl, names[i].n)) return names[i].k;
    return S_NONE;
}

static void set_pos(const char *name, int pos)
{
    FEAccessor *a = FEAccess_Find(name);
    if (a) FEAccess_SetPos(a, pos);
}

static int pos_of(const char *name)
{
    FEAccessor *a = FEAccess_Find(name);
    return a ? FEAccess_Pos(a) : 0;
}

/* The game's description (0x5091c0): Comms.GameName's name (32), eight players at most, the track's id,
   its direction's string, its laps (on a circuit), the positional updates. */
static NetGameInfo s_info, s_sent;   /* 0x699de8, 0x699e20 */
static void describe(NetGameInfo *g)
{
    memset(g, 0, sizeof *g);
    FEAccessor *n = FEAccess_Find("Comms.GameName");
    snprintf(g->name, sizeof g->name, "%s", n ? FEAccess_Text(n) : "");
    g->maxplayers = 8;
    g->s[1] = (int16_t)FEData_TrackLocale();
    g->s[0] = (int16_t)FEAccess_ItemId(FEAccess_Find("Track1.Direction"));
    g->b[2] = FEData_TrackLooped(3) ? (uint8_t)pos_of("Track1.Laps") : 0;
    g->b[1] = (uint8_t)pos_of("Comms.Positional");
}

static bool described_differs(const NetGameInfo *a, const NetGameInfo *b)   /* 0x5092f0 */
{
    bool d = strcmp(a->name, b->name) != 0;
    for (int i = 0; i < 4; i++) d |= a->b[i] != b->b[i];
    for (int i = 0; i < 2; i++) d |= a->s[i] != b->s[i] || a->i[i] != b->i[i];
    return d;
}

/* Hosting (0x5090c0): the description kept, my name the session's, the game made on <port>. */
static void host(int port)
{
    describe(&s_info);
    s_sent = s_info;
    set_nick(player_name());
    Net_HostGame(&s_info, port);
}

/* The application's tick's (FEApplication_v1 0x4d25d0 -> 0x509130): the host's description again when
   it changed. */
static void redescribe(void)
{
    if (!seated() || !Net_IsFirst()) return;
    describe(&s_info);
    if (described_differs(&s_info, &s_sent)) {
        Net_HostGame(&s_info, 0);
        s_sent = s_info;
    }
}

void FEComms_Open(const char *cbl)
{
    Kind k = kind_of(cbl);
    memset(&S, 0, sizeof S);
    S.kind = k;
    S.name_box = -1;
    switch (k) {
    case S_CREATE:   /* 0x508680 */
        if (0 <= me()) {
            g_cardesc[me()][D_CARNAME] = 0;
            desc_set(me(), D_READY, 1);
        }
        FEApp_SetGameType(3);
        break;
    case S_JOIN:     /* 0x509810 */
        S.nplayers = live() ? g_net->nplayers : 0;
        desc_set(me(), D_READY, 0);
        FEApp_SetGameType(3);
        break;
    case S_LAN:      /* 0x50a010 (FECommsGamesScreen 0x5093a0 first: Positional 0): the LAN's games looked for */
        set_pos("Comms.Positional", 0);
        if (g_net) Net_End();
        set_nick(player_name());
        Net_JoinGame(0, 0, NULL);
        set_pos("Comms.Positional", 0);
        break;
    case S_WEB:      /* 0x50acb0 */
        set_pos("Comms.Positional", 0);
        set_pos("Comms.Positional", 1);
        break;
    case S_MODEM:    /* 0x50a2f0: the modems listed (Net_JoinGame(2) without an address, 0x55e920: pending) */
        Net_JoinGame(2, 0, NULL);
        break;
    case S_SERVER:   /* 0x50aa00: positional updates, the Internet's entry server */
        set_pos("Comms.Positional", 1);
        Net_JoinGame(1, 0, primary_address());
        break;
    default:
        break;
    }
}

void FEComms_Close(const char *cbl)
{
    Kind k = kind_of(cbl);
    if (k == S_JOIN && in_game()) send_ready(0);   /* FECommsJoinScreen_v0 0x509920 */
    if (k == S_LAN || k == S_WEB) FEApp_DialogClose(S.name_box);   /* (FECommsGamesScreen_v0: its box) */
    if (S.kind == k) S.kind = S_NONE;
}

/* The top list's back (its slot 45 0xb4 by 0x65b928's +0xe0; one screen back without a list,
   0x4d1e40(0)). */
static void back(void)
{
    FertControl *w = FEApp_Top();
    const char *cbl = w ? FEApp_ScreenAttr(w, "callbacklist") : "";
    if (!FEComms_Back(cbl)) FEApp_Back();
}

bool FEComms_Back(const char *cbl)
{
    switch (kind_of(cbl)) {
    case S_CREATE:   /* FECommsCreateScreen_v45 0x508830 */
        S.leaving = true;
        if (live()) Net_Leave();
        FEApp_Back();   /* 0x50c2b0 */
        return true;
    case S_JOIN:     /* FECommsJoinScreen_v45 0x509b70 */
        if (!S.leaving) {
            S.leaving = true;
            if (live()) Net_Leave();
            FEApp_Back();
        }
        return true;
    case S_PEER:     /* 0x50a930 */
        if (g_net) Net_End();
        FEApp_Back();
        return true;
    default:
        return false;
    }
}

/* The cars' check (0x5099c0, 0x509bb0, 0x50c550): a player's car (FEMULTIPLAYERCARID) one this machine
   hasn't (0x4f3ad0), its message (0x32e: the player and the car's name) up; false then. */
static bool cars_ok(bool show)
{
    if (!seated()) return true;
    NETLST_EACH(&g_net->plist, n) {
        NetPlayer *p = n->p;
        int id = desc_i(p->number, D_CARID);
        if (0 < id && !FEData_HaveCarId(id)) {
            if (show) {
                char m[600];
                snprintf(m, sizeof m, FEStrings_Get(0x32e), p->name, FEData_CarNameById(id));
                FEApp_MessageText(m);
            }
            return false;
        }
    }
    return true;
}

static bool idle(void) { return FEApp_Idle(); }

void FEComms_Update(FertControl *w, const char *cbl)
{
    errors();
    redescribe();
    /* FEMainScreen_v1 0x50faf0's: the protocol with a provider, the Internet and the address with UDP,
       the LAN with IPX, the modem with one; signed on, the server list once the session's in its lobby */
    bool modem = Net_HasProvider(2);
    FEApp_Enable(w, "ID_COMMSPROTOCOL", Net_HasProvider(0) || Net_HasProvider(1) || modem);
    if (!Net_HasProvider(1)) {
        FEApp_Enable(w, "ID_COMMSINTERNET", false);
        FEApp_Enable(w, "ID_COMMSPEER", false);
    }
    if (!Net_HasProvider(0)) FEApp_Enable(w, "ID_COMMSLAN", false);
    FEApp_Enable(w, "ID_COMMSMODEM", modem);
    if (s_signon && Net_State() == 4) {
        s_signon = false;
        FEApp_GotoScreen("serverlist", false);
    }
    Kind k = kind_of(cbl);
    if (k != S.kind) return;
    switch (k) {
    case S_CREATE:   /* FECommsCreateScreen_v1 0x508950 */
        if (seated() && !S.sent) {
            /* in: my car's records to everyone, my READY on */
            S.was_in = true;
            FEData_ResyncCars();
            FEAccess_SendShared();   /* 0x4b77b0 */
            desc_set(me(), D_READY, 1);
            send_car(desc_i(me(), D_READY));
            S.sent = true;
        }
        if (idle() && ((live() && g_net->lost) || (S.was_in && !seated())) && !S.leaving) FEComms_Back(cbl);
        FEApp_Enable(w, "ID_GOTOBUTTON", true);
        break;
    case S_JOIN: {   /* FECommsJoinScreen_v1 0x509bb0 */
        if (!S.sent && seated()) {
            S.was_in = true;
            FEData_ResyncCars();
            S.sent = true;
            send_car(0);
        }
        if (seated()) {
            /* a player's car I haven't: the message, my READY off */
            if (desc_i(me(), D_READY) && !cars_ok(true)) {
                desc_set(me(), D_READY, 0);
                send_ready(0);
            }
        }
        if (idle()) {
            if (!(live() && g_net->lost) && (!S.was_in || seated())) {
                if (seated()) {
                    FEApp_Enable(w, "ID_COMMSREADY", g_net->in);
                    if (Net_IsFirst()) FEApp_GotoScreen("creategame", false);   /* (the host gone: I am it) */
                }
            } else if (!S.leaving) {
                FEComms_Back(cbl);
            }
        }
        /* the race started by the host: PLAYGAME here too (slot 0xd4) */
        if (live() && !g_net->lost && !g_net->raceover && S.kind == S_JOIN) FEApp_PlayDirect();
        break;
    }
    case S_LAN:
    case S_WEB: {   /* FECommsGamesScreen_v1 0x509660 */
        if (!S.lobby && in_game()) S.lobby = true;
        if (idle()) {
            if (live() && g_net->lost) {
                back();
                return;
            }
            int st = FEApp_DialogState(S.name_box);
            if (0 < st) {
                if (st == 1) {
                    FEAccessor *n = FEAccess_Find("Comms.GameName");
                    if (n) FEAccess_SetText(n, FEApp_DialogInput());
                    host(0);
                    S.name_box = -1;
                    FEApp_GotoScreen("creategame", true);
                    return;
                }
                S.name_box = -1;
            }
        }
        if (k == S_LAN) {   /* FECommsLanGamesScreen_v1 0x50a200 */
            FEApp_Enable(w, "ID_COMMSJOINGAME", FEComms_Count(0) > 0);
        } else {            /* FECommsWebGamesScreen_v1 0x50ae20 */
            FEApp_Enable(w, "ID_COMMSCREATEGAME", in_game());
            FEApp_Enable(w, "ID_COMMSJOINGAME", in_game() && FEComms_Count(0) > 0);
        }
        break;
    }
    case S_MODEM:   /* FECommsModemScreen_v1 0x50a5b0 */
        FEApp_Enable(w, "ID_MODEMCREATEGAME", FEAccess_Count(FEAccess_Find("Comms.ModemList")) > 0);
        FEApp_Enable(w, "ID_DIALBUTTON", FEAccess_Count(FEAccess_Find("Comms.ModemList")) > 0);
        if (in_game() && S.connecting && !S.went) {
            FEApp_GotoScreen("joingame", true);
            S.went = true;
        }
        break;
    case S_PEER:    /* FECommsPeerToPeerIP_v1 0x50a950 */
        if (in_game() && S.connecting && !S.went) {
            FEApp_GotoScreen("joingame", true);
            S.went = true;
        }
        break;
    case S_SERVER: {   /* FECommsServerScreen_v1 0x50aba0: a lost connection's ticks past five times
                          0x5deb48, the entry server again */
        int frames = live() ? (g_net->lostroom ? -1 : g_net->frames) : 0;
        if (0 <= frames && (int32_t)exe_u32(0x5deb48) * 5 < frames) {
            if (g_net) Net_End();
            Net_JoinGame(1, 0, primary_address());
        }
        FEApp_Enable(w, "ID_COMMSENTERSERVER", in_game() && FEComms_Count(1) > 0);
        break;
    }
    default:
        break;
    }
}

/* The game name box of CREATE (0x509500: 0x259, 25 letters; an empty name made of the player's,
   0x25a's "%s's game"). */
static void name_box(void)
{
    FEAccessor *n = FEAccess_Find("Comms.GameName");
    if (!n) return;
    if (!FEAccess_Text(n)[0]) {
        char b[32];
        Save *sv = FESave_Current();
        const char *who = pos_of("Comms.GameMode") == 0 ? player_name() : sv ? sv->player : "";
        snprintf(b, sizeof b, FEStrings_Get(0x25a), who);
        FEAccess_SetText(n, b);
    }
    S.name_box = FEApp_DialogOpen(2, FEStrings_Get(0x259), 0, 0, 0x19);
    snprintf(FEApp_DialogInput(), 32, "%s", FEAccess_Text(n));
}

/* JOIN on a games list (FECommsLanGamesScreen_v55 0x50a140, FECommsWebGamesScreen_v55 0x50ad60): the
   game chosen, unless its version differs (0x336) or it's full (0x11e). */
static void join_chosen(int mode)
{
    int row = pos_of("Comms.GamesList");
    lock();
    NetGameRec *g = NULL;
    int i = 0;
    if (live())
        NETLST_EACH(&g_net->found, n) {
            if (i++ == row) g = n->p;
        }
    bool vers = g && strcmp(g->version, g_net->version) != 0;
    bool full = g && 7 < g->players;
    int id = g ? g->id : 0;
    unlock();
    if (vers) {
        FEApp_Message(0x336);
        return;
    }
    if (full) {
        FEApp_Message(0x11e);
        return;
    }
    set_nick(player_name());   /* (0x516210) */
    Net_JoinGame(mode, id, NULL);
    FEApp_GotoScreen("joingame", true);
}

bool FEComms_Callback(const char *cbl, const char *cb)
{
    Kind k = kind_of(cbl);
    if (!strncmp(cb, "SETMODE", 7)) {
        /* FEMainScreen's (0x510660 .. 0x510740): Comms.GameMode; the Internet's sign on box first
           (0x512840(0x4e): its accessor's text cleared), then the session's version (0x4a54b0) */
        static const char *const modes[4] = {"SETMODENET", "SETMODELAN", "SETMODEMODEM", "SETMODEIP"};
        for (int i = 0; i < 4; i++)
            if (!strcmp(cb, modes[i])) set_pos("Comms.GameMode", i);
        return true;
    }
    if (!strcmp(cb, "SIGNON")) {   /* 0x510290: the account and password the session's, the entry server */
        FEAccessor *id = FEAccess_Find("COMMS.ACCOUNTID"), *pw = FEAccess_Find("COMMS.PASSWORD");
        if (g_net) {
            g_net->name[0] = 99;   /* (+0x58 'c', then the id from +0x59) */
            snprintf(g_net->name + 1, sizeof g_net->name - 1, "%s", id ? FEAccess_Text(id) : "");
            snprintf(g_net->password, sizeof g_net->password, "%s", pw ? FEAccess_Text(pw) : "");
        }
        set_nick(player_name());
        Net_JoinGame(1, 0, primary_address());
        s_signon = true;
        return true;
    }
    if (!strcmp(cb, "REGISTERUSER")) {   /* 0x510270: the registration page (0x655a4c 1, 0x4d51b0(0): the front end left) */
        PENDING_NOTE(0x510270, "REGISTERUSER: EA's account page in a browser (the front end quits to it)");
        return true;
    }
    if (!strcmp(cb, "CLEARACCOUNTINFO")) {   /* 0x50d3c0 */
        FEAccessor *id = FEAccess_Find("COMMS.ACCOUNTID"), *pw = FEAccess_Find("COMMS.PASSWORD");
        if (id) FEAccess_SetText(id, "");
        if (pw) FEAccess_SetText(pw, "");
        return true;
    }
    switch (k) {
    case S_LAN:
    case S_WEB:
        if (!strcmp(cb, "CREATE")) {
            name_box();
            return true;
        }
        if (!strcmp(cb, "JOIN")) {
            join_chosen(k == S_LAN ? 0 : 1);
            return true;
        }
        if (!strcmp(cb, "REFRESH")) return true;   /* 0x509650: the list's own (it's read live) */
        break;
    case S_PEER:
        if (!strcmp(cb, "CREATE")) {   /* 0x50a800 */
            S.connecting = false;
            host(0);
            return true;
        }
        if (!strcmp(cb, "CONNECT")) {   /* 0x50a810: Comms.IP's address (an empty one 0x330) */
            S.connecting = true;
            FEAccessor *ip = FEAccess_Find("Comms.IP");
            char addr[0x100];
            snprintf(addr, sizeof addr, "%s", ip ? FEAccess_Text(ip) : "");
            if (!addr[0]) {
                FEApp_Message(0x330);
                return true;
            }
            set_nick(player_name());
            Net_JoinGame(1, 0, addr);
            return true;
        }
        break;
    case S_MODEM:
        if (!strcmp(cb, "DIAL")) {   /* 0x50a480: Comms.PhoneNumber on the modem chosen (an empty one 0x337) */
            S.connecting = true;
            FEAccessor *ph = FEAccess_Find("Comms.PhoneNumber");
            char num[0x100];
            snprintf(num, sizeof num, "%s", ph ? FEAccess_Text(ph) : "");
            if (!num[0]) {
                FEApp_Message(0x337);
                return true;
            }
            set_nick(player_name());
            Net_JoinGame(2, pos_of("Comms.ModemList"), num);
            return true;
        }
        if (!strcmp(cb, "CREATE")) {   /* 0x50a660 */
            Net_JoinGame(2, 0, NULL);
            S.connecting = false;
            host(pos_of("Comms.ModemList"));
            return true;
        }
        break;
    case S_SERVER:
        if (!strcmp(cb, "ENTER")) {   /* 0x50ab30: the server chosen's address */
            const char *t = FEComms_Text(1, pos_of("Comms.ServerList"));
            char addr[0x100];
            snprintf(addr, sizeof addr, "%s", t);
            Net_JoinGame(1, 0, addr);
            return true;
        }
        break;
    case S_CREATE:
        if (!strcmp(cb, "EJECTPLAYER")) {   /* 0x5087d0: the player chosen out */
            FEAccessor *pl = FEAccess_Find("Comms.PlayerList");
            if (pl && FEAccess_Pos(pl) >= 0) {
                Net_Kick(FEComms_RowNumber(3, FEAccess_Pos(pl)));
                FEAccess_Unselect(pl);
            }
            return true;
        }
        break;
    case S_JOIN:
        if (!strcmp(cb, "READY")) {   /* 0x5099c0: my READY turned over, unless a car's missing */
            if (seated() && g_net->raceover) {   /* (+0xbc: no race running) */
                if (!cars_ok(true)) return true;
                int r = desc_i(me(), D_READY);
                send_ready(r == 0);
            }
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

void FEComms_Leaving(void)
{
    if (live() && g_net->in) send_ready(0);
}

bool FEComms_CanPlay(void)
{
    if (!seated()) return false;
    bool all = true;
    lock();
    NETLST_EACH(&g_net->plist, n)
        if (!desc_i(((NetPlayer *)n->p)->number, D_READY)) all = false;
    bool ok = !(live() && g_net->racing) && all;
    unlock();
    return ok;
}

/* PLAYGAME in a network game (0x50c550). */
bool FEComms_Play(void)
{
    if (S.playing) return false;
    S.playing = true;
    bool ok = false;
    if (seated()) {
        bool ready = desc_i(me(), D_READY) != 0;
        if (me() != 0) {
            desc_set(me(), D_READY, 0);
            send_ready(0);
        }
        Net_SendStart();   /* 0x48d2a0 */
        /* the start waited for (+0xc4), Escape or the session's loss giving up */
        for (;;) {
            if (!seated()) goto abort;
            if (g_net->startwait) break;
            uint8_t keys[KEY_COUNT];
            if (plat_keys(keys) && keys[1]) goto abort;
            THREAD_sleep(1);    /* 0x55f4f0 */
            SYNCTASK_run(0);    /* 0x536450 */
        }
        if (live()) Net_SyncBarrier();
        ok = false;
        if (ready) ok = cars_ok(true);
        if (me() == 0 && !ok) {
            /* (the host: READY on, the start given up after the barrier below) */
            ready = true;
            S.playing = false;
            ok = true;
        }
        desc_set(me(), D_READY, ready);
        send_ready(ok);
        if (live()) {
            Net_SyncBarrier();
            if (seated()) {
                NETLST_EACH(&g_net->plist, n)
                    if (!desc_i(((NetPlayer *)n->p)->number, D_READY)) S.playing = false;
            }
        }
    }
    if (S.playing) {
        S.playing = false;
        return true;
    }
abort:
    S.playing = false;
    if (seated() && !g_net->aborted) {
        g_net->aborted = 1;
        g_net->raceover = 1;
        g_net->startwait = 0;
        Net_Finish(-2, -1);   /* 0x48d990 */
    }
    return false;
}

int FEComms_ReturnStack(const char *names[4])
{
    int n = 0;
    switch (pos_of("Comms.GameMode")) {
    case 0:
        names[n++] = "serverlist";
        names[n++] = "udpip";
        break;
    case 1:
        names[n++] = "local";
        break;
    case 2:
        names[n++] = "modem";
        break;
    case 3:
        names[n++] = "peertopeerip";
        break;
    default:
        break;
    }
    names[n++] = Net_IsFirst() ? "creategame" : "joingame";
    return n;
}

/* ---------------------------------------------------------------- the lists */

int FEComms_Count(int list)
{
    if (!live()) return list == 3 ? 8 : 0;
    int n = 0;
    lock();
    switch (list) {
    case 0:   /* FEGameListAccessor_v2 0x4b9b90: the games found */
        NETLST_EACH(&g_net->found, x) n++;
        break;
    case 1:   /* FECommInfoAccessor_v2 0x4b92e0: the servers that answered a ping */
        NETLST_EACH(&g_net->ports, x)
            if (0 <= Net_GamePing(x->p)) n++;
        break;
    case 2:   /* FELobbyPlayerListAccessor_v2 0x4ba920 */
        NETLST_EACH(&g_net->plist, x) n++;
        break;
    default:  /* FEPlayerListAccessor 0x4bb7b0: eight seats */
        n = 8;
        break;
    }
    unlock();
    return n;
}

static NetPlayer *player_at(int row)
{
    int i = 0;
    if (!live()) return NULL;
    NETLST_EACH(&g_net->plist, x)
        if (i++ == row) return x->p;
    return NULL;
}

/* The player list's rows (0x4bb7b0): the players while the screen's on (an open seat, 0x11f, for the
   rest, and all eight while it isn't). */
static NetPlayer *seat_at(int row)
{
    if (!idle() || !live()) return NULL;
    return player_at(row);
}

int FEComms_RowNumber(int list, int row)
{
    NetPlayer *p = list == 3 ? seat_at(row) : list == 2 ? player_at(row) : NULL;
    return p ? p->number : -1;
}

const char *FEComms_Text(int list, int row)
{
    static char b[0x48];
    b[0] = 0;
    lock();
    if (list == 0) {
        snprintf(b, sizeof b, "%s", exe_str(0x5d7334));   /* FEGameListAccessor_v12 0x4b9b80 */
    } else if (list == 1) {
        /* FECommInfoAccessor_v12 0x4b9220: the row's server (of those with a ping) */
        int i = 0;
        if (live())
            NETLST_EACH(&g_net->ports, x) {
                NetGameRec *g = x->p;
                if (Net_GamePing(g) < 0) continue;
                if (i++ == row) snprintf(b, sizeof b, "%s", g->name);
            }
    } else {
        NetPlayer *p = list == 3 ? seat_at(row) : player_at(row);
        snprintf(b, sizeof b, "%s", p ? p->name : list == 3 ? FEStrings_Get(0x11f) : "");
    }
    unlock();
    return b;
}

static void cell(FECommsRow *r, const char *pos, int align, uint32_t box, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
#include <stdarg.h>
static void cell(FECommsRow *r, const char *pos, int align, uint32_t box, const char *fmt, ...)
{
    if (r->n >= 7) return;
    FECommsCell *c = &r->cell[r->n++];
    c->pos = pos;
    c->align = align;
    c->box = box;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->text, sizeof c->text, fmt, ap);
    va_end(ap);
}

bool FEComms_Row(int list, int row, FECommsRow *out)
{
    memset(out, 0, sizeof *out);
    if (!live()) {
        if (list == 3) cell(out, "ID_NAMEPOS", 0, 0, "%s", FEStrings_Get(0x11f));
        return list == 3;
    }
    lock();
    bool ok = false;
    if (list == 0) {   /* FEGameListAccessor_v15 0x4b9c10 */
        int i = 0;
        NETLST_EACH(&g_net->found, x) {
            NetGameRec *g = x->p;
            if (i++ != row) continue;
            cell(out, "ID_GAMEPOS", 2, 0, "%d", row + 1);
            cell(out, "ID_GAMEPOS", 0, 0, "%s", g->name);
            if (pos_of("Comms.GameMode") == 1) {
                cell(out, "ID_PLAYERSPOS", 2, 0, "%d", g->players);
                if (0 <= Net_GamePing(g)) cell(out, "ID_TRACKPOS", 2, 0, "%d", Net_GamePing(g));
            } else {
                cell(out, "ID_TRACKPOS", 2, 0, "%d", g->players);
            }
            cell(out, "ID_TRACKPOS", 0, 0, "%s", FEStrings_Get(g->info.s[1] + 0x28));
            cell(out, "ID_MODEPOS", 0, 0, "%s", FEStrings_Get((int)exe_u32(0x5b9674 + 4u * (g->info.b[1] & 1))));
            cell(out, "ID_VERPOS", 0, 0, "%s", g->version);
            ok = true;
        }
    } else if (list == 1) {   /* FECommInfoAccessor_v15 0x4b9360 */
        int i = 0;
        NETLST_EACH(&g_net->ports, x) {
            NetGameRec *g = x->p;
            int ping = Net_GamePing(g);
            if (ping < 0) continue;
            if (i++ != row) continue;
            /* (the name past its fifth '.', the whole name without) */
            const char *s = g->name;
            for (int k = 0; k < 5 && s; k++) {
                s = strchr(s, '.');
                if (s) s++;
            }
            cell(out, "ID_SEVERPOS", 2, 0, "%d", row + 1);
            cell(out, "ID_SEVERPOS", 0, 0, "%s", s ? s : g->name);
            cell(out, "ID_GAMEPOS", 2, 0, "%d", g->info.i[0] >> 0x10);
            cell(out, "ID_PLAYERPOS", 2, 0, "%d", g->players);
            cell(out, "ID_PINGPOS", 2, 0, "%d", ping < 10000 ? ping : 9999);
            ok = true;
        }
    } else if (list == 2) {   /* FELobbyPlayerListAccessor_v15 0x4baad0 */
        NetPlayer *p = player_at(row);
        if (p) {
            int ping = Net_PlayerPing(p);
            cell(out, "ID_SMALLPLAYERNAMETHINGPOS", 2, 0, "%d", row + 1);
            cell(out, "ID_SMALLPLAYERNAMETHINGPOS", 0, 0, "%s", p->name);
            if (0 <= ping) cell(out, "ID_SMALLPINGTHINGPOS", 2, 0, "%d", ping < 10000 ? ping : 9999);
            ok = true;
        }
    } else {   /* FEPlayerListAccessor_v15 0x4bba90: only seated, the screen on */
        if (seated() && idle()) {
            NetPlayer *p = seat_at(row);
            cell(out, "ID_NAMEPOS", 2, 0, "%d", row + 1);
            cell(out, "ID_NAMEPOS", 0, 0, "%s", p ? p->name : FEStrings_Get(0x11f));
            if (p) {
                int ping = Net_PlayerPing(p);
                if (0 <= ping) cell(out, "ID_CARPOS", 2, 0, "%d", ping < 10000 ? ping : 9999);
                int n = p->number;
                cell(out, "ID_CARPOS", 0, (uint32_t)desc_i(n, D_COLOUR) | 0x1000000u, "%s", (const char *)g_cardesc[n] + D_CARNAME);
                bool racing = (g_net->racing >> (n & 7) & 1) != 0;
                cell(out, "ID_STATUSPOS", 0, 0, "%s", FEStrings_Get(racing ? 0x11b : desc_i(n, D_READY) ? 0x118 : 0x11c));
            }
            ok = true;
        }
    }
    unlock();
    return ok;
}

const char *FEComms_Data(const char *arg)   /* FEMultiplayerDataAccessor_v5 0x50b490 */
{
    if (!strcmp(arg, "TRACKNAME")) return FEStrings_Get(FEData_TrackLocaleOption() + 0x28);
    if (!strcmp(arg, "TRACKDIRECTION")) return FEStrings_Get((int)exe_u32(0x5b963c + 4u * (uint32_t)FEData_TrackOption(1)));
    if (!strcmp(arg, "TRACKTRAFFIC")) return FEStrings_Get((int)exe_u32(0x5b9628 + 4u * (uint32_t)FEData_TrackOption(2)));
    if (!strcmp(arg, "TRACKLAPS"))
        return FEStrings_Get(FEData_TrackLooped(3) ? (int)exe_u32(0x5b9650 + 4u * (uint32_t)FEData_TrackOption(3)) : 299);
    if (!strcmp(arg, "MYIPADDRESS")) return g_net_server ? g_net_server->address : "";   /* (0x628c74 + 0x50) */
    if (!strcmp(arg, "GAMENAME")) {
        int m = pos_of("Comms.GameMode");
        if (m == 2 || m == 3) return "";
        return seated() ? g_net->joined : "";
    }
    if (!strcmp(arg, "POSITIONALUPDATES")) return FEStrings_Get((int)exe_u32(0x5b9674 + 4u * (uint32_t)(g_race_network & 1)));
    return exe_str(0x5d73bc);   /* "NOT FOUND" */
}

/* ---------------------------------------------------------------- the chat */

enum { CHAT_LINES = 100 };
static char *s_lines[CHAT_LINES];   /* 0x65b6a8 */
static int s_nlines;
typedef struct { char text[0xf8]; int32_t from, to; } ChatMsg;
static ChatMsg s_inbox[32];          /* 0x65b6cc: the session's (its thread's) */
static int s_ninbox;

static void chat_in(const char *text, int32_t from, int32_t to)   /* 0x4c28c0 */
{
    if (s_ninbox == 32) return;
    ChatMsg *m = &s_inbox[s_ninbox++];
    snprintf(m->text, sizeof m->text, "%s", text);
    m->from = from;
    m->to = to;
}

static const char *name_of(int number)
{
    int row = 0, i = 0;
    if (live())
        NETLST_EACH(&g_net->plist, x) {
            if (((NetPlayer *)x->p)->number == number) row = i;
            i++;
        }
    return FEComms_Text(3, row);
}

static void chat_add(const ChatMsg *m, int width)   /* 0x4c20d0 */
{
    char line[0x200];
    char from[0x48];
    snprintf(from, sizeof from, "%s", name_of(m->from));
    if (m->to == -1) {
        snprintf(line, sizeof line, "%s : %s", from, m->text);   /* ("%s : %s" 0x5d7e24) */
    } else if (m->to == (seated() ? g_net->me : -2)) {
        snprintf(line, sizeof line, FEStrings_Get(0x244), from, m->text);
    } else {
        char to[0x48];
        snprintf(to, sizeof to, "%s", name_of(m->to));
        snprintf(line, sizeof line, FEStrings_Get(0x245), from, to, m->text);
    }
    /* (the text broken into the box's width less 8, Univers8b, 0x522900) */
    char *rows[16];
    int n = FEApp_WrapText(line, width - 8, rows, 16);
    for (int i = 0; i < n; i++) {
        if (s_nlines == CHAT_LINES) {
            free(s_lines[0]);
            memmove(s_lines, s_lines + 1, sizeof *s_lines * (CHAT_LINES - 1));
            s_nlines--;
        }
        s_lines[s_nlines++] = rows[i];
    }
}

void FEComms_ChatTick(int width)   /* FEChatDisplayBox_v21 0x4c2520 (the hook) and _v1 0x4c2990 */
{
    if (g_net) g_net->chat = chat_in;
    lock();
    for (int i = 0; i < s_ninbox; i++) chat_add(&s_inbox[i], width);
    s_ninbox = 0;
    unlock();
}

void FEComms_ChatClear(void)   /* (FEChatDisplayBox_v21 0x4c2520's: the lines cleared) */
{
    for (int i = 0; i < s_nlines; i++) free(s_lines[i]);
    s_nlines = 0;
}

int FEComms_ChatLines(void) { return s_nlines; }
const char *FEComms_ChatLine(int i) { return 0 <= i && i < s_nlines ? s_lines[i] : ""; }

void FEComms_ChatSend(const char *text, int to) { Net_Chat(text, to); }
