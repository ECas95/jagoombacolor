#include "includes.h"

/*
 * Fullscreen stretch backend for Jagoomba Color.
 *
 * The stock renderer uses a centred 160x144 Mode 0 presentation. This backend
 * composes a native 160x144 indexed frame from GB VRAM/OAM, uploads it to the
 * hidden Mode 4 page, and lets the GBA affine unit stretch it to 240x160.
 */

#define FS_SOURCE_WIDTH       160
#define FS_SOURCE_HEIGHT      144
#define FS_DEST_WIDTH         240
#define FS_DEST_HEIGHT        160
#define FS_FRAME_SIZE         (FS_SOURCE_WIDTH * FS_SOURCE_HEIGHT)
#define FS_MODE4_PAGE1        ((volatile u8 *)0x0600A000)

#define FS_REG16(address)     (*(volatile u16 *)(address))
#define FS_REG32(address)     (*(volatile u32 *)(address))
#define FS_REG_DISPCNT        FS_REG16(0x04000000)
#define FS_REG_DISPSTAT       FS_REG16(0x04000004)
#define FS_REG_BG2CNT         FS_REG16(0x0400000C)
#define FS_REG_BG2PA          FS_REG16(0x04000020)
#define FS_REG_BG2PB          FS_REG16(0x04000022)
#define FS_REG_BG2PC          FS_REG16(0x04000024)
#define FS_REG_BG2PD          FS_REG16(0x04000026)
#define FS_REG_BG2X           FS_REG32(0x04000028)
#define FS_REG_BG2Y           FS_REG32(0x0400002C)
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

#define FS_AFFINE_PA          171
#define FS_AFFINE_PD          231

#define FS_BG_PALETTE_BASE    ((volatile u16 *)0x05000000)
#define FS_OBJ_PALETTE_BASE   ((volatile u16 *)0x05000200)

extern volatile u8 lcdstate[];
extern volatile u8 g_scanline;
extern void (* volatile _scanlinehook)(void);
extern void fullscreen_scanline_hook(void);
extern void default_scanlinehook(void);

EWRAM_BSS u8 fullscreen_frame[FS_FRAME_SIZE] __attribute__((aligned(4)));
EWRAM_BSS u8 fullscreen_bg_color[FS_SOURCE_WIDTH];
EWRAM_BSS u8 fullscreen_bg_priority[FS_SOURCE_WIDTH];
EWRAM_BSS u16 fullscreen_saved_bg_palette[128] __attribute__((aligned(4)));

EWRAM_BSS volatile u8 fullscreen_active;
EWRAM_BSS volatile u8 fullscreen_visible;
EWRAM_BSS volatile u8 fullscreen_frame_ready;
EWRAM_BSS u8 fullscreen_window_line;
EWRAM_BSS u8 fullscreen_render_phase;
EWRAM_BSS u8 fullscreen_render_this_frame;

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
    return romstart != 0 && !(ui_border_visible & 1);
}

static void fullscreen_fill_background_line(u8 *destination)
{
    int x;
    for (x = 0; x < FS_SOURCE_WIDTH; ++x) {
        destination[x] = 0x80;
        fullscreen_bg_color[x] = 0;
        fullscreen_bg_priority[x] = 0;
    }
}

static void fullscreen_render_bg_span(
    u8 *destination,
    int destination_x,
    int count,
    int pixel_x,
    int pixel_y,
    u8 lcdc,
    int use_window)
{
    const u8 *vram = fullscreen_vram();
    u32 map_base;
    int map_y = pixel_y & 0xFF;

    if (use_window) {
        map_base = (lcdc & 0x40) ? 0x1C00 : 0x1800;
    } else {
        map_base = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    }

    while (count > 0) {
        int map_x = pixel_x & 0xFF;
        int tile_pixel_x = map_x & 7;
        int tile_pixel_y = map_y & 7;
        int run = 8 - tile_pixel_x;
        u32 map_offset;
        u8 tile;
        u8 attr = 0;
        int tile_address;
        int tile_row;
        u8 low;
        u8 high;
        u8 palette_base;
        u8 priority;
        int i;

        if (run > count) {
            run = count;
        }

        map_offset = map_base + ((map_y >> 3) << 5) + (map_x >> 3);
        tile = vram[map_offset];
        if (gbc_mode) {
            attr = vram[0x2000 + map_offset];
        }

        if (lcdc & 0x10) {
            tile_address = ((int)tile) << 4;
        } else {
            tile_address = 0x1000 + (((int)(s8)tile) << 4);
        }
        if (attr & 0x08) {
            tile_address += 0x2000;
        }

        tile_row = tile_pixel_y;
        if (attr & 0x40) {
            tile_row = 7 - tile_row;
        }

        low = vram[tile_address + (tile_row << 1)];
        high = vram[tile_address + (tile_row << 1) + 1];
        palette_base = (u8)(0x80 | ((attr & 7) << 4));
        priority = (u8)((attr >> 7) & 1);

        for (i = 0; i < run; ++i) {
            int tile_x = tile_pixel_x + i;
            int bit;
            u8 color;

            if (attr & 0x20) {
                tile_x = 7 - tile_x;
            }
            bit = 7 - tile_x;
            color = (u8)(((low >> bit) & 1) | (((high >> bit) & 1) << 1));

            destination[destination_x + i] = (u8)(palette_base | color);
            fullscreen_bg_color[destination_x + i] = color;
            fullscreen_bg_priority[destination_x + i] = priority;
        }

        destination_x += run;
        pixel_x = (pixel_x + run) & 0xFF;
        count -= run;
    }
}

