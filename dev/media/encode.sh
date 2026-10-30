#!/usr/bin/env bash
# Turns the raw recordings from dev/media/record.sh (in ci-logs) into docs/media: the
# screenshots as they are, each video as an MP4 and as a GIF for the README.
#
#   dev/media/encode.sh [ci-logs]
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
IN=${1:-$ROOT/ci-logs}
OUT="$ROOT/docs/media"
mkdir -p "$OUT"

cp "$IN/group.png" "$IN/nested.png" "$OUT/"
for v in create fullscreen; do
    ffmpeg -v error -y -i "$IN/$v.mp4" -an -vf "scale=1280:-2:flags=lanczos" \
        -c:v libx264 -preset slow -crf 28 -pix_fmt yuv420p -movflags +faststart "$OUT/$v.mp4"
    ffmpeg -v error -y -i "$IN/$v.mp4" -vf "fps=12,scale=800:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" \
        "$OUT/$v.gif"
done
ls -la "$OUT"
