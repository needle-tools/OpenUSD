
// Emscripten sets this runtime flag before pre-js; pthread workers receive
// the main module's shared memory after startup and must not allocate another.
if (!ENVIRONMENT_IS_PTHREAD && !Module["wasmMemory"]){
    function isMobileDevice(){
        if (typeof navigator === "undefined") {
            return false;
        }
        return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
    }

    const MAX_MEMORY_MOBILE=1024*1024*1024;
    const MAX_MEMORY_DESKTOP=2*1024*1024*1024;
    const MAX_DEVICE_MEMORY=isMobileDevice() ? MAX_MEMORY_MOBILE : MAX_MEMORY_DESKTOP;
    Module['wasmMemory'] = new WebAssembly.Memory({"initial":16777216/65536,"maximum":MAX_DEVICE_MEMORY/65536,"shared":true});
}

// Emscripten forwards only its built-in handlers to pthread workers. Resolver
// downloads also need to reach the module owner, including when composition
// opens layers on a worker. Use the existing callHandler message protocol.
if (ENVIRONMENT_IS_PTHREAD) {
    Module['onAssetFetchProgress'] = function(payload) {
        postMessage({cmd:'callHandler', handler:'onAssetFetchProgress', args:[payload]});
    };
} else if (!Module['onAssetFetchProgress']) {
    Module['onAssetFetchProgress'] = function() {};
}
