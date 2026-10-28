#ifndef PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_PHYSICS_H
#define PXR_USD_IMAGING_HD_EMSCRIPTEN_WEB_PHYSICS_H

#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/imaging/hd/meshTopology.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdPhysics/collisionAPI.h"
#include "pxr/usd/usdPhysics/massAPI.h"
#include "pxr/usd/usdPhysics/metrics.h"
#include "pxr/usd/usdPhysics/parseUtils.h"
#include <cmath>
#include <set>

PXR_NAMESPACE_OPEN_SCOPE
namespace HdWebPhysics {
inline JsValue Number(double value) { return std::isfinite(value) ? JsValue(value) : JsValue(); }
inline JsValue Vector(const GfVec3f& v) { return JsValue(JsArray{Number(v[0]), Number(v[1]), Number(v[2])}); }
inline JsValue Quaternion(const GfQuatf& q) {
    const auto& v = q.GetImaginary();
    return JsValue(JsArray{Number(v[0]), Number(v[1]), Number(v[2]), Number(q.GetReal())});
}
inline bool Finite(const GfVec3f& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
inline JsValue Paths(const SdfPathVector& paths) {
    JsArray out; for (const auto& path : paths) out.emplace_back(path.GetString()); return JsValue(out);
}
inline JsValue Axis(UsdPhysicsAxis axis) { return JsValue(axis == UsdPhysicsAxis::X ? "X" : axis == UsdPhysicsAxis::Y ? "Y" : "Z"); }
inline JsValue Limit(const UsdPhysicsJointLimit& l) {
    return JsValue(JsObject{{"enabled", JsValue(l.enabled)}, {"lower", Number(l.lower)}, {"upper", Number(l.upper)}});
}
inline JsValue Drive(const UsdPhysicsJointDrive& d) {
    return JsValue(JsObject{{"enabled", JsValue(d.enabled)}, {"targetPosition", Number(d.targetPosition)},
        {"targetVelocity", Number(d.targetVelocity)}, {"stiffness", Number(d.stiffness)}, {"damping", Number(d.damping)},
        {"acceleration", JsValue(d.acceleration)}, {"forceLimit", Number(d.forceLimit)}});
}
inline const char* Dof(UsdPhysicsJointDOF dof) {
    switch (dof) {
    case UsdPhysicsJointDOF::Distance: return "distance";
    case UsdPhysicsJointDOF::TransX: return "transX";
    case UsdPhysicsJointDOF::TransY: return "transY";
    case UsdPhysicsJointDOF::TransZ: return "transZ";
    case UsdPhysicsJointDOF::RotX: return "rotX";
    case UsdPhysicsJointDOF::RotY: return "rotY";
    case UsdPhysicsJointDOF::RotZ: return "rotZ";
    } return "unknown";
}

class Exporter {
public:
    explicit Exporter(const UsdStageRefPtr& stage) : stage(stage) {}
    JsArray scenes, bodies, shapes, joints, materials, groups, unsupported;
    std::set<SdfPath> reportedShapes;
    const UsdStageRefPtr stage;

    void Unsupported(const SdfPath& path, const std::string& type, const char* reason) {
        unsupported.emplace_back(JsObject{{"path", JsValue(path.GetString())}, {"type", JsValue(type)}, {"reason", JsValue(reason)}});
    }
    void Mass(JsObject& out, const SdfPath& path) {
        const UsdPhysicsMassAPI api(stage->GetPrimAtPath(path));
        float mass = 0, density = 0;
        if (api) { api.GetMassAttr().Get(&mass); api.GetDensityAttr().Get(&density); }
        out["mass"] = Number(std::isfinite(mass) && mass > 0 ? mass : 0);
        out["density"] = Number(std::isfinite(density) && density > 0 ? density : 0);
        if (!api) return;
        GfVec3f center, inertia; GfQuatf axes;
        if (api.GetCenterOfMassAttr().Get(&center) && Finite(center)) out["centerOfMass"] = Vector(center);
        if (api.GetDiagonalInertiaAttr().Get(&inertia) && Finite(inertia) && inertia != GfVec3f(0)) out["diagonalInertia"] = Vector(inertia);
        if (api.GetPrincipalAxesAttr().Get(&axes) && std::isfinite(axes.GetReal()) && Finite(axes.GetImaginary()) && axes.GetLength() > 0) out["principalAxes"] = Quaternion(axes);
    }
    JsObject Object(const UsdPhysicsObjectDesc& d) { return {{"path", JsValue(d.primPath.GetString())}}; }
    JsObject Shape(const UsdPhysicsShapeDesc& d, const char* type) {
        reportedShapes.insert(d.primPath);
        JsObject out = Object(d);
        out["type"] = JsValue(type); out["body"] = JsValue(d.rigidBody.GetString());
        out["position"] = Vector(d.localPos); out["rotation"] = Quaternion(d.localRot); out["scale"] = Vector(d.localScale);
        out["collisionEnabled"] = JsValue(d.collisionEnabled); out["materials"] = Paths(d.materials);
        out["filteredCollisions"] = Paths(d.filteredCollisions); out["collisionGroups"] = Paths(d.collisionGroups);
        out["simulationOwners"] = Paths(d.simulationOwners); Mass(out, d.primPath); return out;
    }
    JsObject Joint(const UsdPhysicsJointDesc& d, const char* type) {
        JsObject out = Object(d); out["type"] = JsValue(type);
        out["body0"] = JsValue(d.body0.GetString()); out["body1"] = JsValue(d.body1.GetString());
        out["position0"] = Vector(d.localPose0Position); out["position1"] = Vector(d.localPose1Position);
        out["rotation0"] = Quaternion(d.localPose0Orientation); out["rotation1"] = Quaternion(d.localPose1Orientation);
        out["jointEnabled"] = JsValue(d.jointEnabled); out["collisionEnabled"] = JsValue(d.collisionEnabled);
        out["breakForce"] = Number(d.breakForce); out["breakTorque"] = Number(d.breakTorque); return out;
    }
    void Add(const UsdPhysicsSceneDesc& d) {
        auto out = Object(d); out["gravityDirection"] = Vector(d.gravityDirection); out["gravityMagnitude"] = Number(d.gravityMagnitude); scenes.emplace_back(out);
    }
    void Add(const UsdPhysicsRigidBodyDesc& d) {
        auto out = Object(d); out["position"] = Vector(d.position); out["rotation"] = Quaternion(d.rotation); out["scale"] = Vector(d.scale);
        out["rigidBodyEnabled"] = JsValue(d.rigidBodyEnabled); out["kinematicBody"] = JsValue(d.kinematicBody); out["startsAsleep"] = JsValue(d.startsAsleep);
        out["linearVelocity"] = Vector(d.linearVelocity); out["angularVelocity"] = Vector(d.angularVelocity);
        out["filteredCollisions"] = Paths(d.filteredCollisions); out["simulationOwners"] = Paths(d.simulationOwners); Mass(out,d.primPath); bodies.emplace_back(out);
    }
    void Add(const UsdPhysicsRigidBodyMaterialDesc& d) {
        auto out = Object(d); out["staticFriction"] = Number(d.staticFriction); out["dynamicFriction"] = Number(d.dynamicFriction);
        out["restitution"] = Number(d.restitution); out["density"] = Number(d.density > 0 ? d.density : 0); materials.emplace_back(out);
    }
    void Add(const UsdPhysicsCollisionGroupDesc& d) {
        auto out = Object(d); out["filteredGroups"] = Paths(d.filteredGroups); out["invertFilteredGroups"] = JsValue(d.invertFilteredGroups);
        out["mergedGroups"] = Paths(d.mergedGroups); groups.emplace_back(out);
    }
    void Add(const UsdPhysicsCubeShapeDesc& d) { auto out = Shape(d,"cube"); out["halfExtents"] = Vector(d.halfExtents); shapes.emplace_back(out); }
    void Add(const UsdPhysicsSphereShapeDesc& d) { auto out = Shape(d,"sphere"); out["radius"] = Number(d.radius); shapes.emplace_back(out); }
    template<class T> void AxialShape(const T& d, const char* type) {
        auto out = Shape(d,type); out["radius"] = Number(d.radius); out["halfHeight"] = Number(d.halfHeight); out["axis"] = Axis(d.axis); shapes.emplace_back(out);
    }
    void Add(const UsdPhysicsCapsuleShapeDesc& d) { AxialShape(d,"capsule"); }
    void Add(const UsdPhysicsCylinderShapeDesc& d) { AxialShape(d,"cylinder"); }
    void Add(const UsdPhysicsConeShapeDesc& d) { AxialShape(d,"cone"); }
    void Add(const UsdPhysicsPlaneShapeDesc& d) { auto out = Shape(d,"plane"); out["axis"] = Axis(d.axis); shapes.emplace_back(out); }
    void Add(const UsdPhysicsMeshShapeDesc& d) {
        auto out = Shape(d,"mesh"); out["approximation"] = JsValue(d.approximation.GetString()); out["meshScale"] = Vector(d.meshScale);
        const UsdGeomMesh mesh(stage->GetPrimAtPath(d.primPath));
        VtVec3fArray points; VtIntArray counts, indices, holes; TfToken orientation;
        if (!mesh || !mesh.GetPointsAttr().Get(&points) || !mesh.GetFaceVertexCountsAttr().Get(&counts) || !mesh.GetFaceVertexIndicesAttr().Get(&indices)) {
            Unsupported(d.primPath,"mesh","Missing mesh collision topology"); return;
        }
        size_t indexCount = 0;
        for (int count : counts) { if (count < 3) { Unsupported(d.primPath,"mesh","Degenerate mesh collision face"); return; } indexCount += count; }
        if (indexCount != indices.size()) { Unsupported(d.primPath,"mesh","Inconsistent mesh collision topology"); return; }
        for (int i : indices) if (i < 0 || size_t(i) >= points.size()) { Unsupported(d.primPath,"mesh","Mesh collision index out of range"); return; }
        mesh.GetHoleIndicesAttr().Get(&holes); mesh.GetOrientationAttr().Get(&orientation);
        HdMeshTopology topology(TfToken("none"), orientation, counts, indices, holes);
        VtVec3iArray triangles; VtIntArray params; HdMeshUtil(&topology,d.primPath).ComputeTriangleIndices(&triangles,&params);
        JsArray pointValues, indexValues;
        for (const auto& point : points) { if (!Finite(point)) { Unsupported(d.primPath,"mesh","Non-finite mesh point"); return; } for (int c=0;c<3;c++) pointValues.emplace_back(double(point[c])); }
        for (const auto& triangle : triangles) for (int c=0;c<3;c++) indexValues.emplace_back(triangle[c]);
        out["points"] = JsValue(pointValues); out["indices"] = JsValue(indexValues); shapes.emplace_back(out);
    }
    void Add(const UsdPhysicsFixedJointDesc& d) { joints.emplace_back(Joint(d,"fixed")); }
    template<class T> void AxialJoint(const T& d, const char* type) {
        auto out=Joint(d,type); out["axis"]=Axis(d.axis); out["limit"]=Limit(d.limit); out["drive"]=Drive(d.drive); joints.emplace_back(out);
    }
    void Add(const UsdPhysicsRevoluteJointDesc& d) { AxialJoint(d,"revolute"); }
    void Add(const UsdPhysicsPrismaticJointDesc& d) { AxialJoint(d,"prismatic"); }
    void Add(const UsdPhysicsSphericalJointDesc& d) { auto out=Joint(d,"spherical"); out["axis"]=Axis(d.axis); out["limit"]=Limit(d.limit); joints.emplace_back(out); }
    void Add(const UsdPhysicsDistanceJointDesc& d) {
        auto out=Joint(d,"distance"); out["limit"]=Limit(d.limit); out["minEnabled"]=JsValue(d.minEnabled); out["maxEnabled"]=JsValue(d.maxEnabled); joints.emplace_back(out);
    }
    void Add(const UsdPhysicsD6JointDesc& d) {
        auto out=Joint(d,"d6"); JsArray limits, drives;
        for (const auto& entry : d.jointLimits) limits.emplace_back(JsObject{{"dof",JsValue(Dof(entry.first))},{"limit",Limit(entry.second)}});
        for (const auto& entry : d.jointDrives) drives.emplace_back(JsObject{{"dof",JsValue(Dof(entry.first))},{"drive",Drive(entry.second)}});
        out["limits"]=JsValue(limits); out["drives"]=JsValue(drives); joints.emplace_back(out);
    }
    template<class T> void Batch(TfSpan<const SdfPath> paths, TfSpan<const UsdPhysicsObjectDesc> span, const char* type, bool supported=true) {
        // _CallReportFn exposes derived vector storage as a base span. Cast
        // ONCE before indexing: indexing span[i] uses the wrong sizeof(base).
        const T* values=static_cast<const T*>(span.data());
        for (size_t i=0;i<paths.size();i++) {
            if (!values[i].isValid) { Unsupported(paths[i],type,"Invalid OpenUSD physics descriptor"); continue; }
            if (!supported) { Unsupported(paths[i],type,"Physics descriptor is not supported by the web adapter"); continue; }
            Add(values[i]);
        }
    }
    // Unsupported overloads permit the same typed batch dispatch without
    // pretending tapered shapes, sphere clouds or articulations are supported.
    template<class T> void Add(const T&) {}
    void Report(UsdPhysicsObjectType type, TfSpan<const SdfPath> paths, TfSpan<const UsdPhysicsObjectDesc> span) {
#define HD_WEB_PHYSICS_CASE(Name, Supported) case UsdPhysicsObjectType::Name: Batch<UsdPhysics##Name##Desc>(paths,span,#Name,Supported); break
        switch(type) {
        HD_WEB_PHYSICS_CASE(Scene,true); HD_WEB_PHYSICS_CASE(RigidBody,true); HD_WEB_PHYSICS_CASE(RigidBodyMaterial,true); HD_WEB_PHYSICS_CASE(CollisionGroup,true);
        HD_WEB_PHYSICS_CASE(CubeShape,true); HD_WEB_PHYSICS_CASE(SphereShape,true); HD_WEB_PHYSICS_CASE(CapsuleShape,true); HD_WEB_PHYSICS_CASE(CylinderShape,true);
        HD_WEB_PHYSICS_CASE(ConeShape,true); HD_WEB_PHYSICS_CASE(PlaneShape,true); HD_WEB_PHYSICS_CASE(MeshShape,true);
        HD_WEB_PHYSICS_CASE(FixedJoint,true); HD_WEB_PHYSICS_CASE(RevoluteJoint,true); HD_WEB_PHYSICS_CASE(PrismaticJoint,true);
        HD_WEB_PHYSICS_CASE(SphericalJoint,true); HD_WEB_PHYSICS_CASE(DistanceJoint,true); HD_WEB_PHYSICS_CASE(D6Joint,true);
        HD_WEB_PHYSICS_CASE(Capsule1Shape,false); HD_WEB_PHYSICS_CASE(Cylinder1Shape,false); HD_WEB_PHYSICS_CASE(CustomShape,false);
        HD_WEB_PHYSICS_CASE(SpherePointsShape,false); HD_WEB_PHYSICS_CASE(CustomJoint,false); HD_WEB_PHYSICS_CASE(Articulation,false);
        default: for (const auto& path : paths) Unsupported(path,std::to_string(int(type)),"Unknown physics descriptor type");
        }
#undef HD_WEB_PHYSICS_CASE
    }
};

inline std::string SceneJson(const UsdStageRefPtr& stage) {
    if (!stage) return "{}";
    Exporter out(stage);
    const bool success=UsdPhysicsLoadStageFromPrimRange(stage,{SdfPath::AbsoluteRootPath()},
        [&out](UsdPhysicsObjectType type, TfSpan<const SdfPath> paths, TfSpan<const UsdPhysicsObjectDesc> descs, const VtValue&) { out.Report(type,paths,descs); },VtValue());
    if (!success) out.Unsupported(SdfPath::AbsoluteRootPath(),"stage","OpenUSD physics parsing failed");
    // The parser can omit an unknown CollisionAPI geometry entirely. Preserve
    // that limitation explicitly rather than reporting a complete empty scene.
    for (const UsdPrim& prim : stage->Traverse()) if (prim.HasAPI<UsdPhysicsCollisionAPI>() && !out.reportedShapes.count(prim.GetPath())) {
        bool reported=false; for (const auto& issue : out.unsupported) if (issue.GetJsObject().at("path").GetString()==prim.GetPath().GetString()) reported=true;
        if (!reported) out.Unsupported(prim.GetPath(),prim.GetTypeName().GetString(),"Collision geometry was not recognized by the OpenUSD physics parser");
    }
    return JsWriteToString(JsValue(JsObject{{"metersPerUnit",Number(UsdGeomGetStageMetersPerUnit(stage))},
        {"kilogramsPerUnit",Number(UsdPhysicsGetStageKilogramsPerUnit(stage))}, {"upAxis",JsValue(UsdGeomGetStageUpAxis(stage).GetString())},
        {"scenes",JsValue(out.scenes)}, {"bodies",JsValue(out.bodies)}, {"shapes",JsValue(out.shapes)}, {"joints",JsValue(out.joints)},
        {"materials",JsValue(out.materials)}, {"groups",JsValue(out.groups)}, {"unsupported",JsValue(out.unsupported)}}));
}
} // namespace HdWebPhysics
PXR_NAMESPACE_CLOSE_SCOPE
#endif
