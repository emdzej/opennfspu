/* The network race's car states (the state mode, g_race_network 1: Net_Tick sends the local car's
   every 8 ticks, 0xbe; the others' arrive here). */
#include "game/net.h"
#include "pending.h"
#include <string.h>

void NetCar_GetState(uint8_t out[0x24], const void *car)   /* 0x49ff00 */
{
    (void)car;
    memset(out, 0, 0x24);
    PENDING_NOTE(0x49ff00, "a car's state for the network (position, orientation 0x49fc50, velocity)");
}

void NetCar_SetState(void *car, const uint8_t in[0x24])   /* 0x4a0bd0 */
{
    (void)car;
    (void)in;
    PENDING_NOTE(0x4a0bd0, "a network car's state into the car (0x4a01c0, the correction 0x4a0820)");
}
