// IMPORT THIRD-PARTY LIBRARIES
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <iostream>
#include <filesystem>
#include <fstream>

// IMPORT LOCAL LIBRARIES
#include "resolver.h"

PXR_NAMESPACE_OPEN_SCOPE

AR_DEFINE_RESOLVER(HttpResolver, ArDefaultResolver);

HttpResolver::HttpResolver() : ArDefaultResolver() {}
HttpResolver::~HttpResolver() {}

struct AssetData {
    int ptrToContent;
    int length; // Use int for compatibility with JavaScript's setValue; adjust if necessary
};

EM_ASYNC_JS(void, fetch_asset, (const char* route, int dataPtr), {
    const routeString = UTF8ToString(route);
    const verbose = false;
    let absoluteUrl = routeString;

    // Sanitization: we need to turn the prim path back into a URL.
    // The only thing that gets lost is the second colon.
    // Additionally, things like query parameters aren't supported,
    // since USD doesn't understand the filetype then.
    if (absoluteUrl.startsWith("/http")) absoluteUrl = absoluteUrl.slice(1);
    if (absoluteUrl.includes("http:/"))
        absoluteUrl = absoluteUrl.replace("http:/", "http://");
    if (absoluteUrl.includes("https:/"))
        absoluteUrl = absoluteUrl.replace("https:/", "https://");

    /** @typedef {string | FileSystemFileHandle | FileSystemFileEntry | ArrayBuffer | File | Blob} Result */
    /** @type {Result | null} */
    let callbackResult = null;

    /** @type {ArrayBuffer | null} */
    let buffer = null;

    // From a worker thread, we call back to the main thread to get a chance to modify what we're doing
    // to get the asset into memory.
    // What is returned from fetch_asset becomes put on disk.
    // We could get it from a dropped file (and can transfer the FileSystemFileHandle to the worker)
    // or from a blob URL (which will then be fetched inside the worker).
    if (ENVIRONMENT_IS_PTHREAD) {
        if (verbose)
        console.log("we're in a thread, calling urlCallback", absoluteUrl);
        let result;
        try {
        result = await Module["urlCallbackFromWorker"](absoluteUrl);
        } catch (e) {
        console.error(
            "Error in thread callback for",
            absoluteUrl,
            "error:",
            e,
        );
        }
        if (verbose) console.log("got result inside worker", result);
        // check what we got. if it's a handle, we can resolve it;
        // if it's a buffer, we can stop here and don't need to fetch anymore;
        // if it's a URL, we still need to fetch it below.
        callbackResult = result;
    }
    // From the main thread, we can directly call the URL modifier.
    else if (typeof Module["urlModifier"] === "function") {
        const prev = absoluteUrl;
        const callback = Module["urlModifier"];
        if (verbose) console.log("callback", callback);
        let result = callback(absoluteUrl);
        if (result instanceof Promise) result = await result;

        callbackResult = result;
        if (verbose)
        console.log(
            "found modifier, URL is now",
            callbackResult,
            "was",
            prev,
            "modifier now",
            Module["urlModifier"],
        );
    } else {
        if (verbose)
        console.log("no URL modifier found", Module["urlModifier"]);
    }

    // Resolve asset. we could have received a number of different things from the callback.
    // All of these types are transferable to the worker thread.
    // Even better would be to transfer a FileSystemFileHandle directly, because
    // then even getting the file from the file system would be done in the worker.
    try {
        if (callbackResult && typeof callbackResult === "object") {
        // https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileHandle
        if ("getFile" in callbackResult) {
            buffer = await (await callbackResult.getFile()).arrayBuffer();
        }
        // https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileEntry
        else if ("file" in callbackResult) {
            buffer = await new Promise((resolve, reject) => {
            callbackResult.file((x) => {
                const reader = new FileReader();
                // @ts-ignore
                reader.onload = () => resolve(reader.result);
                reader.onerror = reject;
                reader.readAsArrayBuffer(x);
            }, reject);
            });
        }
        // https://developer.mozilla.org/en-US/docs/Web/API/File
        else if (callbackResult instanceof File) {
            buffer = await callbackResult.arrayBuffer();
        }
        // https://developer.mozilla.org/en-US/docs/Web/API/Blob
        else if (callbackResult instanceof Blob) {
            buffer = await new Promise((resolve, reject) => {
            const reader = new FileReader();
            // @ts-ignore
            reader.onload = () => resolve(reader.result);
            reader.onerror = reject;
            reader.readAsArrayBuffer(callbackResult);
            });
        }
        // https://developer.mozilla.org/en-US/docs/Web/API/ArrayBuffer
        else if (callbackResult instanceof ArrayBuffer) {
            buffer = callbackResult;
        }
        }
        // regular URL
        else if (typeof callbackResult === "string") {
        absoluteUrl = callbackResult;
        }
    } catch (e) {
        console.error(
        "Error after main thread callback in fetch_asset for",
        absoluteUrl,
        "error:",
        e,
        );
        Module.HEAP32[dataPtr >> 2] = 0;
        Module.HEAP32[(dataPtr >> 2) + 1] = 0;
        return;
    }

    if (verbose) console.log("fetching asset", absoluteUrl);
    try {
        // If we don't already have a buffer, we assume we need to fetch it from the absoluteUrl.
        // Otherwise, we can skip this step.
        if (buffer === null) {
        buffer = await fetch(absoluteUrl)
            .then((r) => {
            if (verbose) console.log(r.status + " " + r.statusText);
            return r.arrayBuffer();
            })
            .catch((e) => null);
        }

        if (!buffer || buffer.byteLength === 0) {
        console.error("Error fetching asset – couldn't fetch", absoluteUrl);

        /// TODO not sure why we can't just return here,
        /// potentially there's missing error correction on the C++ side to
        /// check the return type...
        /// We just want to continue execution and not crash
        // Module.HEAP32[dataPtr >> 2] = 0;
        // Module.HEAP32[(dataPtr >> 2) + 1] = 0;
        // return;

        // Workaround for the issue mentioned above
        buffer = new ArrayBuffer(1);
        }

        if (verbose) console.log("after awaiting buffer", buffer);
        const length = buffer.byteLength;
        const ptr = _malloc(length);

        /// useful for debugging to see what response we actually get
        // const fileReader = new FileReader();
        // fileReader.onload = function() { console.log("fileReader.onload", fileReader.result); };
        // fileReader.readAsText(new Blob([buffer]));

        if (verbose)
        console.log(
            "fetch complete for ",
            absoluteUrl,
            " ->",
            length,
            "bytes",
        );
        GROWABLE_HEAP_U8().set(new Uint8Array(buffer), ptr >>> 0);
        Module.HEAP32[dataPtr >> 2] = ptr;
        Module.HEAP32[(dataPtr >> 2) + 1] = length;
        return;
    } catch (err) {
        console.error("Error in fetch_asset for", absoluteUrl, err);
        Module.HEAP32[dataPtr >> 2] = 0;
        Module.HEAP32[(dataPtr >> 2) + 1] = 0;
        return;
    }
});

