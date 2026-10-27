//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/pxr.h"
#include "pxr/base/arch/virtualMemory.h"
#include "pxr/base/arch/defines.h"
#include "pxr/usd/sdf/pool.h"

#include <cstdlib>
#include <cstring>

PXR_NAMESPACE_OPEN_SCOPE

char *
Sdf_PoolReserveRegion(size_t numBytes)
{
#if defined(ARCH_OS_WASM_VM)
    // Emscripten implements anonymous mmap by allocating AND clearing the
    // whole mapping. A path pool reserves 256/320 MiB on wasm32, even when
    // only a single small span will be used. Reserve address space without
    // touching it; each fresh span is initialized when committed below.
    // Keep the existing handle layout so separately built plugins stay ABI
    // compatible with the core USD libraries.
    return static_cast<char *>(std::malloc(numBytes));
#else
    return static_cast<char *>(ArchReserveVirtualMemory(numBytes));
#endif
}

bool
Sdf_PoolCommitRange(char *start, char *end)
{
#if defined(ARCH_OS_WASM_VM)
    // _ReserveSpan only commits new, disjoint spans. Clear that exact range,
    // rather than its surrounding pages, which may belong to other threads.
    std::memset(start, 0, end - start);
    return true;
#else
    return ArchCommitVirtualMemoryRange(start, end-start);
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
