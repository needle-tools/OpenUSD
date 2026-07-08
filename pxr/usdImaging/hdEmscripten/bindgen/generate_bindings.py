#!/usr/bin/env python3
#
# Copyright 2026
#
# Generates the hdEmscripten core USD Embind surface and matching TypeScript
# declarations from a declarative manifest. Keep this script dependency-free so
# it can run in the OpenUSD CMake build.

from __future__ import annotations

import argparse
import json
from pathlib import Path


def _args(method: dict) -> str:
    return ", ".join(f"{arg['name']}: {arg['type']}" for arg in method.get("tsArgs", []))


def _return_type(entry: dict) -> str:
    ts_return = entry["tsReturn"]
    if entry.get("tsMayAsync"):
        return f"MaybePromise<{ts_return}>"
    return ts_return


def _policies(entry: dict, allow_raw: bool) -> str:
    policies = []
    if allow_raw:
        policies.append("allow_raw_pointers()")
    if entry.get("tsMayAsync"):
        policies.append("async()")
    return f", {', '.join(policies)}" if policies else ""


def _lambda(cpp_type: str, method: dict) -> tuple[str, bool]:
    op = method["op"]
    cpp = method.get("cpp", "")

    if op == "direct":
        return cpp, False
    if op == "boolOperator":
        return f"emscripten::optional_override([]({cpp_type} const& self) {{ return static_cast<bool>(self); }})", False
    if op == "tokenString":
        return f"emscripten::optional_override([]({cpp_type} const& self) {{ return self.{cpp}().GetString(); }})", False
    if op == "pathString":
        return f"emscripten::optional_override([]({cpp_type} const& self) {{ return self.{cpp}().GetAsString(); }})", False
    if op == "attributeTypeName":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr) { return attr.GetTypeName().GetAsToken().GetString(); })", False
    if op == "attributeValueString":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr) {
      pxr::VtValue value;
      if (!attr.Get(&value, pxr::UsdTimeCode::Default())) {
        return std::string();
      }
      return pxr::TfStringify(value);
    })""", False
    if op == "attributeValueStringAtTime":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, double timeCode) {
      pxr::VtValue value;
      if (!attr.Get(&value, pxr::UsdTimeCode(timeCode))) {
        return std::string();
      }
      return pxr::TfStringify(value);
    })""", False
    if op == "attributeConnections":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr) {
      pxr::SdfPathVector connections;
      std::vector<std::string> result;
      if (attr.GetConnections(&connections)) {
        result.reserve(connections.size());
        for (pxr::SdfPath const& connection : connections) {
          result.push_back(connection.GetAsString());
        }
      }
      return result;
    })""", False
    if op == "attributeResolveInfo":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, double timeCode) {
      return _ResolveInfoToVal(attr.GetResolveInfo(_TimeCode(timeCode)));
    })""", False
    if op == "attributeHasAuthoredValue":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr) { return attr.HasAuthoredValue(); })", False
    if op == "attributeHasAuthoredValueOpinion":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr) { return attr.HasAuthoredValueOpinion(); })", False
    if op == "attributeGetNumTimeSamples":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr) { return attr.GetNumTimeSamples(); })", False
    if op == "attributeGetTimeSamples":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr) {
      std::vector<double> result;
      attr.GetTimeSamples(&result);
      return result;
    })""", False
    if op == "attributeSetBool":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr, bool value, double timeCode) { return attr.Set(value, _TimeCode(timeCode)); })", False
    if op == "attributeSetInt":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr, int value, double timeCode) { return attr.Set(value, _TimeCode(timeCode)); })", False
    if op == "attributeSetFloat":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr, float value, double timeCode) { return attr.Set(value, _TimeCode(timeCode)); })", False
    if op == "attributeSetDouble":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr, double value, double timeCode) { return attr.Set(value, _TimeCode(timeCode)); })", False
    if op == "attributeSetString":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr, std::string const& value, double timeCode) { return attr.Set(value, _TimeCode(timeCode)); })", False
    if op == "attributeSetToken":
        return "emscripten::optional_override([](pxr::UsdAttribute const& attr, std::string const& value, double timeCode) { return attr.Set(pxr::TfToken(value), _TimeCode(timeCode)); })", False
    if op == "attributeSetVal":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, emscripten::val value, double timeCode) {
      return _SetAttributeFromVal(attr, value, _TimeCode(timeCode));
    })""", False
    if op == "attributeAddConnection":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, std::string const& path) {
      return attr.AddConnection(pxr::SdfPath(path));
    })""", False
    if op == "attributeSetColor3f":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, float r, float g, float b, double timeCode) {
      return attr.Set(pxr::GfVec3f(r, g, b), _TimeCode(timeCode));
    })""", False
    if op == "attributeSetVec3f":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, float x, float y, float z, double timeCode) {
      return attr.Set(pxr::GfVec3f(x, y, z), _TimeCode(timeCode));
    })""", False
    if op == "attributeSetVec3d":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr, double x, double y, double z, double timeCode) {
      return attr.Set(pxr::GfVec3d(x, y, z), _TimeCode(timeCode));
    })""", False
    if op == "attributeSetMatrix4d":
        return """emscripten::optional_override([](pxr::UsdAttribute const& attr,
      double m00, double m01, double m02, double m03,
      double m10, double m11, double m12, double m13,
      double m20, double m21, double m22, double m23,
      double m30, double m31, double m32, double m33,
      double timeCode) {
      return attr.Set(pxr::GfMatrix4d(
        m00, m01, m02, m03,
        m10, m11, m12, m13,
        m20, m21, m22, m23,
        m30, m31, m32, m33), _TimeCode(timeCode));
    })""", False
    if op == "relationshipTargets":
        return """emscripten::optional_override([](pxr::UsdRelationship const& rel) {
      pxr::SdfPathVector targets;
      std::vector<std::string> result;
      if (rel.GetTargets(&targets)) {
        result.reserve(targets.size());
        for (pxr::SdfPath const& target : targets) {
          result.push_back(target.GetAsString());
        }
      }
      return result;
    })""", False
    if op == "relationshipAddTarget":
        return """emscripten::optional_override([](pxr::UsdRelationship const& rel, std::string const& path) {
      return rel.AddTarget(pxr::SdfPath(path));
    })""", False
    if op == "relationshipClearTargets":
        return """emscripten::optional_override([](pxr::UsdRelationship const& rel, bool removeSpec) {
      return rel.ClearTargets(removeSpec);
    })""", False
    if op == "objectGetAllMetadata":
        return f"emscripten::optional_override([]({cpp_type} const& self) {{ return _ObjectMetadataToVal(self); }})", False
    if op == "objectGetMetadataString":
        return f"""emscripten::optional_override([]({cpp_type} const& self, std::string const& key) {{
      pxr::VtValue value;
      if (!self.GetMetadata(pxr::TfToken(key), &value)) {{
        return std::string();
      }}
      return _VtValueToString(value);
    }})""", False
    if op == "objectHasAuthoredMetadata":
        return f"emscripten::optional_override([]({cpp_type} const& self, std::string const& key) {{ return self.HasAuthoredMetadata(pxr::TfToken(key)); }})", False
    if op == "propertyStack":
        return f"emscripten::optional_override([]({cpp_type} const& self, double timeCode) {{ return _PropertyStackToVal(self.GetPropertyStack(_TimeCode(timeCode))); }})", False
    if op == "propertyStackWithLayerOffsets":
        return f"emscripten::optional_override([]({cpp_type} const& self, double timeCode) {{ return _PropertyStackWithLayerOffsetsToVal(self.GetPropertyStackWithLayerOffsets(_TimeCode(timeCode))); }})", False
    if op == "layerExportToString":
        return """emscripten::optional_override([](pxr::SdfLayer& layer) {
      std::string result;
      layer.ExportToString(&result);
      return result;
    })""", False
    if op == "layerSave":
        return "emscripten::optional_override([](pxr::SdfLayer& layer) { return layer.Save(); })", False
    if op == "layerExport":
        return "emscripten::optional_override([](pxr::SdfLayer& layer, std::string const& path) { return layer.Export(path); })", False
    if op == "layerGetRealPath":
        return "emscripten::optional_override([](pxr::SdfLayer const& layer) { return layer.GetRealPath(); })", False
    if op == "primChildren":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      std::vector<pxr::UsdPrim> result;
      for (pxr::UsdPrim const& child : prim.GetChildren()) {
        result.push_back(child);
      }
      return result;
    })""", False
    if op == "tokenVectorString":
        return f"""emscripten::optional_override([]({cpp_type} const& self) {{
      std::vector<std::string> result;
      for (pxr::TfToken const& name : self.{cpp}()) {{
        result.push_back(name.GetString());
      }}
      return result;
    }})""", False
    if op == "primGetAttribute":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& name) {
      return prim.GetAttribute(pxr::TfToken(name));
    })""", False
    if op == "primGetAttributes":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return prim.GetAttributes();
    })""", False
    if op == "primGetRelationship":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& name) {
      return prim.GetRelationship(pxr::TfToken(name));
    })""", False
    if op == "primGetRelationships":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return prim.GetRelationships();
    })""", False
    if op == "primGetSpecifier":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return pxr::TfStringify(prim.GetSpecifier());
    })""", False
    if op == "primIsAbstract":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.IsAbstract(); })", False
    if op == "primIsInstance":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.IsInstance(); })", False
    if op == "primIsInPrototype":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.IsInPrototype(); })", False
    if op == "primIsPrototype":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.IsPrototype(); })", False
    if op == "primGetDisplayName":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.GetDisplayName(); })", False
    if op == "primHasAuthoredReferences":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.HasAuthoredReferences(); })", False
    if op == "primHasAuthoredInherits":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.HasAuthoredInherits(); })", False
    if op == "primHasAuthoredSpecializes":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.HasAuthoredSpecializes(); })", False
    if op == "primHasAuthoredInstanceable":
        return "emscripten::optional_override([](pxr::UsdPrim const& prim) { return prim.HasAuthoredInstanceable(); })", False
    if op == "primPrimStack":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return _PrimStackToVal(prim.GetPrimStack());
    })""", False
    if op == "primPrimStackWithLayerOffsets":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return _PrimStackWithLayerOffsetsToVal(prim.GetPrimStackWithLayerOffsets());
    })""", False
    if op == "primGetPrimIndex":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return _PrimIndexToVal(prim);
    })""", False
    if op == "primGetCompositionArcs":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      return _CompositionArcsToVal(prim);
    })""", False
    if op == "primCreateRelationship":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& name, bool custom) {
      return prim.CreateRelationship(pxr::TfToken(name), custom);
    })""", False
    if op == "primApplyAPI":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& schemaIdentifier) {
      return prim.ApplyAPI(pxr::TfToken(schemaIdentifier));
    })""", False
    if op == "primCreateAttribute":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& name, std::string const& typeName, bool custom) {
      return prim.CreateAttribute(pxr::TfToken(name), _FindValueTypeName(typeName), custom);
    })""", False
    if op == "primLoad":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      prim.Load();
    })""", False
    if op == "primUnload":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      prim.Unload();
    })""", False
    if op == "primAddPayload":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& assetPath, std::string const& primPath) {
      return prim.GetPayloads().AddPayload(assetPath, pxr::SdfPath(primPath));
    })""", False
    if op == "primAddVariant":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName, std::string const& variantName) {
      return prim.GetVariantSet(variantSetName).AddVariant(variantName);
    })""", False
    if op == "primSetVariantSelection":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName, std::string const& variantName) {
      return prim.GetVariantSet(variantSetName).SetVariantSelection(variantName);
    })""", False
    if op == "primGetVariantSelection":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName) {
      return prim.GetVariantSet(variantSetName).GetVariantSelection();
    })""", False
    if op == "primClearVariantSelection":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName) {
      return prim.GetVariantSet(variantSetName).ClearVariantSelection();
    })""", False
    if op == "primBlockVariantSelection":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName) {
      return prim.GetVariantSet(variantSetName).BlockVariantSelection();
    })""", False
    if op == "primGetVariantSetNames":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim) {
      std::vector<std::string> result;
      for (std::string const& name : prim.GetVariantSets().GetNames()) {
        result.push_back(name);
      }
      return result;
    })""", False
    if op == "primGetVariantNames":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName) {
      return prim.GetVariantSet(variantSetName).GetVariantNames();
    })""", False
    if op == "primDefinePrimInVariant":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& variantSetName, std::string const& variantName, std::string const& path, std::string const& typeName) {
      pxr::UsdVariantSet variantSet = prim.GetVariantSet(variantSetName);
      if (!variantSet.AddVariant(variantName) || !variantSet.SetVariantSelection(variantName)) {
        return pxr::UsdPrim();
      }
      pxr::UsdEditContext context(variantSet.GetVariantEditContext());
      return prim.GetStage()->DefinePrim(pxr::SdfPath(path), pxr::TfToken(typeName));
    })""", False
    if op == "stageRootLayerPointer":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      return get_pointer(stage.GetRootLayer());
    })""", True
    if op == "stageGetPrimAtPath":
        return """emscripten::optional_override([](pxr::UsdStage& stage, std::string const& path) {
      return stage.GetPrimAtPath(pxr::SdfPath(path));
    })""", False
    if op == "stageDefinePrim":
        return """emscripten::optional_override([](pxr::UsdStage& stage, std::string const& path, std::string const& typeName) {
      return stage.DefinePrim(pxr::SdfPath(path), pxr::TfToken(typeName));
    })""", False
    if op == "stageTraverse":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      std::vector<pxr::UsdPrim> result;
      for (pxr::UsdPrim const& prim : stage.Traverse()) {
        result.push_back(prim);
      }
      return result;
    })""", False
    if op == "stageTraverseAll":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      std::vector<pxr::UsdPrim> result;
      for (pxr::UsdPrim const& prim : stage.TraverseAll()) {
        result.push_back(prim);
      }
      return result;
    })""", False
    if op == "stageGetLayerStack":
        return """emscripten::optional_override([](pxr::UsdStage& stage, bool includeSessionLayers) {
      emscripten::val result = emscripten::val::array();
      int index = 0;
      for (auto const& layer : stage.GetLayerStack(includeSessionLayers)) {
        result.set(index++, _LayerToVal(layer));
      }
      return result;
    })""", False
    if op == "stageGetUsedLayers":
        return """emscripten::optional_override([](pxr::UsdStage& stage, bool includeClipLayers) {
      emscripten::val result = emscripten::val::array();
      int index = 0;
      for (auto const& layer : stage.GetUsedLayers(includeClipLayers)) {
        result.set(index++, _LayerToVal(layer));
      }
      return result;
    })""", False
    if op == "stageGetCompositionErrors":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      emscripten::val result = emscripten::val::array();
      int index = 0;
      for (pxr::PcpErrorBasePtr const& error : stage.GetCompositionErrors()) {
        result.set(index++, error ? error->ToString() : std::string());
      }
      return result;
    })""", False
    if op == "stageRegisterObjectsChanged":
        return """emscripten::optional_override([](pxr::UsdStage& stage, emscripten::val callback) {
      return _RegisterObjectsChanged(stage, callback);
    })""", False
    if op == "stageRevokeObjectsChanged":
        return """emscripten::optional_override([](pxr::UsdStage& stage, int listenerId) {
      return _RevokeObjectsChanged(listenerId);
    })""", False
    if op == "stageUpAxis":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      pxr::TfToken upAxis;
      if (stage.HasAuthoredMetadata(pxr::UsdGeomTokens->upAxis)) {
        stage.GetMetadata(pxr::UsdGeomTokens->upAxis, &upAxis);
      } else {
        upAxis = pxr::UsdGeomGetFallbackUpAxis();
      }
      return upAxis == pxr::UsdGeomTokens->z ? 'z' : 'y';
    })""", False
    if op == "stageSetUpAxis":
        return """emscripten::optional_override([](pxr::UsdStage& stage, std::string const& upAxis) {
      return pxr::UsdGeomSetStageUpAxis(pxr::UsdStageWeakPtr(&stage), upAxis == "Z" || upAxis == "z" ? pxr::UsdGeomTokens->z : pxr::UsdGeomTokens->y);
    })""", False
    if op == "stageExport":
        return "emscripten::optional_override([](pxr::UsdStage& stage, std::string const& path) { return stage.Export(path); })", False
    if op == "stageExportToString":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      std::string result;
      stage.ExportToString(&result);
      return result;
    })""", False

    raise ValueError(f"Unsupported op: {op}")


