// Scene, console and build requests of the AI Agent's tools (see AgentQueries.h)
#include "AgentQueries.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Project/Project.h"
#include "Scene/ComponentState.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptEngine.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <format>
#include <unordered_map>

namespace Stardust {
using json = nlohmann::json;

namespace {
std::string Hex(u64 v) { return std::format("{:016x}", v); }
u64 ParseHex(const json& j) { try { return j.is_string() ? std::stoull(j.get<std::string>(), nullptr, 16) : 0; } catch (...) { return 0; } }
std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; }
bool BaseKey(const std::string& k) {
    static const char* keys[] = { "uuid", "name", "layer", "active", "tag", "parent", "transform", "prefabSource", "prefabInstance" };
    for (auto* key : keys) if (k == key) return true;
    return false;
}
// Euler angles (degrees) without a quaternion: the rotation follows them
void FixRotation(json& transform) {
    if (!transform.is_object() || !transform.contains("euler") || transform.contains("rotation")) return;
    auto& e = transform["euler"];
    if (!e.is_array() || e.size() != 3) return;
    glm::quat q(glm::radians(glm::vec3(e[0].get<float>(), e[1].get<float>(), e[2].get<float>())));
    transform["rotation"] = { q.w, q.x, q.y, q.z };
}
}

std::string AgentSceneQuery(AgentContext& ctx, const std::string& request) {
    json q = json::parse(request, nullptr, false), r = json::object();
    if (!q.is_object() || !ctx.scene) return "{}";
    Scene& scene = *ctx.scene;
    auto& reg = scene.Reg();
    const std::string kind = q.value("kind", "");
    auto find = [&](const json& id) { return scene.FindByUUID(ParseHex(id)); };
    auto uuid = [&](EntityID e) { return Hex(reg.Get<IDComponent>(e).uuid); };
    auto entityJson = [&](EntityID e) { return json::parse(SceneSerializer::EntityToJson(scene, e))["entities"][0]; };
    auto fail = [&](const std::string& message) { r["error"] = message; return r.dump(); };
    try {
        if (kind == "agentProject") {
            r["root"] = FileSystem::Utf8(Project::Dir());
            r["assets"] = FileSystem::Utf8(Project::AssetsDir());
            r["scene"] = FileSystem::Utf8(ctx.scenePath);
            r["sceneName"] = scene.Name();
            r["playing"] = ctx.playing;
        }
        else if (kind == "agentHierarchy") {
            const std::string filter = Lower(q.value("filter", ""));
            const int limit = std::clamp(q.value("limit", 200), 1, 2000);
            json all = json::parse(SceneSerializer::ToJson(scene, true));
            std::unordered_map<std::string, int> depth;
            json list = json::array();
            int total = 0;
            for (auto& j : all["entities"]) {
                const std::string id = j.value("uuid", ""), parent = j.value("parent", "");
                const int d = parent.empty() ? 0 : depth[parent] + 1;
                depth[id] = d;
                json components = json::array();
                for (auto& [key, value] : j.items()) if (!BaseKey(key)) components.push_back(key);
                if (j.contains("scripts")) for (auto& s : j["scripts"]) components.push_back("script:" + s.value("class", ""));
                const std::string name = j.value("name", "");
                bool match = filter.empty() || Lower(name).find(filter) != std::string::npos;
                for (auto& c : components) match = match || Lower(c.get<std::string>()).find(filter) != std::string::npos;
                if (!match) continue;
                ++total;
                if ((int)list.size() >= limit) continue;
                json row = { { "id", id }, { "name", name }, { "depth", d }, { "components", components } };
                if (!parent.empty()) row["parent"] = parent;
                if (!j.value("active", true)) row["active"] = false;
                if (j.contains("prefabInstance")) row["prefab"] = j["prefabInstance"].value("asset", "");
                list.push_back(row);
            }
            r["entities"] = list;
            r["total"] = total;
            r["scene"] = scene.Name();
        }
        else if (kind == "agentName") {
            EntityID e = find(q["id"]);
            r["name"] = e ? reg.Get<NameComponent>(e).name : std::string();
        }
        else if (kind == "agentEntity") {
            EntityID e = find(q["id"]);
            if (!e) return fail("no entity with id " + q.value("id", ""));
            r["entity"] = entityJson(e);
            if (EntityID p = scene.GetParent(e)) r["entity"]["parent"] = uuid(p);
            json children = json::array();
            for (EntityID c : scene.GetChildren(e)) children.push_back({ { "id", uuid(c) }, { "name", reg.Get<NameComponent>(c).name } });
            r["children"] = children;
            const glm::mat4 world = scene.GetWorldMatrix(e);
            r["worldPosition"] = { world[3].x, world[3].y, world[3].z };
        }
        else if (kind == "agentCreate" || kind == "agentEdit") {
            const bool create = kind == "agentCreate";
            EntityID e = kNullEntity;
            if (create) {
                const std::string name = q.value("name", "GameObject"), primitive = Lower(q.value("primitive", ""));
                int type = -1;
                for (int t = 0; t < (int)PrimitiveType::Count; ++t) if (Lower(PrimitiveName((PrimitiveType)t)) == primitive) type = t;
                if (!primitive.empty() && type < 0) return fail("unknown primitive '" + primitive + "' (cube, sphere, plane, cylinder)");
                e = type >= 0 ? scene.CreatePrimitive((PrimitiveType)type, name) : scene.CreateEntity(name);
            } else {
                e = find(q["id"]);
                if (!e) return fail("no entity with id " + q.value("id", ""));
            }
            json patch = q.value(create ? "components" : "patch", json::object());
            if (patch.is_string()) patch = json::parse(patch.get<std::string>(), nullptr, false);
            if (!patch.is_object()) patch = json::object();
            patch.erase("uuid");
            patch.erase("parent");
            if (patch.contains("transform")) FixRotation(patch["transform"]);
            if (!patch.empty()) {
                json data = json::parse(SceneSerializer::EntityToJson(scene, e));
                json& root = data["entities"][0];
                // An Euler-only edit replaces the stored rotation
                if (patch.contains("transform") && patch["transform"].contains("rotation") && root.contains("transform")) root["transform"].erase("euler");
                root.merge_patch(patch);
                const bool selected = (*ctx.selected) == e;
                EntityID rebuilt = SceneSerializer::ReplaceJson(scene, e, data.dump());
                if (!rebuilt) return fail("the entity could not be rebuilt from the patch");
                for (auto& s : (*ctx.selection)) if (s == e) s = rebuilt;
                if (selected) (*ctx.selected) = rebuilt;
                e = rebuilt;
            }
            if (q.contains("parent") && q["parent"].is_string()) {
                const std::string parentId = q["parent"].get<std::string>();
                EntityID p = parentId.empty() ? kNullEntity : find(q["parent"]);
                if (!parentId.empty() && !p) return fail("no parent entity with id " + parentId);
                if (p && (p == e || scene.IsDescendantOf(p, e))) return fail("an entity cannot be parented to itself or its children");
                scene.SetParent(e, p, !create);
            }
            ctx.edited = true;
            r["entity"] = entityJson(e);
            r["id"] = uuid(e);
        }
        else if (kind == "agentDelete") {
            EntityID e = find(q["id"]);
            if (!e) return fail("no entity with id " + q.value("id", ""));
            r["deleted"] = reg.Get<NameComponent>(e).name;
            std::erase_if((*ctx.selection), [&](EntityID s) { return s == e || scene.IsDescendantOf(s, e); });
            if ((*ctx.selected) && ((*ctx.selected) == e || scene.IsDescendantOf((*ctx.selected), e))) (*ctx.selected) = kNullEntity;
            if (scene.IsRunning()) scene.QueueDestroy(e); else scene.DestroyEntity(e);
            ctx.edited = true;
        }
        else if (kind == "agentComponents") {
            // Default JSON of each component, from an entity of a scratch scene
            Scene scratch;
            EntityID e = scratch.CreateEntity("Defaults");
            json wanted = q.value("keys", json::array()), keys = json::array(), defaults = json::object();
            VisitToggleComponents([&]<typename T>(int, const char* key) {
                keys.push_back(key);
                bool want = false;
                for (auto& k : wanted) want = want || (k.is_string() && k.get<std::string>() == key);
                if (want && !scratch.Reg().Has<T>(e)) scratch.Reg().Add<T>(e);
            });
            if (!wanted.empty()) {
                json data = json::parse(SceneSerializer::EntityToJson(scratch, e))["entities"][0];
                for (auto& k : wanted) if (k.is_string() && data.contains(k.get<std::string>())) defaults[k.get<std::string>()] = data[k.get<std::string>()];
                for (auto& k : wanted) if (k == "transform" || k == "scripts") defaults[k.get<std::string>()] = k == "transform" ? data["transform"] : json::array({ { { "class", "MyScript" }, { "enabled", true }, { "fields", json::object() } } });
            }
            r["keys"] = keys;
            r["defaults"] = defaults;
        }
        else if (kind == "agentConsole") {
            const int count = std::clamp(q.value("count", 40), 1, 500);
            const bool errorsOnly = q.value("errorsOnly", false);
            auto entries = Log::Snapshot();
            json lines = json::array();
            for (auto it = entries.rbegin(); it != entries.rend() && (int)lines.size() < count; ++it) {
                if (errorsOnly && it->level < LogLevel::Warn) continue;
                static const char* names[] = { "trace", "info", "warning", "error" };
                lines.push_back({ { "level", names[(int)it->level] }, { "message", it->message.size() > 2000 ? it->message.substr(0, 2000) + "..." : it->message } });
            }
            std::reverse(lines.begin(), lines.end());
            r["entries"] = lines;
        }
        else if (kind == "agentBuild") {
            if (q.value("start", false) && !ScriptEngine::IsBuilding()) ScriptEngine::BuildAsync();
            r["building"] = ScriptEngine::IsBuilding();
            r["generation"] = ScriptEngine::BuildDiagnosticsGeneration();
            r["diagnostics"] = ScriptEngine::BuildDiagnostics();
        }
        else if (kind == "agentSelection") {
            json ids = json::array();
            for (EntityID e : (*ctx.selection)) if (reg.Valid(e)) ids.push_back({ { "id", uuid(e) }, { "name", reg.Get<NameComponent>(e).name } });
            if (ids.empty() && (*ctx.selected) && reg.Valid((*ctx.selected))) ids.push_back({ { "id", uuid((*ctx.selected)) }, { "name", reg.Get<NameComponent>((*ctx.selected)).name } });
            r["selection"] = ids;
            if (!ctx.selectedAsset.empty()) r["asset"] = ctx.selectedAsset;
        }
        else if (kind == "agentSelect") {
            (*ctx.selection).clear();
            for (auto& id : q.value("ids", json::array())) if (EntityID e = find(id)) (*ctx.selection).push_back(e);
            (*ctx.selected) = (*ctx.selection).empty() ? kNullEntity : (*ctx.selection).back();
            r["selected"] = (*ctx.selection).size();
        }
        else if (kind == "agentSave") {
            if (ctx.playing) return fail("the scene cannot be saved in Play mode");
            if (ctx.scenePath.empty()) return fail("the scene has no file yet: ask the user to save it with File > Save Scene As");
            if (ctx.save) ctx.save();
            r["saved"] = FileSystem::Utf8(ctx.scenePath);
        }
        else return fail("unknown request " + kind);
    } catch (const std::exception& e) { return fail(std::string("failed: ") + e.what()); }
    return r.dump();
}

} // namespace Stardust
