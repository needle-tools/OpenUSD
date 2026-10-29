// Focused native test: compile against an installed OpenUSD (pxOsd, vt, gf,
// tf and OpenSubdiv osdCPU with Bfr); no wasm rebuild.
#include "../webAdaptiveSubdivision.h"
#include "pxr/imaging/pxOsd/meshTopology.h"
#include "pxr/imaging/pxOsd/subdivTags.h"
#include "pxr/imaging/pxOsd/tokens.h"
// Checks call the code under test; keep them in release configurations.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <utility>

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
            uvs[2 * (4 * face + i) + 0] = corners[i][0];
            uvs[2 * (4 * face + i) + 1] = corners[i][1];
        }
    }
    PxOsdTopologyRefinerSharedPtr fvarRefiner = PxOsdRefinerFactory::Create(
        _Cube(PxOsdOpenSubdivTokens->catmullClark, holes),
        fvarTopologies, TfToken());
    std::vector<float> cornerUVs(6 * adaptive.GetNumTriangles());
    assert(adaptive.EvaluateFaceVarying(
        fvarRefiner, uvs.data(), uvs.size() / 2, 2, cornerUVs.data()));
    for (float value : cornerUVs) {
        assert(value >= -1e-5f && value <= 1.0f + 1e-5f);
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

} // namespace

int main()
{
    _TestCatmarkCube();
    _TestHolesAndFaceVarying();
    _TestLoopTetrahedron();
    _TestFlatBilinearGrid();
    std::cout << "testWebAdaptiveSubdivision OK" << std::endl;
    return 0;
}
