#ifndef HD_EMSCRIPTEN_WEB_STAGE_EXPORT_H
#define HD_EMSCRIPTEN_WEB_STAGE_EXPORT_H

#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/ar/packageUtils.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/zipFile.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdUtils/dependencies.h"
#include "pxr/usd/usdUtils/usdzPackage.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

using WebExportProgress = std::function<void(std::string const&, size_t, size_t)>;

// Called on a pthread with the stage's resolver context bound. All USD work,
// including dirty layer export and localization, stays off the browser thread.
inline void WebExportStagePackage(UsdStageRefPtr const& stage,
    std::string const& directory, WebExportProgress const& progress)
{
    namespace fs = std::filesystem;
    struct Mount { std::string directory, first; };
    std::map<std::string, Mount> mounts;
    fs::create_directories(directory);
    auto isPackage = [](std::string const& path) {
        return TfStringToLower(fs::path(path).extension().string()) == ".usdz";
    };
    std::function<std::string(std::string const&)> unpack;
    unpack = [&](std::string const& path) -> std::string {
        const bool nested = ArIsPackageRelativePath(path);
        if (!nested && !isPackage(path)) return path;
        auto const parts = nested ? ArSplitPackageRelativePathOuter(path)
                                  : std::make_pair(path, std::string());
        auto found = mounts.find(parts.first);
        if (found == mounts.end()) {
            SdfZipFile archive = SdfZipFile::Open(parts.first);
            if (!archive) throw std::runtime_error("Cannot read USD package");
            Mount mount{directory + "/package-" + std::to_string(mounts.size()), {}};
            for (auto file = archive.begin(); file != archive.end(); ++file) {
                const std::string name = *file;
                if (name.empty() || name.back() == '/') continue;
                fs::path relative(name);
                if (relative.is_absolute() || name.find('\\') != std::string::npos)
                    throw std::runtime_error("Unsafe USD package path");
                for (auto const& part : relative) if (part == "..")
                    throw std::runtime_error("Unsafe USD package path");
                auto const info = file.GetFileInfo();
                if (info.compressionMethod || info.encrypted)
                    throw std::runtime_error("USDZ members must be stored and unencrypted");
                if (mount.first.empty()) mount.first = name;
                const fs::path target = fs::path(mount.directory) / relative;
                fs::create_directories(target.parent_path());
                std::ofstream output(target, std::ios::binary);
                output.write(file.GetFile(), info.size);
                if (!output) throw std::runtime_error("Cannot extract USD package member");
            }
            if (mount.first.empty()) throw std::runtime_error("Empty USD package");
            found = mounts.emplace(parts.first, std::move(mount)).first;
        }
        return unpack(found->second.directory + "/" +
                      (nested ? parts.second : found->second.first));
    };

    progress("preparing", 0, 0);
    SdfLayerHandle root = stage->GetRootLayer();
    std::string source = root->GetRealPath();
    if (source.empty()) source = root->GetIdentifier();
    if (ArIsPackageRelativePath(source) || isPackage(source)) {
        source = unpack(source);
        // UsdUtils otherwise copies existing packages verbatim, dropping edits
        // in their inner layers. Overlay live authored layers on private copies.
        for (SdfLayerHandle const& layer : stage->GetUsedLayers(true)) {
            if (!ArIsPackageRelativePath(layer->GetRealPath())) continue;
            if (!layer->Export(unpack(layer->GetRealPath())))
                throw std::runtime_error("Cannot export authored USD layer");
        }
        if (!root->Export(source)) throw std::runtime_error("Cannot export package root");
    }

    progress("dependencies", 0, 0);
    std::vector<SdfLayerRefPtr> layers;
    std::vector<std::string> assets, unresolved;
    if (!UsdUtilsComputeAllDependencies(SdfAssetPath(source), &layers, &assets, &unresolved)
        || !unresolved.empty()) throw std::runtime_error("Cannot package USD with unresolved dependencies");
    auto& resolver = ArGetResolver();
    size_t completed = 0;
    for (auto const& asset : assets) {
        auto const resolved = resolver.Resolve(asset);
        if (resolved.empty() || !resolver.OpenAsset(resolved))
            throw std::runtime_error("Cannot read package dependency: " + asset);
        progress("dependencies", ++completed, assets.size());
    }
    progress("packaging", 0, 0);
    if (!UsdUtilsCreateNewUsdzPackage(SdfAssetPath(source), directory + "/stage.usdz"))
        throw std::runtime_error("Could not package USD and its dependencies");
}

PXR_NAMESPACE_CLOSE_SCOPE
#endif
