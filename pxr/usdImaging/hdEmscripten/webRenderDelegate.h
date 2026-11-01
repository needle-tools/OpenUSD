#ifndef PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_RENDER_DELEGATE_H
#define PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/instancer.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/enums.h"
#include "pxr/imaging/hd/vertexAdjacency.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4i.h"

#include <emscripten/bind.h>
#include <emscripten/threading.h>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

void runInMainThread(std::function<void()> fun);
void runInMainThreadAsync(std::function<void()> fun);

class WebRenderDelegate final : public HdRenderDelegate
{
public:
    WebRenderDelegate(emscripten::val renderDelegateInterface) :
             _renderDelegateInterface(renderDelegateInterface)
    {

    };
    virtual ~WebRenderDelegate() = default;

    void SetStage(UsdStageRefPtr const &stage) { _stage = stage; }
    UsdStageRefPtr const &GetStage() const { return _stage; }
    void SetTime(UsdTimeCode time) { _time = time; }
    UsdTimeCode GetTime() const { return _time; }
    void TrackMdlMaterial(SdfPath const &id, bool timeVarying) {
        std::lock_guard<std::mutex> lock(_mdlMaterialMutex);
        _mdlMaterials[id] = timeVarying;
    }
    void ForgetMdlMaterial(SdfPath const &id) {
        std::lock_guard<std::mutex> lock(_mdlMaterialMutex);
        _mdlMaterials.erase(id);
    }
    std::map<SdfPath, bool> GetMdlMaterials() const {
        std::lock_guard<std::mutex> lock(_mdlMaterialMutex);
        return _mdlMaterials;
    }
    // Refined meshes use Bfr limit-surface tessellation instead of uniform
    // refinement. Callers must mark rprim display styles dirty on change.
    void SetAdaptiveSubdivision(bool enabled) { _adaptiveSubdivision = enabled; }
    bool GetAdaptiveSubdivision() const { return _adaptiveSubdivision; }

    virtual const TfTokenVector &GetSupportedRprimTypes() const override;
    virtual const TfTokenVector &GetSupportedSprimTypes() const override;
    virtual const TfTokenVector &GetSupportedBprimTypes() const override;
    virtual HdRenderParam *GetRenderParam() const override;
    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;
    virtual TfTokenVector GetMaterialRenderContexts() const override;
    virtual TfTokenVector GetShaderSourceTypes() const override;
    virtual TfTokenVector GetShadingSystems() const override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Renderpass factory
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                HdRprimCollection const& collection) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Instancer Factory
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                         SdfPath const& id) override;

    virtual void DestroyInstancer(HdInstancer *instancer) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Prim Factories
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual HdRprim *CreateRprim(TfToken const& typeId,
                                 SdfPath const& rprimId) override;

    virtual void DestroyRprim(HdRprim *rPrim) override;

    virtual HdSprim *CreateSprim(TfToken const& typeId,
                                 SdfPath const& sprimId) override;

    virtual HdSprim *CreateFallbackSprim(TfToken const& typeId) override;
    virtual void DestroySprim(HdSprim *sprim) override;

    virtual HdBprim *CreateBprim(TfToken const& typeId,
                                 SdfPath const& bprimId) override;

    virtual HdBprim *CreateFallbackBprim(TfToken const& typeId) override;

    virtual void DestroyBprim(HdBprim *bprim) override;

    ////////////////////////////////////////////////////////////////////////////
    ///
    /// Sync, Execute & Dispatch Hooks
    ///
    ////////////////////////////////////////////////////////////////////////////

    virtual void CommitResources(HdChangeTracker *tracker) override;
    void UpdateVolumeDependencies(HdRenderIndex* index);

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    WebRenderDelegate(
                                const WebRenderDelegate &) = delete;
    WebRenderDelegate &operator =(
                                const WebRenderDelegate &) = delete;

    emscripten::val _renderDelegateInterface;
    UsdStageRefPtr _stage;
    UsdTimeCode _time = UsdTimeCode::Default();
    mutable std::mutex _mdlMaterialMutex;
    std::map<SdfPath, bool> _mdlMaterials;
    std::atomic<bool> _adaptiveSubdivision{false};
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_RENDER_DELEGATE_H
