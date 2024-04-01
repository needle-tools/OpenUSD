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

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *data) {
    data->append((char*)contents, size * nmemb);
    return size * nmemb;
}

EM_ASYNC_JS(int, fetch_asset, (const char *route), {
    const routeString = UTF8ToString(route);
    try {
        const response = await fetch(routeString);
        const assetContent = await response.text();
        const lengthBytes = lengthBytesUTF8(assetContent) + 1;
        const ptrToAssetContent = _malloc(lengthBytes);
        stringToUTF8(assetContent, ptrToAssetContent, lengthBytes);
        return ptrToAssetContent;
    }
    catch(err){
        out("err: ", err);
        return 0;
    }
});

std::filesystem::path HttpResolver::FetchAndSaveAsset(const std::string& route,
                                                   const std::string& baseTempDir,
                                                   const std::string& relativePath) const {
    auto filePath = baseTempDir + relativePath;
    try {
        int ptrToAssetContent = fetch_asset(route.c_str());

        if (ptrToAssetContent == 0) {
            std::cerr << "Fetch failed or returned error." << std::endl;
            return filePath;
        }

        char *assetContentCString = reinterpret_cast<char *>(ptrToAssetContent);
        std::string assetContent = std::string(assetContentCString);
        saveAssetContentToFile(assetContent, filePath);

        // Free the allocated memory for the fetched text
        free(assetContentCString);
    }
    catch (const std::exception& e){
        std::cout << "Error: " << e.what() << std::endl;
        return filePath;
    }

    return filePath;
}

void HttpResolver::saveAssetContentToFile(const std::string& assetContent, const std::string& filePath) const {
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

    std::ofstream outFile(filePath);
    if (outFile) {
        outFile << assetContent;
        outFile.close();
        if (verbose){
            std::cout << "File written successfully." << std::endl;
        }
    } else {
        if (verbose){
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
    std::string stringAssetPathCopy = assetPath;
    std::filesystem::path savedAssetFilePath = assetPath;

    if (assetPath.find("http") != std::string::npos){
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

        if (verbose){
            std::cout << "http PATH: " << stringAssetPathCopy << std::endl;
        }

        std::filesystem::path fullHttpRouteAsPath = stringAssetPathCopy;
        std::filesystem::path rootHttpRouteAsPath = fullHttpRouteAsPath.parent_path();
        setBaseUrl(rootHttpRouteAsPath.generic_string() + "/");

        std::filesystem::path tempDir = std::filesystem::temp_directory_path();
        // This path is chosen because if an asset is found with the path /../../../ it will go up the tmp directory structure
        // in the case of using /tmp/ then all relative paths greater than depth 1, will look the same. using 6 here is arbitrary,
        // is there a way to make this always work?
        setBaseTempDir(tempDir.generic_string() + "/1/1/1/1/1/1/");
        savedAssetFilePath = FetchAndSaveAsset(assetPath,
                                                   baseTempDir,
                                                   fullHttpRouteAsPath.filename());

        return ArResolvedPath(savedAssetFilePath);
    }
    else if (std::filesystem::exists(assetPath)){
        if (verbose) {
            std::cout << "Already Exists: " << assetPath << std::endl;
        }
    }
    else {
        std::filesystem::path systemPath = stringAssetPathCopy;
        if (verbose){
            std::cout << "systemPath: " << systemPath << std::endl;
            std::cout << "baseTempDir: " << baseTempDir << std::endl;
        }
        std::filesystem::path relativePath = std::filesystem::relative(systemPath, baseTempDir);
        if (verbose){
            std::cout << "111111: " << relativePath << std::endl;
        }

        std::string route = combineUrl(baseUrl, relativePath);
        if (verbose){
            std::cout << "Relative Path before: " << relativePath << std::endl;
        }

        savedAssetFilePath = FetchAndSaveAsset(route, baseTempDir, relativePath);
        if (verbose){
            std::cout << "Assumed to exist now, trying from baseUrl: " << savedAssetFilePath << std::endl;
        }
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
