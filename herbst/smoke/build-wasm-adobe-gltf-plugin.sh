#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

rm -rf "${ADOBE_PLUGIN_WASM_PREFIX}-build" "${ADOBE_PLUGIN_WASM_PREFIX}"

emcmake cmake \
  -S "${ADOBE_PLUGIN_REPO}" \
  -B "${ADOBE_PLUGIN_WASM_PREFIX}-build" \
  -DCMAKE_INSTALL_PREFIX="${ADOBE_PLUGIN_WASM_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -Dpxr_ROOT="${OPENUSD_WASM_PREFIX}" \
  -Dpxr_DIR="${OPENUSD_WASM_PREFIX}" \
  -DCMAKE_PREFIX_PATH="${OPENUSD_WASM_PREFIX}" \
  -DCMAKE_FIND_ROOT_PATH="${OPENUSD_WASM_PREFIX}" \
  -DPXR_FIND_TBB_IN_CONFIG=OFF \
  -DTBB_INCLUDE_DIRS="${OPENUSD_WASM_PREFIX}/include" \
  -DTBB_tbb_LIBRARY_RELEASE="${OPENUSD_WASM_PREFIX}/lib/libtbb.a" \
  -DTBB_tbbmalloc_LIBRARY_RELEASE="${OPENUSD_WASM_PREFIX}/lib/libtbbmalloc.a" \
  -DZLIB_ROOT="${OPENUSD_WASM_PREFIX}" \
  -DUSD_FILEFORMATS_BUILD_TESTS=OFF \
  -DUSD_FILEFORMATS_ENABLE_FBX=OFF \
  -DUSD_FILEFORMATS_ENABLE_GLTF=ON \
  -DUSD_FILEFORMATS_ENABLE_OBJ=OFF \
  -DUSD_FILEFORMATS_ENABLE_PLY=OFF \
  -DUSD_FILEFORMATS_ENABLE_SPZ=OFF \
  -DUSD_FILEFORMATS_ENABLE_STL=OFF \
  -DUSD_FILEFORMATS_ENABLE_SBSAR=OFF \
  -DUSD_FILEFORMATS_ENABLE_DRACO=ON \
  -DUSD_FILEFORMATS_FETCH_DRACO=ON \
  -DDRACO_JS_GLUE=OFF \
  -DUSD_FILEFORMATS_ENABLE_OPENIMAGEIO=OFF \
  -DUSD_FILEFORMATS_FETCH_TINYGLTF=ON \
  -DUSD_FILEFORMATS_FETCH_ZLIB=OFF \
  -DCMAKE_CXX_FLAGS="-pthread --use-port=zlib" \
  -DCMAKE_C_FLAGS="-pthread --use-port=zlib" \
  -DCMAKE_EXE_LINKER_FLAGS="-pthread"

cmake --build "${ADOBE_PLUGIN_WASM_PREFIX}-build" \
  --config Release \
  --target usdGltf \
  --parallel "${BUILD_PARALLELISM:-8}"

mkdir -p \
  "${ADOBE_PLUGIN_WASM_PREFIX}/include" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/lib" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/plugin/usd/usdGltf/resources"

draco_lib="$(find "${ADOBE_PLUGIN_WASM_PREFIX}-build" -name 'libdraco.a' -print -quit)"
if [[ -z "${draco_lib}" ]]; then
  echo "Draco was enabled, but libdraco.a was not produced." >&2
  exit 1
fi
draco_include_root="$(find "${ADOBE_PLUGIN_WASM_PREFIX}-build/_deps" -path '*/draco-src/src/draco/compression/decode.h' -print -quit)"
if [[ -z "${draco_include_root}" ]]; then
  echo "Draco was enabled, but Draco headers were not found." >&2
  exit 1
fi
draco_include_root="${draco_include_root%/draco/compression/decode.h}"
draco_features_header="${ADOBE_PLUGIN_WASM_PREFIX}-build/draco/draco_features.h"
if [[ ! -f "${draco_features_header}" ]]; then
  echo "Draco was enabled, but generated draco_features.h was not found." >&2
  exit 1
fi

cp "${ADOBE_PLUGIN_WASM_PREFIX}-build/gltf/src/libusdGltf.a" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/lib/libusdGltf.a"
cp "${ADOBE_PLUGIN_WASM_PREFIX}-build/utils/libfileformatUtils.a" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/lib/libfileformatUtils.a"
cp "${ADOBE_PLUGIN_WASM_PREFIX}-build/_deps/tinygltf-build/libtinygltf.a" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/lib/libtinygltf.a"
cp "${draco_lib}" "${ADOBE_PLUGIN_WASM_PREFIX}/lib/libdraco.a"
cp -R "${draco_include_root}/draco" "${ADOBE_PLUGIN_WASM_PREFIX}/include/draco"
cp "${draco_features_header}" "${ADOBE_PLUGIN_WASM_PREFIX}/include/draco/draco_features.h"
cp "${ADOBE_PLUGIN_WASM_PREFIX}-build/gltf/src/plugInfo.json" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/plugin/usd/usdGltf/resources/plugInfo.json"
cp "${ADOBE_PLUGIN_REPO}/gltf/src/plugInfo.root.json" \
  "${ADOBE_PLUGIN_WASM_PREFIX}/plugin/usd/plugInfo.json"
