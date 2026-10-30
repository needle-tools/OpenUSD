// Focused native test: compile against an installed OpenUSD (pxOsd, vt, gf,
// tf and OpenSubdiv osdCPU with Bfr); no wasm rebuild.
#include "../webAdaptiveSubdivision.h"
#include "../webMeshInvalidation.h"
#include "pxr/imaging/pxOsd/meshTopology.h"
#include "pxr/imaging/pxOsd/subdivTags.h"
#include "pxr/imaging/pxOsd/tokens.h"
#include <opensubdiv/far/primvarRefiner.h>
// Checks call the code under test; keep them in release configurations.
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using Adaptive = HdEmscriptenAdaptiveSubdivision;

PxOsdMeshTopology
_Cube(TfToken const &scheme, VtIntArray const &holes = VtIntArray())
{
    const VtIntArray counts = {4, 4, 4, 4, 4, 4};
    const VtIntArray indices = {
        0, 1, 3, 2,  2, 3, 5, 4,  4, 5, 7, 6,
        6, 7, 1, 0,  1, 7, 5, 3,  6, 0, 2, 4};
    return PxOsdMeshTopology(
        scheme, PxOsdOpenSubdivTokens->rightHanded, counts, indices, holes);
}

const std::vector<float> &
_CubePoints()
{
    static const std::vector<float> points = {
        -0.5f, -0.5f,  0.5f,   0.5f, -0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,   0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f, -0.5f,   0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,   0.5f, -0.5f, -0.5f};
    return points;
}

// Every undirected display edge must be used exactly twice with opposite
// winding on a closed surface: shared edge points, no cracks or T-junctions.
void
_AssertClosedAndOriented(VtIntArray const &indices)
{
    std::map<std::pair<int, int>, int> directed;
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        for (int i = 0; i < 3; ++i) {
            const int a = indices[t + i];
            const int b = indices[t + (i + 1) % 3];
            assert(a != b);
            ++directed[std::make_pair(a, b)];
        }
    }
    for (auto const &entry : directed) {
        assert(entry.second == 1);
        auto const reverse =
            directed.find(std::make_pair(entry.first.second, entry.first.first));
        assert(reverse != directed.end() && reverse->second == 1);
    }
}

void
_TestCatmarkCube()
{
    PxOsdTopologyRefinerSharedPtr refiner = PxOsdRefinerFactory::Create(
        _Cube(PxOsdOpenSubdivTokens->catmullClark), TfToken());
    assert(refiner);

    Adaptive::Settings settings;
    settings.maxEdgeRate = 16;
    Adaptive adaptive;
    assert(adaptive.Build(refiner, _CubePoints().data(), 8, settings));
    for (int rate : adaptive.GetEdgeRates()) {
        // A cube cage turns 90 degrees along every limit edge.
        assert(rate > 1 && rate <= 16);
    }

    VtIntArray counts, indices;
    std::vector<int> coarseFaces;
    adaptive.GetTriangles(&counts, &indices, &coarseFaces);
    assert(counts.size() == adaptive.GetNumTriangles());
    assert(coarseFaces.size() == adaptive.GetNumTriangles());
    _AssertClosedAndOriented(indices);

    VtVec3fArray points, normals;
    assert(adaptive.EvaluatePositions(
        _CubePoints().data(), 8, &points, &normals));
    assert(points.size() == adaptive.GetNumPoints());
    for (size_t i = 0; i < points.size(); ++i) {
        // Limit surface lies strictly inside the cage and normals face out.
        assert(std::abs(points[i][0]) < 0.5f);
        assert(GfDot(normals[i], points[i]) > 0.0f);
        assert(std::abs(normals[i].GetLength() - 1.0f) < 1e-4f);
    }

    // Vertex primvar evaluation reproduces positions exactly.
    std::vector<float> evaluated(3 * adaptive.GetNumPoints());
    assert(adaptive.EvaluateVertexOrVarying(
        _CubePoints().data(), 8, 3, false, evaluated.data()));
    for (size_t i = 0; i < points.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            assert(std::abs(evaluated[3 * i + k] - points[i][k]) < 1e-6f);
        }
    }

    // Budget: never more than the matching uniform level.
    Adaptive::Settings bounded = settings;
    bounded.triangleBudget = Adaptive::EstimateUniformTriangles(*refiner, 1);
    Adaptive cheap;
    assert(cheap.Build(refiner, _CubePoints().data(), 8, bounded));
    assert(cheap.GetNumTriangles() <= bounded.triangleBudget);
    assert(cheap.GetNumTriangles() < adaptive.GetNumTriangles());
}

