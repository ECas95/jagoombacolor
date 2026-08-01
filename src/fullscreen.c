#include "includes.h"

/*
 * Fullscreen stretch backend for Jagoomba Color.
 *
 * The stock renderer draws a 160x144 GB image in the centre of the GBA screen.
 * This backend composes each emulated scanline from raw GB VRAM/OAM, expands it
 * to 240x160 with nearest-neighbour sampling, and displays it from Mode 4 page 1.
 * Page 1 is used deliberately: the menu font and tilemaps below 0x0600A000 stay
 * intact, allowing the original Mode 0 menu to return without reloading assets.
 */

#define FS_SOURCE_WIDTH       160
#define FS_SOURCE_HEIGHT      144
#define FS_DEST_WIDTH         240
#define FS_DEST_HEIGHT        160
#define FS_FRAME_SIZE         (FS_DEST_WIDTH * FS_DEST_HEIGHT)
#define FS_MODE4_PAGE1        ((void *)0x0600A000)

#define FS_REG16(address)     (*(volatile u16 *)(address))
#define FS_REG_DISPCNT        FS_REG16(0x04000000)
#define FS_REG_DISPSTAT       FS_REG16(0x04000004)
#define FS_REG_BLDCNT         FS_REG16(0x04000050)
#define FS_REG_BLDALPHA       FS_REG16(0x04000052)
#define FS_REG_BLDY           FS_REG16(0x04000054)
#define FS_REG_DMA0CNT_H      FS_REG16(0x040000BA)
#define FS_REG_DMA1CNT_H      FS_REG16(0x040000C6)
#define FS_REG_IE             FS_REG16(0x04000200)

#define FS_IRQ_HBLANK         0x0002
#define FS_IRQ_VCOUNT         0x0004
#define FS_DISPSTAT_HBLANK    0x0010
#define FS_DISPSTAT_VCOUNT    0x0020
#define FS_DISPCNT_MODE4      0x0004
#define FS_DISPCNT_PAGE1      0x0010
#define FS_DISPCNT_BG2        0x0400

#define FS_BG_PALETTE_BASE    ((volatile u16 *)0x05000000)
#define FS_OBJ_PALETTE_BASE   ((volatile u16 *)0x05000200)

extern volatile u8 lcdstate[];
extern volatile u8 g_scanline;
extern void (* volatile _scanlinehook)(void);
extern void fullscreen_scanline_hook(void);

EWRAM_BSS u8 fullscreen_frame[FS_FRAME_SIZE] __attribute__((aligned(4)));
EWRAM_BSS u8 fullscreen_source_line[FS_SOURCE_WIDTH];
EWRAM_BSS u8 fullscreen_bg_color[FS_SOURCE_WIDTH];
EWRAM_BSS u8 fullscreen_bg_priority[FS_SOURCE_WIDTH];
EWRAM_BSS u8 fullscreen_sprite_owner_x[FS_SOURCE_WIDTH];
EWRAM_BSS u8 fullscreen_sprite_owner_index[FS_SOURCE_WIDTH];
EWRAM_BSS u16 fullscreen_saved_bg_palette[128] __attribute__((aligned(4)));

EWRAM_BSS volatile u8 fullscreen_active;
EWRAM_BSS volatile u8 fullscreen_frame_ready;
EWRAM_BSS u8 fullscreen_window_line;

static inline const u8 *fullscreen_vram(void)
{
#if RESIZABLE
    return XGB_vram;
#else
    return XGB_VRAM;
#endif
}

static inline int fullscreen_wanted(void)
{
    /* ui_x reaches 256 only after the ROM/menu transition has finished. */
    return romstart != 0 && ui_x >= 240;
}

