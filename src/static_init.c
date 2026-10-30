/* The static initializers (see static_init.h). Entries not listed belong to modules that aren't ported
   yet; add them here, in table order, as their modules are. */
#include "static_init.h"
#include "real/texture.h"

void PORT_StaticInit(void)
{
    TEXTURE_initstates();   /* 0x5cb754 -> 0x535710: the texture stage-state cache's state ids and values */
}
