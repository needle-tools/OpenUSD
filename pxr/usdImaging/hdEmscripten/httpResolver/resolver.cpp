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
    int length;
};

EM_JS(void, fetch_asset, (const char* route, int dataPtr), {
    const routeString = UTF8ToString(route);
    try {
        const request = new XMLHttpRequest();
        request.open('GET', routeString, false);
        request.overrideMimeType('text/plain; charset=x-user-defined');
        request.send(null);
        if (request.status !== 0 && (request.status < 200 || request.status >= 300)) {
            throw new Error('Fetch failed: ' + request.statusText);
        }
        const response = request.responseText;
        if (response == null) throw new Error('Fetch failed: empty response');
        const length = response.length;
        const ptr = _malloc(length);
        const heap8 = typeof GROWABLE_HEAP_U8 === 'function' ? GROWABLE_HEAP_U8() : HEAPU8;
        for (let i = 0; i < length; i++) {
            heap8[ptr + i] = response.charCodeAt(i) & 0xff;
        }
        const heap32 = typeof GROWABLE_HEAP_I32 === 'function' ? GROWABLE_HEAP_I32() : HEAP32;
        heap32[dataPtr >> 2] = ptr;
        heap32[(dataPtr >> 2) + 1] = length;
    } catch (err) {
        console.error("Error in fetch_asset: ", err);
        const heap32 = typeof GROWABLE_HEAP_I32 === 'function' ? GROWABLE_HEAP_I32() : HEAP32;
        heap32[dataPtr >> 2] = 0;
        heap32[(dataPtr >> 2) + 1] = 0;
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
            return filePath;
        }
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

void HttpResolver::setBaseUrl(const std::string& url) const {
    baseUrl = url;
}

void HttpResolver::setBaseTempDir(const std::string& tempDir) const {
    baseTempDir = tempDir;
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
    // Step 1: Strip off the scheme
    auto schemeEnd = baseUrl.find(":/");
    if (schemeEnd == std::string::npos) {
        return baseUrl + relativePath;
    }
    std::string scheme = baseUrl.substr(0, schemeEnd + 3); // Include "://"
    std::string basePath = baseUrl.substr(schemeEnd + 3);

    // Extract the domain
    auto pathStart = basePath.find('/');
    std::string domain = basePath.substr(0, pathStart);
    std::string pathOnly = basePath.substr(pathStart); // Path without the domain

    // Step 2: Use filesystem::path for manipulation
    std::filesystem::path pathObj = pathOnly;
    pathObj = pathObj.remove_filename(); // Ensure we're manipulating the directory part
    pathObj /= relativePath; // Append the relative path
    pathObj = pathObj.lexically_normal(); // Normalize the path (resolve "..", ".", etc.)

    // Step 3: Recombine
    std::string combinedUrl = scheme + domain + pathObj.string();

    return combinedUrl;
}

ArResolvedPath HttpResolver::_Resolve(const std::string& assetPath) const {
    if (verbose){
        std::cout << "_Resolve: " << assetPath << std::endl;
    }
    std::string stringAssetPathCopy = extractHttpUrl(assetPath);
    std::filesystem::path savedAssetFilePath = assetPath;
    if (std::filesystem::exists(assetPath)){
        if (verbose) {
            std::cout << "Already Exists: " << assetPath << std::endl;
        }
    }
    else if (isHttpUrl(stringAssetPathCopy)) {
        std::string githubName = "github.com";
        std::string rawGithubName = "raw.githubusercontent.com";
        std::string blob = "/blob";

        size_t pos = stringAssetPathCopy.find(githubName);
        if (pos!= std::string::npos) {
            stringAssetPathCopy.replace(pos, githubName.length(), rawGithubName);
        }

        size_t pos_blob = stringAssetPathCopy.find(blob);
        if (pos_blob!= std::string::npos) {
            stringAssetPathCopy.erase(pos_blob, blob.length());
        }

        std::filesystem::path fullHttpRouteAsPath = stringAssetPathCopy;
        std::filesystem::path rootHttpRouteAsPath = fullHttpRouteAsPath.parent_path();

        auto finalBaseUrl = rootHttpRouteAsPath.generic_string() + "/";
        if (verbose){
            std::cout << "http PATH: " << stringAssetPathCopy << std::endl;
            std::cout << "finalBaseUrl: " << finalBaseUrl << std::endl;
        }

        setBaseUrl(finalBaseUrl);

        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        // This path is chosen because if an asset is found with the path /../../../ it will go up the tmp directory structure
        // in the case of using /tmp/ then all relative paths greater than depth 1, will look the same. using 6 here is arbitrary,
        // is there a way to make this always work?
        setBaseTempDir(tempDir.generic_string() + "/1/1/1/1/1/1/");
        auto filePath = baseTempDir + fullHttpRouteAsPath.filename().generic_string();
        savedAssetFilePath = FetchAndSaveAsset(stringAssetPathCopy, filePath);
        resolvedRoutes[savedAssetFilePath.generic_string()] = stringAssetPathCopy;
    }
    else if (!baseUrl.empty()){
        std::filesystem::path systemPath = stringAssetPathCopy;
        std::filesystem::path relativePath = std::filesystem::relative(systemPath, baseTempDir);

        std::string route = combineUrl(baseUrl, relativePath);
        if (verbose){
            std::cout << "Relative Path before: " << relativePath << std::endl;
        }

        savedAssetFilePath = FetchAndSaveAsset(route, systemPath);
        resolvedRoutes[savedAssetFilePath.generic_string()] = route;
        if (verbose){
            std::cout << "Assumed to exist now, trying from baseUrl: " << systemPath << std::endl;
        }
    }
    else {
        return ArDefaultResolver::_Resolve(assetPath);
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

    const std::string path = resolvedPath.GetPathString();
    if (!std::filesystem::exists(path)) {
        std::string route;
        const auto routeIt = resolvedRoutes.find(path);
        if (routeIt != resolvedRoutes.end()) {
            route = routeIt->second;
        }
        else if (!baseUrl.empty() && !baseTempDir.empty()) {
            std::filesystem::path systemPath = path;
            std::filesystem::path relativePath = std::filesystem::relative(systemPath, baseTempDir);
            if (!relativePath.empty() && relativePath.native().rfind("..", 0) != 0) {
                route = combineUrl(baseUrl, relativePath.generic_string());
                resolvedRoutes[path] = route;
            }
        }

        if (!route.empty()) {
            FetchAndSaveAsset(route, path);
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
