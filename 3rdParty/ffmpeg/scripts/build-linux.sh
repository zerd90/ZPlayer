#!/usr/bin/env bash
# Placeholder: Linux FFmpeg vendoring is not implemented yet.
#
# Intended target: FFmpeg n9.0.2 / 9.0.2 → 3rdParty/ffmpeg/lib/linux
# (same include/ tree as macOS/Windows).
#
# When implemented, mirror build-macos.sh configure flags (static or shared —
# document the choice) and refuse to silently pick up system pkg-config FFmpeg.
#
# Hardware decode (ZPlayer MediaPlayer):
#   Prefer VAAPI via av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_VAAPI). This is
#   best-effort — if libva / device init fails at runtime, player falls back to
#   software decode. Build should keep VAAPI hwaccel enabled when libva is
#   available (--enable-vaapi / autodetect). Not a stub: code path is real;
#   vendored lib/linux is what is still missing.
set -euo pipefail

echo "build-linux.sh: not implemented."
echo "  Expected output: 3rdParty/ffmpeg/lib/linux/{libavformat,libavcodec,libavutil,libswscale,libswresample}.*"
echo "  Source tag: n9.0.2"
echo "  HW decode note: ZPlayer will try VAAPI then soft-fallback once libs exist."
echo "  Until then, CMake will error on Linux if lib/linux is empty."
exit 1
