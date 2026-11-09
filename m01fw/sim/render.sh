#!/bin/sh
# Render UI screenshots on the PC: sim/out/*.png
set -e
cd "$(dirname "$0")/.."
CC="${HOST_CC:-gcc}"
"$CC" -specs=sim/nomanifest.specs -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -o sim/out/host.exe sim/host.c src/ui.c src/gfx.c src/fonts.c src/mdp_host.c
./sim/out/host.exe
python -I -c "
import glob
from PIL import Image
for p in glob.glob('sim/out/*.ppm'):
    Image.open(p).resize((640,480), Image.NEAREST).save(p[:-4]+'.png')
"
