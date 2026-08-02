#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_ld="$repo_root/src/gba_cart_my.ld"
target_ld="$repo_root/src/gba_cart_fullscreen.ld"

python3 - "$source_ld" "$target_ld" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1])
target = Path(sys.argv[2])
text = source.read_text(encoding="utf-8")

old_vram1 = "\tvram1   : ORIGIN = 0x0600F000, LENGTH = 3584"
new_vram1 = "\tvram1   : ORIGIN = 0x06013600, LENGTH = 2560"

if text.count(old_vram1) != 1:
    raise SystemExit("Expected exactly one original VRAM1 region declaration")

text = text.replace(old_vram1, new_vram1)
text = text.replace(
    "* vram1 section to stick code into VRAM",
    "* vram1 section placed in the unused tail after the visible Mode 4 page-1 framebuffer",
    1,
)

target.write_text(text, encoding="utf-8")
PY

echo "Generated $target_ld with .vram1 in the non-visible Mode 4 page-1 tail"
