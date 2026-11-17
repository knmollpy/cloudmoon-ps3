#!/bin/sh
set -eu
SDK=${PS3DEV:-vendor/PS3-Moonlight/ps3dev}
HEADER="$SDK/ppu/include/ppu-lv2.h"
if [ -f "$HEADER" ]; then
  sed -i 's/ asm(/ __asm__(/g' "$HEADER"
fi
