#ifndef PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_ADAPTIVE_SUBDIVISION_H
#define PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_ADAPTIVE_SUBDIVISION_H

// CPU limit-surface tessellation for the web delegate.
//
// Each non-hole base face is evaluated as an OpenSubdiv Bfr::Surface (the
// exact limit surface for catmullClark/loop/bilinear, including creases,
// corners, boundary and face-varying rules carried by the refiner options)
// and tessellated with Bfr::Tessellation. Tessellation rates are assigned
// per base *edge*, so both faces sharing an edge use the same rate, and the
// boundary points of each face are shared by index with its neighbours: the
// output is a single indexed triangle mesh without T-junctions or cracks.
//
// Rate semantics (camera independent): the limit curve of every base edge is
// sampled at five points; the edge rate is the total turning of the limit
// normal (or of the sampled chord, whichever is larger) divided by
// Settings::maxSegmentAngle, clamped to [1, Settings::maxEdgeRate]. When
// Settings::triangleBudget is non-zero, all rates are scaled down uniformly
// until the estimated triangle count fits the budget.

#include "pxr/pxr.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/types.h"
#include "pxr/imaging/pxOsd/refinerFactory.h"

#include <opensubdiv/bfr/parameterization.h>
#include <opensubdiv/bfr/refinerSurfaceFactory.h>
#include <opensubdiv/bfr/surface.h>
#include <opensubdiv/bfr/tessellation.h>
#include <opensubdiv/far/topologyLevel.h>
#include <opensubdiv/far/topologyRefiner.h>
#include <opensubdiv/sdc/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

class HdEmscriptenAdaptiveSubdivision
{
public:
    using Factory = OpenSubdiv::Bfr::RefinerSurfaceFactory<>;
    using Surface = OpenSubdiv::Bfr::Surface<float>;
    using PointDesc = Surface::PointDescriptor;
    using Parameterization = OpenSubdiv::Bfr::Parameterization;
    using Tessellation = OpenSubdiv::Bfr::Tessellation;
    using TopologyLevel = OpenSubdiv::Far::TopologyLevel;
    using ConstIndexArray = OpenSubdiv::Far::ConstIndexArray;

    struct Settings {
        // Upper bound on segments per base edge.
        int maxEdgeRate = 16;
        // Target limit-normal turning per segment (radians, 7.5 degrees).
        float maxSegmentAngle = 0.13089969f;
        // Upper bound on estimated triangles; 0 disables the budget.
        size_t triangleBudget = 0;
    };

    // Triangle count of a uniform refinement at refineLevel, materialized the
    // way the web delegate triangulates the last uniform level. Used as the
    // adaptive budget so adaptive never costs more than the matching uniform
    // detail level.
    static size_t EstimateUniformTriangles(
        OpenSubdiv::Far::TopologyRefiner const &refiner,
        int refineLevel)
    {
        TopologyLevel const &base = refiner.GetLevel(0);
        const bool loop =
            refiner.GetSchemeType() == OpenSubdiv::Sdc::SCHEME_LOOP;
        const int level = std::max(refineLevel, 1);
        const double quadsPerSubFace = std::pow(4.0, level - 1);
        double total = 0.0;
        for (int face = 0; face < base.GetNumFaces(); ++face) {
            if (base.IsFaceHole(face)) {
                continue;
            }
            const int size = base.GetFaceVertices(face).size();
            if (loop) {
                total += 4.0 * quadsPerSubFace;
            } else if (size == 4) {
                total += 8.0 * quadsPerSubFace;
            } else {
                total += 2.0 * size * quadsPerSubFace;
            }
        }
        return static_cast<size_t>(total);
    }

    bool IsValid() const { return static_cast<bool>(_factory); }
    size_t GetNumCoarseVertices() const { return _numCoarseVertices; }
    size_t GetNumPoints() const { return _pointOwnerCoord.size(); }
    size_t GetNumTriangles() const { return _triangleCoords.size() / 3; }
    std::vector<int> const &GetEdgeRates() const { return _edgeRates; }

