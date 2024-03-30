// IMPORT THIRD-PARTY LIBRARIES
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <iostream>
#include <filesystem>
#include <fstream>

// IMPORT LOCAL LIBRARIES
#include "resolver.h"

PXR_NAMESPACE_OPEN_SCOPE

struct FetchUserData {
    std::string filePath;
};

AR_DEFINE_RESOLVER(HttpResolver, ArDefaultResolver);

HttpResolver::HttpResolver() : ArDefaultResolver() {}
HttpResolver::~HttpResolver() {}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *data) {
    data->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static void downloadSucceeded(emscripten_fetch_t *fetch) {
    std::cout << "Download succeeded." << std::endl;

    FetchUserData* userData = static_cast<FetchUserData*>(fetch->userData);

    bool verbose = true;
    if (verbose){
        std::cout << fetch->data << std::endl;
    }

    std::filesystem::path dirPath = std::filesystem::path(userData->filePath).parent_path();

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

    std::ofstream outFile(userData->filePath);
    if (outFile) {
        outFile << fetch->data;
        outFile.close();
        if (verbose){
            std::cout << "File written successfully." << std::endl;
        }
    } else {
        if (verbose){
            std::cout << "Failed to open file for writing." << std::endl;
        }
    }

    delete userData;
    emscripten_fetch_close(fetch);
}

static void downloadFailed(emscripten_fetch_t *fetch) {
    printf("Downloading %s failed, HTTP failure status code: %d.\n", fetch->url, fetch->status);
    FetchUserData* userData = static_cast<FetchUserData*>(fetch->userData);

    delete userData;
    emscripten_fetch_close(fetch);
}

std::filesystem::path HttpResolver::FetchAndDownloadAsset(const std::string& baseUrl,
                                                   const std::string& baseTempDir,
                                                   const std::string& relativePath) const{
    FetchUserData* userData = new FetchUserData();
    userData->filePath = baseTempDir + relativePath;
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.onsuccess = downloadSucceeded;
    attr.onerror = downloadFailed;
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.userData = (void*)userData;

    std::string route = baseUrl + relativePath;

    const char* url = route.c_str();

    if (verbose){
        std::cout << "Fetching from: " << url << std::endl;
    }

    emscripten_fetch(&attr, url);
    // TODO: change this to be an async call to JS?
    emscripten_sleep(500);

    auto filePath = baseTempDir + relativePath;

    // file already saved by now

    std::cout << "baseTempDir: " << baseTempDir << std::endl;
    std::cout << "relativePath: " << relativePath << std::endl;

    return filePath;
}

void HttpResolver::setBaseUrl(const std::string& url) const {
    baseUrl = url;
}

void HttpResolver::setBaseTempDir(const std::string& tempDir) const {
    baseTempDir = tempDir;
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
        setBaseTempDir(tempDir.generic_string() + "/");
        savedAssetFilePath = FetchAndDownloadAsset(baseUrl,
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
        std::filesystem::path relativePath = std::filesystem::relative(systemPath, baseTempDir);

        savedAssetFilePath = FetchAndDownloadAsset(baseUrl, baseTempDir, relativePath);
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
