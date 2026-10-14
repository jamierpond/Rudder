#!/usr/bin/env bash
# Regenerates Assets/Icon/{Icon.png,Icon.ico,Tray.png,TrayWindows.png} from Assets/Icon/Source.png
# (the cow-spotted jet on white). Needs ImageMagick 7. The outputs are
# committed; run this only when the source art changes.
set -euo pipefail

cd "$(dirname "$0")/../Assets/Icon"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Silhouette: flood the white background from a corner; everything it can't
# reach (the outlined jet) is the shape.
magick Source.png -colorspace gray -threshold 92% -type truecolor \
    -fill red -fuzz 1% -draw "color 0,0 floodfill" \
    -fill black +opaque red -fill white -opaque red -negate \
    -alpha off -colorspace gray "$tmp/mask.png"
magick "$tmp/mask.png" -morphology Close Disk:2 -blur 0x1.2 -level 45%,55% "$tmp/body.png"
magick "$tmp/mask.png" -morphology Dilate Disk:9 -blur 0x1.5 -level 45%,55% "$tmp/outline.png"

# The jet keeps its spots and sits on a white outline so the black spots on its
# edge don't run into the black tile.
crop=$(magick "$tmp/outline.png" -format "%@" info:)
magick Source.png "$tmp/body.png" -alpha off -compose CopyOpacity -composite "$tmp/jet.png"
magick -size 1536x1024 xc:white "$tmp/outline.png" -alpha off -compose CopyOpacity -composite \
    "$tmp/jet.png" -compose Over -composite -crop "$crop" +repage "$tmp/glyph.png"

# The bare white silhouette, for sizes too small to show the spots.
magick -size 1536x1024 xc:white "$tmp/body.png" -alpha off -compose CopyOpacity -composite \
    -crop "$crop" +repage "$tmp/silhouette.png"

# tile <canvas> <tile size> <corner radius> <glyph height> <out> [glyph]
tile() {
    local inset=$((($1 - $2) / 2)) last=$(($2 - 1))
    magick -size "$1x$1" xc:none \
        \( -size "$2x$2" gradient:'#262626-#050505' \
           \( -size "$2x$2" xc:black -fill white -draw "roundrectangle 0,0 $last,$last $3,$3" \) \
           -alpha off -compose CopyOpacity -composite \) \
        -geometry "+$inset+$inset" -compose Over -composite \
        \( "${6:-$tmp/glyph.png}" -resize "x$4" \) -gravity center -compose Over -composite \
        "$5"
}

# macOS: Apple's icon grid, an 824 tile on a 1024 canvas, which leaves room for
# the shadow the system draws.
tile 1024 824 185 660 Icon.png

# Windows draws the .ico as-is with no grid of its own, so the tile runs nearly
# to the edge and the jet fills more of it. Below 64 px the spots are noise, so
# those frames carry the plain white silhouette instead. eacp_set_app_icon's
# IMAGE path would downscale one picture to every size; this .ico goes in
# through its ICO argument.
tile 1024 1000 150 860 "$tmp/windows.png"
tile 1024 1000 150 880 "$tmp/small.png" "$tmp/silhouette.png"
frames=()
for size in 16 24 32 48; do
    magick "$tmp/small.png" -resize "${size}x$size" "$tmp/f$size.png"
    frames+=("$tmp/f$size.png")
done
for size in 64 128 256; do
    magick "$tmp/windows.png" -resize "${size}x$size" "$tmp/f$size.png"
    frames+=("$tmp/f$size.png")
done
magick "${frames[@]}" Icon.ico

# Menu bar: the bare silhouette as an alpha-only template (the system tints
# it), 36 px for an 18 pt @2x slot.
magick "$tmp/body.png" -trim +repage -resize x34 -background black -alpha shape \
    -gravity center -background none -extent 36x36 Tray.png

# Windows tray: no template tinting there, and the taskbar may be light or
# dark, so it gets the small tile itself.
magick "$tmp/small.png" -resize 32x32 TrayWindows.png
