#!/usr/bin/env bash
# Build FFmpeg n9.0.2 for macOS and install into 3rdParty/ffmpeg/{include,lib/macos}.
#
# Library type: static (.a) — preferred for local app linking (no @rpath / dylib
# install-name issues). Windows uses the official shared (DLL) package separately.
#
# License intent: LGPL — do NOT pass --enable-gpl / --enable-nonfree / external
# GPL encoders (x264/x265/…). Built-in demuxers/decoders remain available.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFMPEG_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TAG="n9.0.2"
VERSION="9.0.2"
SRC_DIR="${FFMPEG_ROOT}/.build-src/ffmpeg-${VERSION}"
PREFIX="${FFMPEG_ROOT}/.build-prefix-macos"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
ARCH="$(uname -m)"

echo "==> FFmpeg ${TAG} macOS build (${ARCH}), jobs=${JOBS}"
echo "    output: ${FFMPEG_ROOT}/lib/macos + ${FFMPEG_ROOT}/include"

mkdir -p "${FFMPEG_ROOT}/.build-src"
rm -rf "${PREFIX}"
mkdir -p "${PREFIX}"

if [[ ! -d "${SRC_DIR}/.git" ]]; then
  rm -rf "${SRC_DIR}"
  git clone --depth 1 --branch "${TAG}" https://github.com/FFmpeg/FFmpeg.git "${SRC_DIR}"
else
  git -C "${SRC_DIR}" fetch --depth 1 origin "refs/tags/${TAG}:refs/tags/${TAG}" || true
  git -C "${SRC_DIR}" checkout -f "${TAG}"
fi

cd "${SRC_DIR}"
make distclean >/dev/null 2>&1 || true

# MVP libs: avformat / avcodec / avutil / swscale / swresample.
# Disable large unused components; keep decoder/demuxer matrix intact.
# No --enable-gpl / --enable-nonfree → LGPL. Autodetect picks up system
# iconv/zlib/bzlib and Apple VideoToolbox (CONFIG_VIDEOTOOLBOX=1) for hwaccel;
# that does not flip GPL. ZPlayer prefers VT hw decode with soft fallback.
# Explicit --enable-videotoolbox is not required on Apple when frameworks exist.
./configure \
  --prefix="${PREFIX}" \
  --enable-static \
  --disable-shared \
  --disable-doc \
  --disable-programs \
  --disable-avdevice \
  --disable-avfilter \
  --disable-network \
  --disable-debug \
  --enable-pic \
  --cc="${CC:-clang}" \
  --cxx="${CXX:-clang++}"

make -j"${JOBS}"
make install

rm -rf "${FFMPEG_ROOT}/include"
mkdir -p "${FFMPEG_ROOT}/include"
cp -R "${PREFIX}/include/"* "${FFMPEG_ROOT}/include/"

rm -rf "${FFMPEG_ROOT}/lib/macos"
mkdir -p "${FFMPEG_ROOT}/lib/macos"
cp "${PREFIX}/lib/"libavutil.a \
   "${PREFIX}/lib/"libavcodec.a \
   "${PREFIX}/lib/"libavformat.a \
   "${PREFIX}/lib/"libswscale.a \
   "${PREFIX}/lib/"libswresample.a \
   "${FFMPEG_ROOT}/lib/macos/"

if ls "${PREFIX}/lib/pkgconfig/"*.pc >/dev/null 2>&1; then
  mkdir -p "${FFMPEG_ROOT}/lib/macos/pkgconfig"
  cp "${PREFIX}/lib/pkgconfig/"*.pc "${FFMPEG_ROOT}/lib/macos/pkgconfig/"
fi

# LICENSE for this LGPL static build (macOS). Windows GPL shared package may
# ship its own notice; root LICENSE documents the macOS build as primary.
cp "${SRC_DIR}/COPYING.LGPLv2.1" "${FFMPEG_ROOT}/LICENSE"
cp "${SRC_DIR}/COPYING.LGPLv3" "${FFMPEG_ROOT}/LICENSE.LGPLv3" 2>/dev/null || true

{
  echo "version=${VERSION}"
  echo "tag=${TAG}"
  echo "commit=$(git -C "${SRC_DIR}" rev-parse HEAD)"
  echo "arch=${ARCH}"
  echo "type=static"
  echo "license=LGPL (no --enable-gpl)"
  echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "configure=static; MVP libs; --disable-avdevice/avfilter/network/programs/doc; no --enable-gpl"
} > "${FFMPEG_ROOT}/lib/macos/BUILD_INFO.txt"

echo "==> Done. Libraries:"
ls -la "${FFMPEG_ROOT}/lib/macos"
echo "==> Headers top-level:"
ls "${FFMPEG_ROOT}/include"
