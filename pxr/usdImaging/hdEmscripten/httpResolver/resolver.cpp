// IMPORT THIRD-PARTY LIBRARIES
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <atomic>

// IMPORT LOCAL LIBRARIES
#include "resolver.h"

PXR_NAMESPACE_OPEN_SCOPE

AR_DEFINE_RESOLVER(HttpResolver, ArDefaultResolver);

HttpResolver::HttpResolver() : ArDefaultResolver() {}
HttpResolver::~HttpResolver() {}

struct AssetData {
    int ptrToContent;
    int length;
};

EM_ASYNC_JS(void, fetch_asset, (const char* route, int dataPtr), {
    const routeString = UTF8ToString(route);
    const heap32 = () => typeof GROWABLE_HEAP_I32 === 'function' ? GROWABLE_HEAP_I32() : HEAP32;
    const heap8 = () => typeof GROWABLE_HEAP_U8 === 'function' ? GROWABLE_HEAP_U8() : HEAPU8;
    const analyzePath = Module['FS_analyzePath'] || (typeof FS !== 'undefined' ? FS.analyzePath.bind(FS) : null);
    const readFileFromFs = Module['FS_readFile'] || (typeof FS !== 'undefined' ? FS.readFile.bind(FS) : null);
    const writeBytes = (bytes) => {
        const ptr = _malloc(bytes.byteLength);
        heap8().set(bytes, ptr);
        const view = heap32();
        view[dataPtr >> 2] = ptr;
        view[(dataPtr >> 2) + 1] = bytes.byteLength;
        return ptr;
    };
    const emitProgress = (detail) => {
        const payload = Object.assign({ url: routeString }, detail);
        Module['onAssetFetchProgress']?.(payload);
        Module['onUsdAssetFetchProgress']?.(payload);
        const target = typeof globalThis !== 'undefined' ? globalThis : undefined;
        if (target?.dispatchEvent && typeof CustomEvent !== 'undefined') {
            target.dispatchEvent(new CustomEvent('needle-usd-asset-fetch-progress', { detail: payload }));
        }
    };
    const fail = () => {
        const view = heap32();
        view[dataPtr >> 2] = 0;
        view[(dataPtr >> 2) + 1] = 0;
    };

    try {
        emitProgress({ state: 'start', loaded: 0, total: 0 });
        const filesystemCandidates = [];
        try {
            const base = globalThis?.location?.href || 'http://localhost/';
            const resolvedUrl = new URL(routeString, base);
            if (!globalThis?.location?.origin || resolvedUrl.origin === globalThis.location.origin) {
                filesystemCandidates.push(decodeURIComponent(resolvedUrl.pathname));
            }
        }
        catch (_) {}
        filesystemCandidates.push(routeString);
        for (const candidate of [...new Set(filesystemCandidates)]) {
            try {
                if (!analyzePath || !readFileFromFs) continue;
                const analysis = analyzePath(candidate);
                if (!analysis?.exists || analysis?.object?.isFolder) continue;
                const bytes = readFileFromFs(candidate);
                writeBytes(bytes);
                emitProgress({ state: 'done', loaded: bytes.byteLength, total: bytes.byteLength });
                return;
            }
            catch (_) {}
        }

        const response = await fetch(routeString);
        if (!response.ok) {
            throw new Error('Fetch failed: ' + response.status + ' ' + response.statusText);
        }

        let bytes;
        const total = Number(response.headers.get('Content-Length') || 0);
        if (response.body?.getReader) {
            const reader = response.body.getReader();
            const chunks = [];
            let loaded = 0;
            for (;;) {
                const { done, value } = await reader.read();
                if (done) break;
                if (!value) continue;
                chunks.push(value);
                loaded += value.byteLength;
                emitProgress({ state: 'progress', loaded, total });
            }

            bytes = new Uint8Array(loaded);
            let offset = 0;
            for (const chunk of chunks) {
                bytes.set(chunk, offset);
                offset += chunk.byteLength;
            }
        }
        else {
            bytes = new Uint8Array(await response.arrayBuffer());
        }

        writeBytes(bytes);
        emitProgress({ state: 'done', loaded: bytes.byteLength, total: total || bytes.byteLength });
    } catch (err) {
        console.warn("Failed to fetch asset", routeString, ": ", err);
        fail();
        emitProgress({ state: 'error', loaded: 0, total: 0, error: String(err?.message || err) });
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
                std::cout << "Directories created successfully: " << dirPath << std::endl;
            }
        } else {
            if (verbose){
                std::cout << "Directories already exist or cannot be created.\n";
            }
        }

        AssetData* data = new AssetData();
        fetch_asset(route.c_str(), reinterpret_cast<int>(data));
        if (data->ptrToContent == 0 || data->length == 0) {
            delete data;
            return std::filesystem::path();
        }
        char *assetContentCString = reinterpret_cast<char *>(data->ptrToContent);
        saveBinaryAssetContentToFile(assetContentCString, data->length, filePath);

        free(reinterpret_cast<void*>(data->ptrToContent));
        delete data;

    }
    catch (const std::exception& e){
        std::cout << "Error: " << e.what() << std::endl;
        return std::filesystem::path();
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

std::string correctURL(const std::string& url) {
    std::string correctedUrl = url;
    size_t pos;

    // Correct https:/ to https://
    pos = correctedUrl.find("https:/");
    if (pos != std::string::npos && correctedUrl.substr(pos, 7) == "https:/" && (pos + 7 == correctedUrl.size() || correctedUrl[pos + 7] != '/')) {
        correctedUrl.replace(pos, 7, "https://");
    }

    // Correct http:/ to http://
    pos = correctedUrl.find("http:/");
    if (pos != std::string::npos && correctedUrl.substr(pos, 6) == "http:/" && (pos + 6 == correctedUrl.size() || correctedUrl[pos + 6] != '/')) {
        correctedUrl.replace(pos, 6, "http://");
    }

    return correctedUrl;
}

bool isHttpUrl(const std::string& path) {
    return path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0;
}

bool pathStartsWithParentTraversal(const std::filesystem::path& path) {
    auto native = path.native();
    return native == ".." || native.rfind("../", 0) == 0 || native.rfind("..\\", 0) == 0;
}

bool isPathInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !relative.empty() && !pathStartsWithParentTraversal(relative);
}

