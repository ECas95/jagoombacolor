#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_ld="$repo_root/src/gba_cart_my.ld"
target_ld="$repo_root/src/gba_cart_fullscreen.ld"
lcd_source="$repo_root/src/lcd.s"

python3 - "$source_ld" "$target_ld" "$lcd_source" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1])
target = Path(sys.argv[2])
lcd_source = Path(sys.argv[3])
text = source.read_text(encoding="utf-8")

old_ewram = "\tewram\t: ORIGIN = 0x02000000, LENGTH = 256K"
new_ewram = "\tewram\t: ORIGIN = 0x02000000, LENGTH = 252K"
old_vram1 = "\tvram1   : ORIGIN = 0x0600F000, LENGTH = 3584"
new_vram1 = "\tvram1   : ORIGIN = 0x0203F000, LENGTH = 4096"

if text.count(old_ewram) != 1:
    raise SystemExit("Expected exactly one original EWRAM region declaration")
if text.count(old_vram1) != 1:
    raise SystemExit("Expected exactly one original VRAM1 region declaration")

text = text.replace(old_ewram, new_ewram)
text = text.replace(old_vram1, new_vram1)
text = text.replace(
    "* vram1 section to stick code into VRAM",
    "* vram1 section relocated to reserved high EWRAM for fullscreen Mode 4",
    1,
)
target.write_text(text, encoding="utf-8")

lcd = lcd_source.read_text(encoding="utf-8")
old_call = "\t@ensure that GBA vblank is enabled when this function exits\n\tbl reenable_gba_vblank\n"
new_call = "\t@ensure that GBA vblank is enabled when this function exits\n\tbl_long reenable_gba_vblank\n"
if old_call in lcd:
    lcd = lcd.replace(old_call, new_call, 1)
elif new_call not in lcd:
    raise SystemExit("Could not locate the relocated .vram1 far call")
lcd_source.write_text(lcd, encoding="utf-8")
PY

echo "Generated $target_ld and patched relocated far calls"