    void Clear()
    {
        // The factory references the refiner; release it first.
        _factory.reset();
        _refiner.reset();
        _numCoarseVertices = 0;
        _numCoarseFaces = 0;
        _edgeRates.clear();
        _faceCoordOffsets.clear();
        _faceTriangleOffsets.clear();
        _coordUVs.clear();
        _coordPoints.clear();
        _pointOwnerCoord.clear();
        _triangleCoords.clear();
        _triangleCoarseFaces.clear();
    }

    // Build the tessellation pattern for an unrefined refiner. Rates are
    // measured from the given control points; the pattern is reused for
    // later point-only updates (animation, skinning) via EvaluatePositions.
    bool Build(PxOsdTopologyRefinerSharedPtr const &refiner,
               float const *points,
               size_t numPoints,
               Settings const &settings)
    {
        Clear();
        if (!refiner || !points || refiner->GetNumLevels() < 1) {
            return false;
        }
        TopologyLevel const &base = refiner->GetLevel(0);
        if (base.GetNumVertices() <= 0 ||
            numPoints < static_cast<size_t>(base.GetNumVertices())) {
            return false;
        }

        _refiner = refiner;
        _factory.reset(new Factory(*_refiner));
        _numCoarseVertices = static_cast<size_t>(base.GetNumVertices());
        _numCoarseFaces = base.GetNumFaces();

        _ComputeEdgeRates(points, settings);
        if (!_BuildPattern()) {
            Clear();
            return false;
        }
        return true;
    }

    // Display triangles in display-point indices, plus the coarse face that
    // produced each triangle.
    void GetTriangles(VtIntArray *faceVertexCounts,
                      VtIntArray *faceVertexIndices,
                      std::vector<int> *triangleCoarseFaces) const
    {
        const size_t numTriangles = GetNumTriangles();
        faceVertexCounts->assign(numTriangles, 3);
        faceVertexIndices->resize(_triangleCoords.size());
        for (size_t i = 0; i < _triangleCoords.size(); ++i) {
            (*faceVertexIndices)[i] = _coordPoints[_triangleCoords[i]];
        }
        *triangleCoarseFaces = _triangleCoarseFaces;
    }

    // Limit positions and limit normals (normalized Du x Dv, averaged over
    // the faces sharing a display point) for the given control points.
    bool EvaluatePositions(float const *points,
                           size_t numPoints,
                           VtVec3fArray *outPoints,
                           VtVec3fArray *outNormals) const
    {
        if (!_factory || !points || numPoints < _numCoarseVertices) {
            return false;
        }
        const size_t numDisplayPoints = GetNumPoints();
        outPoints->assign(numDisplayPoints, GfVec3f(0.0f));
        std::vector<GfVec3f> normalSums(numDisplayPoints, GfVec3f(0.0f));

        Surface surface;
        std::vector<float> patchPoints;
        for (int face = 0; face < _numCoarseFaces; ++face) {
            const int c0 = _faceCoordOffsets[face];
            const int c1 = _faceCoordOffsets[face + 1];
            if (c0 == c1 || !_factory->InitVertexSurface(face, &surface)) {
                continue;
            }
            patchPoints.resize(surface.GetNumPatchPoints() * 3);
            surface.PreparePatchPoints(
                points, PointDesc(3), patchPoints.data(), PointDesc(3));
            for (int c = c0; c < c1; ++c) {
                float p[3], du[3], dv[3];
                surface.Evaluate(&_coordUVs[2 * c], patchPoints.data(),
                    PointDesc(3), p, du, dv);
                const int point = _coordPoints[c];
                if (_pointOwnerCoord[point] == c) {
                    (*outPoints)[point] = GfVec3f(p[0], p[1], p[2]);
                }
                GfVec3f normal = GfCross(GfVec3f(du[0], du[1], du[2]),
                                         GfVec3f(dv[0], dv[1], dv[2]));
                const float length = normal.GetLength();
                if (length > 0.0f && std::isfinite(length)) {
                    normalSums[point] += normal / length;
                }
            }
        }

        // Derivatives can vanish at degenerate limit points; fall back to
        // area-weighted triangle normals there.
        std::vector<GfVec3f> fallback;
        for (size_t i = 0; i < numDisplayPoints; ++i) {
            if (normalSums[i].GetLength() <= 1e-6f) {
                fallback.assign(numDisplayPoints, GfVec3f(0.0f));
                for (size_t t = 0; t < GetNumTriangles(); ++t) {
                    const int a = _coordPoints[_triangleCoords[3 * t + 0]];
                    const int b = _coordPoints[_triangleCoords[3 * t + 1]];
                    const int c = _coordPoints[_triangleCoords[3 * t + 2]];
                    const GfVec3f n = GfCross(
                        (*outPoints)[b] - (*outPoints)[a],
                        (*outPoints)[c] - (*outPoints)[a]);
                    fallback[a] += n;
                    fallback[b] += n;
                    fallback[c] += n;
                }
                break;
            }
        }

        outNormals->resize(numDisplayPoints);
        for (size_t i = 0; i < numDisplayPoints; ++i) {
            GfVec3f normal = normalSums[i];
            if (normal.GetLength() <= 1e-6f && !fallback.empty()) {
                normal = fallback[i];
            }
            if (normal.Normalize() <= 0.0f) {
                normal = GfVec3f(0.0f, 0.0f, 1.0f);
            }
            (*outNormals)[i] = normal;
        }
        return true;
    }