std::string getHttpDirectory(const std::string& url) {
    const std::string normalizedUrl = correctURL(url);
    const size_t schemeEnd = normalizedUrl.find("://");
    const size_t searchStart = schemeEnd == std::string::npos
        ? 0
        : schemeEnd + 3;
    const size_t slash = normalizedUrl.find_last_of('/');
    if (slash == std::string::npos || slash < searchStart) {
        return normalizedUrl + "/";
    }
    return normalizedUrl.substr(0, slash + 1);
}

std::string getHttpFilename(const std::string& url) {
    const std::string normalizedUrl = correctURL(url);
    const size_t slash = normalizedUrl.find_last_of('/');
    if (slash == std::string::npos) {
        return normalizedUrl;
    }
    return normalizedUrl.substr(slash + 1);
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        const size_t end = slash == std::string::npos ? path.size() : slash;
        if (end > start) {
            parts.push_back(path.substr(start, end - start));
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

std::string joinPathParts(const std::vector<std::string>& parts) {
    std::string result;
    for (const std::string& part : parts) {
        result += "/";
        result += part;
    }
    return result.empty() ? "/" : result;
}

std::string lexicalRelativePath(const std::string& targetPath, const std::string& baseDirectory) {
    std::vector<std::string> targetParts = splitPath(targetPath);
    std::vector<std::string> baseParts = splitPath(baseDirectory);
    while (!baseParts.empty() && baseParts.back().empty()) {
        baseParts.pop_back();
    }

    size_t common = 0;
    while (common < targetParts.size()
        && common < baseParts.size()
        && targetParts[common] == baseParts[common]) {
        ++common;
    }

    std::vector<std::string> relativeParts;
    for (size_t i = common; i < baseParts.size(); ++i) {
        relativeParts.push_back("..");
    }
    for (size_t i = common; i < targetParts.size(); ++i) {
        relativeParts.push_back(targetParts[i]);
    }

    if (relativeParts.empty()) {
        return std::string();
    }

    std::string result;
    for (size_t i = 0; i < relativeParts.size(); ++i) {
        if (i > 0) {
            result += "/";
        }
        result += relativeParts[i];
    }
    return result;
}

std::string extractHttpUrl(const std::string& path) {
    if (isHttpUrl(path)) {
        return path;
    }

    size_t pos = path.find("https:/");
    if (pos == std::string::npos) {
        pos = path.find("http:/");
    }
    if (pos == std::string::npos) {
        return path;
    }

    return correctURL(path.substr(pos));
}

std::string combineUrl(const std::string& baseUrl, const std::string& relativePath) {
    if (isHttpUrl(relativePath)) {
        return relativePath;
    }

    const std::string normalizedBaseUrl = correctURL(baseUrl);

    // Step 1: Strip off the scheme
    auto schemeEnd = normalizedBaseUrl.find("://");
    if (schemeEnd == std::string::npos) {
        return normalizedBaseUrl + relativePath;
    }
    std::string scheme = normalizedBaseUrl.substr(0, schemeEnd + 3); // Include "://"
    std::string basePath = normalizedBaseUrl.substr(schemeEnd + 3);

    // Extract the domain
    auto pathStart = basePath.find('/');
    std::string domain = pathStart == std::string::npos
        ? basePath
        : basePath.substr(0, pathStart);
    std::string pathOnly = pathStart == std::string::npos
        ? "/"
        : basePath.substr(pathStart); // Path without the domain

    std::vector<std::string> parts = splitPath(pathOnly);
    if (!pathOnly.empty() && pathOnly.back() != '/' && !parts.empty()) {
        parts.pop_back();
    }

    for (const std::string& part : splitPath(relativePath)) {
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
            continue;
        }
        parts.push_back(part);
    }

    return scheme + domain + joinPathParts(parts);
}

HttpResolverCacheScope::~HttpResolverCacheScope() {
    Release();
}

void HttpResolverCacheScope::Release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    directories.clear();
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

std::string HttpResolverCacheScope::ResolveUrl(const std::string& url) {
    const std::string directory = getHttpDirectory(url);
    std::lock_guard<std::mutex> lock(mutex);
    if (released) return {};
    auto it = directories.find(directory);
    if (it == directories.end()) {
        // Keep six levels for authored parent traversal, within this URL's
        // namespace. Different absolute URL directories never share a cache.
        const std::string path = root + "/url-" +
            std::to_string(directories.size()) + "/1/1/1/1/1/1/";
        it = directories.emplace(directory, path).first;
    }
    return it->second + getHttpFilename(url);
}

std::string HttpResolverCacheScope::GetUrl(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& entry : directories) {
        const std::string& base = entry.second;
        const std::string urlRoot = base.substr(0, base.size() - 12);
        if (isPathInside(path, urlRoot)) {
            return combineUrl(entry.first, lexicalRelativePath(path, base));
        }
    }
    return {};
}

