#ifndef PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_TESTDRIVER_H
#define PXR_USD_IMAGING_USD_IMAGING_EMSCRIPTEN_TESTDRIVER_H

/// \file usdImaging/emscripteTest/testdriver.h

#include "pxr/pxr.h"
#include "pxr/base/tf/weakBase.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/imaging/hd/material.h"
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
#include "httpResolver/resolver.h"

#include "webRenderDelegate.h"
#include "webImagingDelegate.h"
#include "webPhysics.h"
#include "webStageExport.h"
#include "pxr/imaging/hd/unitTestNullRenderPass.h"
#include <emscripten/bind.h>
#include "pxr/usd/usdSkel/bakeSkinning.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

PXR_NAMESPACE_OPEN_SCOPE

using HdRenderPassSharedPtr = std::shared_ptr<HdRenderPass>;

// Stage composition may dispatch TBB tasks that fetch HTTP layers. Waiting for
// those tasks on the browser thread blocks rendering and progress delivery.
// Keep the existing async binding contract, but yield while a worker owns USD.
// Callers serialize access to the stage for the duration of this operation.
template <class Operation>
auto WebRunComposition(Operation operation, bool asynchronous = true)
    -> decltype(operation()) {
    if (!asynchronous || !emscripten_is_main_runtime_thread()) return operation();
    std::packaged_task<decltype(operation())()> task(std::move(operation));
    auto result = task.get_future();
    std::thread(std::move(task)).detach();
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        emscripten_sleep(1);
    }
    return result.get();
}


class _HdEmscriptenSyncTimer {
public:
    explicit _HdEmscriptenSyncTimer(std::string label)
        : _label(std::move(label))
        , _start(std::chrono::steady_clock::now())
        , _enabled(_TimingLogsEnabled())
    {
        if (!_enabled) {
            return;
        }
        const std::string message =
            std::string("[hdEmscripten timing] begin ") + _label;
        emscripten_log(EM_LOG_CONSOLE, "%s", message.c_str());
    }

    ~_HdEmscriptenSyncTimer()
    {
        if (!_enabled) {
            return;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - _start).count();
        const std::string message =
            std::string("[hdEmscripten timing] end ") + _label + " " +
            std::to_string(elapsed) + "ms";
        emscripten_log(EM_LOG_CONSOLE, "%s", message.c_str());
    }

private:
    static bool _TimingLogsEnabled()
    {
        const char *value = std::getenv("HDEMSCRIPTEN_TIMING_LOGS");
        return value && value[0] && std::string(value) != "0";
    }