static inline u8 fullscreen_read_bg_pixel(
    u8 lcdc,
    int pixel_x,
    int pixel_y,
    int use_window,
    u8 *raw_color,
    u8 *priority)
{
    const u8 *vram = fullscreen_vram();
    u32 map_base;
    u32 map_offset;
    u32 tile_address;
    u8 tile;
    u8 attr = 0;
    int tile_x;
    int tile_y;
    int bit;
    u8 low;
    u8 high;
    u8 color;
    u8 palette;

    if (use_window) {
        map_base = (lcdc & 0x40) ? 0x1C00 : 0x1800;
    } else {
        map_base = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    }

    pixel_x &= 0xFF;
    pixel_y &= 0xFF;
    map_offset = map_base + ((pixel_y >> 3) << 5) + (pixel_x >> 3);
    tile = vram[map_offset];

    if (gbc_mode) {
        attr = vram[0x2000 + map_offset];
    }

    if (lcdc & 0x10) {
        tile_address = ((u32)tile) << 4;
    } else {
        tile_address = 0x1000 + ((s32)(s8)tile << 4);
    }

    if (attr & 0x08) {
        tile_address += 0x2000;
    }

    tile_x = pixel_x & 7;
    tile_y = pixel_y & 7;
    if (attr & 0x20) {
        tile_x = 7 - tile_x;
    }
    if (attr & 0x40) {
        tile_y = 7 - tile_y;
    }

    low = vram[tile_address + (tile_y << 1)];
    high = vram[tile_address + (tile_y << 1) + 1];
    bit = 7 - tile_x;
    color = (u8)(((low >> bit) & 1) | (((high >> bit) & 1) << 1));
    palette = (u8)(attr & 7);

    *raw_color = color;
    *priority = (u8)((attr >> 7) & 1);

    /* BG palettes 0-7 are mirrored by the stock renderer into banks 8-15. */
    return (u8)(0x80 | (palette << 4) | color);
}

static int fullscreen_render_background_line(u8 source_y, u8 lcdc)
{
    int source_x;
    int scroll_x = lcdstate[2];
    int scroll_y = lcdstate[3];
    int window_x = (int)lcdstate[10] - 7;
    int window_y = lcdstate[11];
    int dmg_bg_disabled = (!gbc_mode && !(lcdc & 0x01));
    int lcd_disabled = !(lcdc & 0x80);
    int window_visible =
        !lcd_disabled &&
        !dmg_bg_disabled &&
        (lcdc & 0x20) &&
        source_y >= window_y &&
        window_x < FS_SOURCE_WIDTH;

    for (source_x = 0; source_x < FS_SOURCE_WIDTH; ++source_x) {
        u8 color = 0;
        u8 priority = 0;
        u8 output = 0x80;

        if (!lcd_disabled && !dmg_bg_disabled) {
            int use_window = window_visible && source_x >= window_x;
            int pixel_x;
            int pixel_y;

            if (use_window) {
                pixel_x = source_x - window_x;
                pixel_y = fullscreen_window_line;
            } else {
                pixel_x = scroll_x + source_x;
                pixel_y = scroll_y + source_y;
            }

            output = fullscreen_read_bg_pixel(
                lcdc,
                pixel_x,
                pixel_y,
                use_window,
                &color,
                &priority);
        }

        fullscreen_source_line[source_x] = output;
        fullscreen_bg_color[source_x] = color;
        fullscreen_bg_priority[source_x] = priority;
    }

    return window_visible;
}

