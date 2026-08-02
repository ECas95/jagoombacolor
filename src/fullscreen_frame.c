#include "includes.h"

/*
 * Deterministic full-frame entry point.
 *
 * The first fullscreen prototypes depended on _scanlinehook receiving every
 * emulated LCD line.  On the current Jagoomba timing path that did not yield a
 * complete frame before the stock renderer regained control.  This function is
 * called from the public newframe_vblank wrapper once per emulated GB frame and
 * invokes the existing compositor for all 144 visible lines directly.
 */

extern volatile u8 g_scanline;
extern volatile u8 fullscreen_active;
extern volatile u8 fullscreen_frame_ready;
extern volatile u8 fullscreen_gameplay_allowed;
extern u8 fullscreen_window_line;
extern u8 fullscreen_render_phase;
extern u8 fullscreen_render_this_frame;

extern void fullscreen_scanline_render(void);

EWRAM_BSS u8 fullscreen_compose_phase;

void fullscreen_compose_frame_now(void)
{
    u8 saved_scanline;
    int y;

    if (!fullscreen_gameplay_allowed || !fullscreen_active || romstart == 0) {
        fullscreen_compose_phase = 0;
        return;
    }

    /* Compose at 30 fps. GB/GBC timing and input still run at the original rate;
       only the software-generated fullscreen image is refreshed every second
       emulated frame. This keeps the first validation build within the ARM7
       budget while preserving a deterministic rendering path. */
    fullscreen_compose_phase ^= 1;
    if (!fullscreen_compose_phase) {
        return;
    }

    saved_scanline = g_scanline;
    fullscreen_window_line = 0;
    fullscreen_render_phase = 0;
    fullscreen_render_this_frame = 1;
    fullscreen_frame_ready = 0;

    for (y = 0; y < 144; ++y) {
        g_scanline = (u8)y;
        fullscreen_scanline_render();
    }

    g_scanline = saved_scanline;

    /* fullscreen_scanline_render marks the frame ready on line 143. Keep this
       assignment explicit so a future compositor refactor cannot silently stop
       presentation at the hardware VBlank boundary. */
    fullscreen_frame_ready = 1;
}
