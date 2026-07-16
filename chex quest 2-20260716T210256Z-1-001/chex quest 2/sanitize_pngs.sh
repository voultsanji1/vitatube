#!/bin/bash
set -e

ASSETS="${1:-assets}"
OUT="${2:-sce_sys}"

mkdir -p "$OUT/livearea/contents"

sanitize() {
    local input="$1" output="$2" w="$3" h="$4"
    if [ ! -f "$input" ]; then echo "SKIP: $input"; return 0; fi
    echo "Processing: $input → $output"
    python3 -c "
from PIL import Image, PngImagePlugin
img = Image.open('$input')
if img.mode in ('RGBA', 'LA', 'PA'):
    bg = Image.new('RGB', img.size, (0, 0, 0))
    bg.paste(img, mask=img.split()[-1])
    img = bg
else:
    img = img.convert('RGB')
img = img.resize(($w, $h), Image.LANCZOS)
img = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
info = PngImagePlugin.PngInfo()
img.save('$output', 'PNG', optimize=True, pnginfo=info)
"
    if command -v optipng &>/dev/null; then
        optipng -strip all -quiet "$output"
    fi
    echo "  OK: $(stat -c%s "$output") bytes — $(file -b "$output")"
}

sanitize "$ASSETS/icon0.png"   "$OUT/icon0.png"                     128 128
sanitize "$ASSETS/bg.png"      "$OUT/livearea/contents/bg.png"       840 500
sanitize "$ASSETS/startup.png" "$OUT/livearea/contents/startup.png"  280 158

echo ""
echo "=== Done ==="
find "$OUT" -name "*.png" -exec file {} \;