    // Vertex or varying primvar evaluated at every display point. out must
    // hold GetNumPoints() * dimension floats.
    bool EvaluateVertexOrVarying(float const *values,
                                 size_t numValues,
                                 int dimension,
                                 bool varying,
                                 float *out) const
    {
        if (!_factory || !values || !out || dimension <= 0 ||
            numValues < _numCoarseVertices) {
            return false;
        }
        Surface surface;
        std::vector<float> patchPoints;
        const PointDesc desc(dimension);
        for (int face = 0; face < _numCoarseFaces; ++face) {
            const int c0 = _faceCoordOffsets[face];
            const int c1 = _faceCoordOffsets[face + 1];
            if (c0 == c1) {
                continue;
            }
            const bool initialized = varying
                ? _factory->InitVaryingSurface(face, &surface)
                : _factory->InitVertexSurface(face, &surface);
            if (!initialized) {
                continue;
            }
            patchPoints.resize(surface.GetNumPatchPoints() * dimension);
            surface.PreparePatchPoints(
                values, desc, patchPoints.data(), desc);
            for (int c = c0; c < c1; ++c) {
                const int point = _coordPoints[c];
                if (_pointOwnerCoord[point] != c) {
                    continue;
                }
                surface.Evaluate(&_coordUVs[2 * c], patchPoints.data(),
                    desc, out + static_cast<size_t>(point) * dimension);
            }
        }
        return true;
    }

