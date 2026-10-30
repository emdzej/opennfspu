/* The original's static initializers: the constructors of global objects that MSVC's startup runs before
   WinMain, from the table at 0x5cb004..0x5cb780 (479 entries, __xc_a..__xc_z). The port runs the ones whose
   modules are ported, in the table's order, at the start of app_init. */
#pragma once

void PORT_StaticInit(void);
