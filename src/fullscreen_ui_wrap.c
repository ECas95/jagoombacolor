#include "includes.h"

/*
 * Explicit gameplay/menu gate for the fullscreen backend.
 *
 * ui_border_visible is owned by the stock renderer and is rewritten while a
 * game is running, so it cannot be used as persistent fullscreen state.  Keep
 * a dedicated flag and refresh the compatibility bit immediately before the
 * fullscreen VBlank checks.
 */

extern void __real_make_ui_visible(void);
extern void __real_make_ui_invisible(void);
extern void __real_ui(void);

extern int main_ui_selection;

EWRAM_BSS volatile u8 fullscreen_gameplay_allowed;

static inline void fullscreen_block_gameplay(void)
{
    fullscreen_gameplay_allowed = 0;
    ui_border_visible |= 1;
}

static inline void fullscreen_allow_gameplay(void)
{
    /* ui() uses -1 to represent gameplay/no active internal menu.  The initial
       ROM frontend did not initialise this value, so do it at the exact point
       where the frontend finishes hiding itself. */
    main_ui_selection = -1;
    fullscreen_gameplay_allowed = 1;
    ui_border_visible &= (u8)~1;
}

void fullscreen_gate_refresh(void)
{
    if (fullscreen_gameplay_allowed &&
        main_ui_selection < 0 &&
        romstart != 0) {
        ui_border_visible &= (u8)~1;
    } else {
        ui_border_visible |= 1;
    }
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