void
_TestLeftHandedNormals()
{
    const auto cube = _Cube(PxOsdOpenSubdivTokens->catmullClark);
    const PxOsdMeshTopology left(cube.GetScheme(), PxOsdOpenSubdivTokens->leftHanded,
        cube.GetFaceVertexCounts(), cube.GetFaceVertexIndices());
    auto refiner = PxOsdRefinerFactory::Create(left, TfToken());
    Adaptive adaptive;
    assert(adaptive.Build(refiner, _CubePoints().data(), 8, Adaptive::Settings()));
    VtVec3fArray points, normals, cornerNormals;
    assert(adaptive.EvaluatePositions(_CubePoints().data(), 8, &points, &normals, &cornerNormals));
    // Reversing the authored orientation must reverse surface winding/normals.
    for (size_t i=0;i<points.size();++i) assert(GfDot(normals[i], points[i]) < 0.0f);
}

void
_TestHolesAndFaceVarying()
{
    const VtIntArray holes = {2};
    PxOsdTopologyRefinerSharedPtr refiner = PxOsdRefinerFactory::Create(
        _Cube(PxOsdOpenSubdivTokens->catmullClark, holes), TfToken());
    Adaptive adaptive;
    assert(adaptive.Build(
        refiner, _CubePoints().data(), 8, Adaptive::Settings()));

    VtIntArray counts, indices;
    std::vector<int> coarseFaces;
    adaptive.GetTriangles(&counts, &indices, &coarseFaces);
    assert(!coarseFaces.empty());
    for (int face : coarseFaces) {
        assert(face != 2);
    }

    // Per-face-unique UVs: every coarse edge is a seam. Values must stay in
    // their face's [0,1] chart while positions remain shared.
    std::vector<VtIntArray> fvarTopologies(1);
    fvarTopologies[0].resize(24);
    std::vector<float> uvs(48);
    for (int face = 0; face < 6; ++face) {
        const float corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 4; ++i) {
            fvarTopologies[0][4 * face + i] = 4 * face + i;
            uvs[2 * (4 * face + i) + 0] = 2 * face + corners[i][0];
            uvs[2 * (4 * face + i) + 1] = corners[i][1];
        }
    }
    PxOsdTopologyRefinerSharedPtr fvarRefiner = PxOsdRefinerFactory::Create(
        _Cube(PxOsdOpenSubdivTokens->catmullClark, holes),
        fvarTopologies, TfToken());
    std::vector<float> cornerUVs(6 * adaptive.GetNumTriangles());
    assert(adaptive.EvaluateFaceVarying(
        fvarRefiner, uvs.data(), uvs.size() / 2, 2, cornerUVs.data()));
    for (size_t triangle = 0; triangle < coarseFaces.size(); ++triangle) {
        for (int corner = 0; corner < 3; ++corner) {
            const float u = cornerUVs[6*triangle+2*corner];
            const float v = cornerUVs[6*triangle+2*corner+1];
            assert(u >= 2*coarseFaces[triangle]-1e-5f && u <= 2*coarseFaces[triangle]+1+1e-5f);
            assert(v >= -1e-5f && v <= 1+1e-5f);
        }
    }
}

void
_TestLoopTetrahedron()
{
    const VtIntArray counts = {3, 3, 3, 3};
    const VtIntArray indices = {0, 1, 2,  0, 3, 1,  1, 3, 2,  2, 3, 0};
    const std::vector<float> points = {
        1, 1, 1,  -1, -1, 1,  -1, 1, -1,  1, -1, -1};
    PxOsdTopologyRefinerSharedPtr refiner = PxOsdRefinerFactory::Create(
        PxOsdMeshTopology(PxOsdOpenSubdivTokens->loop,
            PxOsdOpenSubdivTokens->rightHanded, counts, indices),
        TfToken());
    Adaptive adaptive;
    assert(adaptive.Build(refiner, points.data(), 4, Adaptive::Settings()));
    VtIntArray displayCounts, displayIndices;
    std::vector<int> coarseFaces;
    adaptive.GetTriangles(&displayCounts, &displayIndices, &coarseFaces);
    _AssertClosedAndOriented(displayIndices);
}

