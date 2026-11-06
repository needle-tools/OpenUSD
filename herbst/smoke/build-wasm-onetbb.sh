#!/usr/bin/env bash
# Builds oneTBB 2021.12.0 as static wasm libraries into OPENUSD_WASM_DEPENDENCIES_PREFIX,
# with the exception model of WASM_RUNTIME (the same options build_usd.py uses for wasm).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/env.sh"

activate_emsdk

ONETBB_SRC_PARENT="${ONETBB_SRC_PARENT:-/Users/herbst/oneTBB-2021.12.0-wasm${WASM_RUNTIME_SUFFIX}-src}"
ONETBB_BUILD_DIR="${ONETBB_BUILD_DIR:-/Users/herbst/oneTBB-2021.12.0-wasm${WASM_RUNTIME_SUFFIX}-build}"

if [[ ! -f "${ONETBB_ZIP}" ]]; then
  echo "Missing oneTBB source zip: ${ONETBB_ZIP}" >&2
  exit 1
fi

rm -rf "${ONETBB_SRC_PARENT}" "${ONETBB_BUILD_DIR}"
mkdir -p "${ONETBB_SRC_PARENT}"
unzip -q "${ONETBB_ZIP}" -d "${ONETBB_SRC_PARENT}"
# oneTBB hardcodes Emscripten's JavaScript exceptions; use the runtime's model instead.
if [[ -n "${WASM_EXCEPTION_FLAGS}" ]]; then
  perl -pi -e 's/set\(TBB_COMMON_COMPILE_FLAGS \$\{TBB_COMMON_COMPILE_FLAGS\} -fexceptions\)/set(TBB_COMMON_COMPILE_FLAGS \${TBB_COMMON_COMPILE_FLAGS} '"${WASM_EXCEPTION_FLAGS}"')/' \
    "${ONETBB_SRC_PARENT}/oneTBB-2021.12.0/cmake/compilers/Clang.cmake"
  grep -q -- "${WASM_EXCEPTION_FLAGS}" "${ONETBB_SRC_PARENT}/oneTBB-2021.12.0/cmake/compilers/Clang.cmake"
fi

emcmake cmake \
  -S "${ONETBB_SRC_PARENT}/oneTBB-2021.12.0" \
  -B "${ONETBB_BUILD_DIR}" \
  -DCMAKE_INSTALL_PREFIX="${OPENUSD_WASM_DEPENDENCIES_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTBB_TEST=OFF \
  -DTBB_STRICT=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_CXX_FLAGS="${WASM_COMPILE_FLAGS}" \
  -DCMAKE_C_FLAGS="${WASM_COMPILE_FLAGS}"

cmake --build "${ONETBB_BUILD_DIR}" --config Release --parallel "${JOBS:-6}" --target install

test -f "${OPENUSD_WASM_DEPENDENCIES_PREFIX}/lib/libtbb.a"
test -f "${OPENUSD_WASM_DEPENDENCIES_PREFIX}/lib/libtbbmalloc.a"
echo "wasm oneTBB installed into ${OPENUSD_WASM_DEPENDENCIES_PREFIX}"
