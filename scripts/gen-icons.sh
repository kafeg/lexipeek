#!/bin/sh
# The app icon and the menu-bar glyph from mac/Icon/*.svg into the asset catalog and qt/icons (needs rsvg-convert: brew install librsvg).
set -eu
cd "$(dirname "$0")/../mac"
set_dir=Sources/LexiPeek/Assets.xcassets/AppIcon.appiconset
mkdir -p "$set_dir" Sources/LexiPeek/Assets.xcassets/MenuBarIcon.imageset
for s in 16 32 64 128 256 512 1024; do rsvg-convert -w $s -h $s Icon/AppIcon.svg -o "$set_dir/icon_$s.png"; done
printf '{"info":{"version":1,"author":"xcode"}}\n' > Sources/LexiPeek/Assets.xcassets/Contents.json
python3 -I - "$set_dir/Contents.json" <<'PY'
import json, sys
images = []
for pt in (16, 32, 128, 256, 512):
    for scale in (1, 2):
        images.append({"idiom": "mac", "size": f"{pt}x{pt}", "scale": f"{scale}x", "filename": f"icon_{pt * scale}.png"})
json.dump({"images": images, "info": {"version": 1, "author": "xcode"}}, open(sys.argv[1], "w"), indent=1)
PY
cp Icon/MenuBarIcon.svg Sources/LexiPeek/Assets.xcassets/MenuBarIcon.imageset/MenuBarIcon.svg
cat > Sources/LexiPeek/Assets.xcassets/MenuBarIcon.imageset/Contents.json <<'J'
{"images":[{"idiom":"universal","filename":"MenuBarIcon.svg"}],"info":{"version":1,"author":"xcode"},"properties":{"template-rendering-intent":"template","preserves-vector-representation":true}}
J
# The Qt shell's window and tray icons.
for s in 16 32 64 256; do cp "$set_dir/icon_$s.png" ../qt/icons/; done