void
_TestFlatBilinearGrid()
{
    const VtIntArray counts = {4, 4};
    const VtIntArray indices = {0, 1, 4, 3,  1, 2, 5, 4};
    const std::vector<float> points = {
        0, 0, 0,  1, 0, 0,  2, 0, 0,  0, 1, 0,  1, 1, 0,  2, 1, 0};
    PxOsdTopologyRefinerSharedPtr refiner = PxOsdRefinerFactory::Create(
        PxOsdMeshTopology(PxOsdOpenSubdivTokens->bilinear,
            PxOsdOpenSubdivTokens->rightHanded, counts, indices),
        TfToken());
    Adaptive adaptive;
    assert(adaptive.Build(refiner, points.data(), 6, Adaptive::Settings()));
    // Flat input needs no subdivision: two triangles per quad, shared edge.
    assert(adaptive.GetNumTriangles() == 4);
    assert(adaptive.GetNumPoints() == 6);
}

// Every cube edge creased with the given sharpness.
PxOsdMeshTopology
_CreasedCube(float sharpness)
{
    PxOsdMeshTopology topology = _Cube(PxOsdOpenSubdivTokens->catmullClark);
    auto initial = PxOsdRefinerFactory::Create(topology, TfToken());
    const auto &base = initial->GetLevel(0);
    VtIntArray creaseIndices;
    for (int edge = 0; edge < base.GetNumEdges(); ++edge) {
        const auto vertices = base.GetEdgeVertices(edge);
        creaseIndices.push_back(vertices[0]);
        creaseIndices.push_back(vertices[1]);
    }
    PxOsdSubdivTags tags;
    tags.SetCreaseIndices(creaseIndices);
    tags.SetCreaseLengths(VtIntArray(base.GetNumEdges(), 2));
    tags.SetCreaseWeights(VtFloatArray(base.GetNumEdges(), sharpness));
    return topology.WithSubdivTags(tags);
}

void
_TestSharpCreaseNormals()
{
    const auto refiner = PxOsdRefinerFactory::Create(_CreasedCube(10.0f), TfToken());
    Adaptive adaptive;
    assert(adaptive.Build(refiner, _CubePoints().data(), 8, Adaptive::Settings()));
    VtVec3fArray points, vertexNormals, cornerNormals;
    assert(adaptive.EvaluatePositions(_CubePoints().data(), 8, &points, &vertexNormals, &cornerNormals));
    VtIntArray counts, indices;
    std::vector<int> coarseFaces;
    adaptive.GetTriangles(&counts, &indices, &coarseFaces);
    assert(cornerNormals.size() == indices.size());
    for (size_t t = 0; t < coarseFaces.size(); ++t) {
        GfVec3f expected = GfCross(points[indices[3*t+1]]-points[indices[3*t]],
                                   points[indices[3*t+2]]-points[indices[3*t]]);
        expected.Normalize();
        for (int corner = 0; corner < 3; ++corner) {
            assert(GfDot(expected, cornerNormals[3*t+corner]) > .999f);
        }
    }
}

void
_TestAsymmetricBudget()
{
    auto points = _CubePoints();
    points[0] -= 1.5f; points[10] += 1.0f; points[17] += .7f;
    const auto refiner = PxOsdRefinerFactory::Create(_Cube(PxOsdOpenSubdivTokens->catmullClark), TfToken());
    for (size_t budget : {size_t(24), size_t(48), size_t(96), size_t(160)}) {
        Adaptive::Settings settings;
        settings.maxEdgeRate = 16; settings.triangleBudget = budget;
        Adaptive adaptive;
        assert(adaptive.Build(refiner, points.data(), 8, settings));
        assert(adaptive.GetNumTriangles() <= budget);
        VtIntArray counts, indices; std::vector<int> faces;
        adaptive.GetTriangles(&counts, &indices, &faces);
        _AssertClosedAndOriented(indices);
    }
}

