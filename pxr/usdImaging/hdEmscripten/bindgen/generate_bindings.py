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
    if op == "primGetRelationship":
        return """emscripten::optional_override([](pxr::UsdPrim const& prim, std::string const& name) {
      return prim.GetRelationship(pxr::TfToken(name));
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
        "std::vector<UsdStageRefPtr>& _StageRegistry()",
        "{",
        "  static std::vector<UsdStageRefPtr> stages;",
        "  return stages;",
        "}",
        "",
        "void RegisterUsdCoreBindings()",
        "{",
        "  using namespace emscripten;",
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
    ]
    for vector in manifest.get("vectors", []):
        lines.extend([
            f"declare type {vector['tsName']} = {{",
            "    size(): number,",
            f"    get(index: number): {vector['elementType']},",
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