    // Face-varying primvar evaluated per display triangle corner, using a
    // refiner built from the same topology plus one face-varying channel.
    // out must hold 3 * GetNumTriangles() * dimension floats. Values are
    // evaluated per coarse face, so UV seams stay discontinuous while the
    // position vertices remain shared.
    bool EvaluateFaceVarying(PxOsdTopologyRefinerSharedPtr const &fvarRefiner,
                             float const *values,
                             size_t numValues,
                             int dimension,
                             float *out) const
    {
        if (!_factory || !fvarRefiner || !values || !out || dimension <= 0 ||
            fvarRefiner->GetNumFVarChannels() < 1) {
            return false;
        }
        TopologyLevel const &base = fvarRefiner->GetLevel(0);
        if (base.GetNumFaces() != _numCoarseFaces ||
            static_cast<size_t>(base.GetNumFVarValues(0)) > numValues) {
            return false;
        }

        OpenSubdiv::Bfr::SurfaceFactory::Options options;
        options.SetDefaultFVarID(0);
        Factory fvarFactory(*fvarRefiner, options);

        Surface surface;
        std::vector<float> patchPoints;
        std::vector<float> coordValues;
        const PointDesc desc(dimension);
        for (int face = 0; face < _numCoarseFaces; ++face) {
            const int c0 = _faceCoordOffsets[face];
            const int c1 = _faceCoordOffsets[face + 1];
            const int t0 = _faceTriangleOffsets[face];
            const int t1 = _faceTriangleOffsets[face + 1];
            if (t0 == t1) {
                continue;
            }
            if (!fvarFactory.InitFaceVaryingSurface(face, &surface)) {
                return false;
            }
            patchPoints.resize(surface.GetNumPatchPoints() * dimension);
            surface.PreparePatchPoints(
                values, desc, patchPoints.data(), desc);
            coordValues.resize(static_cast<size_t>(c1 - c0) * dimension);
            for (int c = c0; c < c1; ++c) {
                surface.Evaluate(&_coordUVs[2 * c], patchPoints.data(), desc,
                    &coordValues[static_cast<size_t>(c - c0) * dimension]);
            }
            for (int t = t0; t < t1; ++t) {
                for (int corner = 0; corner < 3; ++corner) {
                    const int coord = _triangleCoords[3 * t + corner];
                    std::copy_n(
                        &coordValues[static_cast<size_t>(coord - c0) *
                            dimension],
                        dimension,
                        out + (static_cast<size_t>(3 * t + corner) *
                            dimension));
                }
            }
        }
        return true;
    }

private:
    static float _Angle(GfVec3f a, GfVec3f b)
    {
        const float la = a.Normalize();
        const float lb = b.Normalize();
        if (la <= 0.0f || lb <= 0.0f ||
            !std::isfinite(la) || !std::isfinite(lb)) {
            return 0.0f;
        }
        return std::acos(std::max(-1.0f, std::min(1.0f, GfDot(a, b))));
    }

    static int _MeasureEdgeRate(Surface const &surface,
                                float const *patchPoints,
                                int edge,
                                Settings const &settings)
    {
        constexpr int kSamples = 5;
        const Parameterization param = surface.GetParameterization();
        GfVec3f positions[kSamples];
        GfVec3f normals[kSamples];
        for (int k = 0; k < kSamples; ++k) {
            float uv[2];
            param.GetEdgeCoord(
                edge, static_cast<float>(k) / (kSamples - 1), uv);
            float p[3], du[3], dv[3];
            surface.Evaluate(uv, patchPoints, PointDesc(3), p, du, dv);
            positions[k] = GfVec3f(p[0], p[1], p[2]);
            normals[k] = GfCross(GfVec3f(du[0], du[1], du[2]),
                                 GfVec3f(dv[0], dv[1], dv[2]));
        }

        float normalTurn = 0.0f;
        for (int k = 0; k + 1 < kSamples; ++k) {
            normalTurn += _Angle(normals[k], normals[k + 1]);
        }
        float chordTurn = 0.0f;
        for (int k = 1; k + 1 < kSamples; ++k) {
            chordTurn += _Angle(positions[k] - positions[k - 1],
                                positions[k + 1] - positions[k]);
        }

        const float tolerance = settings.maxSegmentAngle > 0.0f
            ? settings.maxSegmentAngle : 0.13089969f;
        const float turn = std::max(normalTurn, chordTurn);
        int rate = 1;
        if (std::isfinite(turn)) {
            rate = static_cast<int>(std::ceil(turn / tolerance - 1e-3f));
        }
        return std::max(1, std::min(std::max(1, settings.maxEdgeRate), rate));
    }

