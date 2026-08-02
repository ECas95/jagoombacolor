#!/usr/bin/env python3
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "src" / "fullscreen.c"
text = source.read_text(encoding="utf-8")

replacements = {
    "#define FS_MODE4_PAGE1        ((volatile u8 *)0x0600A000)":
        "#define FS_MODE4_PAGE0        ((volatile u8 *)0x06000000)",
    "#define FS_REG_DMA1CNT_H      FS_REG16(0x040000C6)\n#define FS_REG_IE             FS_REG16(0x04000200)":
        "#define FS_REG_DMA1CNT_H      FS_REG16(0x040000C6)\n"
        "#define FS_REG_DMA2CNT_H      FS_REG16(0x040000D2)\n"
        "#define FS_REG_IE             FS_REG16(0x04000200)\n"
        "#define FS_REG_IME            FS_REG16(0x04000208)",
    "#define FS_DISPCNT_PAGE1      0x0010":
        "#define FS_DISPCNT_PAGE1      0x0000",
    "void *destination = (void *)(FS_MODE4_PAGE1 + (y * FS_DEST_WIDTH));":
        "void *destination = (void *)(FS_MODE4_PAGE0 + (y * FS_DEST_WIDTH));",
    "    _scanlinehook = fullscreen_scanline_hook;\n}":
        "    _scanlinehook = default_scanlinehook;\n}",
}

for old, new in replacements.items():
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"Expected one occurrence, found {count}: {old!r}")
    text = text.replace(old, new, 1)

signature = "void fullscreen_vblank_post(void)\n{"
start = text.find(signature)
if start < 0:
    raise SystemExit("Could not locate fullscreen_vblank_post")

brace_start = text.find("{", start)
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
    raise SystemExit("Could not find end of fullscreen_vblank_post")

replacement_function = r'''void fullscreen_vblank_post(void)
{
    u16 previous_ime;

    if (!fullscreen_wanted()) {
        return;
    }

    if (!fullscreen_active) {
        fullscreen_enter();
    }

    /* The stock renderer schedules VCOUNT HDMA before this post hook.  The
       fullscreen upload must therefore block nested IRQs and stop all display
       DMAs before touching bitmap VRAM.  Previous builds performed this after
       the upload, allowing IRQ re-entry while the C helper and DMA3 loop were
       still using the interrupted System stack. */
    previous_ime = FS_REG_IME;
    FS_REG_IME = 0;

    FS_REG_IE &= (u16)~(FS_IRQ_HBLANK | FS_IRQ_VCOUNT);
    FS_REG_DISPSTAT &= (u16)~(FS_DISPSTAT_HBLANK | FS_DISPSTAT_VCOUNT);
    FS_REG_DMA0CNT_H = 0;
    FS_REG_DMA1CNT_H = 0;
    FS_REG_DMA2CNT_H = 0;

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

        /* Page 0 is visible from 0x06000000 through 0x060095FF.  The original
           time-critical .vram1 block remains untouched at 0x0600F000. */
        FS_REG_DISPCNT = FS_DISPCNT_MODE4 | FS_DISPCNT_PAGE1 | FS_DISPCNT_BG2;
    }

    FS_REG_IME = previous_ime;
}'''

text = text[:start] + replacement_function + text[end:]
source.write_text(text, encoding="utf-8")
print("Patched fullscreen.c for Mode 4 page 0 and IRQ-safe upload")
