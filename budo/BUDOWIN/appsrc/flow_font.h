#ifndef BUDOWIN_FLOW_FONT_H
#define BUDOWIN_FLOW_FONT_H
#include "../sdk/font.h"
#include "flow_encoding.h"
static inline const unsigned char *glyph_for(char c)
{
    return budo_glyph_rows(c);
}
#endif
