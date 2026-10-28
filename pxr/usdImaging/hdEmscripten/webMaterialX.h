// Per-material resolver-backed MaterialX libraries for the web delegate.
#ifndef PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MATERIAL_X_H
#define PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MATERIAL_X_H
#include "pxr/imaging/hdMtlx/hdMtlx.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/usd/usdMtlx/utils.h"
#include <set>

PXR_NAMESPACE_OPEN_SCOPE

inline MaterialX::DocumentPtr
HdEmscriptenMaterialXLibraries(HdMaterialNetwork2& network)
{
    const auto standard = HdMtlxStdLibraries();
    MaterialX::DocumentPtr libraries;
    std::set<std::string> imported;
    for (auto& entry : network.nodes) {
        const auto& id = entry.second.nodeTypeId;
        if (standard->getNodeDef(id.GetString())) continue;
        const auto shader = SdrRegistry::GetInstance().GetShaderNodeByIdentifier(id);
        if (!shader || shader->GetSourceType() != TfToken("mtlx")) continue;
        const auto uri = shader->GetResolvedImplementationURI();
        if (uri.empty()) continue;
        if (imported.insert(uri).second) {
            const auto document = UsdMtlxGetDocument(uri);
            if (document) {
                if (!libraries) {
                    libraries = MaterialX::createDocument();
                    libraries->importLibrary(standard);
                }
                libraries->importLibrary(document);
            }
        }
        // Source-asset Sdr identifiers are generated. Normalize to the actual
        // nodedef so HdMtlx uses the data library rather than importing the
        // complete original scene graph into its generated material document.
        const auto implementation = shader->GetImplementationName();
        if (libraries && libraries->getNodeDef(implementation)) {
            entry.second.nodeTypeId = TfToken(implementation);
        }
    }
    return libraries ? libraries : standard;
}

PXR_NAMESPACE_CLOSE_SCOPE
#endif
