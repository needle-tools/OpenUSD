#pragma once

// IMPORT THIRD-PARTY LIBRARIES
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>

PXR_NAMESPACE_OPEN_SCOPE

class HttpResolver : public ArDefaultResolver {
public:
    HttpResolver();
    ~HttpResolver();

    ArResolvedPath _Resolve(const std::string& path) const override;
    std::shared_ptr<ArAsset> _OpenAsset(const pxrInternal_v0_23__pxrReserved__::ArResolvedPath &resolvedPath) const override;
    ArResolvedPath _ResolveForNewAsset(const std::string &assetPath) const override;
    void setBaseUrl(const std::string &url) const;
    void setBaseTempDir(const std::string &tempDir) const;
private:
    mutable std::string baseUrl;
    bool verbose = true;
    mutable std::string baseTempDir;
};

PXR_NAMESPACE_CLOSE_SCOPE