void
_TestMeshInvalidation()
{
    using Invalidation = HdEmscriptenMeshInvalidation;
    assert(!Invalidation().DisplayTopologyDirty());
    assert(Invalidation().GeomSubsetsDirty(true));

    // Uniform points-only frames keep refinement, triangles and sections.
    Invalidation uniformPoints;
    uniformPoints.points = true;
    assert(!uniformPoints.DisplayTopologyDirty());
    assert(!uniformPoints.GeomSubsetsDirty(false));

    // Adaptive patterns follow the points; sections index their triangles.
    Invalidation adaptivePoints = uniformPoints;
    adaptivePoints.adaptive = true;
    assert(adaptivePoints.DisplayTopologyDirty());
    assert(adaptivePoints.GeomSubsetsDirty(false));

    // Animated tags and holes, detail level and repr rebuild either path.
    for (bool adaptive : {false, true}) {
        for (int flag = 0; flag < 4; ++flag) {
            Invalidation invalidation;
            invalidation.adaptive = adaptive;
            invalidation.topology = flag == 0;
            invalidation.subdivTags = flag == 1;
            invalidation.displayStyle = flag == 2;
            invalidation.repr = flag == 3;
            assert(invalidation.DisplayTopologyDirty());
            assert(invalidation.GeomSubsetsDirty(false));
        }
    }
}

struct _Vertex {
    float p[3] = {0.0f, 0.0f, 0.0f};
    void Clear(void * = nullptr) { p[0] = p[1] = p[2] = 0.0f; }
    void AddWithWeight(_Vertex const &src, float weight) {
        for (int k = 0; k < 3; ++k) p[k] += src.p[k] * weight;
    }
};

std::vector<float>
_RefineUniform(PxOsdTopologyRefinerSharedPtr const &refiner,
               std::vector<float> const &points, int level)
{
    std::vector<_Vertex> vertices(refiner->GetNumVerticesTotal());
    for (int i = 0; i < refiner->GetLevel(0).GetNumVertices(); ++i) {
        std::copy_n(&points[3 * i], 3, vertices[i].p);
    }
    OpenSubdiv::Far::PrimvarRefiner primvarRefiner(*refiner);
    _Vertex *src = vertices.data();
    for (int l = 1; l <= level; ++l) {
        _Vertex *dst = src + refiner->GetLevel(l - 1).GetNumVertices();
        primvarRefiner.Interpolate(l, src, dst);
        src = dst;
    }
    std::vector<float> refined;
    for (int i = 0; i < refiner->GetLevel(level).GetNumVertices(); ++i) {
        refined.insert(refined.end(), src[i].p, src[i].p + 3);
    }
    return refined;
}

PxOsdTopologyRefinerSharedPtr
_UniformRefiner(PxOsdMeshTopology const &topology, int level)
{
    auto refiner = PxOsdRefinerFactory::Create(topology, TfToken());
    OpenSubdiv::Far::TopologyRefiner::UniformOptions options(level);
    options.fullTopologyInLastLevel = true;
    refiner->RefineUniform(options);
    return refiner;
}

// Points-only frames reuse the uniform refiner: its weights depend only on
// topology and tags, so a kept refiner refines deformed points exactly like
// a new one, while animated crease sharpness must rebuild it.
void
_TestUniformRefinerReuseAcrossPoints()
{
    const int level = 2;
    const auto kept = _UniformRefiner(_CreasedCube(0.5f), level);
    auto deformed = _CubePoints();
    deformed[0] -= 0.3f; deformed[13] += 0.4f; deformed[23] -= 0.2f;
    assert(_RefineUniform(kept, deformed, level) ==
           _RefineUniform(_UniformRefiner(_CreasedCube(0.5f), level),
                          deformed, level));
    assert(_RefineUniform(kept, deformed, level) !=
           _RefineUniform(_UniformRefiner(_CreasedCube(4.0f), level),
                          deformed, level));
}

} // namespace

int main()
{
    _TestCatmarkCube();
    _TestLeftHandedNormals();
    _TestHolesAndFaceVarying();
    _TestLoopTetrahedron();
    _TestFlatBilinearGrid();
    _TestSharpCreaseNormals();
    _TestAsymmetricBudget();
    _TestMeshInvalidation();
    _TestUniformRefinerReuseAcrossPoints();
    std::cout << "testWebAdaptiveSubdivision OK" << std::endl;
    return 0;
}
