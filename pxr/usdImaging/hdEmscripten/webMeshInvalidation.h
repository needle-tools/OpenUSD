#ifndef PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MESH_INVALIDATION_H
#define PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MESH_INVALIDATION_H

#include "pxr/pxr.h"

PXR_NAMESPACE_OPEN_SCOPE

// Which derived mesh state a Sync must rebuild. Uniform refinement and
// triangulation depend only on topology, subdiv tags and refine level, so
// points-only frames keep them. Adaptive patterns are measured from the
// control points, so animated points also change the display triangles.
struct HdEmscriptenMeshInvalidation
{
    bool topology = false;
    bool subdivTags = false;
    bool displayStyle = false;
    bool repr = false;
    bool points = false;
    bool adaptive = false;

    bool DisplayTopologyDirty() const
    {
        return topology || subdivTags || displayStyle || repr ||
            (points && adaptive);
    }

    // GeomSubset sections index display triangles.
    bool GeomSubsetsDirty(bool materialIdDirty) const
    {
        return materialIdDirty || DisplayTopologyDirty();
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_MESH_INVALIDATION_H
