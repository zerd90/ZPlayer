#!/usr/bin/env bash
# Fetch Windows FFmpeg 9.0.2 shared (DLL + import .lib + headers) from the
# official gyan.dev / GyanD codexffmpeg release linked on ffmpeg.org builds page.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFMPEG_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION="9.0.2"
URL="https://github.com/GyanD/codexffmpeg/releases/download/${VERSION}/ffmpeg-${VERSION}-full_build-shared.zip"
CACHE_DIR="${FFMPEG_ROOT}/.build-src"
ZIP="${CACHE_DIR}/ffmpeg-${VERSION}-full_build-shared.zip"
EXTRACT="${CACHE_DIR}/ffmpeg-${VERSION}-full_build-shared"

echo "==> Downloading Windows FFmpeg ${VERSION} shared build"
echo "    ${URL}"

mkdir -p "${CACHE_DIR}"
if [[ ! -f "${ZIP}" ]]; then
  curl -L --fail --retry 3 -o "${ZIP}.partial" "${URL}"
  mv "${ZIP}.partial" "${ZIP}"
else
  echo "    using cached ${ZIP}"
fi

rm -rf "${EXTRACT}"
mkdir -p "${EXTRACT}"
# Prefer ditto/unzip (zip); fall back to 7zz for .7z if needed.
if command -v unzip >/dev/null 2>&1; then
  unzip -q "${ZIP}" -d "${EXTRACT}"
else
  7zz x "${ZIP}" -o"${EXTRACT}" >/dev/null
fi

# Zip nests as <extract>/ffmpeg-*-full_build-shared/{bin,lib,include,...}
ROOT_DIR="$(find "${EXTRACT}" -mindepth 1 -maxdepth 3 -type d -name 'lib' -exec dirname {} \; | head -1)"
if [[ -z "${ROOT_DIR}" || ! -d "${ROOT_DIR}/bin" ]]; then
  echo "error: could not locate extracted package root under ${EXTRACT}" >&2
  ls -laR "${EXTRACT}" | head -80 >&2
  exit 1
fi

echo "    extracted: ${ROOT_DIR}"

# Headers: only fill include/ if empty, or refresh from this exact 9.0.2 package.
# Prefer Windows package headers when include is empty; macOS build may overwrite
# later with the same 9.0.2 API.
if [[ ! -d "${FFMPEG_ROOT}/include/libavutil" ]]; then
  rm -rf "${FFMPEG_ROOT}/include"
  mkdir -p "${FFMPEG_ROOT}/include"
  cp -R "${ROOT_DIR}/include/"* "${FFMPEG_ROOT}/include/"
fi

rm -rf "${FFMPEG_ROOT}/lib/windows"
mkdir -p "${FFMPEG_ROOT}/lib/windows"

# Import libraries + DLLs for MVP libs only (drop avdevice/avfilter to shrink tree).
for base in avutil avcodec avformat swresample swscale; do
  cp "${ROOT_DIR}/lib/${base}.lib" "${FFMPEG_ROOT}/lib/windows/" 2>/dev/null || true
  # DLL names are versioned, e.g. avutil-61.dll
  cp "${ROOT_DIR}/bin/${base}"-*.dll "${FFMPEG_ROOT}/lib/windows/" 2>/dev/null || true
done
{
  echo "version=${VERSION}"
  echo "type=shared (DLL + import .lib)"
  echo "source=${URL}"
  echo "package=ffmpeg-${VERSION}-full_build-shared (GyanD / gyan.dev)"
  echo "license=GPL (gyan full shared builds are GPLv3; see package LICENSE)"
  echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "${FFMPEG_ROOT}/lib/windows/BUILD_INFO.txt"

# Preserve package license text alongside root LICENSE if present.
if [[ -f "${ROOT_DIR}/LICENSE" ]]; then
  cp "${ROOT_DIR}/LICENSE" "${FFMPEG_ROOT}/lib/windows/LICENSE"
fi

echo "==> Done. Windows libs:"
ls -la "${FFMPEG_ROOT}/lib/windows" | head -60