def _function_expr(function: dict) -> tuple[str, bool]:
    op = function["op"]
    if op == "createStage":
        return """emscripten::optional_override([](std::string const& path) {
      pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateNew(path);
      _StageRegistry().push_back(stage);
      return get_pointer(stage);
    })""", True
    if op == "openStage":
        return """emscripten::optional_override([](std::string const& path) {
      pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(path);
      _StageRegistry().push_back(stage);
      return get_pointer(stage);
    })""", True
    if op == "releaseStage":
        return """emscripten::optional_override([](pxr::UsdStage& stage) {
      std::vector<pxr::UsdStageRefPtr>& stages = _StageRegistry();
      for (auto it = stages.begin(); it != stages.end(); ++it) {
        if (get_pointer(*it) == &stage) {
          stages.erase(it);
          return true;
        }
      }
      return false;
    })""", False
    if op == "createUsdzPackage":
        return """emscripten::optional_override([](std::string const& assetPath, std::string const& usdzPath) {
      return pxr::UsdUtilsCreateNewUsdzPackage(pxr::SdfAssetPath(assetPath), usdzPath);
    })""", False
    if op == "readFile":
        return """emscripten::optional_override([](std::string const& path) {
      std::ifstream input(path, std::ios::binary);
      if (!input) {
        return emscripten::val::global("Uint8Array").new_(0);
      }
      std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
      return emscripten::val::global("Uint8Array").new_(emscripten::typed_memory_view(bytes.size(), bytes.data()));
    })""", False
    raise ValueError(f"Unsupported function op: {op}")


