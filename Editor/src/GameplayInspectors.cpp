// Inspectors of the components in Scene/GameplayComponents.h (drawn inside Panels.cpp's component sections)
#include "GameplayInspectors.h"
#include "InspectorWidgets.h"
#include "AssetUI.h"
#include "Scene/Scene.h"
#include "Scene/Navigation.h"
#include "Scene/Splines.h"
#include "Project/Project.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <format>

namespace Stardust::GameplayInspector {

static bool Float(const char* label, float& v, float speed, float mn, float mx) {
    bool changed = Insp::DragFloat(label, &v, speed, mn, mx);
    if (mx > mn) v = std::clamp(v, mn, mx);
    return changed;
}
static bool Bool(const char* label, bool& v) { return Insp::Checkbox(label, &v); }
static bool Vec3(const char* label, glm::vec3& v, float speed = 0.01f) { return Insp::DragFloat3(label, glm::value_ptr(v), speed); }

static bool EntityRef(Scene& scene, const char* label, u64& uuid) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    nlohmann::json ref = uuid ? std::format("{:016x}", uuid) : "0";
    bool changed = false;
    if (AssetUI::EntityField(scene, "Entity", ref)) { uuid = std::strtoull(ref.get<std::string>().c_str(), nullptr, 16); changed = true; }
    ImGui::PopID();
    return changed;
}

void Draw(Scene&, EntityID, CharacterControllerComponent& c, bool playing) {
    Float("Slope Limit", c.slopeLimit, 0.5f, 0.0f, 89.0f);
    Float("Step Offset", c.stepOffset, 0.01f, 0.0f, 5.0f);
    Float("Skin Width", c.skinWidth, 0.001f, 0.001f, 0.2f);
    Float("Min Move Distance", c.minMoveDistance, 0.001f, 0.0f, 1.0f);
    Vec3("Center", c.center);
    Float("Radius", c.radius, 0.01f, 0.01f, 50.0f);
    Float("Height", c.height, 0.01f, 0.02f, 100.0f);
    Float("Push Force (N)", c.pushForce, 1.0f, 0.0f, 100000.0f);
    Float("Mass (kg)", c.mass, 0.5f, 1.0f, 10000.0f);
    ImGui::TextDisabled("Moved by scripts: CharacterController.Move / SimpleMove.");
    if (playing) ImGui::Text("Grounded %s   Velocity %.2f m/s", c.grounded ? "yes" : "no", glm::length(c.velocity));
}

void Draw(Scene& scene, EntityID, JointComponent& c, bool playing) {
    Insp::Combo("Type", &c.type, "Fixed\0Hinge\0Spring\0Slider\0Ball (cone and twist)\0");
    EntityRef(scene, "Connected Body (none = the world)", c.connectedBody);
    Vec3("Anchor", c.anchor);
    if (c.type == 1 || c.type == 3 || c.type == 4) Vec3("Axis", c.axis);
    Bool("Enable Collision", c.enableCollision);
    if (c.type != 0) {
        Bool(c.type == 2 ? "Min / Max Distance" : "Use Limits", c.useLimits);
        if (c.useLimits) {
            if (c.type == 4) { Float("Swing Limit", c.swingLimit, 0.5f, 0.0f, 179.0f); Float("Twist Min", c.twistMin, 0.5f, -179.0f, 0.0f); Float("Twist Max", c.twistMax, 0.5f, 0.0f, 179.0f); }
            else { const float range = c.type == 1 ? 179.0f : 1000.0f; Float(c.type == 1 ? "Min Angle" : "Min", c.minLimit, 0.1f, c.type == 2 ? 0.0f : -range, range); Float(c.type == 1 ? "Max Angle" : "Max", c.maxLimit, 0.1f, c.type == 2 ? 0.0f : -range, range); }
        }
    }
    if (c.type == 2 || (c.useLimits && c.type != 0 && c.type != 4)) {
        Float("Spring Frequency (Hz)", c.springFrequency, 0.05f, 0.0f, 100.0f);
        Float("Spring Damping", c.springDamping, 0.01f, 0.0f, 1.0f);
    }
    if (c.type == 1 || c.type == 3) {
        Bool("Use Motor", c.useMotor);
        if (c.useMotor) { Float(c.type == 1 ? "Target Velocity (deg/s)" : "Target Velocity (m/s)", c.motorTargetVelocity, 0.5f, -100000.0f, 100000.0f); Float("Max Force", c.motorForce, 1.0f, 0.0f, 1e7f); }
    }
    if (c.type == 1 || c.type == 3 || c.type == 4) Float("Friction", c.friction, 0.1f, 0.0f, 1e6f);
    Float("Break Force (0 = never)", c.breakForce, 10.0f, 0.0f, 1e9f);
    Float("Break Torque (0 = never)", c.breakTorque, 10.0f, 0.0f, 1e9f);
    ImGui::TextDisabled("Needs a Rigidbody on this object (and on the connected one).");
    if (playing) {
        if (c.broken) ImGui::TextColored(ImVec4(1, .5f, .4f, 1), "Broken");
        else ImGui::Text("Force %.0f N   Torque %.0f N m%s", glm::length(c.currentForce), glm::length(c.currentTorque), c.type == 1 ? std::format("   Angle {:.1f}", c.currentAngle).c_str() : "");
    }
}

