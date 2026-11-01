/* The used car market (0x691b88: a vector of car objects; its section of the save, UsedCarMarket, is
   the market's own cars, 0x4f7030). The player's cars put up for sale join it (0x4f8760) while the
   session lasts: the save keeps only their flags in the car object, and the market's loader (0x4f6d70)
   builds the list from its own cars, so a car left for sale when the game quit stays marked and is no
   longer in the market (the original's behaviour; DONTSELLCAR then finds nothing to take back).
   A car object's market fields: +0x68c the asking price, +0x690 sold (the market's own), +0x691 for
   sale, +0x692 an offer made, +0x694 the offer, +0x6a0 the player's. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* An owned car (the save's PlayerInfo index) put up at a price (0x4f8760). */
void Market_List(int owned, int32_t price);
/* Taken back (0x4f88e0): nothing when it isn't in the market. */
void Market_Unlist(int owned);
/* The player's cars in the market (0x4f8960's count). */
int Market_Listed(void);
/* The market a year on (0x4f7350): the buyers' turn (0x4f7430), the market's sold cars gone, its others
   repriced at their value, new ones in (0x4f7660). */
void Market_Update(int year);
/* The first of the player's cars with an offer (0x4f8b10), its owned index; -1 for none. */
int Market_FirstOffer(void);
/* The offer taken (0x501740): the money in, the car out of the garage and the market. */
void Market_Accept(int owned);
/* The offer turned down (0x4f8cc0). */
void Market_Decline(int owned);
