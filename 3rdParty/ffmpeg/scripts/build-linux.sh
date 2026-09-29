#!/usr/bin/env bash
# Placeholder: Linux FFmpeg vendoring is not implemented yet.
#
# Intended target: FFmpeg n9.0.2 / 9.0.2 → 3rdParty/ffmpeg/lib/linux
# (same include/ tree as macOS/Windows).
#
# When implemented, mirror build-macos.sh configure flags (static or shared —
# document the choice) and refuse to silently pick up system pkg-config FFmpeg.
set -euo pipefail

echo "build-linux.sh: not implemented."
echo "  Expected output: 3rdParty/ffmpeg/lib/linux/{libavformat,libavcodec,libavutil,libswscale,libswresample}.*"
echo "  Source tag: n9.0.2"
echo "  Until then, CMake will error on Linux if lib/linux is empty."
exit 1