    std::string _label;
    std::chrono::steady_clock::time_point _start;
    bool _enabled;
};

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
        _HdEmscriptenSyncTimer timer("WebSyncTask::Sync");
        {
            _HdEmscriptenSyncTimer renderPassTimer("HdRenderPass::Sync");
            _renderPass->Sync();
        }

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
class HdWebSyncDriver final : public TfWeakBase {
public:
    HdWebSyncDriver(emscripten::val renderDelegateInterface,
                                    std::string const& usdFilePath)
        : _engine()
        , _renderDelegate(renderDelegateInterface)
        , _renderIndex(nullptr)
        , _delegate(nullptr)
        , _geometryPass()
        , _stage()
        , _complexity(1.0f)
    {
        HdRprimCollection collection = _GetCollectionForComplexity();

        auto* resolver = dynamic_cast<HttpResolver*>(&ArGetUnderlyingResolver());
        const auto context = resolver ? resolver->CreateScopedContext(usdFilePath)
                                      : ArGetResolver().CreateDefaultContextForAsset(usdFilePath);
        if (const auto* httpContext = context.Get<HttpResolverContext>()) {
            _httpCache = httpContext->scope;
        }
        _Init(UsdStage::Open(usdFilePath, context),
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
        , _complexity(1.0f)
    {
        HdRprimCollection collection = _GetCollectionForComplexity();
        if (usdStage) {
            if (const auto* context = usdStage->GetPathResolverContext().Get<HttpResolverContext>()) {
                _httpCache = context->scope;
            }
        }

        _Init(usdStage,
              collection,
              SdfPath::AbsoluteRootPath(),
              _GetDefaultRenderTags());
    }

    ~HdWebSyncDriver()
    {
        delete _delegate;
        delete _renderIndex;
        _stage.Reset();
        // USD may retain resolver contexts in its layer caches. Closing this
        // driver's private scope releases files immediately without touching
        // another driver; retained contexts no longer resolve its old routes.
        if (_httpCache) _httpCache->Release();
    }

    bool HasScopedHttpCache() const { return true; }

    void Draw() {
        _HdEmscriptenSyncTimer timer("HdWebSyncDriver::Draw");
        if (!_stage || !_delegate || !_geometryPass) {
            return;
        }
        {
            _HdEmscriptenSyncTimer updatesTimer(
                "UsdImagingDelegate::ApplyPendingUpdates");
            _delegate->ApplyPendingUpdates();
        }
        _renderDelegate.UpdateVolumeDependencies(_renderIndex);
        HdTaskSharedPtrVector tasks = {
            std::make_shared<WebSyncTask>(_geometryPass, _renderTags)
        };
        {
            _HdEmscriptenSyncTimer executeTimer("HdEngine::Execute");
            _engine.Execute(&_delegate->GetRenderIndex(), &tasks);
        }
    }

    bool StartDraw() {
        if (!_stage || !_delegate || !_geometryPass) {
            return false;
        }

        bool expected = false;
        if (!_drawPending.compare_exchange_strong(expected, true)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(_drawErrorMutex);
            _drawError.clear();
        }

        std::thread([this]() {
            try {
                Draw();
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(_drawErrorMutex);
                _drawError = e.what();
            } catch (...) {
                std::lock_guard<std::mutex> lock(_drawErrorMutex);
                _drawError = "Unknown Hydra draw failure.";
            }
            _drawPending.store(false);
        }).detach();

        return true;
    }

    bool DrawAsync(emscripten::val resolve, emscripten::val reject) {
        if (!_stage || !_delegate || !_geometryPass) {
            resolve(emscripten::val::undefined());
            return false;
        }

        bool expected = false;
        if (!_drawPending.compare_exchange_strong(expected, true)) {
            return false;
        }

        std::thread([this, resolve, reject]() mutable {
            std::string error;
            try {
                if (const char *value =
                        std::getenv("HDEMSCRIPTEN_TIMING_LOGS")) {
                    if (value[0] && std::string(value) != "0") {
                        emscripten_log(
                            EM_LOG_CONSOLE,
                            "%s",
                            "[hdEmscripten timing] DrawAsync thread entered");
                    }
                }
                Draw();
            } catch (const std::exception& e) {
                error = e.what();
            } catch (...) {
                error = "Unknown Hydra draw failure.";
            }
            _drawPending.store(false);

            runInMainThread([resolve, reject, error]() mutable {
                if (error.empty()) {
                    resolve(emscripten::val::undefined());
                } else {
                    reject(emscripten::val(error));
                }
            });
        }).detach();

        return true;
    }

    bool IsDrawPending() const {
        return _drawPending.load();
    }

    bool ExportUsdzAsync(std::string const& directory, emscripten::val resolve,
                        emscripten::val reject, emscripten::val progress) {
        bool expected = false;
        if (!_stage || _drawPending.load() ||
            !_exportPending.compare_exchange_strong(expected, true)) return false;
        std::thread([this, stage = _stage, directory, resolve, reject, progress]() mutable {
            std::string error, previous;
            auto last = std::chrono::steady_clock::now();
            try {
                ArResolverContextBinder binder(stage->GetPathResolverContext());
                WebExportStagePackage(stage, directory, [&](std::string const& phase, size_t completed, size_t total) {
                    auto now = std::chrono::steady_clock::now();
                    if (phase == previous && completed != total &&
                        std::chrono::duration<double, std::milli>(now - last).count() < 50) return;
                    previous = phase; last = now;
                    runInMainThread([progress, phase, completed, total]() mutable {
                        progress(emscripten::val(phase), double(completed), double(total));
                    });
                });
            } catch (std::exception const& e) { error = e.what(); }
              catch (...) { error = "Unknown USD export failure"; }
            _exportPending.store(false);
            runInMainThread([resolve, reject, error]() mutable {
                if (error.empty()) resolve(emscripten::val::undefined());
                else reject(emscripten::val(error));
            });
        }).detach();
        return true;
    }

    std::string ConsumeDrawError() {
        std::lock_guard<std::mutex> lock(_drawErrorMutex);
        std::string error = _drawError;
        _drawError.clear();
        return error;
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

        HdRprimCollection collection = _GetCollectionForComplexity();

        _Init(_stage,
              collection,
              SdfPath::AbsoluteRootPath(),
              _renderTags);
    }

    int GetRefineLevelFallback() const {
        if (!_delegate) {
            return 0;
        }
        return _delegate->GetRefineLevelFallback();
    }

    void SetRefineLevelFallback(int level) {
        _complexity = _GetComplexityForRefineLevel(level);
        if (!_delegate) {
            return;
        }
        _delegate->SetRefineLevelFallback(level);
        _UpdateCollectionForComplexity();
    }

    float GetComplexity() const {
        return _complexity;
    }

    // Adaptive limit tessellation for meshes refined by the current
    // complexity. The complexity's refine level bounds its cost.
    bool GetAdaptiveSubdivision() const {
        return _renderDelegate.GetAdaptiveSubdivision();
    }

    void SetAdaptiveSubdivision(bool enabled) {
        if (_renderDelegate.GetAdaptiveSubdivision() == enabled) {
            return;
        }
        _renderDelegate.SetAdaptiveSubdivision(enabled);
        if (_renderIndex) {
            _renderIndex->GetChangeTracker().MarkAllRprimsDirty(
                HdChangeTracker::DirtyDisplayStyle);
        }
    }

    void SetComplexity(float complexity) {
        _complexity = complexity;
        if (!_delegate) {
            return;
        }
        _delegate->SetRefineLevelFallback(_GetRefineLevel(complexity));
        _UpdateCollectionForComplexity();
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

    std::string resolveAssetUrl(std::string filename) {
        try {
            auto* httpResolver = dynamic_cast<HttpResolver*>(&ArGetUnderlyingResolver());
            if (!httpResolver) {
                return std::string();
            }
            return httpResolver->GetUrlForResolvedPath(filename);
        } catch (const std::exception& e) {
            std::cerr << "Failed to resolve asset URL for "
                      << filename << ": " << e.what() << std::endl;
            return std::string();
        } catch (...) {
            std::cerr << "Failed to resolve asset URL for "
                      << filename << std::endl;
            return std::string();
        }
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

    std::string GetPhysicsSceneJson() const {
        return HdWebPhysics::SceneJson(_stage);
    }

    std::string GetPhysicsSceneMetadataJson() const {
        return HdWebPhysics::SceneJson(_stage, false);
    }

    emscripten::val GetPhysicsMeshBuffers(const std::string& path) const {
        return HdWebPhysics::MeshBuffers(_stage, path);
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
    WebImagingDelegate  *_delegate;
    HdRenderPassSharedPtr _geometryPass;
    UsdStageRefPtr _stage;
    std::shared_ptr<HttpResolverCacheScope> _httpCache;
    TfTokenVector _renderTags;
    float _complexity;
    std::atomic<bool> _drawPending{false};
    std::atomic<bool> _exportPending{false};
    std::mutex _drawErrorMutex;
    std::string _drawError;

    static int _GetRefineLevel(float c) {
        int refineLevel = 0;

        // Match UsdImagingGLEngine's complexity-to-refine-level conversion.
        c = std::min(c + 0.01f, 2.0f);
        if (1.0f <= c && c < 1.1f) {
            refineLevel = 0;
        } else if (1.1f <= c && c < 1.2f) {
            refineLevel = 1;
        } else if (1.2f <= c && c < 1.3f) {
            refineLevel = 2;
        } else if (1.3f <= c && c < 1.4f) {
            refineLevel = 3;
        } else if (1.4f <= c && c < 1.5f) {
            refineLevel = 4;
        } else if (1.5f <= c && c < 1.6f) {
            refineLevel = 5;
        } else if (1.6f <= c && c < 1.7f) {
            refineLevel = 6;
        } else if (1.7f <= c && c < 1.8f) {
            refineLevel = 7;
        } else if (1.8f <= c && c <= 2.0f) {
            refineLevel = 8;
        } else {
            TF_CODING_ERROR("Invalid complexity %f, expected range is [1.0,2.0]\n", c);
        }
        return refineLevel;
    }

    static float _GetComplexityForRefineLevel(int level) {
        if (level <= 0) {
            return 1.0f;
        }
        return std::min(1.0f + 0.1f * static_cast<float>(level), 1.8f);
    }

    static TfToken _GetReprTokenForRefineLevel(int refineLevel) {
        return refineLevel > 0
            ? HdReprTokens->refined
            : HdReprTokens->smoothHull;
    }

    HdRprimCollection _GetCollectionForComplexity() const {
        return HdRprimCollection(
            HdTokens->geometry,
            HdReprSelector(
                _GetReprTokenForRefineLevel(_GetRefineLevel(_complexity))));
    }

    void _UpdateCollectionForComplexity() {
        if (!_renderIndex || !_geometryPass) {
            return;
        }
        HdRprimCollection collection = _GetCollectionForComplexity();
        _renderIndex->GetChangeTracker().MarkCollectionDirty(
            collection.GetName());
        _geometryPass->SetRprimCollection(collection);
    }

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
        _delegate = new WebImagingDelegate(_renderIndex, delegateId, usdStage);
        _delegate->SetRefineLevelFallback(_GetRefineLevel(_complexity));

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
