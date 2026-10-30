/* Calls into code that isn't ported yet (docs/internals/porting.md): a pending function logs its name and
   address once, then returns a neutral value. */
#pragma once
#include "platform.h"

#define PENDING_NOTE(addr, name) do { \
        static int noted_; \
        if (!noted_) { noted_ = 1; plat_log("pending: " name " " #addr); } \
    } while (0)