static void fullscreen_render_sprites(u8 source_y, u8 lcdc)
{
    const u8 *vram = fullscreen_vram();
    const u8 *oam = _gb_oam_buffer_screen;
    u8 selected[10];
    int selected_count = 0;
    int sprite_height = (lcdc & 0x04) ? 16 : 8;
    int sprite_index;
    int selected_index;

    if (!(lcdc & 0x02) || oam == 0) {
        return;
    }

    memset(fullscreen_sprite_owner_x, 0xFF, FS_SOURCE_WIDTH);
    memset(fullscreen_sprite_owner_index, 0xFF, FS_SOURCE_WIDTH);

    /* The GB PPU selects only the first ten OAM entries touching a scanline. */
    for (sprite_index = 0; sprite_index < 40 && selected_count < 10; ++sprite_index) {
        int raw_y = oam[sprite_index * 4];
        int sprite_y;

        if (raw_y == 0 || raw_y >= 160) {
            continue;
        }

        sprite_y = raw_y - 16;
        if ((int)source_y >= sprite_y && (int)source_y < sprite_y + sprite_height) {
            selected[selected_count++] = (u8)sprite_index;
        }
    }

    /* Draw in reverse selection order so lower OAM indices remain on top. */
    for (selected_index = selected_count - 1; selected_index >= 0; --selected_index) {
        int i = selected[selected_index];
        int raw_y = oam[i * 4];
        int raw_x = oam[i * 4 + 1];
        u8 tile = oam[i * 4 + 2];
        u8 attr = oam[i * 4 + 3];
        int sprite_y = raw_y - 16;
        int sprite_x = raw_x - 8;
        int row = (int)source_y - sprite_y;
        int bank = gbc_mode ? ((attr >> 3) & 1) : 0;
        int palette = gbc_mode ? (attr & 7) : ((attr >> 4) & 1);
        u32 tile_address;
        u8 low;
        u8 high;
        int x;

        if (attr & 0x40) {
            row = sprite_height - 1 - row;
        }

        if (sprite_height == 16) {
            tile &= 0xFE;
            tile = (u8)(tile + (row >> 3));
            row &= 7;
        }

        tile_address = ((u32)tile << 4) + ((u32)bank << 13) + ((u32)row << 1);
        low = vram[tile_address];
        high = vram[tile_address + 1];

        for (x = 0; x < 8; ++x) {
            int destination_x = sprite_x + x;
            int source_pixel_x = (attr & 0x20) ? (7 - x) : x;
            int bit;
            u8 color;
            u8 owner;

            if (destination_x < 0 || destination_x >= FS_SOURCE_WIDTH) {
                continue;
            }

            bit = 7 - source_pixel_x;
            color = (u8)(((low >> bit) & 1) | (((high >> bit) & 1) << 1));
            if (color == 0) {
                continue;
            }

            if (fullscreen_bg_color[destination_x] != 0) {
                if (gbc_mode) {
                    if ((lcdc & 0x01) &&
                        (fullscreen_bg_priority[destination_x] || (attr & 0x80))) {
                        continue;
                    }
                } else if (attr & 0x80) {
                    continue;
                }
            }

            owner = fullscreen_sprite_owner_index[destination_x];
            if (!gbc_mode && owner != 0xFF) {
                u8 owner_x = fullscreen_sprite_owner_x[destination_x];
                if (raw_x > owner_x || (raw_x == owner_x && i > owner)) {
                    continue;
                }
            }

            /* OBJ palettes are copied into BG palette banks 0-7 in fullscreen. */
            fullscreen_source_line[destination_x] = (u8)((palette << 4) | color);
            fullscreen_sprite_owner_x[destination_x] = (u8)raw_x;
            fullscreen_sprite_owner_index[destination_x] = (u8)i;
        }
    }
}

static void fullscreen_stretch_line(u8 source_y)
{
    int destination_y_start = ((int)source_y * 10 + 8) / 9;
    int destination_y_end = (((int)source_y + 1) * 10 + 8) / 9;
    int destination_y;

    for (destination_y = destination_y_start;
         destination_y < destination_y_end && destination_y < FS_DEST_HEIGHT;
         ++destination_y) {
        u8 *destination = fullscreen_frame + destination_y * FS_DEST_WIDTH;
        int source_x;
        int destination_x = 0;

        /* Exact 3:2 expansion: [A,B] becomes [A,A,B]. */
        for (source_x = 0; source_x < FS_SOURCE_WIDTH; source_x += 2) {
            u8 a = fullscreen_source_line[source_x];
            u8 b = fullscreen_source_line[source_x + 1];
            destination[destination_x++] = a;
            destination[destination_x++] = a;
            destination[destination_x++] = b;
        }
    }
}