static int fullscreen_render_background_line(u8 *destination, u8 source_y, u8 lcdc)
{
    int scroll_x = lcdstate[2];
    int scroll_y = lcdstate[3];
    int window_x = (int)lcdstate[10] - 7;
    int window_y = lcdstate[11];
    int lcd_disabled = !(lcdc & 0x80);
    int dmg_bg_disabled = (!gbc_mode && !(lcdc & 0x01));
    int window_visible;

    if (lcd_disabled || dmg_bg_disabled) {
        fullscreen_fill_background_line(destination);
    } else {
        fullscreen_render_bg_span(
            destination,
            0,
            FS_SOURCE_WIDTH,
            scroll_x,
            scroll_y + source_y,
            lcdc,
            0);
    }

    window_visible =
        !lcd_disabled &&
        !dmg_bg_disabled &&
        (lcdc & 0x20) &&
        source_y >= window_y &&
        window_x < FS_SOURCE_WIDTH;

    if (window_visible) {
        int start_x = window_x;
        int window_pixel_x = 0;

        if (start_x < 0) {
            window_pixel_x = -start_x;
            start_x = 0;
        }

        if (start_x < FS_SOURCE_WIDTH) {
            fullscreen_render_bg_span(
                destination,
                start_x,
                FS_SOURCE_WIDTH - start_x,
                window_pixel_x,
                fullscreen_window_line,
                lcdc,
                1);
        }
    }

    return window_visible;
}

static int fullscreen_sprite_is_lower_priority(
    const u8 *oam,
    int first,
    int second)
{
    int first_x = oam[first * 4 + 1];
    int second_x = oam[second * 4 + 1];

    if (first_x != second_x) {
        return first_x > second_x;
    }
    return first > second;
}

static void fullscreen_sort_dmg_sprites(const u8 *oam, u8 *selected, int count)
{
    int i;
    int j;

    for (i = 0; i < count - 1; ++i) {
        for (j = i + 1; j < count; ++j) {
            if (!fullscreen_sprite_is_lower_priority(oam, selected[i], selected[j])) {
                u8 temp = selected[i];
                selected[i] = selected[j];
                selected[j] = temp;
            }
        }
    }
}

static void fullscreen_draw_sprite(
    u8 *destination,
    const u8 *vram,
    const u8 *oam,
    int sprite_index,
    int source_y,
    int sprite_height,
    u8 lcdc)
{
    int raw_y = oam[sprite_index * 4];
    int raw_x = oam[sprite_index * 4 + 1];
    u8 tile = oam[sprite_index * 4 + 2];
    u8 attr = oam[sprite_index * 4 + 3];
    int sprite_y = raw_y - 16;
    int sprite_x = raw_x - 8;
    int row = source_y - sprite_y;
    int bank = gbc_mode ? ((attr >> 3) & 1) : 0;
    int palette = gbc_mode ? (attr & 7) : ((attr >> 4) & 1);
    int tile_address;
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

    tile_address = (((int)tile) << 4) + (bank << 13) + (row << 1);
    low = vram[tile_address];
    high = vram[tile_address + 1];

    for (x = 0; x < 8; ++x) {
        int destination_x = sprite_x + x;
        int tile_x = (attr & 0x20) ? (7 - x) : x;
        int bit;
        u8 color;

        if (destination_x < 0 || destination_x >= FS_SOURCE_WIDTH) {
            continue;
        }

        bit = 7 - tile_x;
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

        destination[destination_x] = (u8)((palette << 4) | color);
    }
}

