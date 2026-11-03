/* The front end's network game screens ([frontend] 0x508630 .. 0x50b4c0): their callback lists
   (FECommsErrorHandler, vtable 0x5b8edc, the base of FECommsCreateScreen 0x5b8dfc, FECommsGamesScreen
   0x5b8fbc and its LAN 0x5b9180 and web 0x5b9504 kinds, FECommsJoinScreen 0x5b90a0, FECommsModemScreen
   0x5b9264, FECommsPeerToPeerIP 0x5b9344, FECommsServerScreen 0x5b9424), the session's lists for the
   accessors (FEGameListAccessor, FECommInfoAccessor, FELobbyPlayerListAccessor, FEPlayerListAccessor,
   FEMultiplayerDataAccessor) and the chat boxes (FEChatDisplayBox 0x4c2020, FEChatInputBox 0x4c2e30).

   The flow: the game setup's multiplayer (Game.GameType 3) opens the protocol screen, whose buttons set
   Comms.GameMode (0 the Internet, 1 the LAN, 2 a modem, 3 an address) on their way to the mode's screen:
   accountlogin (then serverlist and udpip), local, modem, PeerToPeerIP. CREATE hosts a game
   (Net_HostGame with the game's description) and goes to creategame; JOIN or CONNECT joins one and goes
   to joingame. There the players choose their cars (their option records to everyone, 0x226 0x141 0x225,
   READY 0x255), the host the track (0xc, 0xd, 0xf, 0x14); the host's PLAYGAME starts the race for all
   (0x50c550: the start, a barrier, the cars checked, a barrier). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "front/fert.h"

/* The top screen's callback list made (on its way on) and let go (on its way off). */
void FEComms_Open(const char *cbl);
void FEComms_Close(const char *cbl);
/* The screen's update (their v1s, each after FECommsErrorHandler_v1 0x508c10, which every screen's
   main list runs too); w the top screen. */
void FEComms_Update(FertControl *w, const char *cbl);
/* A callback of theirs (SETMODE*, SIGNON, REGISTERUSER, CREATE, JOIN, REFRESH, CONNECT, ENTER, DIAL,
   EJECTPLAYER, READY): true when it was one. */
bool FEComms_Callback(const char *cbl, const char *cb);
/* The back button on their screens (slot 45: the create and join screens leave the game first); true when
   handled. */
bool FEComms_Back(const char *cbl);
/* PLAYGAME in a network game (FECallBackList_v53 0x50c550): the start sent, everyone waited for, the cars
   checked; true to start the race. */
bool FEComms_Play(void);
/* A screen going on in a game (FEApplication_GotoScreen 0x4d1b50's first): my READY off. */
void FEComms_Leaving(void);
/* The create screen's play button (FECommsCreateScreen_v41 0x508870): seated, every player's READY on
   and no race running (+0xe5 none). */
bool FEComms_CanPlay(void);
/* The error handler on (FEApplication_Run 0x4d2e70's 0x508b50) or off (0x508b60, its dialogs gone). */
void FEComms_Errors(bool on);
/* A game in the session (0x4d2e70's test): the front end's start goes back to its screens. */
bool FEComms_InGame(void);
/* The screens (their layouts) to go back to after a network race (0x4d2e70): the mode's, then creategame
   or joingame; their count. */
int FEComms_ReturnStack(const char *names[4]);

/* The lists: 0 Comms.GamesList, 1 Comms.ServerList, 2 Comms.LobbyPlayerList, 3 Comms.PlayerList. */
int FEComms_Count(int list);
const char *FEComms_Text(int list, int row);
/* A row's cells as the lists draw them (their slot 15): at a control's x (its name), right of it (align
   0, +4) or up to it (2, -4), a text, or a colour box 15 x 10 (box != 0) before the text. */
typedef struct {
    const char *pos;
    int align;
    char text[72];
    uint32_t box;
} FECommsCell;
typedef struct {
    int n;
    FECommsCell cell[7];
} FECommsRow;
bool FEComms_Row(int list, int row, FECommsRow *out);
/* The player's number of a PlayerList row (0x4bb720), -1 for an open seat. */
int FEComms_RowNumber(int list, int row);
/* Comms.Data's texts by argument (FEMultiplayerDataAccessor_v5 0x50b490). */
const char *FEComms_Data(const char *arg);
/* The chat box's lines (FEChatDisplayBox): the messages taken (0x4c28c0 from the session, its v1 into
   lines of the box's width), their count, a line. */
void FEComms_ChatTick(int width);
void FEComms_ChatClear(void);
int FEComms_ChatLines(void);
const char *FEComms_ChatLine(int i);
/* The chat input's Enter (FEChatInputBox_v4 0x4c2ee0): the text to the player chosen (or all). */
void FEComms_ChatSend(const char *text, int to);
