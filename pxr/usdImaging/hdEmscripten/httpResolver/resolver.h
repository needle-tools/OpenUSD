#pragma once

// IMPORT THIRD-PARTY LIBRARIES
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <emscripten/fetch.h>
#include <emscripten.h>
#include <filesystem>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <pxr/usd/ar/resolverContext.h>

PXR_NAMESPACE_OPEN_SCOPE

// A stage retains its cache through its resolver context. No consumer chooses
// cache paths or removes resolver files. Driver disposal explicitly releases
// its scope; the destructor also handles construction failures.
struct HttpResolverCacheScope {
    explicit HttpResolverCacheScope(std::string rootPath) : root(std::move(rootPath)) {}
    ~HttpResolverCacheScope();
    void Release();
    std::string ResolveUrl(const std::string& url);
    std::string GetUrl(const std::string& path) const;
    const std::string root;
    mutable std::mutex mutex;
    bool released = false;
    std::unordered_map<std::string, std::string> directories;
};

struct HttpResolverContext {
    std::shared_ptr<HttpResolverCacheScope> scope;
    bool operator==(const HttpResolverContext& other) const { return scope == other.scope; }
    bool operator<(const HttpResolverContext& other) const { return scope < other.scope; }
};
inline size_t hash_value(const HttpResolverContext& context) {
    return std::hash<const void*>()(context.scope.get());
}
AR_DECLARE_RESOLVER_CONTEXT(HttpResolverContext);

class HttpResolver : public ArDefaultResolver {
public:
    HttpResolver();
    ~HttpResolver();

    ArResolvedPath _Resolve(const std::string& path) const override;
    std::shared_ptr<ArAsset> _OpenAsset(const ArResolvedPath &resolvedPath) const override;
    ArResolvedPath _ResolveForNewAsset(const std::string &assetPath) const override;
    std::string GetUrlForResolvedPath(const std::string& resolvedPath) const;
    std::filesystem::path FetchAndSaveAsset(const std::string& baseUrl, const std::string& filePath) const;
    void saveBinaryAssetContentToFile(const char* assetContent, size_t length, const std::string& filePath) const;
    ArResolverContext CreateScopedContext(const std::string& assetPath) const;
    bool _IsContextDependentPath(const std::string& path) const override;
private:
    std::shared_ptr<HttpResolverCacheScope> _NewScope() const;
    std::shared_ptr<HttpResolverCacheScope> _CurrentScope() const;
    bool verbose = false;
    mutable std::mutex _scopeMutex;
    mutable std::unordered_map<std::string, std::weak_ptr<HttpResolverCacheScope>> _scopes;
    mutable std::shared_ptr<HttpResolverCacheScope> _unscoped;
};

PXR_NAMESPACE_CLOSE_SCOPE
