This folder is reserved for BUDOSTACK utilities.

Utilities are programs that use paging for their output by default. Commands
listed in `utilities/nopaging.ini` bypass paging when run from BUDOSTACK.

Requirements for Utilities:
- Written with plain C with no header files
- #define _POSIX_C_SOURCE 200112L  // Enable POSIX.1-2001 features
