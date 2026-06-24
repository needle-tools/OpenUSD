#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

OPENSUBDIV_ZIP="${OPENSUBDIV_ZIP:-/Users/herbst/OpenUSD-26.05-native/src/v3_6_1.zip}"
OPENSUBDIV_SRC_PARENT="${OPENSUBDIV_SRC_PARENT:-/Users/herbst/OpenSubdiv-3_6_1-wasm-src}"
OPENSUBDIV_SRC_DIR="${OPENSUBDIV_SRC_PARENT}/OpenSubdiv-3_6_1"
OPENSUBDIV_BUILD_DIR="${OPENSUBDIV_BUILD_DIR:-/Users/herbst/OpenSubdiv-3_6_1-wasm-build}"

if [[ ! -f "${OPENSUBDIV_ZIP}" ]]; then
  echo "Missing OpenSubdiv source zip: ${OPENSUBDIV_ZIP}" >&2
  exit 1
fi

rm -rf "${OPENSUBDIV_SRC_PARENT}" "${OPENSUBDIV_BUILD_DIR}"
mkdir -p "${OPENSUBDIV_SRC_PARENT}"
unzip -q "${OPENSUBDIV_ZIP}" -d "${OPENSUBDIV_SRC_PARENT}"

emcmake cmake \
  -S "${OPENSUBDIV_SRC_DIR}" \
  -B "${OPENSUBDIV_BUILD_DIR}" \
  -DCMAKE_INSTALL_PREFIX="${OPENUSD_WASM_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNO_EXAMPLES=ON \
  -DNO_TUTORIALS=ON \
  -DNO_REGRESSION=ON \
  -DNO_DOC=ON \
  -DNO_OMP=ON \
  -DNO_CUDA=ON \
  -DNO_OPENCL=ON \
  -DNO_DX=ON \
  -DNO_TESTS=ON \
  -DNO_GLEW=ON \
  -DNO_GLFW=ON \
  -DNO_PTEX=ON \
  -DNO_TBB=ON \
  -DNO_METAL=ON \
  -DNO_OPENGL=ON \
  -DBUILD_SHARED_LIB=OFF \
  -DOSD_PATCH_SHADER_SOURCE_GLSL=ON \
  -DCMAKE_CXX_FLAGS="-pthread --use-port=zlib" \
  -DCMAKE_C_FLAGS="-pthread --use-port=zlib"

emmake cmake --build "${OPENSUBDIV_BUILD_DIR}" --config Release --parallel "${JOBS:-8}" --target install

test -f "${OPENUSD_WASM_PREFIX}/include/opensubdiv/version.h"
test -f "${OPENUSD_WASM_PREFIX}/lib/libosdCPU.a"
echo "wasm OpenSubdiv installed into ${OPENUSD_WASM_PREFIX}"
