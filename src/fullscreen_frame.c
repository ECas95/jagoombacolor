#include "includes.h"

/*
 * Deterministic full-frame entry point.
 *
 * The fullscreen compositor is intentionally decoupled from the per-scanline
 * hook.  It runs after Jagoomba's original frame-boundary bookkeeping and
 * rebuilds the 160x144 logical frame from emulated GB/GBC VRAM and OAM.
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

    /* V10 prioritises playable emulation over display refresh.  Rebuilding the
       complete frame in C is currently the dominant CPU cost, so compose once
       every four emulated frames (about 15 fps) while the emulator, input and
       audio continue at their original timing. */
    fullscreen_compose_phase = (u8)((fullscreen_compose_phase + 1) & 3);
    if (fullscreen_compose_phase != 1) {
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
    fullscreen_frame_ready = 1;
}