void fullscreen_scanline_render(void)
{
    u8 source_y;
    u8 lcdc;
    int window_visible;

    if (!fullscreen_active) {
        return;
    }

    source_y = g_scanline;
    if (source_y >= FS_SOURCE_HEIGHT) {
        return;
    }

    if (source_y == 0) {
        fullscreen_window_line = 0;
    }

    lcdc = lcdstate[0];
    window_visible = fullscreen_render_background_line(source_y, lcdc);
    fullscreen_render_sprites(source_y, lcdc);
    fullscreen_stretch_line(source_y);

    if (window_visible) {
        ++fullscreen_window_line;
    }

    if (source_y == FS_SOURCE_HEIGHT - 1) {
        fullscreen_frame_ready = 1;
    }
}

static void fullscreen_copy_obj_palettes(void)
{
    int i;
    for (i = 0; i < 128; ++i) {
        FS_BG_PALETTE_BASE[i] = FS_OBJ_PALETTE_BASE[i];
    }
}

static void fullscreen_enter(void)
{
    int i;

    for (i = 0; i < 128; ++i) {
        fullscreen_saved_bg_palette[i] = FS_BG_PALETTE_BASE[i];
    }

    memset(fullscreen_frame, 0x80, sizeof(fullscreen_frame));
    fullscreen_window_line = 0;
    fullscreen_frame_ready = 1;
    fullscreen_active = 1;
    _scanlinehook = fullscreen_scanline_hook;
}

static void fullscreen_exit(void)
{
    int i;

    _scanlinehook = default_scanlinehook;
    fullscreen_active = 0;
    fullscreen_frame_ready = 0;
    fullscreen_window_line = 0;

    for (i = 0; i < 128; ++i) {
        FS_BG_PALETTE_BASE[i] = fullscreen_saved_bg_palette[i];
    }

    /* Restore the stock VCOUNT-driven Mode 0 renderer before it runs. */
    FS_REG_IE |= FS_IRQ_VCOUNT;
    FS_REG_DISPSTAT |= FS_DISPSTAT_VCOUNT;
    FS_REG_DISPCNT = 0;
}

void fullscreen_vblank_pre(void)
{
    if (fullscreen_active && !fullscreen_wanted()) {
        fullscreen_exit();
    }
}

void fullscreen_vblank_post(void)
{
    if (!fullscreen_wanted()) {
        return;
    }

    if (!fullscreen_active) {
        fullscreen_enter();
    }

    /* loadcart() restores the stock hook, so enforce ours every frame. */
    if (_scanlinehook != fullscreen_scanline_hook) {
        _scanlinehook = fullscreen_scanline_hook;
    }

    /* The stock renderer programs Mode 0 through VCOUNT/DMA.  Stop that output
       after it has completed its normal bookkeeping for this VBlank. */
    FS_REG_DMA0CNT_H = 0;
    FS_REG_DMA1CNT_H = 0;
    FS_REG_IE &= (u16)~(FS_IRQ_HBLANK | FS_IRQ_VCOUNT);
    FS_REG_DISPSTAT &= (u16)~(FS_DISPSTAT_HBLANK | FS_DISPSTAT_VCOUNT);

    fullscreen_copy_obj_palettes();

    if (fullscreen_frame_ready) {
        dmaCopy(fullscreen_frame, FS_MODE4_PAGE1, FS_FRAME_SIZE);
        fullscreen_frame_ready = 0;
    }

    FS_REG_BLDCNT = 0;
    FS_REG_BLDALPHA = 0;
    FS_REG_BLDY = 0;
    FS_REG_DISPCNT = FS_DISPCNT_MODE4 | FS_DISPCNT_PAGE1 | FS_DISPCNT_BG2;
}
