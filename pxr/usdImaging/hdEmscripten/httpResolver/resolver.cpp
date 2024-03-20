// IMPORT THIRD-PARTY LIBRARIES
#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <iostream>
#include <filesystem>
//#include <curl/curl.h>
#include <fstream>

// IMPORT LOCAL LIBRARIES
#include "resolver.h"

PXR_NAMESPACE_OPEN_SCOPE

AR_DEFINE_RESOLVER(HttpResolver, ArDefaultResolver);

HttpResolver::HttpResolver() : ArDefaultResolver() { 
    std::cout << "init assetresolver: " << std::endl;
    //curl_global_init(CURL_GLOBAL_ALL); 
    }
HttpResolver::~HttpResolver() { 
    //curl_global_cleanup(); 
    }

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *data) {
    data->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::filesystem::path FetchAndDownloadAsset(const std::string& baseUrl,
                                                   const std::string& baseTempDir,
                                                   const std::string& relativePath,
                                                   bool verbose) {

    auto filePath = baseTempDir + relativePath;


    return filePath;
}

// static std::filesystem::path FetchAndDownloadAsset(const std::string& baseUrl,
//                                                    const std::string& baseTempDir,
//                                                    const std::string& relativePath,
//                                                    bool verbose) {
//     CURL *curl;
//     CURLcode res;
//     std::string readBuffer;

//     curl = curl_easy_init();
//     auto filePath = baseTempDir + relativePath;


//     if(curl) {
//         std::string route = baseUrl + relativePath;
//         if (verbose){
//             std::cout << "Fetching from: " << route << std::endl;
//         }
//         curl_easy_setopt(curl, CURLOPT_URL, route.c_str());
//         curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
//         curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

//         res = curl_easy_perform(curl);

//         std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();

//         // Attempt to create the directory (and any necessary parent directories)
//         if (std::filesystem::create_directories(dirPath)) {
//             if (verbose){
//                 std::cout << "Directories created successfully: " << dirPath << std::endl;
//             }
//         } else {
//             if (verbose){
//                 std::cout << "Directories already exist or cannot be created.\n";
//             }
//         }

//         if(res != CURLE_OK)
//             std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
//         else{
//             if (verbose){
//                 std::cout << readBuffer << std::endl;
//             }
//             std::ofstream outFile(filePath);
//             if (outFile) { // Check if the file was successfully opened
//                 outFile << readBuffer; // Write the string to the file
//                 outFile.close(); // Close the file
//                 if (verbose){
//                     std::cout << "File written successfully." << filePath << std::endl;
//                 }
//             } else {
//                 if (verbose){
//                     std::cout << "Failed to open file for writing." << std::endl;
//                 }
//             }
//         }

//         curl_easy_cleanup(curl);
//     }

//     return filePath;
// }

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
        setBaseTempDir(tempDir.generic_string());

        savedAssetFilePath = FetchAndDownloadAsset(baseUrl,
                                                   baseTempDir,
                                                   fullHttpRouteAsPath.filename(),
                                                   verbose);

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

        savedAssetFilePath = FetchAndDownloadAsset(baseUrl, baseTempDir, relativePath, verbose);
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