    void _ComputeEdgeRates(float const *points, Settings const &settings)
    {
        TopologyLevel const &base = _refiner->GetLevel(0);
        _edgeRates.assign(base.GetNumEdges(), 0);

        Surface surface;
        std::vector<float> patchPoints;
        for (int face = 0; face < _numCoarseFaces; ++face) {
            ConstIndexArray faceEdges = base.GetFaceEdges(face);
            bool pending = false;
            for (int i = 0; i < faceEdges.size(); ++i) {
                pending |= _edgeRates[faceEdges[i]] == 0;
            }
            // Holes have no limit surface; their edges are rated by a
            // neighbouring face, or default to 1 when unused.
            if (!pending || !_factory->InitVertexSurface(face, &surface)) {
                continue;
            }
            patchPoints.resize(surface.GetNumPatchPoints() * 3);
            surface.PreparePatchPoints(
                points, PointDesc(3), patchPoints.data(), PointDesc(3));
            for (int i = 0; i < faceEdges.size(); ++i) {
                int &rate = _edgeRates[faceEdges[i]];
                if (rate == 0) {
                    rate = _MeasureEdgeRate(
                        surface, patchPoints.data(), i, settings);
                }
            }
        }
        for (int &rate : _edgeRates) {
            rate = std::max(rate, 1);
        }

        if (settings.triangleBudget == 0) {
            return;
        }
        for (int iteration = 0; iteration < 16; ++iteration) {
            const double estimate = _EstimateTriangles();
            if (estimate <= static_cast<double>(settings.triangleBudget)) {
                return;
            }
            const double scale = std::sqrt(
                static_cast<double>(settings.triangleBudget) / estimate);
            bool changed = false;
            for (int &rate : _edgeRates) {
                const int scaled = std::max(1,
                    static_cast<int>(std::floor(rate * scale)));
                changed |= scaled != rate;
                rate = scaled;
            }
            if (!changed) {
                return;
            }
        }
    }

    double _EstimateTriangles() const
    {
        TopologyLevel const &base = _refiner->GetLevel(0);
        const bool loop =
            _refiner->GetSchemeType() == OpenSubdiv::Sdc::SCHEME_LOOP;
        double total = 0.0;
        for (int face = 0; face < _numCoarseFaces; ++face) {
            if (base.IsFaceHole(face)) {
                continue;
            }
            ConstIndexArray edges = base.GetFaceEdges(face);
            const int size = edges.size();
            if (size == 4 && !loop) {
                const double u =
                    0.5 * (_edgeRates[edges[0]] + _edgeRates[edges[2]]);
                const double v =
                    0.5 * (_edgeRates[edges[1]] + _edgeRates[edges[3]]);
                total += 2.0 * u * v;
                continue;
            }
            double sum = 0.0;
            for (int i = 0; i < size; ++i) {
                sum += _edgeRates[edges[i]];
            }
            const double average = sum / std::max(size, 1);
            total += (size == 3 && loop)
                ? average * average
                : 0.5 * size * average * average;
        }
        return total;
    }

    int _NewPoint(int ownerCoord)
    {
        _pointOwnerCoord.push_back(ownerCoord);
        return static_cast<int>(_pointOwnerCoord.size()) - 1;
    }

