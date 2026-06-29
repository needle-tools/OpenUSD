#ifndef PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_TESTDRIVER_H
#define PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_TESTDRIVER_H

/// \file usdImaging/emscripteTest/testdriver.h

#include "pxr/pxr.h"
#include "pxr/usdImaging/usdImaging/delegate.h"

#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/engine.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/rprim.h"
#include "pxr/imaging/hd/rprimCollection.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/ar/resolverContextBinder.h"
#include "pxr/usd/sdf/layerUtils.h"

#include "webRenderDelegate.h"
#include "pxr/imaging/hd/unitTestNullRenderPass.h"
#include <emscripten/bind.h>
#include "pxr/usd/usdSkel/bakeSkinning.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"

#include <algorithm>
#include <memory>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

using HdRenderPassSharedPtr = std::shared_ptr<HdRenderPass>;

/// A simple test task that just causes sync processing
class WebSyncTask final : public HdTask
{
public:
    WebSyncTask(HdRenderPassSharedPtr const &renderPass,
                        TfTokenVector const &renderTags)
        : HdTask(SdfPath::EmptyPath())
        , _renderPass(renderPass)
        , _renderTags(renderTags)
    {
    }

    virtual void Sync(HdSceneDelegate* delegate,
                      HdTaskContext* ctx,
                      HdDirtyBits* dirtyBits) override {
        _renderPass->Sync();

        *dirtyBits = HdChangeTracker::Clean;
    }

    virtual void Prepare(HdTaskContext* ctx,
                         HdRenderIndex* renderIndex) override {
    }

    virtual void Execute(HdTaskContext* ctx) override {
    }

    virtual const TfTokenVector &GetRenderTags() const override {
        return _renderTags;
    }

private:
    HdRenderPassSharedPtr _renderPass;
    TfTokenVector _renderTags;
};

/// \class HdWebSyncDriver
///
/// A driver that syncs to the Emscripten Web Renderer.
///
/// \note This test driver uses a Null render delegate, so
/// no images are produced.  It just syncs between Hydra and
/// a Web Renderer.
///
class HdWebSyncDriver final {
public:
    HdWebSyncDriver(emscripten::val renderDelegateInterface,
                                    std::string const& usdFilePath)
        : _engine()
        , _renderDelegate(renderDelegateInterface)
        , _renderIndex(nullptr)
        , _delegate(nullptr)
        , _geometryPass()
        , _stage()
    {
        HdRprimCollection collection = HdRprimCollection(
                HdTokens->geometry,
                HdReprSelector(HdReprTokens->smoothHull));

        _Init(UsdStage::Open(usdFilePath),
              collection,
              SdfPath::AbsoluteRootPath(),
              _GetDefaultRenderTags());
    }

    HdWebSyncDriver(emscripten::val renderDelegateInterface,
                                    UsdStageRefPtr const& usdStage)
        : _engine()
        , _renderDelegate(renderDelegateInterface)
        , _renderIndex(nullptr)
        , _delegate(nullptr)
        , _geometryPass()
        , _stage()
    {
        HdRprimCollection collection = HdRprimCollection(
                HdTokens->geometry,
                HdReprSelector(HdReprTokens->smoothHull));

        _Init(usdStage,
              collection,
              SdfPath::AbsoluteRootPath(),
              _GetDefaultRenderTags());
    }

    ~HdWebSyncDriver()
    {
        delete _delegate;
        delete _renderIndex;
    }

    void Draw() {
        if (!_stage || !_delegate || !_geometryPass) {
            return;
        }
        _delegate->ApplyPendingUpdates();
        HdTaskSharedPtrVector tasks = {
            std::make_shared<WebSyncTask>(_geometryPass, _renderTags)
        };
        _engine.Execute(&_delegate->GetRenderIndex(), &tasks);
    }

    void Repopulate() {
        if (!_stage) {
            return;
        }

        delete _delegate;
        _delegate = nullptr;
        delete _renderIndex;
        _renderIndex = nullptr;
        _geometryPass.reset();

        HdRprimCollection collection = HdRprimCollection(
                HdTokens->geometry,
                HdReprSelector(HdReprTokens->smoothHull));

        _Init(_stage,
              collection,
              SdfPath::AbsoluteRootPath(),
              _renderTags);
    }

