#!/bin/bash
# Build a macOS icon set from a square PNG.
#
#     scripts/macos/make-icns.sh resources/peakemi-1024.png build/peakemi.icns
#
# Uses sips and iconutil, both part of a stock macOS. Every slot is rendered
# from the same high-resolution source, so no size is an upscale of another.
set -euo pipefail

source_png="${1:?usage: make-icns.sh <source.png> <output.icns>}"
output_icns="${2:?usage: make-icns.sh <source.png> <output.icns>}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "make-icns.sh only runs on macOS" >&2
    exit 1
fi

workdir="$(mktemp -d)"
trap 'rm -rf "${workdir}"' EXIT
iconset="${workdir}/peakemi.iconset"
mkdir -p "${iconset}"

# The slot names are fixed by iconutil: <size> and <size>@2x for each step.
for size in 16 32 128 256 512; do
    sips --setProperty format png --resampleHeightWidth "${size}" "${size}" \
        "${source_png}" --out "${iconset}/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips --setProperty format png --resampleHeightWidth "${double}" "${double}" \
        "${source_png}" --out "${iconset}/icon_${size}x${size}@2x.png" >/dev/null
done

mkdir -p "$(dirname "${output_icns}")"
iconutil --convert icns --output "${output_icns}" "${iconset}"
echo "wrote ${output_icns}"
