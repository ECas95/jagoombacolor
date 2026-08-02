#include "includes.h"

/*
 * Linker wrappers that expose the actual gameplay/menu transition to the
 * fullscreen backend.  ui_border_visible is also used for the stock border
 * renderer, so its value cannot be interpreted as a menu-only flag.  These
 * wrappers reserve bit 0 as the fullscreen gate at the exact transition points
 * used by the standalone ROM frontend and the in-game L+R menu.
 */

extern void __real_make_ui_visible(void);
extern void __real_make_ui_invisible(void);
extern void __real_ui(void);

static inline void fullscreen_block_gameplay(void)
{
    ui_border_visible |= 1;
}

static inline void fullscreen_allow_gameplay(void)
{
    ui_border_visible &= (u8)~1;
}

void __wrap_make_ui_visible(void)
{
    fullscreen_block_gameplay();
    __real_make_ui_visible();
    fullscreen_block_gameplay();
}

void __wrap_make_ui_invisible(void)
{
    __real_make_ui_invisible();
    fullscreen_allow_gameplay();
}

void __wrap_ui(void)
{
    fullscreen_block_gameplay();
    __real_ui();
    fullscreen_allow_gameplay();
}
