#!/usr/bin/env python3
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "src" / "fullscreen.c"
text = source.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Expected one occurrence, found {count}: {old!r}")
    text = text.replace(old, new, 1)


def replace_function(signature: str, replacement: str) -> None:
    global text
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"Could not locate function: {signature}")

    brace_start = text.find("{", start)
    if brace_start < 0:
        raise SystemExit(f"Could not locate opening brace: {signature}")

    depth = 0
    end = None
    for index in range(brace_start, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                break

    if end is None:
        raise SystemExit(f"Could not locate closing brace: {signature}")

    text = text[:start] + replacement + text[end:]


replace_once(
    "#define FS_FRAME_SIZE         (FS_SOURCE_WIDTH * FS_SOURCE_HEIGHT)",
    "#define FS_FRAME_STRIDE       FS_DEST_WIDTH\n"
    "#define FS_FRAME_SIZE         (FS_FRAME_STRIDE * FS_SOURCE_HEIGHT)",
)
replace_once(
    "#define FS_MODE4_PAGE1        ((volatile u8 *)0x0600A000)",
    "#define FS_MODE4_PAGE0        ((volatile u8 *)0x06000000)",
)
replace_once(
    "#define FS_REG_DMA1CNT_H      FS_REG16(0x040000C6)\n"
    "#define FS_REG_IE             FS_REG16(0x04000200)",
    "#define FS_REG_DMA1CNT_H      FS_REG16(0x040000C6)\n"
    "#define FS_REG_IE             FS_REG16(0x04000200)\n"
    "#define FS_REG_IME            FS_REG16(0x04000208)",
)
replace_once(
    "#define FS_DISPCNT_PAGE1      0x0010",
    "#define FS_DISPCNT_PAGE1      0x0000",
)
replace_once(
    "EWRAM_BSS u8 fullscreen_render_this_frame;",
    "EWRAM_BSS u8 fullscreen_render_this_frame;\n"
    "EWRAM_BSS u16 fullscreen_saved_ie;\n"
    "EWRAM_BSS u16 fullscreen_saved_dispstat;\n"
    "EWRAM_BSS u16 fullscreen_saved_dma0cnt_h;",
)
replace_once(
    "destination = fullscreen_frame + ((int)source_y * FS_SOURCE_WIDTH);",
    "destination = fullscreen_frame + ((int)source_y * FS_FRAME_STRIDE);",
)

replace_function(
    "static void fullscreen_upload_frame(void)",
    r'''static void fullscreen_upload_frame(void)
{
    /* The logical frame uses the native 240-byte Mode 4 stride.  Only the first
       160 bytes of each row are composed; the affine unit does not sample the
       unused 80-byte tail.  Upload the complete buffer with one DMA3 transfer. */
    dmaCopy(fullscreen_frame, (void *)FS_MODE4_PAGE0, FS_FRAME_SIZE);
}''',
)

replace_function(
    "static void fullscreen_enter(void)",
    r'''static void fullscreen_enter(void)
{
    fullscreen_save_bg_palette();
    fullscreen_window_line = 0;
    fullscreen_render_phase = 0;
    fullscreen_render_this_frame = 0;
    fullscreen_frame_ready = 0;
    fullscreen_visible = 0;

    /* Capture the stock display state.  It is restored before every original
       VBlank and captured again afterwards.  DMA1/DMA2 are audio channels and
       are never touched by fullscreen code. */
    fullscreen_saved_ie = FS_REG_IE;
    fullscreen_saved_dispstat = FS_REG_DISPSTAT;
    fullscreen_saved_dma0cnt_h = FS_REG_DMA0CNT_H;

    fullscreen_active = 1;
    _scanlinehook = default_scanlinehook;
}''',
)

replace_function(
    "static void fullscreen_exit(void)",
    r'''static void fullscreen_exit(void)
{
    u16 previous_ime = FS_REG_IME;

    FS_REG_IME = 0;

    _scanlinehook = default_scanlinehook;
    fullscreen_active = 0;
    fullscreen_visible = 0;
    fullscreen_frame_ready = 0;
    fullscreen_window_line = 0;
    fullscreen_render_phase = 0;
    fullscreen_render_this_frame = 0;

    fullscreen_restore_bg_palette();
    FS_REG_DISPCNT = 0;

    FS_REG_DMA0CNT_H = fullscreen_saved_dma0cnt_h;
    FS_REG_DISPSTAT = fullscreen_saved_dispstat;
    FS_REG_IE = fullscreen_saved_ie;

    FS_REG_IME = previous_ime;
}''',
)

replace_function(
    "void fullscreen_vblank_pre(void)",
    r'''void fullscreen_vblank_pre(void)
{
    u16 previous_ime;

    if (fullscreen_active && !fullscreen_wanted()) {
        fullscreen_exit();
        return;
    }

    if (!fullscreen_active) {
        return;
    }

    /* The complete stock VBlank path must run every frame.  Restore its last
       DMA0/IRQ state immediately before calling it; V9 skipped this path and
       gameplay/input stopped progressing. */
    previous_ime = FS_REG_IME;
    FS_REG_IME = 0;
    FS_REG_DMA0CNT_H = fullscreen_saved_dma0cnt_h;
    FS_REG_DISPSTAT = fullscreen_saved_dispstat;
    FS_REG_IE = fullscreen_saved_ie;
    FS_REG_IME = previous_ime;
}''',
)

replace_function(
    "void fullscreen_vblank_post(void)",
    r'''void fullscreen_vblank_post(void)
{
    u16 previous_ime;

    if (!fullscreen_wanted()) {
        return;
    }

    if (!fullscreen_active) {
        fullscreen_enter();
    }

    previous_ime = FS_REG_IME;
    FS_REG_IME = 0;

    /* Preserve the display state produced by the original VBlank so it can be
       restored before the next one.  Disable only display-specific DMA0 and
       HBlank/VCount during the visible bitmap frame. */
    fullscreen_saved_dma0cnt_h = FS_REG_DMA0CNT_H;
    fullscreen_saved_dispstat = FS_REG_DISPSTAT;
    fullscreen_saved_ie = FS_REG_IE;

    FS_REG_DMA0CNT_H = 0;
    FS_REG_IE &= (u16)~(FS_IRQ_HBLANK | FS_IRQ_VCOUNT);
    FS_REG_DISPSTAT &= (u16)~(FS_DISPSTAT_HBLANK | FS_DISPSTAT_VCOUNT);

    if (fullscreen_frame_ready) {
        fullscreen_upload_frame();
        fullscreen_frame_ready = 0;
        fullscreen_visible = 1;
    }

    if (fullscreen_visible) {
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

    FS_REG_IME = previous_ime;
}''',
)

source.write_text(text, encoding="utf-8")
print("Patched fullscreen.c for conservative page-0 VBlank coexistence")
