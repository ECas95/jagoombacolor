#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
source_ld="$repo_root/src/gba_cart_my.ld"
target_ld="$repo_root/src/gba_cart_fullscreen.ld"

# Keep the original Jagoomba VRAM execution layout.  The fullscreen renderer
# uses Mode 4 page 0, whose visible bytes end at 0x06009600, so the original
# .vram1 block at 0x0600F000 remains outside the framebuffer.
cp "$source_ld" "$target_ld"

echo "Generated $target_ld with the original .vram1 layout"