void Draw(Scene& scene, EntityID e, NavMeshSurfaceComponent& c, bool playing) {
    Float("Agent Radius", c.agentRadius, 0.01f, 0.05f, 20.0f);
    Float("Agent Height", c.agentHeight, 0.01f, 0.1f, 50.0f);
    Float("Max Slope", c.maxSlope, 0.5f, 0.0f, 89.0f);
    Float("Step Height", c.stepHeight, 0.01f, 0.0f, 10.0f);
    if (ImGui::TreeNode("Advanced")) {
        Float("Cell Size", c.cellSize, 0.005f, 0.02f, 5.0f);
        Float("Cell Height", c.cellHeight, 0.005f, 0.02f, 5.0f);
        Float("Min Region Area", c.minRegionArea, 0.1f, 0.0f, 10000.0f);
        Insp::InputInt("Tile Size (voxels)", &c.tileSize); c.tileSize = std::clamp(c.tileSize, 16, 512);
        Insp::Combo("Collect Geometry", &c.collectFrom, "Render Meshes\0Physics Colliders\0");
        Bool("Use Bounds", c.useBounds);
        if (c.useBounds) { Vec3("Bounds Center", c.boundsCenter, 0.1f); Vec3("Bounds Size", c.boundsSize, 0.1f); }
        ImGui::TreePop();
    }
    AssetUI::AssetField("Data", c.data, { ".navmesh" });
    ImGui::BeginDisabled(playing);
    if (ImGui::Button("Bake")) {
        std::string error;
        if (Navigation::Bake(scene, e, &error)) SD_INFO("Nav mesh baked: {}", c.data);
        else SD_ERROR("Nav mesh bake failed: {}", error);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { c.data.clear(); c.loadedVersion = 0; Navigation::Unload(scene); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    Bool("Show##navmesh", Navigation::DebugDraw());
    auto info = Navigation::Info(scene);
    if (!info.empty()) ImGui::TextDisabled("%s", info.c_str());
}

void Draw(Scene&, EntityID, NavMeshAgentComponent& c, bool playing) {
    Float("Speed", c.speed, 0.05f, 0.0f, 1000.0f);
    Float("Angular Speed", c.angularSpeed, 1.0f, 0.0f, 100000.0f);
    Float("Acceleration", c.acceleration, 0.05f, 0.0f, 1000.0f);
    Float("Stopping Distance", c.stoppingDistance, 0.01f, 0.0f, 1000.0f);
    Bool("Auto Braking", c.autoBraking);
    Float("Radius", c.radius, 0.01f, 0.05f, 50.0f);
    Float("Height", c.height, 0.01f, 0.1f, 50.0f);
    Float("Base Offset", c.baseOffset, 0.01f, -100.0f, 100.0f);
    Insp::InputInt("Avoidance Priority", &c.avoidancePriority); c.avoidancePriority = std::clamp(c.avoidancePriority, 0, 99);
    Bool("Update Position", c.updatePosition);
    Bool("Update Rotation", c.updateRotation);
    if (playing) ImGui::Text("%s  Remaining %.2f m  Speed %.2f", c.onNavMesh ? (c.hasPath ? "Moving" : "Idle") : "Not on the nav mesh", c.remainingDistance, glm::length(c.velocity));
}

void Draw(Scene&, EntityID, LineRendererComponent& c, bool) {
    Bool("Use World Space", c.useWorldSpace);
    Bool("Loop", c.loop);
    if (ImGui::TreeNodeEx("Positions", ImGuiTreeNodeFlags_DefaultOpen, "Positions (%d)", (int)c.positions.size())) {
        int remove = -1;
        for (size_t i = 0; i < c.positions.size(); ++i) {
            ImGui::PushID((int)i);
            Insp::DragFloat3(std::format("{}", i).c_str(), glm::value_ptr(c.positions[i]), 0.01f);
            ImGui::SameLine(); if (ImGui::SmallButton("x")) remove = (int)i;
            ImGui::PopID();
        }
        if (remove >= 0) c.positions.erase(c.positions.begin() + remove);
        if (ImGui::SmallButton("+ Add")) c.positions.push_back(c.positions.empty() ? glm::vec3(0) : c.positions.back() + glm::vec3(0, 0, 1));
        ImGui::TreePop();
    }
    Float("Start Width", c.startWidth, 0.005f, 0.0f, 100.0f);
    Float("End Width", c.endWidth, 0.005f, 0.0f, 100.0f);
    Insp::ColorEdit4("Start Color", &c.startColor.x);
    Insp::ColorEdit4("End Color", &c.endColor.x);
    Insp::InputInt("Corner Vertices", &c.cornerVertices); c.cornerVertices = std::clamp(c.cornerVertices, 0, 16);
    Insp::Combo("Alignment", &c.alignment, "View\0Local (object Z)\0");
    Insp::Combo("Texture Mode", &c.textureMode, "Stretch\0Tile\0");
    if (c.textureMode == 1) Float("Tile Length", c.tileLength, 0.01f, 0.01f, 100.0f);
    AssetUI::AssetField("Texture", c.texture, { ".png", ".jpg", ".tga" });
    AssetUI::AssetField("Material", c.material, { ".mat" });
    Insp::Combo("Blend Mode", &c.blendMode, "Alpha\0Additive\0");
    Float("Intensity", c.intensity, 0.01f, 0.0f, 50.0f);
    Insp::InputInt("Sorting Order", &c.sortingOrder);
}

void Draw(Scene& scene, EntityID, SplineComponent& c, bool playing) {
    Insp::Combo("Mode", &c.mode, "Auto (smooth)\0Bezier\0Linear\0");
    Bool("Closed", c.closed);
    if (ImGui::TreeNodeEx("Knots", ImGuiTreeNodeFlags_DefaultOpen, "Knots (%d)", (int)c.knots.size())) {
        int remove = -1, insert = -1;
        for (size_t i = 0; i < c.knots.size(); ++i) {
            ImGui::PushID((int)i);
            auto& k = c.knots[i];
            Insp::DragFloat3(std::format("{}", i).c_str(), glm::value_ptr(k.position), 0.05f);
            ImGui::SameLine(); if (ImGui::SmallButton("+")) insert = (int)i;
            ImGui::SameLine(); if (ImGui::SmallButton("x")) remove = (int)i;
            if (c.mode == 1) { Insp::DragFloat3("  In", glm::value_ptr(k.tangentIn), 0.05f); Insp::DragFloat3("  Out", glm::value_ptr(k.tangentOut), 0.05f); }
            Float("  Roll", k.roll, 0.5f, -360.0f, 360.0f);
            ImGui::PopID();
        }
        if (remove >= 0 && c.knots.size() > 2) c.knots.erase(c.knots.begin() + remove);
        if (insert >= 0) {
            SplineKnot k = c.knots[insert];
            if (insert + 1 < (int)c.knots.size()) k.position = (k.position + c.knots[insert + 1].position) * 0.5f; else k.position += glm::vec3(0, 0, 2);
            c.knots.insert(c.knots.begin() + insert + 1, k);
        }
        if (ImGui::SmallButton("+ Add Knot")) { SplineKnot k; k.position = c.knots.empty() ? glm::vec3(0) : c.knots.back().position + glm::vec3(0, 0, 3); c.knots.push_back(k); }
        if (c.mode == 1) { ImGui::SameLine(); if (ImGui::SmallButton("Auto Tangents")) Splines::AutoTangents(c); }
        ImGui::TreePop();
    }
    ImGui::TextDisabled("Length %.2f m. Drag the knots in the Scene view.", Splines::Length(c));
    if (ImGui::TreeNode("Follow")) {
        EntityRef(scene, "Follower", c.follower);
        Float("Speed (m/s)", c.followSpeed, 0.05f, -1000.0f, 1000.0f);
        Insp::Combo("Loop Mode", &c.followLoop, "Loop\0Ping-pong\0Once\0");
        Bool("Align To Path", c.alignToPath);
        ImGui::TreePop();
    }
    (void)playing;
}

void Draw(Scene&, EntityID, ReflectionProbeComponent& c, bool) {
    Insp::Combo("Mode", &c.mode, "On Load\0Realtime\0Via Scripting\0");
    if (c.mode == 1) Float("Interval (s, 0 = every frame)", c.interval, 0.01f, 0.0f, 60.0f);
    Vec3("Box Size", c.size, 0.05f); c.size = glm::max(c.size, glm::vec3(0.01f));
    Vec3("Box Offset", c.center, 0.05f);
    Bool("Box Projection", c.boxProjection);
    Float("Blend Distance", c.blendDistance, 0.01f, 0.0f, 100.0f);
    Float("Intensity", c.intensity, 0.01f, 0.0f, 10.0f);
    int res = c.resolution == 64 ? 0 : c.resolution == 128 ? 1 : c.resolution == 256 ? 2 : 3;
    if (Insp::Combo("Resolution", &res, "64\0128\0256\0512\0")) { c.resolution = 64 << res; c.dirty = true; }
    Insp::InputInt("Importance", &c.importance);
    Float("Near Clip", c.nearClip, 0.01f, 0.01f, 100.0f);
    Float("Far Clip", c.farClip, 1.0f, 1.0f, 100000.0f);
    Bool("Shadows", c.shadows);
    if (ImGui::Button("Capture Now")) c.dirty = true;
}

void Draw(Scene&, EntityID, DecalComponent& c, bool) {
    AssetUI::AssetField("Texture", c.texture, { ".png", ".jpg", ".tga" });
    AssetUI::AssetField("Normal Map", c.normalMap, { ".png", ".jpg", ".tga" });
    Insp::ColorEdit4("Color", &c.color.x);
    Vec3("Size (W, H, Depth)", c.size, 0.01f); c.size = glm::max(c.size, glm::vec3(0.001f));
    Float("Opacity", c.opacity, 0.01f, 0.0f, 1.0f);
    Float("Normal Strength", c.normalStrength, 0.01f, 0.0f, 4.0f);
    Float("Angle Fade", c.angleFade, 0.01f, 0.0f, 1.0f);
    Float("Smoothness", c.smoothness, 0.01f, 0.0f, 1.0f);
    Insp::DragFloat4("Tiling / Offset", &c.tilingOffset.x, 0.01f);
    Float("Emission", c.emission, 0.01f, 0.0f, 100.0f);
    Insp::InputInt("Sorting Order", &c.sortingOrder);
    ImGui::TextDisabled("Projects along the object's -Z axis onto opaque surfaces.");
}

} // namespace Stardust::GameplayInspector