    bool _BuildPattern()
    {
        TopologyLevel const &base = _refiner->GetLevel(0);
        std::vector<int> vertexPoints(base.GetNumVertices(), -1);
        std::vector<int> edgePointStarts(base.GetNumEdges(), -1);
        _faceCoordOffsets.assign(_numCoarseFaces + 1, 0);
        _faceTriangleOffsets.assign(_numCoarseFaces + 1, 0);

        Tessellation::Options tessOptions;
        tessOptions.SetFacetSize(3);
        const OpenSubdiv::Sdc::SchemeType scheme = _refiner->GetSchemeType();

        std::vector<int> rates;
        std::vector<int> facets;
        std::vector<float> coords;
        for (int face = 0; face < _numCoarseFaces; ++face) {
            _faceCoordOffsets[face] = static_cast<int>(_coordPoints.size());
            _faceTriangleOffsets[face] =
                static_cast<int>(_triangleCoords.size() / 3);
            if (!_factory->FaceHasLimitSurface(face)) {
                continue;
            }

            ConstIndexArray faceVertices = base.GetFaceVertices(face);
            ConstIndexArray faceEdges = base.GetFaceEdges(face);
            const int size = faceVertices.size();
            const Parameterization param(scheme, size);
            if (!param.IsValid()) {
                continue;
            }
            rates.resize(size);
            for (int i = 0; i < size; ++i) {
                rates[i] = _edgeRates[faceEdges[i]];
            }
            Tessellation tessellation(param, size, rates.data(), tessOptions);
            if (!tessellation.IsValid()) {
                continue;
            }

            const int numCoords = tessellation.GetNumCoords();
            const int numBoundary = tessellation.GetNumBoundaryCoords();
            int rateSum = 0;
            for (int rate : rates) {
                rateSum += rate;
            }
            if (rateSum != numBoundary) {
                return false;
            }
            coords.resize(2 * static_cast<size_t>(numCoords));
            tessellation.GetCoords(coords.data());
            const int coordBase = static_cast<int>(_coordPoints.size());
            _coordPoints.resize(coordBase + numCoords, -1);
            _coordUVs.insert(_coordUVs.end(), coords.begin(), coords.end());

            // Boundary coords run counter-clockwise from vertex 0: each
            // corner, then the interior points of the edge leaving it.
            int boundary = 0;
            for (int i = 0; i < size; ++i) {
                float cornerUV[2];
                param.GetVertexCoord(i, cornerUV);
                if (std::abs(coords[2 * boundary] - cornerUV[0]) > 1e-5f ||
                    std::abs(coords[2 * boundary + 1] - cornerUV[1]) > 1e-5f) {
                    // Point sharing relies on this boundary ordering.
                    return false;
                }
                int &cornerPoint = vertexPoints[faceVertices[i]];
                if (cornerPoint < 0) {
                    cornerPoint = _NewPoint(coordBase + boundary);
                }
                _coordPoints[coordBase + boundary++] = cornerPoint;

                const int edge = faceEdges[i];
                const int count = rates[i] - 1;
                if (count <= 0) {
                    continue;
                }
                ConstIndexArray edgeVertices = base.GetEdgeVertices(edge);
                const bool forward = edgeVertices[0] == faceVertices[i];
                const bool created = edgePointStarts[edge] < 0;
                if (created) {
                    edgePointStarts[edge] =
                        static_cast<int>(_pointOwnerCoord.size());
                    _pointOwnerCoord.resize(
                        _pointOwnerCoord.size() + count, -1);
                }
                for (int k = 0; k < count; ++k) {
                    const int point = edgePointStarts[edge] +
                        (forward ? k : count - 1 - k);
                    if (created) {
                        _pointOwnerCoord[point] = coordBase + boundary;
                    }
                    _coordPoints[coordBase + boundary++] = point;
                }
            }
            if (boundary != numBoundary) {
                return false;
            }
            for (int c = numBoundary; c < numCoords; ++c) {
                _coordPoints[coordBase + c] = _NewPoint(coordBase + c);
            }

            facets.resize(3 * static_cast<size_t>(tessellation.GetNumFacets()));
            tessellation.GetFacets(facets.data());
            for (int index : facets) {
                _triangleCoords.push_back(coordBase + index);
            }
            _triangleCoarseFaces.insert(_triangleCoarseFaces.end(),
                tessellation.GetNumFacets(), face);
        }
        _faceCoordOffsets[_numCoarseFaces] =
            static_cast<int>(_coordPoints.size());
        _faceTriangleOffsets[_numCoarseFaces] =
            static_cast<int>(_triangleCoords.size() / 3);
        return true;
    }

    PxOsdTopologyRefinerSharedPtr _refiner;
    std::unique_ptr<Factory> _factory;
    size_t _numCoarseVertices = 0;
    int _numCoarseFaces = 0;
    std::vector<int> _edgeRates;
    // Per coarse face: [offset, next offset) into coords / triangles.
    std::vector<int> _faceCoordOffsets;
    std::vector<int> _faceTriangleOffsets;
    // Per tessellation coord: (u,v) in its face and the shared display point.
    std::vector<float> _coordUVs;
    std::vector<int> _coordPoints;
    // Per display point: the coord whose evaluation defines it.
    std::vector<int> _pointOwnerCoord;
    // Per display triangle corner: tessellation coord.
    std::vector<int> _triangleCoords;
    std::vector<int> _triangleCoarseFaces;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_ADAPTIVE_SUBDIVISION_H