EM_JS(void, addToLoadedFiles, (const char* path), {
    if (typeof self !== 'undefined') {
        // Safe to use self here
        if (typeof self.loadedFiles === 'undefined') {
            self.loadedFiles = [];
        }
    } else {
    // Handle case where neither window nor self are available
        console.log('Neither window nor self is defined');
    }
    self.loadedFiles.push(UTF8ToString(path));
});

std::filesystem::path HttpResolver::FetchAndSaveAsset(const std::string& route,
                                                   const std::string& filePath) const {
    try {
        std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();

        // Attempt to create the directory (and any necessary parent directories)
        if (std::filesystem::create_directories(dirPath)) {
            if (verbose){
                std::cout << "Directories created successfully: " << dirPath << std::endl << " --> for route: " << route << std::endl;
            }
        } else {
            if (verbose){
                std::cout << "Directories already exist or cannot be created.\n";
            }
        }

        AssetData* data = new AssetData();
        fetch_asset(route.c_str(), reinterpret_cast<int>(data));
        char *assetContentCString = reinterpret_cast<char *>(data->ptrToContent);
        saveBinaryAssetContentToFile(assetContentCString, data->length, filePath);

        free(reinterpret_cast<void*>(data->ptrToContent));
        delete data;

    }
    catch (const std::exception& e){
        std::cout << "Error: " << e.what() << std::endl;
        return filePath;
    }

    addToLoadedFiles(filePath.c_str());
    return filePath;
}

void HttpResolver::saveBinaryAssetContentToFile(const char* assetContent, size_t length, const std::string& filePath) const {

    std::ofstream outFile(filePath, std::ios::out | std::ios::binary);
    if (outFile) {
        // Write the binary content directly to the file
        outFile.write(assetContent, length);
        outFile.close();
        if (verbose) {
            std::cout << "File written successfully." << std::endl;
        }
    } else {
        if (verbose) {
            std::cout << "Failed to open file for writing." << std::endl;
        }
    }
}

ArResolvedPath HttpResolver::_Resolve(const std::string& assetPath) const {
    if (verbose){
        std::cout << "_Resolve: " << assetPath << std::endl;
    }
    std::string stringAssetPathCopy = assetPath;
    std::filesystem::path savedAssetFilePath = assetPath;
    if (std::filesystem::exists(assetPath)){
        if (verbose) {
            std::cout << "Already Exists: " << assetPath << std::endl;
        }
    }
    else {
        auto path = assetPath;
        // Nudge USD to interpret the URL as path and put assets there
        if (path.rfind("http", 0) == 0) {
            path = "/" + path;
        }

        // pass through JS so we can modify it there
        savedAssetFilePath = FetchAndSaveAsset(path, path);

        if (verbose) {
            std::cout << "FetchAndSaveAsset returns: " << path << " -->" << savedAssetFilePath << std::endl;
        }
    }
    if (verbose){
        std::cout << "ENDDD_Resolve: " << savedAssetFilePath << std::endl;
    }

    return ArResolvedPath(savedAssetFilePath);
}

std::shared_ptr<ArAsset> HttpResolver::_OpenAsset(const ArResolvedPath &resolvedPath) const {
    if (verbose){
        std::cout << "_OpenAsset: " << resolvedPath.GetPathString() << std::endl;
    }

    return ArDefaultResolver::_OpenAsset(resolvedPath);
}

ArResolvedPath HttpResolver::_ResolveForNewAsset(const std::string &assetPath) const {
    if (verbose){
        std::cout << "Resolve for new asset" << std::endl;
    }

    return ArDefaultResolver::_ResolveForNewAsset(assetPath);
}

PXR_NAMESPACE_CLOSE_SCOPE