std::shared_ptr<HttpResolverCacheScope> HttpResolver::_NewScope() const {
    static std::atomic<uint64_t> nextId{0};
    const auto root = std::filesystem::temp_directory_path() /
        "hdEmscripten-http" / std::to_string(++nextId);
    auto scope = std::make_shared<HttpResolverCacheScope>(root.generic_string());
    std::lock_guard<std::mutex> lock(_scopeMutex);
    for (auto it = _scopes.begin(); it != _scopes.end();) {
        if (it->second.expired()) it = _scopes.erase(it);
        else ++it;
    }
    _scopes.emplace(scope->root, scope);
    return scope;
}

ArResolverContext HttpResolver::CreateScopedContext(const std::string& assetPath) const {
    return ArResolverContext(HttpResolverContext{_NewScope()},
        ArDefaultResolver::_CreateDefaultContextForAsset(assetPath));
}

std::shared_ptr<HttpResolverCacheScope> HttpResolver::_CurrentScope() const {
    if (const auto* context = _GetCurrentContextObject<HttpResolverContext>()) {
        if (context->scope) return context->scope;
    }
    // Calls outside a stage context retain a module-lifetime cache. URL
    // directories still have separate identities; driver-owned scopes do not
    // use or delete this cache.
    {
        std::lock_guard<std::mutex> lock(_scopeMutex);
        if (_unscoped) return _unscoped;
    }
    auto scope = _NewScope();
    std::lock_guard<std::mutex> lock(_scopeMutex);
    if (!_unscoped) _unscoped = scope;
    return _unscoped;
}

bool HttpResolver::_IsContextDependentPath(const std::string& path) const {
    return isHttpUrl(extractHttpUrl(path)) ||
        ArDefaultResolver::_IsContextDependentPath(path);
}

ArResolvedPath HttpResolver::_Resolve(const std::string& assetPath) const {
    std::string url = extractHttpUrl(assetPath);
    if (isHttpUrl(url)) {
        const std::string githubName = "github.com";
        const auto github = url.find(githubName);
        if (github != std::string::npos) {
            url.replace(github, githubName.size(), "raw.githubusercontent.com");
            const auto blob = url.find("/blob");
            if (blob != std::string::npos) url.erase(blob, 5);
        }
        return ArResolvedPath(_CurrentScope()->ResolveUrl(url));
    }
    // Anchored paths carry their scope, even without a context bound on this
    // thread (e.g. asynchronous texture requests). Never use another stage's
    // most recently opened URL as their base.
    if (!GetUrlForResolvedPath(assetPath).empty()) {
        return ArResolvedPath(assetPath);
    }
    return ArDefaultResolver::_Resolve(assetPath);
}

std::string HttpResolver::GetUrlForResolvedPath(const std::string& path) const {
    std::shared_ptr<HttpResolverCacheScope> scope;
    {
        std::lock_guard<std::mutex> lock(_scopeMutex);
        for (const auto& entry : _scopes) {
            if (isPathInside(path, entry.first)) {
                scope = entry.second.lock();
                break;
            }
        }
    }
    return scope ? scope->GetUrl(path) : std::string();
}

std::shared_ptr<ArAsset> HttpResolver::_OpenAsset(const ArResolvedPath& resolvedPath) const {
    const std::string path = resolvedPath.GetPathString();
    if (!std::filesystem::exists(path)) {
        const std::string route = GetUrlForResolvedPath(path);
        if (!route.empty() && FetchAndSaveAsset(route, path).empty()) {
            return nullptr;
        }
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
