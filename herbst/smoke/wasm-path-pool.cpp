// Focused Node/WASM smoke for Sdf path-pool backing. Compile with pool.cpp and
// the existing wasm libusd_arch.a; no OpenUSD rebuild or stage data is needed.
#include "pxr/pxr.h"
#include "pxr/usd/sdf/instantiatePool.h"
#include <emscripten.h>
#include <emscripten/heap.h>
#include <cassert>
#include <cstdio>

int main()
{
    char *region = PXR_NS::Sdf_PoolReserveRegion(256 * 1024 * 1024);
    assert(region);
    assert(PXR_NS::Sdf_PoolCommitRange(region, region + 65536));
    for (size_t i = 0; i < 65536; ++i) {
        assert(region[i] == 0);
    }
    region[65535] = 42;
    assert(PXR_NS::Sdf_PoolCommitRange(region + 65536, region + 131072));
    assert(region[65535] == 42);
    for (size_t i = 65536; i < 131072; ++i) {
        assert(region[i] == 0);
    }
    std::printf("heap_bytes=%zu\n", emscripten_get_heap_size());
    EM_ASM({ console.log("rss_bytes=" + process.memoryUsage().rss); });
}