def _schema_define(schema: dict) -> str:
    return f"""emscripten::optional_override([](pxr::UsdStage& stage, std::string const& path) {{
      return {schema['cppType']}::Define(pxr::UsdStageWeakPtr(&stage), pxr::SdfPath(path));
    }})"""


def _schema_get(schema: dict) -> str:
    return f"""emscripten::optional_override([](pxr::UsdStage& stage, std::string const& path) {{
      return {schema['cppType']}::Get(pxr::UsdStageWeakPtr(&stage), pxr::SdfPath(path));
    }})"""


def _schema_create_attr(schema: dict, attr: dict) -> str:
    return f"""emscripten::optional_override([]({schema['cppType']} const& schema, emscripten::val defaultValue, bool writeSparsely) {{
      pxr::UsdAttribute attr = schema.Create{attr['name']}Attr(pxr::VtValue(), writeSparsely);
      if (!defaultValue.isUndefined() && !defaultValue.isNull()) {{
        _SetAttributeFromVal(attr, defaultValue, pxr::UsdTimeCode::Default());
      }}
      return attr;
    }})"""


def _schema_get_attr(schema: dict, attr: dict) -> str:
    return f"emscripten::optional_override([]({schema['cppType']} const& schema) {{ return schema.Get{attr['name']}Attr(); }})"


def _schema_extra_method(schema: dict, method: str) -> str:
    cpp_type = schema["cppType"]
    xform_precision = {
        "AddTranslateOp": "pxr::UsdGeomXformOp::PrecisionDouble",
        "AddRotateXYZOp": "pxr::UsdGeomXformOp::PrecisionFloat",
        "AddScaleOp": "pxr::UsdGeomXformOp::PrecisionFloat",
        "AddTransformOp": "pxr::UsdGeomXformOp::PrecisionDouble",
    }
    if method in xform_precision:
        return f"""emscripten::optional_override([]({cpp_type} const& schema) {{
      return pxr::UsdGeomXformable(schema).{method}({xform_precision[method]});
    }})"""
    if method == "MakeVisible":
        return f"emscripten::optional_override([]({cpp_type} const& schema) {{ return pxr::UsdGeomImageable(schema).MakeVisible(); }})"
    if method == "MakeInvisible":
        return f"emscripten::optional_override([]({cpp_type} const& schema) {{ return pxr::UsdGeomImageable(schema).MakeInvisible(); }})"
    raise ValueError(f"Unsupported schema extra method: {method}")


def _schema_extra_ts(method: str) -> str:
    if method.startswith("Add"):
        return "UsdGeomXformOp"
    if method.startswith("Make"):
        return "void"
    return "unknown"


