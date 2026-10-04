#ifndef BUDOWIN_SDK_H
#define BUDOWIN_SDK_H

/*
 * Public SDK shim.  The ABI is still under active development.
 * Native applications should include this file rather than private
 * BUDOWIN implementation headers.
 */

#include "../src/bwa.h"

#define BUDOWIN_SDK_ABI_MAJOR BWA_ABI_MAJOR
#define BUDOWIN_SDK_ABI_MINOR BWA_ABI_MINOR

/*
 * Legacy fixed palette/geometry aliases.  New applications should prefer
 * host->get_system_color() and host->get_system_metric(); those APIs keep
 * applications independent from the active BUDOWIN theme.
 */
#define BUDO_COLOR_DESKTOP       0U
#define BUDO_COLOR_TEXT          1U
#define BUDO_COLOR_SHADOW        2U
#define BUDO_COLOR_MIDGRAY       3U
#define BUDO_COLOR_WHITE         4U
#define BUDO_COLOR_CHROME        5U
#define BUDO_COLOR_TITLE         6U
#define BUDO_COLOR_TITLE_TEXT    4U

#define BUDO_WINDOW_BORDER       4
#define BUDO_TITLE_HEIGHT        18
#define BUDO_TITLE_TEXT_Y        6
#define BUDO_TITLE_BUTTON_W      14
#define BUDO_TITLE_BUTTON_H      14
#define BUDO_DESKTOP_ICON_W      32
#define BUDO_DESKTOP_ICON_H      32
#define BUDO_BUILTIN_ICON_W      24
#define BUDO_BUILTIN_ICON_H      17

/* Semantic aliases for ABI 1.4+ callers. */
#define BUDO_COLOR_ROLE_DESKTOP      BUDO_SYS_COLOR_DESKTOP
#define BUDO_COLOR_ROLE_TEXT         BUDO_SYS_COLOR_TEXT
#define BUDO_COLOR_ROLE_SHADOW       BUDO_SYS_COLOR_SHADOW
#define BUDO_COLOR_ROLE_MIDGRAY      BUDO_SYS_COLOR_MIDGRAY
#define BUDO_COLOR_ROLE_HIGHLIGHT    BUDO_SYS_COLOR_HIGHLIGHT
#define BUDO_COLOR_ROLE_FACE         BUDO_SYS_COLOR_FACE
#define BUDO_COLOR_ROLE_TITLE        BUDO_SYS_COLOR_TITLE_ACTIVE
#define BUDO_COLOR_ROLE_TITLE_TEXT   BUDO_SYS_COLOR_TITLE_TEXT

#endif