static void fullscreen_render_sprites(
    u8 *destination,
    u8 source_y,
    u8 lcdc)
{
    const u8 *vram = fullscreen_vram();
    const u8 *oam = _gb_oam_buffer_screen;
    u8 selected[10];
    int selected_count = 0;
    int sprite_height = (lcdc & 0x04) ? 16 : 8;
    int sprite_index;

    if (!(lcdc & 0x02) || oam == 0) {
        return;
    }

    for (sprite_index = 0; sprite_index < 40 && selected_count < 10; ++sprite_index) {
        int raw_y = oam[sprite_index * 4];
        int sprite_y;

        if (raw_y == 0 || raw_y >= 160) {
            continue;
        }

        sprite_y = raw_y - 16;
        if ((int)source_y >= sprite_y &&
            (int)source_y < sprite_y + sprite_height) {
            selected[selected_count++] = (u8)sprite_index;
        }
    }

    if (gbc_mode) {
        int i;
        for (i = selected_count - 1; i >= 0; --i) {
            fullscreen_draw_sprite(
                destination,
                vram,
                oam,
                selected[i],
                source_y,
                sprite_height,
                lcdc);
        }
    } else {
        int i;
        fullscreen_sort_dmg_sprites(oam, selected, selected_count);
        for (i = 0; i < selected_count; ++i) {
            fullscreen_draw_sprite(
                destination,
                vram,
                oam,
                selected[i],
                source_y,
                sprite_height,
                lcdc);
        }
    }
}

void fullscreen_scanline_render(void)
{
    u8 source_y;
    u8 lcdc;
    u8 *destination;
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
        fullscreen_render_this_frame = (fullscreen_render_phase == 0);
        fullscreen_render_phase ^= 1;
    }

    if (!fullscreen_render_this_frame) {
        return;
    }

    destination = fullscreen_frame + ((int)source_y * FS_SOURCE_WIDTH);
    lcdc = lcdstate[0];

    window_visible = fullscreen_render_background_line(destination, source_y, lcdc);
    fullscreen_render_sprites(destination, source_y, lcdc);

    if (window_visible) {
        ++fullscreen_window_line;
    }

    if (source_y == FS_SOURCE_HEIGHT - 1) {
        fullscreen_frame_ready = 1;
    }
}

static void fullscreen_upload_frame(void)
{
    int y;

    for (y = 0; y < FS_SOURCE_HEIGHT; ++y) {
        const void *source = fullscreen_frame + (y * FS_SOURCE_WIDTH);
        void *destination = (void *)(FS_MODE4_PAGE1 + (y * FS_DEST_WIDTH));
        dmaCopy(source, destination, FS_SOURCE_WIDTH);
    }
}

static void fullscreen_save_bg_palette(void)
{
    int i;
    for (i = 0; i < 128; ++i) {
        fullscreen_saved_bg_palette[i] = FS_BG_PALETTE_BASE[i];
    }
}

static void fullscreen_restore_bg_palette(void)
{
    int i;
    for (i = 0; i < 128; ++i) {
        FS_BG_PALETTE_BASE[i] = fullscreen_saved_bg_palette[i];
    }
}

static void fullscreen_copy_obj_palettes(void)
{
    dmaCopy((const void *)FS_OBJ_PALETTE_BASE, (void *)FS_BG_PALETTE_BASE, 256);
}

static void fullscreen_enter(void)
{
    fullscreen_save_bg_palette();
    fullscreen_window_line = 0;
    fullscreen_render_phase = 0;
    fullscreen_render_this_frame = 0;
    fullscreen_frame_ready = 0;
    fullscreen_visible = 0;
    fullscreen_active = 1;
    _scanlinehook = fullscreen_scanline_hook;
}

static void fullscreen_exit(void)
{
    _scanlinehook = default_scanlinehook;
    fullscreen_active = 0;
    fullscreen_visible = 0;
    fullscreen_frame_ready = 0;
    fullscreen_window_line = 0;
    fullscreen_render_phase = 0;
    fullscreen_render_this_frame = 0;

    fullscreen_restore_bg_palette();

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

    if (_scanlinehook != fullscreen_scanline_hook) {
        _scanlinehook = fullscreen_scanline_hook;
    }

    if (fullscreen_frame_ready) {
        fullscreen_upload_frame();
        fullscreen_frame_ready = 0;
        fullscreen_visible = 1;
    }

    if (!fullscreen_visible) {
        return;
    }

    FS_REG_DMA0CNT_H = 0;
    FS_REG_DMA1CNT_H = 0;
    FS_REG_IE &= (u16)~(FS_IRQ_HBLANK | FS_IRQ_VCOUNT);
    FS_REG_DISPSTAT &= (u16)~(FS_DISPSTAT_HBLANK | FS_DISPSTAT_VCOUNT);

    fullscreen_copy_obj_palettes();

    FS_REG_BLDCNT = 0;
    FS_REG_BLDALPHA = 0;
    FS_REG_BLDY = 0;

    FS_REG_BG2CNT = 0;
    FS_REG_BG2PA = FS_AFFINE_PA;
    FS_REG_BG2PB = 0;
    FS_REG_BG2PC = 0;
    FS_REG_BG2PD = FS_AFFINE_PD;
    FS_REG_BG2X = 0;
    FS_REG_BG2Y = 0;

    FS_REG_DISPCNT = FS_DISPCNT_MODE4 | FS_DISPCNT_PAGE1 | FS_DISPCNT_BG2;
}