    void getFile(std::string filename, emscripten::val callback) {
        if (!_stage) {
            callback(emscripten::val::undefined());
            return;
        }
        auto& resolver = ArGetResolver();
        ArResolverContextBinder binder(&resolver, _stage->GetPathResolverContext());

        auto openAsset = [&resolver](const std::string& path) {
            std::shared_ptr<ArAsset> asset =
                resolver.OpenAsset(ArResolvedPath(path));
            if (asset) {
                return asset;
            }

            ArResolvedPath resolvedPath = resolver.Resolve(path);
            if (!resolvedPath.empty()) {
                asset = resolver.OpenAsset(resolvedPath);
            }
            return asset;
        };

        std::shared_ptr<ArAsset> asset = openAsset(filename);
        if (!asset) {
            const std::string anchoredPath =
                SdfComputeAssetPathRelativeToLayer(
                    _stage->GetRootLayer(), filename);
            if (!anchoredPath.empty() && anchoredPath != filename) {
                asset = openAsset(anchoredPath);
            }
        }
        if (!asset) {
            callback(emscripten::val::undefined());
            return;
        }

        std::shared_ptr<const char> buffer = asset->GetBuffer();
        if (!buffer) {
            callback(emscripten::val::undefined());
            return;
        }

        size_t bufferSize = asset->GetSize();
        callback(emscripten::val(emscripten::typed_memory_view(bufferSize, buffer.get())));
    }
    void SetTime(double time) {
        if (!_delegate) {
            return;
        }
        _delegate->SetTime(time);
    }

    double GetTime() {
        if (!_delegate) {
            return 0.0;
        }
        return _delegate->GetTime().GetValue();
    }

    /// Marks an rprim in the RenderIndex as dirty with the given dirty flags.
    void MarkRprimDirty(SdfPath path, HdDirtyBits flag) {
        _delegate->GetRenderIndex().GetChangeTracker()
            .MarkRprimDirty(path, flag);
    }

    /// Returns the underlying delegate for this driver.
    UsdImagingDelegate& GetDelegate() {
        return *_delegate;
    }

    /// Returns the populated UsdStage for this driver.
    UsdStageRefPtr const& GetStage() {
        return _stage;
    }

    bool HasStage() const {
        return static_cast<bool>(_stage);
    }

    int GetStageUpAxis() const {
        if (!_stage) {
            return 'y';
        }
        const TfToken upAxis = UsdGeomGetStageUpAxis(_stage);
        return upAxis == UsdGeomTokens->z ? 'z' : 'y';
    }

    double GetStageStartTimeCode() const {
        return _stage ? _stage->GetStartTimeCode() : 0.0;
    }

    double GetStageEndTimeCode() const {
        return _stage ? _stage->GetEndTimeCode() : 0.0;
    }

    double GetStageTimeCodesPerSecond() const {
        return _stage ? _stage->GetTimeCodesPerSecond() : 24.0;
    }

    void SetIncludedPurposes(emscripten::val includedPurposes) {
        if (includedPurposes.isUndefined() || includedPurposes.isNull()) {
            _renderTags = _GetDefaultRenderTags();
            return;
        }

        TfTokenVector renderTags;
        const unsigned length =
            includedPurposes["length"].as<unsigned>();
        for (unsigned i = 0; i < length; ++i) {
            std::string purpose = includedPurposes[i].as<std::string>();
            TfToken renderTag = _PurposeToRenderTag(TfToken(purpose));
            if (std::find(renderTags.begin(), renderTags.end(), renderTag) ==
                    renderTags.end()) {
                renderTags.push_back(renderTag);
            }
        }

        _renderTags = renderTags.empty()
            ? _GetDefaultRenderTags()
            : renderTags;
    }

private:
    HdEngine _engine;
    WebRenderDelegate _renderDelegate;
    HdRenderIndex       *_renderIndex;
    UsdImagingDelegate  *_delegate;
    HdRenderPassSharedPtr _geometryPass;
    UsdStageRefPtr _stage;
    TfTokenVector _renderTags;

    static TfToken _PurposeToRenderTag(TfToken const &purpose) {
        if (purpose == UsdGeomTokens->default_) {
            return HdRenderTagTokens->geometry;
        }
        return purpose;
    }

    static TfTokenVector _GetDefaultRenderTags() {
        TfTokenVector renderTags;
        renderTags.push_back(HdRenderTagTokens->geometry);
        renderTags.push_back(UsdGeomTokens->render);
        return renderTags;
    }

    void _Init(UsdStageRefPtr const& usdStage,
               HdRprimCollection const &collection,
               SdfPath const &delegateId,
               TfTokenVector const &renderTags) {
        _renderIndex = HdRenderIndex::New(&_renderDelegate, HdDriverVector());
        TF_VERIFY(_renderIndex != nullptr);
        _delegate = new UsdImagingDelegate(_renderIndex, delegateId);
        _delegate->SetRefineLevelFallback(2);

        _stage = usdStage;
        if (!_stage) {
            TF_RUNTIME_ERROR("Failed to open USD stage for hdEmscripten driver");
            return;
        }

        UsdSkelBakeSkinning(_stage->Traverse());
        _delegate->Populate(_stage->GetPseudoRoot());

        _geometryPass = HdRenderPassSharedPtr(
                       new Hd_UnitTestNullRenderPass(_renderIndex, collection));

        _renderTags = renderTags;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif //PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_TESTDRIVER_H