def generate_cpp(manifest: dict) -> str:
    lines = [
        "// Generated by pxr/usdImaging/hdEmscripten/bindgen/generate_bindings.py.",
        "// Do not edit by hand; edit bindgen/core-bindings.json instead.",
        "",
        "namespace pxr { namespace hdEmscriptenGenerated {",
        "",
        "UsdTimeCode _TimeCode(double timeCode)",
        "{",
        "  return std::isnan(timeCode) ? UsdTimeCode::Default() : UsdTimeCode(timeCode);",
        "}",
        "",
        "SdfValueTypeName _FindValueTypeName(std::string const& typeName)",
        "{",
        "  return SdfSchema::GetInstance().FindType(typeName);",
        "}",
        "",
        "std::string _TypeOf(emscripten::val const& value)",
        "{",
        "  return value.typeOf().as<std::string>();",
        "}",
        "",
        "bool _IsArray(emscripten::val const& value)",
        "{",
        "  return emscripten::val::global(\"Array\").call<bool>(\"isArray\", value);",
        "}",
        "",
        "bool _HasFunction(emscripten::val const& value, char const* name)",
        "{",
        "  return value[name].typeOf().as<std::string>() == \"function\";",
        "}",
        "",
        "bool _IsArrayLike(emscripten::val const& value)",
        "{",
        "  return _IsArray(value) || (_HasFunction(value, \"size\") && _HasFunction(value, \"get\"));",
        "}",
        "",
        "unsigned int _ArrayLikeLength(emscripten::val const& value)",
        "{",
        "  if (_IsArray(value)) {",
        "    return value[\"length\"].as<unsigned int>();",
        "  }",
        "  return value.call<unsigned int>(\"size\");",
        "}",
        "",
        "emscripten::val _ArrayLikeGet(emscripten::val const& value, unsigned int index)",
        "{",
        "  if (_IsArray(value)) {",
        "    return value[index];",
        "  }",
        "  return value.call<emscripten::val>(\"get\", index);",
        "}",
        "",
        "bool _InstanceOf(emscripten::val const& value, char const* name)",
        "{",
        "  emscripten::val ctor = emscripten::val::module_property(name);",
        "  if (ctor.isUndefined()) {",
        "    ctor = emscripten::val::global(name);",
        "  }",
        "  return !ctor.isUndefined() && value.instanceof(ctor);",
        "}",
        "",
        "pxr::GfVec3f _GfVec3fFromVal(emscripten::val const& value)",
        "{",
        "  if (_InstanceOf(value, \"GfVec3f\")) {",
        "    return value.as<pxr::GfVec3f>();",
        "  }",
        "  if (_InstanceOf(value, \"GfVec3d\")) {",
        "    pxr::GfVec3d v = value.as<pxr::GfVec3d>();",
        "    return pxr::GfVec3f(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));",
        "  }",
        "  if (_IsArray(value)) {",
        "    return pxr::GfVec3f(value[0].as<float>(), value[1].as<float>(), value[2].as<float>());",
        "  }",
        "  if (_TypeOf(value) == \"number\") {",
        "    float v = value.as<float>();",
        "    return pxr::GfVec3f(v, v, v);",
        "  }",
        "  return pxr::GfVec3f(value[\"x\"].as<float>(), value[\"y\"].as<float>(), value[\"z\"].as<float>());",
        "}",
        "",
        "pxr::GfVec3d _GfVec3dFromVal(emscripten::val const& value)",
        "{",
        "  if (_InstanceOf(value, \"GfVec3d\")) {",
        "    return value.as<pxr::GfVec3d>();",
        "  }",
        "  if (_InstanceOf(value, \"GfVec3f\")) {",
        "    pxr::GfVec3f v = value.as<pxr::GfVec3f>();",
        "    return pxr::GfVec3d(v[0], v[1], v[2]);",
        "  }",
        "  if (_IsArray(value)) {",
        "    return pxr::GfVec3d(value[0].as<double>(), value[1].as<double>(), value[2].as<double>());",
        "  }",
        "  if (_TypeOf(value) == \"number\") {",
        "    double v = value.as<double>();",
        "    return pxr::GfVec3d(v, v, v);",
        "  }",
        "  return pxr::GfVec3d(value[\"x\"].as<double>(), value[\"y\"].as<double>(), value[\"z\"].as<double>());",
        "}",
        "",
        "pxr::GfMatrix4d _GfMatrix4dFromVal(emscripten::val const& value)",
        "{",
        "  if (_InstanceOf(value, \"GfMatrix4d\")) {",
        "    return value.as<pxr::GfMatrix4d>();",
        "  }",
        "  if (_IsArray(value)) {",
        "    return pxr::GfMatrix4d(",
        "      value[0].as<double>(), value[1].as<double>(), value[2].as<double>(), value[3].as<double>(),",
        "      value[4].as<double>(), value[5].as<double>(), value[6].as<double>(), value[7].as<double>(),",
        "      value[8].as<double>(), value[9].as<double>(), value[10].as<double>(), value[11].as<double>(),",
        "      value[12].as<double>(), value[13].as<double>(), value[14].as<double>(), value[15].as<double>());",
        "  }",
        "  return pxr::GfMatrix4d(1.0);",
        "}",
        "",
        "pxr::VtArray<pxr::GfVec3f> _GfVec3fArrayFromVal(emscripten::val const& value)",
        "{",
        "  std::vector<pxr::GfVec3f> vector;",
        "  if (_IsArrayLike(value)) {",
        "    unsigned int length = _ArrayLikeLength(value);",
        "    vector.reserve(length);",
        "    for (unsigned int i = 0; i < length; ++i) {",
        "      vector.push_back(_GfVec3fFromVal(_ArrayLikeGet(value, i)));",
        "    }",
        "  }",
        "  pxr::VtArray<pxr::GfVec3f> result;",
        "  result.resize(vector.size());",
        "  for (size_t i = 0; i < vector.size(); ++i) { result[i] = vector[i]; }",
        "  return result;",
        "}",
        "",
        "pxr::VtArray<pxr::GfVec3d> _GfVec3dArrayFromVal(emscripten::val const& value)",
        "{",
        "  std::vector<pxr::GfVec3d> vector;",
        "  if (_IsArrayLike(value)) {",
        "    unsigned int length = _ArrayLikeLength(value);",
        "    vector.reserve(length);",
        "    for (unsigned int i = 0; i < length; ++i) {",
        "      vector.push_back(_GfVec3dFromVal(_ArrayLikeGet(value, i)));",
        "    }",
        "  }",
        "  pxr::VtArray<pxr::GfVec3d> result;",
        "  result.resize(vector.size());",
        "  for (size_t i = 0; i < vector.size(); ++i) { result[i] = vector[i]; }",
        "  return result;",
        "}",
        "",
        "pxr::VtArray<int> _IntArrayFromVal(emscripten::val const& value)",
        "{",
        "  std::vector<int> vector;",
        "  if (_IsArrayLike(value)) {",
        "    unsigned int length = _ArrayLikeLength(value);",
        "    vector.reserve(length);",
        "    for (unsigned int i = 0; i < length; ++i) { vector.push_back(_ArrayLikeGet(value, i).as<int>()); }",
        "  }",
        "  pxr::VtArray<int> result;",
        "  result.resize(vector.size());",
        "  for (size_t i = 0; i < vector.size(); ++i) { result[i] = vector[i]; }",
        "  return result;",
        "}",
        "",
        "pxr::VtArray<float> _FloatArrayFromVal(emscripten::val const& value)",
        "{",
        "  std::vector<float> vector;",
        "  if (_IsArrayLike(value)) {",
        "    unsigned int length = _ArrayLikeLength(value);",
        "    vector.reserve(length);",
        "    for (unsigned int i = 0; i < length; ++i) { vector.push_back(_ArrayLikeGet(value, i).as<float>()); }",
        "  }",
        "  pxr::VtArray<float> result;",
        "  result.resize(vector.size());",
        "  for (size_t i = 0; i < vector.size(); ++i) { result[i] = vector[i]; }",
        "  return result;",
        "}",
        "",
        "pxr::VtArray<double> _DoubleArrayFromVal(emscripten::val const& value)",
        "{",
        "  std::vector<double> vector;",
        "  if (_IsArrayLike(value)) {",
        "    unsigned int length = _ArrayLikeLength(value);",
        "    vector.reserve(length);",
        "    for (unsigned int i = 0; i < length; ++i) { vector.push_back(_ArrayLikeGet(value, i).as<double>()); }",
        "  }",
        "  pxr::VtArray<double> result;",
        "  result.resize(vector.size());",
        "  for (size_t i = 0; i < vector.size(); ++i) { result[i] = vector[i]; }",
        "  return result;",
        "}",
        "",
        "pxr::VtArray<std::string> _StringArrayFromVal(emscripten::val const& value)",
        "{",
        "  std::vector<std::string> vector;",
        "  if (_IsArrayLike(value)) {",
        "    unsigned int length = _ArrayLikeLength(value);",
        "    vector.reserve(length);",
        "    for (unsigned int i = 0; i < length; ++i) { vector.push_back(_ArrayLikeGet(value, i).as<std::string>()); }",
        "  }",
        "  pxr::VtArray<std::string> result;",
        "  result.resize(vector.size());",
        "  for (size_t i = 0; i < vector.size(); ++i) { result[i] = vector[i]; }",
        "  return result;",
        "}",
        "",
        "pxr::VtArray<pxr::TfToken> _TokenArrayFromVal(emscripten::val const& value)",
        "{",
        "  pxr::VtArray<std::string> strings = _StringArrayFromVal(value);",
        "  pxr::VtArray<pxr::TfToken> result;",
        "  result.resize(strings.size());",
        "  for (size_t i = 0; i < strings.size(); ++i) { result[i] = pxr::TfToken(strings[i]); }",
        "  return result;",
        "}",
        "",
        "bool _SetAttributeFromVal(pxr::UsdAttribute const& attr, emscripten::val value, pxr::UsdTimeCode timeCode)",
        "{",
        "  pxr::SdfValueTypeName typeName = attr.GetTypeName();",
        "  if (typeName == pxr::SdfValueTypeNames->Bool) { return attr.Set(value.as<bool>(), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Int) { return attr.Set(value.as<int>(), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Float) { return attr.Set(value.as<float>(), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Double) { return attr.Set(value.as<double>(), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->String) { return attr.Set(value.as<std::string>(), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Token) { return attr.Set(pxr::TfToken(value.as<std::string>()), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Asset) { return attr.Set(pxr::SdfAssetPath(value.as<std::string>()), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Color3f || typeName == pxr::SdfValueTypeNames->Float3 || typeName == pxr::SdfValueTypeNames->Vector3f) { return attr.Set(_GfVec3fFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Double3 || typeName == pxr::SdfValueTypeNames->Vector3d) { return attr.Set(_GfVec3dFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Matrix4d) { return attr.Set(_GfMatrix4dFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->IntArray) { return attr.Set(_IntArrayFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->FloatArray) { return attr.Set(_FloatArrayFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->DoubleArray) { return attr.Set(_DoubleArrayFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->StringArray) { return attr.Set(_StringArrayFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->TokenArray) { return attr.Set(_TokenArrayFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Color3fArray || typeName == pxr::SdfValueTypeNames->Float3Array || typeName == pxr::SdfValueTypeNames->Vector3fArray || typeName == pxr::SdfValueTypeNames->Point3fArray || typeName == pxr::SdfValueTypeNames->Normal3fArray) { return attr.Set(_GfVec3fArrayFromVal(value), timeCode); }",
        "  if (typeName == pxr::SdfValueTypeNames->Double3Array || typeName == pxr::SdfValueTypeNames->Vector3dArray) { return attr.Set(_GfVec3dArrayFromVal(value), timeCode); }",
        "  return attr.Set(pxr::VtValue(value.as<std::string>()), timeCode);",
        "}",
        "",
        "std::vector<UsdStageRefPtr>& _StageRegistry()",
        "{",
        "  static std::vector<UsdStageRefPtr> stages;",
        "  return stages;",
        "}",
        "",
        "std::string _VtValueToString(VtValue const& value)",
        "{",
        "  return value.IsEmpty() ? std::string() : TfStringify(value);",
        "}",
        "",
        "emscripten::val _StringVectorToVal(std::vector<std::string> const& values)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (std::string const& value : values) {",
        "    result.set(index++, value);",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _PathRangeToVal(UsdNotice::ObjectsChanged::PathRange const& paths)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (SdfPath const& path : paths) {",
        "    result.set(index++, path.GetAsString());",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _TokenVectorToVal(TfTokenVector const& tokens)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (TfToken const& token : tokens) {",
        "    result.set(index++, token.GetString());",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _LayerOffsetToVal(SdfLayerOffset const& offset)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  result.set(\"offset\", offset.GetOffset());",
        "  result.set(\"scale\", offset.GetScale());",
        "  result.set(\"isIdentity\", offset.IsIdentity());",
        "  return result;",
        "}",
        "",
        "emscripten::val _LayerToVal(SdfLayerHandle const& layer)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  if (!layer) {",
        "    result.set(\"identifier\", std::string());",
        "    result.set(\"displayName\", std::string());",
        "    result.set(\"realPath\", std::string());",
        "    return result;",
        "  }",
        "  result.set(\"identifier\", layer->GetIdentifier());",
        "  result.set(\"displayName\", layer->GetDisplayName());",
        "  result.set(\"realPath\", layer->GetRealPath());",
        "  return result;",
        "}",
        "",
        "emscripten::val _SpecMetadataToVal(SdfSpecHandle const& spec)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  if (!spec) {",
        "    return result;",
        "  }",
        "  for (TfToken const& key : spec->GetMetaDataInfoKeys()) {",
        "    if (spec->HasInfo(key)) {",
        "      result.set(key.GetString(), _VtValueToString(spec->GetInfo(key)));",
        "    }",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _SpecToVal(SdfSpecHandle const& spec)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  if (!spec) {",
        "    return result;",
        "  }",
        "  result.set(\"path\", spec->GetPath().GetAsString());",
        "  result.set(\"layer\", _LayerToVal(spec->GetLayer()));",
        "  result.set(\"metadata\", _SpecMetadataToVal(spec));",
        "  if (SdfPrimSpecHandle primSpec = TfDynamic_cast<SdfPrimSpecHandle>(spec)) {",
        "    result.set(\"specifier\", TfStringify(primSpec->GetSpecifier()));",
        "    result.set(\"typeName\", primSpec->GetTypeName().GetString());",
        "  }",
        "  if (SdfPropertySpecHandle propertySpec = TfDynamic_cast<SdfPropertySpecHandle>(spec)) {",
        "    result.set(\"name\", propertySpec->GetName());",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _PrimStackToVal(SdfPrimSpecHandleVector const& specs)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (SdfPrimSpecHandle const& spec : specs) {",
        "    result.set(index++, _SpecToVal(spec));",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _PrimStackWithLayerOffsetsToVal(std::vector<std::pair<SdfPrimSpecHandle, SdfLayerOffset>> const& stack)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (auto const& entry : stack) {",
        "    emscripten::val row = _SpecToVal(entry.first);",
        "    row.set(\"layerOffset\", _LayerOffsetToVal(entry.second));",
        "    result.set(index++, row);",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _PropertyStackToVal(SdfPropertySpecHandleVector const& specs)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (SdfPropertySpecHandle const& spec : specs) {",
        "    result.set(index++, _SpecToVal(spec));",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _PropertyStackWithLayerOffsetsToVal(std::vector<std::pair<SdfPropertySpecHandle, SdfLayerOffset>> const& stack)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  int index = 0;",
        "  for (auto const& entry : stack) {",
        "    emscripten::val row = _SpecToVal(entry.first);",
        "    row.set(\"layerOffset\", _LayerOffsetToVal(entry.second));",
        "    result.set(index++, row);",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _ObjectMetadataToVal(UsdObject const& object)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  for (auto const& entry : object.GetAllMetadata()) {",
        "    result.set(entry.first.GetString(), _VtValueToString(entry.second));",
        "  }",
        "  return result;",
        "}",
        "",
        "std::string _ResolveInfoSourceToString(UsdResolveInfoSource source)",
        "{",
        "  switch (source) {",
        "    case UsdResolveInfoSourceNone: return \"None\";",
        "    case UsdResolveInfoSourceFallback: return \"Fallback\";",
        "    case UsdResolveInfoSourceDefault: return \"Default\";",
        "    case UsdResolveInfoSourceTimeSamples: return \"TimeSamples\";",
        "    case UsdResolveInfoSourceValueClips: return \"ValueClips\";",
        "    case UsdResolveInfoSourceSpline: return \"Spline\";",
        "  }",
        "  return \"Unknown\";",
        "}",
        "",
        "emscripten::val _ResolveInfoToVal(UsdResolveInfo const& info)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  result.set(\"source\", _ResolveInfoSourceToString(info.GetSource()));",
        "  result.set(\"hasAuthoredValueOpinion\", info.HasAuthoredValueOpinion());",
        "  result.set(\"hasAuthoredValue\", info.HasAuthoredValue());",
        "  result.set(\"valueIsBlocked\", info.ValueIsBlocked());",
        "  result.set(\"valueSourceMightBeTimeVarying\", info.ValueSourceMightBeTimeVarying());",
        "  result.set(\"hasNextWeakerInfo\", info.HasNextWeakerInfo());",
        "  return result;",
        "}",
        "",
        "emscripten::val _PcpNodeToVal(PcpNodeRef const& node)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  if (!node) {",
        "    return result;",
        "  }",
        "  result.set(\"arcType\", TfStringify(node.GetArcType()));",
        "  result.set(\"path\", node.GetPath().GetAsString());",
        "  result.set(\"pathAtIntroduction\", node.GetPathAtIntroduction().GetAsString());",
        "  result.set(\"layerStackIdentifier\", node.GetLayerStack() ? node.GetLayerStack()->GetIdentifier().rootLayer->GetIdentifier() : std::string());",
        "  emscripten::val children = emscripten::val::array();",
        "  int index = 0;",
        "  for (PcpNodeRef const& child : node.GetChildrenRange()) {",
        "    children.set(index++, _PcpNodeToVal(child));",
        "  }",
        "  result.set(\"children\", children);",
        "  return result;",
        "}",
        "",
        "emscripten::val _PrimIndexToVal(UsdPrim const& prim)",
        "{",
        "  emscripten::val result = emscripten::val::object();",
        "  PcpPrimIndex const& index = prim.GetPrimIndex();",
        "  result.set(\"isValid\", index.IsValid());",
        "  if (index.IsValid()) {",
        "    result.set(\"rootNode\", _PcpNodeToVal(index.GetRootNode()));",
        "  }",
        "  return result;",
        "}",
        "",
        "emscripten::val _CompositionArcsToVal(UsdPrim const& prim)",
        "{",
        "  emscripten::val result = emscripten::val::array();",
        "  UsdPrimCompositionQuery query(prim);",
        "  int index = 0;",
        "  for (UsdPrimCompositionQueryArc const& arc : query.GetCompositionArcs()) {",
        "    emscripten::val row = emscripten::val::object();",
        "    row.set(\"arcType\", TfStringify(arc.GetArcType()));",
        "    row.set(\"targetLayer\", _LayerToVal(arc.GetTargetLayer()));",
        "    row.set(\"targetPrimPath\", arc.GetTargetPrimPath().GetAsString());",
        "    row.set(\"introducingLayer\", _LayerToVal(arc.GetIntroducingLayer()));",
        "    row.set(\"introducingPrimPath\", arc.GetIntroducingPrimPath().GetAsString());",
        "    row.set(\"isImplicit\", arc.IsImplicit());",
        "    row.set(\"isAncestral\", arc.IsAncestral());",
        "    row.set(\"hasSpecs\", arc.HasSpecs());",
        "    row.set(\"isIntroducedInRootLayerStack\", arc.IsIntroducedInRootLayerStack());",
        "    row.set(\"isIntroducedInRootLayerPrimSpec\", arc.IsIntroducedInRootLayerPrimSpec());",
        "    result.set(index++, row);",
        "  }",
        "  return result;",
        "}",
        "",
        "class _ObjectsChangedListener : public TfWeakBase {",
        "public:",
        "  explicit _ObjectsChangedListener(emscripten::val callback) : _callback(callback) {}",
        "  void OnObjectsChanged(UsdNotice::ObjectsChanged const& notice, UsdStageWeakPtr const& sender) {",
        "    emscripten::val payload = emscripten::val::object();",
        "    payload.set(\"resyncedPaths\", _PathRangeToVal(notice.GetResyncedPaths()));",
        "    payload.set(\"changedInfoOnlyPaths\", _PathRangeToVal(notice.GetChangedInfoOnlyPaths()));",
        "    payload.set(\"resolvedAssetPathsResyncedPaths\", _PathRangeToVal(notice.GetResolvedAssetPathsResyncedPaths()));",
        "    emscripten::val changedFields = emscripten::val::object();",
        "    for (SdfPath const& path : notice.GetResyncedPaths()) {",
        "      changedFields.set(path.GetAsString(), _TokenVectorToVal(notice.GetChangedFields(path)));",
        "    }",
        "    for (SdfPath const& path : notice.GetChangedInfoOnlyPaths()) {",
        "      changedFields.set(path.GetAsString(), _TokenVectorToVal(notice.GetChangedFields(path)));",
        "    }",
        "    payload.set(\"changedFields\", changedFields);",
        "    _callback(payload);",
        "  }",
        "  TfNotice::Key key;",
        "private:",
        "  emscripten::val _callback;",
        "};",
        "",
        "std::map<int, std::shared_ptr<_ObjectsChangedListener>>& _ObjectsChangedListeners()",
        "{",
        "  static std::map<int, std::shared_ptr<_ObjectsChangedListener>> listeners;",
        "  return listeners;",
        "}",
        "",
        "int _RegisterObjectsChanged(UsdStage& stage, emscripten::val callback)",
        "{",
        "  static int nextId = 1;",
        "  int id = nextId++;",
        "  std::shared_ptr<_ObjectsChangedListener> listener = std::make_shared<_ObjectsChangedListener>(callback);",
        "  listener->key = TfNotice::Register(TfCreateWeakPtr(listener.get()), &_ObjectsChangedListener::OnObjectsChanged, UsdStageWeakPtr(&stage));",
        "  _ObjectsChangedListeners()[id] = listener;",
        "  return id;",
        "}",
        "",
        "bool _RevokeObjectsChanged(int listenerId)",
        "{",
        "  auto& listeners = _ObjectsChangedListeners();",
        "  auto it = listeners.find(listenerId);",
        "  if (it == listeners.end()) {",
        "    return false;",
        "  }",
        "  TfNotice::Revoke(it->second->key);",
        "  listeners.erase(it);",
        "  return true;",
        "}",
        "",
        "void RegisterUsdCoreBindings()",
        "{",
        "  using namespace emscripten;",
        "",
        "  class_<pxr::SdfPath>(\"SdfPath\")",
        "    .constructor<std::string>()",
        "    .function(\"AppendChild\", optional_override([](pxr::SdfPath const& path, std::string const& childName) { return path.AppendChild(pxr::TfToken(childName)); }))",
        "    .function(\"AppendProperty\", optional_override([](pxr::SdfPath const& path, std::string const& propertyName) { return path.AppendProperty(pxr::TfToken(propertyName)); }))",
        "    .function(\"GetString\", optional_override([](pxr::SdfPath const& path) { return path.GetAsString(); }))",
        "    .function(\"GetAsString\", optional_override([](pxr::SdfPath const& path) { return path.GetAsString(); }))",
        "    .function(\"IsAbsolutePath\", &pxr::SdfPath::IsAbsolutePath)",
        "    .class_function(\"AbsoluteRootPath\", optional_override([]() { return pxr::SdfPath::AbsoluteRootPath(); }))",
        "    ;",
        "",
        "  class_<pxr::GfVec3f>(\"GfVec3f\")",
        "    .constructor<>()",
        "    .constructor<float>()",
        "    .constructor<float, float, float>()",
        "    .function(\"GetString\", optional_override([](pxr::GfVec3f const& value) { return pxr::TfStringify(value); }))",
        "    ;",
        "",
        "  class_<pxr::GfVec3d>(\"GfVec3d\")",
        "    .constructor<>()",
        "    .constructor<double>()",
        "    .constructor<double, double, double>()",
        "    .function(\"GetString\", optional_override([](pxr::GfVec3d const& value) { return pxr::TfStringify(value); }))",
        "    ;",
        "",
        "  class_<pxr::GfMatrix4d>(\"GfMatrix4d\")",
        "    .constructor<>()",
        "    .constructor<double>()",
        "    .function(\"SetTranslate\", optional_override([](pxr::GfMatrix4d& matrix, pxr::GfVec3d const& translation) { matrix.SetTranslate(translation); return matrix; }))",
        "    .function(\"GetString\", optional_override([](pxr::GfMatrix4d const& value) { return pxr::TfStringify(value); }))",
        "    .class_function(\"Identity\", optional_override([]() { return pxr::GfMatrix4d(1.0); }))",
        "    ;",
        "",
        "  class_<pxr::UsdGeomXformOp>(\"UsdGeomXformOp\")",
        "    .constructor<pxr::UsdAttribute, bool>()",
        "    .function(\"GetAttr\", &pxr::UsdGeomXformOp::GetAttr)",
        "    .function(\"IsDefined\", &pxr::UsdGeomXformOp::IsDefined)",
        "    .function(\"GetOpName\", optional_override([](pxr::UsdGeomXformOp const& op) { return op.GetOpName().GetString(); }))",
        "    .function(\"GetTypeName\", optional_override([](pxr::UsdGeomXformOp const& op) { return op.GetTypeName().GetAsToken().GetString(); }))",
        "    .function(\"Set\", optional_override([](pxr::UsdGeomXformOp const& op, emscripten::val value, double timeCode) { return _SetAttributeFromVal(op.GetAttr(), value, _TimeCode(timeCode)); }))",
        "    .function(\"Get\", optional_override([](pxr::UsdGeomXformOp const& op, double timeCode) { pxr::VtValue value; op.Get(&value, _TimeCode(timeCode)); return _VtValueToString(value); }))",
        "    ;",
        "",
        "  class_<pxr::UsdShadeInput>(\"UsdShadeInput\")",
        "    .constructor<pxr::UsdAttribute>()",
        "    .function(\"GetAttr\", &pxr::UsdShadeInput::GetAttr)",
        "    .function(\"GetFullName\", optional_override([](pxr::UsdShadeInput const& input) { return input.GetFullName().GetString(); }))",
        "    .function(\"GetBaseName\", optional_override([](pxr::UsdShadeInput const& input) { return input.GetBaseName().GetString(); }))",
        "    .function(\"GetPrim\", &pxr::UsdShadeInput::GetPrim)",
        "    .function(\"GetTypeName\", optional_override([](pxr::UsdShadeInput const& input) { return input.GetTypeName().GetAsToken().GetString(); }))",
        "    .function(\"Set\", optional_override([](pxr::UsdShadeInput const& input, emscripten::val value, double timeCode) { return _SetAttributeFromVal(input.GetAttr(), value, _TimeCode(timeCode)); }))",
        "    .function(\"ConnectToSource\", optional_override([](pxr::UsdShadeInput const& input, pxr::UsdShadeOutput const& output) { return input.ConnectToSource(output); }))",
        "    .function(\"ConnectToSourcePath\", optional_override([](pxr::UsdShadeInput const& input, std::string const& path) { return input.ConnectToSource(pxr::SdfPath(path)); }))",
        "    ;",
        "",
        "  class_<pxr::UsdShadeOutput>(\"UsdShadeOutput\")",
        "    .constructor<pxr::UsdAttribute>()",
        "    .function(\"GetAttr\", &pxr::UsdShadeOutput::GetAttr)",
        "    .function(\"GetFullName\", optional_override([](pxr::UsdShadeOutput const& output) { return output.GetFullName().GetString(); }))",
        "    .function(\"GetBaseName\", optional_override([](pxr::UsdShadeOutput const& output) { return output.GetBaseName().GetString(); }))",
        "    .function(\"GetPrim\", &pxr::UsdShadeOutput::GetPrim)",
        "    .function(\"GetTypeName\", optional_override([](pxr::UsdShadeOutput const& output) { return output.GetTypeName().GetAsToken().GetString(); }))",
        "    .function(\"Set\", optional_override([](pxr::UsdShadeOutput const& output, emscripten::val value, double timeCode) { return _SetAttributeFromVal(output.GetAttr(), value, _TimeCode(timeCode)); }))",
        "    .function(\"ConnectToSource\", optional_override([](pxr::UsdShadeOutput const& output, pxr::UsdShadeOutput const& source) { return output.ConnectToSource(source); }))",
        "    .function(\"ConnectToSourcePath\", optional_override([](pxr::UsdShadeOutput const& output, std::string const& path) { return output.ConnectToSource(pxr::SdfPath(path)); }))",
        "    ;",
        "",
        "  class_<pxr::UsdShadeMaterialBindingAPI>(\"UsdShadeMaterialBindingAPI\")",
        "    .constructor<pxr::UsdPrim>()",
        "    .function(\"Bind\", optional_override([](pxr::UsdShadeMaterialBindingAPI const& api, pxr::UsdShadeMaterial const& material) { return api.Bind(material); }))",
        "    ;",
        "",
        "  class_<pxr::UsdGeomXformCommonAPI>(\"UsdGeomXformCommonAPI\")",
        "    .constructor<pxr::UsdPrim>()",
        "    .function(\"SetTranslate\", optional_override([](pxr::UsdGeomXformCommonAPI const& api, pxr::GfVec3d const& value, double timeCode) { return api.SetTranslate(value, _TimeCode(timeCode)); }))",
        "    .function(\"SetRotate\", optional_override([](pxr::UsdGeomXformCommonAPI const& api, pxr::GfVec3f const& value, double timeCode) { return api.SetRotate(value, pxr::UsdGeomXformCommonAPI::RotationOrderXYZ, _TimeCode(timeCode)); }))",
        "    .function(\"SetScale\", optional_override([](pxr::UsdGeomXformCommonAPI const& api, pxr::GfVec3f const& value, double timeCode) { return api.SetScale(value, _TimeCode(timeCode)); }))",
        "    ;",
        "",
    ]

    for cls in manifest["classes"]:
        lines.append(f"  class_<{cls['cppType']}>(\"{cls['jsName']}\")")
        for method in cls["methods"]:
            expr, allow_raw = _lambda(cls["cppType"], method)
            suffix = _policies(method, allow_raw)
            lines.append(f"    .function(\"{method['jsName']}\", {expr}{suffix})")
        lines.append("    ;")
        lines.append("")

    for schema in manifest.get("schemas", []):
        lines.append(f"  class_<{schema['cppType']}>(\"{schema['jsName']}\")")
        lines.append("    .constructor<>()")
        lines.append("    .constructor<pxr::UsdPrim>()")
        lines.append(f"    .function(\"IsValid\", optional_override([]({schema['cppType']} const& schema) {{ return static_cast<bool>(schema); }}))")
        lines.append(f"    .function(\"GetPrim\", &{schema['cppType']}::GetPrim)")
        lines.append(f"    .function(\"GetPath\", optional_override([]({schema['cppType']} const& schema) {{ return schema.GetPath(); }}))")
        if schema.get("define", True):
            lines.append(f"    .class_function(\"Define\", {_schema_define(schema)})")
        lines.append(f"    .class_function(\"Get\", {_schema_get(schema)})")
        for attr in schema.get("attributes", []):
            lines.append(f"    .function(\"Get{attr['name']}Attr\", {_schema_get_attr(schema, attr)})")
            lines.append(f"    .function(\"Create{attr['name']}Attr\", {_schema_create_attr(schema, attr)})")
        for method in schema.get("extraMethods", []):
            lines.append(f"    .function(\"{method}\", {_schema_extra_method(schema, method)})")
        for method in schema.get("methods", []):
            lines.append(f"    .function(\"{method['jsName']}\", {method['cpp']})")
        lines.append("    ;")
        lines.append("")

    for vector in manifest.get("vectors", []):
        lines.append(f"  register_vector<{vector['cppType']}>(\"{vector['jsName']}\");")

    if manifest.get("functions"):
        lines.append("")
    for function in manifest.get("functions", []):
        expr, allow_raw = _function_expr(function)
        suffix = _policies(function, allow_raw)
        lines.append(f"  function(\"{function['jsName']}\", {expr}{suffix});")

    lines.extend([
        "}",
        "",
        "}} // namespace pxr::hdEmscriptenGenerated",
        "",
    ])
    return "\n".join(lines)


def generate_dts(manifest: dict) -> str:
    lines = [
        "// Generated by pxr/usdImaging/hdEmscripten/bindgen/generate_bindings.py.",
        "// Do not edit by hand; edit bindgen/core-bindings.json instead.",
        "",
        "declare type MaybePromise<T> = T | Promise<T>",
        "",
        "/** @internal Low-level Emscripten std::vector wrapper, not a USD authoring API. */",
        "declare type EmbindVector<T> = {",
        "    size(): number,",
        "    get(index: number): T,",
        "    /** @internal C++ std::vector append hook exposed by embind. */",
        "    push_back(value: T): void,",
        "    delete(): void,",
        "}",
        "",
    ]
    for vector in manifest.get("vectors", []):
        lines.extend([
            f"declare type {vector['tsName']} = EmbindVector<{vector['elementType']}>",
            "",
        ])
    lines.extend([
        "declare type USDLayerOffset = {",
        "    offset: number,",
        "    scale: number,",
        "    isIdentity: boolean,",
        "}",
        "",
        "declare type USDLayerInfo = {",
        "    identifier: string,",
        "    displayName: string,",
        "    realPath: string,",
        "}",
        "",
        "declare type USDSpecStackEntry = {",
        "    path: string,",
        "    layer: USDLayerInfo,",
        "    layerOffset?: USDLayerOffset,",
        "    metadata: Record<string, string>,",
        "    specifier?: string,",
        "    typeName?: string,",
        "    name?: string,",
        "}",
        "",
        "declare type USDSpecStack = USDSpecStackEntry[]",
        "",
        "declare type USDResolveInfo = {",
        "    source: \"None\" | \"Fallback\" | \"Default\" | \"TimeSamples\" | \"ValueClips\" | \"Spline\" | \"Unknown\",",
        "    hasAuthoredValueOpinion: boolean,",
        "    hasAuthoredValue: boolean,",
        "    valueIsBlocked: boolean,",
        "    valueSourceMightBeTimeVarying: boolean,",
        "    hasNextWeakerInfo: boolean,",
        "}",
        "",
        "declare type USDPcpNode = {",
        "    arcType?: string,",
        "    path?: string,",
        "    pathAtIntroduction?: string,",
        "    layerStackIdentifier?: string,",
        "    children?: USDPcpNode[],",
        "}",
        "",
        "declare type USDPrimIndex = {",
        "    isValid: boolean,",
        "    rootNode?: USDPcpNode,",
        "}",
        "",
        "declare type USDCompositionArc = {",
        "    arcType: string,",
        "    targetLayer: USDLayerInfo,",
        "    targetPrimPath: string,",
        "    introducingLayer: USDLayerInfo,",
        "    introducingPrimPath: string,",
        "    isImplicit: boolean,",
        "    isAncestral: boolean,",
        "    hasSpecs: boolean,",
        "    isIntroducedInRootLayerStack: boolean,",
        "    isIntroducedInRootLayerPrimSpec: boolean,",
        "}",
        "",
        "declare type USDObjectsChangedNotice = {",
        "    resyncedPaths: string[],",
        "    changedInfoOnlyPaths: string[],",
        "    resolvedAssetPathsResyncedPaths: string[],",
        "    changedFields: Record<string, string[]>,",
        "}",
        "",
        "declare type SdfPath = {",
        "    AppendChild(childName: string): SdfPath,",
        "    AppendProperty(propertyName: string): SdfPath,",
        "    GetString(): string,",
        "    GetAsString(): string,",
        "    IsAbsolutePath(): boolean,",
        "    delete(): void,",
        "}",
        "",
        "declare type GfVec3f = { GetString(): string, delete(): void }",
        "declare type GfVec3d = { GetString(): string, delete(): void }",
        "declare type GfMatrix4d = {",
        "    SetTranslate(value: GfVec3d): GfMatrix4d,",
        "    GetString(): string,",
        "    delete(): void,",
        "}",
        "",
        "declare type UsdGeomXformOp = {",
        "    GetAttr(): USDAttribute,",
        "    IsDefined(): boolean,",
        "    GetOpName(): string,",
        "    GetTypeName(): string,",
        "    Set(value: unknown, timeCode: number): boolean,",
        "    Get(timeCode: number): string,",
        "    delete(): void,",
        "}",
        "",
        "declare type UsdShadeInput = {",
        "    GetAttr(): USDAttribute,",
        "    GetFullName(): string,",
        "    GetBaseName(): string,",
        "    GetPrim(): USDPrim,",
        "    GetTypeName(): string,",
        "    Set(value: unknown, timeCode: number): boolean,",
        "    ConnectToSource(output: UsdShadeOutput): boolean,",
        "    ConnectToSourcePath(path: string): boolean,",
        "    delete(): void,",
        "}",
        "",
        "declare type UsdShadeOutput = {",
        "    GetAttr(): USDAttribute,",
        "    GetFullName(): string,",
        "    GetBaseName(): string,",
        "    GetPrim(): USDPrim,",
        "    GetTypeName(): string,",
        "    Set(value: unknown, timeCode: number): boolean,",
        "    ConnectToSource(output: UsdShadeOutput): boolean,",
        "    ConnectToSourcePath(path: string): boolean,",
        "    delete(): void,",
        "}",
        "",
        "declare type UsdShadeMaterialBindingAPI = { Bind(material: UsdShadeMaterial): boolean, delete(): void }",
        "",
        "declare type UsdGeomXformCommonAPI = {",
        "    SetTranslate(value: GfVec3d, timeCode: number): boolean,",
        "    SetRotate(value: GfVec3f, timeCode: number): boolean,",
        "    SetScale(value: GfVec3f, timeCode: number): boolean,",
        "    delete(): void,",
        "}",
        "",
    ])
    for cls in manifest["classes"]:
        lines.append(f"declare type {cls['tsName']} = {{")
        for method in cls["methods"]:
            lines.append(f"    {method['jsName']}({_args(method)}): {_return_type(method)},")
        lines.append("}")
        lines.append("")
    for schema in manifest.get("schemas", []):
        lines.append(f"declare type {schema['tsName']} = {{")
        lines.append("    IsValid(): boolean,")
        lines.append("    GetPrim(): USDPrim,")
        lines.append("    GetPath(): SdfPath,")
        for attr in schema.get("attributes", []):
            lines.append(f"    Get{attr['name']}Attr(): USDAttribute,")
            lines.append(f"    Create{attr['name']}Attr(defaultValue: unknown, writeSparsely: boolean): USDAttribute,")
        for method in schema.get("extraMethods", []):
            lines.append(f"    {method}(): {_schema_extra_ts(method)},")
        for method in schema.get("methods", []):
            lines.append(f"    {method['jsName']}({_args(method)}): {_return_type(method)},")
        lines.append("    delete(): void,")
        lines.append("}")
        lines.append("")
    for function in manifest.get("functions", []):
        lines.append(f"declare function {function['jsName']}({_args(function)}): {_return_type(function)}")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--cpp-out", required=True)
    parser.add_argument("--dts-out", required=True)
    args = parser.parse_args()

    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    Path(args.cpp_out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.dts_out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.cpp_out).write_text(generate_cpp(manifest), encoding="utf-8")
    Path(args.dts_out).write_text(generate_dts(manifest), encoding="utf-8")


if __name__ == "__main__":
    main()
