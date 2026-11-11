#include <format>
#include "Animation/AnimatedProperties.h"
#include "Scene/ComponentState.h"
#include "Scripting/ScriptEngine.h"
#include <nlohmann/json.hpp>

namespace Stardust {

using json = nlohmann::json;

namespace {

template<typename T> struct Value;
template<> struct Value<float> {
    static constexpr int dimension = 1; static constexpr char kind = 'f';
    static glm::vec4 Read(const float& v) { return { v, 0, 0, 0 }; }
    static void Write(float& v, const glm::vec4& in) { v = in.x; }
};
template<> struct Value<int> {
    static constexpr int dimension = 1; static constexpr char kind = 'i';
    static glm::vec4 Read(const int& v) { return { (float)v, 0, 0, 0 }; }
    static void Write(int& v, const glm::vec4& in) { v = (int)std::round(in.x); }
};
template<> struct Value<bool> {
    static constexpr int dimension = 1; static constexpr char kind = 'b';
    static glm::vec4 Read(const bool& v) { return { v ? 1.0f : 0.0f, 0, 0, 0 }; }
    static void Write(bool& v, const glm::vec4& in) { v = in.x >= 0.5f; }
};
template<> struct Value<glm::vec2> {
    static constexpr int dimension = 2; static constexpr char kind = 'f';
    static glm::vec4 Read(const glm::vec2& v) { return { v, 0, 0 }; }
    static void Write(glm::vec2& v, const glm::vec4& in) { v = glm::vec2(in); }
};
template<> struct Value<glm::vec3> {
    static constexpr int dimension = 3; static constexpr char kind = 'f';
    static glm::vec4 Read(const glm::vec3& v) { return { v, 0 }; }
    static void Write(glm::vec3& v, const glm::vec4& in) { v = glm::vec3(in); }
};
template<> struct Value<glm::vec4> {
    static constexpr int dimension = 4; static constexpr char kind = 'f';
    static glm::vec4 Read(const glm::vec4& v) { return v; }
    static void Write(glm::vec4& v, const glm::vec4& in) { v = in; }
};

struct Entry {
    AnimatedProperty property;
    std::function<bool(Scene&, EntityID)> available;
    std::function<void(Scene&, EntityID, glm::vec4&)> get;
    std::function<void(Scene&, EntityID, const glm::vec4&)> set;
};

struct Table {
    std::vector<Entry> entries;
    std::unordered_map<std::string, int> byId;

    void Insert(Entry entry) {
        entry.property.id = entry.property.group + "." + entry.property.label;
        byId[entry.property.id] = (int)entries.size();
        entries.push_back(std::move(entry));
    }

    template<typename C, typename Accessor>
    void Add(const char* group, const char* label, Accessor accessor, bool color = false) {
        using F = std::remove_cvref_t<decltype(accessor(std::declval<C&>()))>;
        Entry e;
        e.property.group = group;
        e.property.label = label;
        e.property.dimension = Value<F>::dimension;
        e.property.kind = color ? 'c' : Value<F>::kind;
        e.available = [](Scene& s, EntityID x) { return s.Reg().Has<C>(x); };
        e.get = [accessor](Scene& s, EntityID x, glm::vec4& out) { out = Value<F>::Read(accessor(s.Reg().Get<C>(x))); };
        e.set = [accessor](Scene& s, EntityID x, const glm::vec4& in) { Value<F>::Write(accessor(s.Reg().Get<C>(x)), in); };
        Insert(std::move(e));
    }

    // An asset path (sprite, texture...): stored in keys as a reference number, stepped like integers
    template<typename C, typename Accessor>
    void AddReference(const char* group, const char* label, Accessor accessor) {
        Entry e;
        e.property.group = group;
        e.property.label = label;
        e.property.dimension = 1;
        e.property.kind = 'r';
        e.available = [](Scene& s, EntityID x) { return s.Reg().Has<C>(x); };
        e.get = [accessor](Scene& s, EntityID x, glm::vec4& out) { out = { AnimatedProperties::ReferenceKey(accessor(s.Reg().Get<C>(x))), 0, 0, 0 }; };
        e.set = [accessor](Scene& s, EntityID x, const glm::vec4& in) {
            auto& path = accessor(s.Reg().Get<C>(x));
            if (AnimatedProperties::ReferenceKey(path) == in.x) return;
            if (in.x == 0) { path.clear(); return; }
            std::string resolved = AnimatedProperties::ReferencePath(in.x);
            if (!resolved.empty()) path = resolved;
        };
        Insert(std::move(e));
    }

    // The toggle at the top of a component
    template<typename C>
    void AddEnabled(const char* group) {
        Entry e;
        e.property.group = group;
        e.property.label = "Enabled";
        e.property.kind = 'b';
        e.available = [](Scene& s, EntityID x) { return s.Reg().Has<C>(x); };
        e.get = [](Scene& s, EntityID x, glm::vec4& out) { out = { s.Reg().IsEnabled<C>(x) ? 1.0f : 0.0f, 0, 0, 0 }; };
        e.set = [](Scene& s, EntityID x, const glm::vec4& in) {
            bool enabled = in.x >= 0.5f;
            if (enabled == s.Reg().IsEnabled<C>(x)) return;
            s.Reg().SetEnabled<C>(x, enabled);
            VisitToggleComponents([&]<typename T>(int type, const char*) { if constexpr (std::is_same_v<T, C>) s.RefreshComponentState(x, type); });
        };
        Insert(std::move(e));
    }
};

#define P(Component, Group, Label, field) t.Add<Component>(Group, Label, [](Component& c) -> auto& { return c.field; })
#define REF(Component, Group, Label, field) t.AddReference<Component>(Group, Label, [](Component& c) -> std::string& { return c.field; })
#define COLOR(Component, Group, Label, field) t.Add<Component>(Group, Label, [](Component& c) -> auto& { return c.field; }, true)

const Table& Properties() {
    static const Table table = [] {
        Table t;
        {
            Entry active;
            active.property.group = "GameObject";
            active.property.label = "Active";
            active.property.kind = 'b';
            active.available = [](Scene&, EntityID) { return true; };
            active.get = [](Scene& s, EntityID x, glm::vec4& out) { out = { s.Reg().Get<NameComponent>(x).active ? 1.0f : 0.0f, 0, 0, 0 }; };
            active.set = [](Scene& s, EntityID x, const glm::vec4& in) { bool on = in.x >= 0.5f; if (on != s.Reg().Get<NameComponent>(x).active) s.SetActive(x, on); };
            t.Insert(std::move(active));
        }
        using RT = RectTransformComponent;
        P(RT, "Rect Transform", "Anchored Position", anchoredPosition);
        P(RT, "Rect Transform", "Size Delta", sizeDelta);
        P(RT, "Rect Transform", "Anchor Min", anchorMin);
        P(RT, "Rect Transform", "Anchor Max", anchorMax);
        P(RT, "Rect Transform", "Pivot", pivot);
        P(RT, "Rect Transform", "Rotation", rotation);
        P(RT, "Rect Transform", "Scale", scale);

        using CG = CanvasGroupComponent;
        t.AddEnabled<CG>("Canvas Group");
        P(CG, "Canvas Group", "Alpha", alpha);
        P(CG, "Canvas Group", "Interactable", interactable);
        P(CG, "Canvas Group", "Blocks Raycasts", blocksRaycasts);

        using CV = CanvasComponent;
        t.AddEnabled<CV>("Canvas");
        P(CV, "Canvas", "Sorting Order", sortingOrder);
        P(CV, "Canvas", "Plane Distance", planeDistance);
        P(CV, "Canvas", "Pixels Per Unit", pixelsPerUnit);

        using IM = UIImageComponent;
        t.AddEnabled<IM>("Image");
        REF(IM, "Image", "Sprite", sprite);
        REF(IM, "Image", "Texture", texture);
        COLOR(IM, "Image", "Color", color);
        P(IM, "Image", "Fill Amount", fillAmount);
        P(IM, "Image", "Fill Method", fillMethod);
        P(IM, "Image", "Type", type);
        P(IM, "Image", "Pixels Per Unit Multiplier", pixelsPerUnitMultiplier);
        P(IM, "Image", "Preserve Aspect", preserveAspect);
        P(IM, "Image", "Raycast Target", raycastTarget);

        using TX = UITextComponent;
        t.AddEnabled<TX>("Text");
        COLOR(TX, "Text", "Color", color);
        P(TX, "Text", "Font Size", fontSize);
        P(TX, "Text", "Min Font Size", minFontSize);
        P(TX, "Text", "Max Font Size", maxFontSize);
        P(TX, "Text", "Character Spacing", characterSpacing);
        P(TX, "Text", "Line Spacing", lineSpacing);
        P(TX, "Text", "Alignment", alignment);
        P(TX, "Text", "Vertical Alignment", verticalAlignment);
        P(TX, "Text", "Overflow", overflow);
        COLOR(TX, "Text", "Outline Color", outlineColor);
        P(TX, "Text", "Outline Width", outlineWidth);
        COLOR(TX, "Text", "Shadow Color", shadowColor);
        P(TX, "Text", "Shadow Offset", shadowOffset);
        P(TX, "Text", "Auto Size", autoSize);
        P(TX, "Text", "Rich Text", richText);
        P(TX, "Text", "Word Wrap", wordWrap);
        P(TX, "Text", "Raycast Target", raycastTarget);

        using BT = UIButtonComponent;
        t.AddEnabled<BT>("Button");
        P(BT, "Button", "Interactable", interactable);
        P(BT, "Button", "Fade Duration", fadeDuration);
        COLOR(BT, "Button", "Highlighted Color", highlightedColor);
        COLOR(BT, "Button", "Pressed Color", pressedColor);
        COLOR(BT, "Button", "Disabled Color", disabledColor);

        using IF = UIInputFieldComponent;
        t.AddEnabled<IF>("Input Field");
        P(IF, "Input Field", "Font Size", fontSize);
        COLOR(IF, "Input Field", "Color", color);
        COLOR(IF, "Input Field", "Placeholder Color", placeholderColor);
        P(IF, "Input Field", "Interactable", interactable);
        P(IF, "Input Field", "Read Only", readOnly);

        using LG = UILayoutGroupComponent;
        t.AddEnabled<LG>("Layout Group");
        P(LG, "Layout Group", "Spacing", spacing);
        P(LG, "Layout Group", "Cell Size", cellSize);
        P(LG, "Layout Group", "Padding", padding);
        P(LG, "Layout Group", "Columns", columns);
        P(LG, "Layout Group", "Fit Content", fitContent);

        t.AddEnabled<RectMaskComponent>("Rect Mask 2D");
        t.AddEnabled<UIMaskComponent>("Mask");
        P(UIMaskComponent, "Mask", "Show Graphic", showGraphic);

        using PS = ParticleSystemComponent;
        t.AddEnabled<PS>("Particle System");
        P(PS, "Particle System", "Rate over Time", rateOverTime);
        P(PS, "Particle System", "Simulation Speed", simulationSpeed);
        P(PS, "Particle System", "Gravity Modifier", gravityModifier);
        P(PS, "Particle System", "Intensity", intensity);
        COLOR(PS, "Particle System", "Start Color", startColor);
        COLOR(PS, "Particle System", "Start Color 2", startColor2);

        using SP = SpriteRendererComponent;
        t.AddEnabled<SP>("Sprite Renderer");
        REF(SP, "Sprite Renderer", "Sprite", sprite);
        REF(SP, "Sprite Renderer", "Material", material);
        COLOR(SP, "Sprite Renderer", "Color", color);
        P(SP, "Sprite Renderer", "Pixels Per Unit", pixelsPerUnit);
        P(SP, "Sprite Renderer", "Flip X", flipX);
        P(SP, "Sprite Renderer", "Flip Y", flipY);
        P(SP, "Sprite Renderer", "Sorting Order", sortingOrder);

        using MR = MeshRendererComponent;
        t.AddEnabled<MR>("Mesh Renderer");
        COLOR(MR, "Mesh Renderer", "Color Tint", tint);
        P(MR, "Mesh Renderer", "Cast Shadows", castShadows);
        P(MR, "Mesh Renderer", "Receive Shadows", receiveShadows);

        using CM = CameraComponent;
        t.AddEnabled<CM>("Camera");
        P(CM, "Camera", "Field Of View", fovDegrees);
        P(CM, "Camera", "Near Clip", nearPlane);
        P(CM, "Camera", "Far Clip", farPlane);
        P(CM, "Camera", "Orthographic", orthographic);
        P(CM, "Camera", "Orthographic Size", orthographicSize);
        P(CM, "Camera", "Primary", primary);

        using DL = DirectionalLightComponent;
        t.AddEnabled<DL>("Directional Light");
        COLOR(DL, "Directional Light", "Color", color);
        P(DL, "Directional Light", "Intensity", intensity);
        P(DL, "Directional Light", "Cast Shadows", castShadows);
        P(DL, "Directional Light", "Shadow Distance", shadowDistance);

        using PL = PointLightComponent;
        t.AddEnabled<PL>("Point Light");
        COLOR(PL, "Point Light", "Color", color);
        P(PL, "Point Light", "Intensity", intensity);
        P(PL, "Point Light", "Range", range);

        using SL = SpotLightComponent;
        t.AddEnabled<SL>("Spot Light");
        COLOR(SL, "Spot Light", "Color", color);
        P(SL, "Spot Light", "Intensity", intensity);
        P(SL, "Spot Light", "Range", range);
        P(SL, "Spot Light", "Inner Angle", innerAngle);
        P(SL, "Spot Light", "Outer Angle", outerAngle);

        using AS = AudioSourceComponent;
        t.AddEnabled<AS>("Audio Source");
        P(AS, "Audio Source", "Volume", volume);
        P(AS, "Audio Source", "Pitch", pitch);
        P(AS, "Audio Source", "Loop", loop);
        P(AS, "Audio Source", "Spatial", spatial);
        P(AS, "Audio Source", "Min Distance", minDistance);
        P(AS, "Audio Source", "Max Distance", maxDistance);

        using VP = VideoPlayerComponent;
        t.AddEnabled<VP>("Video Player");
        P(VP, "Video Player", "Playing", playing);
        P(VP, "Video Player", "Loop", loop);
        P(VP, "Video Player", "Speed", speed);

        using AN = AnimatorComponent;
        P(AN, "Animator", "Speed", speed);

        using EV = EnvironmentComponent;
        t.AddEnabled<EV>("Environment");
        P(EV, "Environment", "Sky Mode", skyMode);
        COLOR(EV, "Environment", "Sky Color", skyColor);
        P(EV, "Environment", "Sky Rotation", skyRotation);
        P(EV, "Environment", "Sky Exposure", skyExposure);
        P(EV, "Environment", "Shadow Strength", shadowStrength);
        P(EV, "Environment", "Minimum Ambient", minimumAmbient);
        P(EV, "Environment", "Exposure", exposure);
        P(EV, "Environment", "Ambient", ambientIntensity);
        COLOR(EV, "Environment", "Ambient Sky", ambientSky);
        COLOR(EV, "Environment", "Ambient Horizon", ambientHorizon);
        COLOR(EV, "Environment", "Ambient Ground", ambientGround);
        P(EV, "Environment", "Cloud Coverage", cloudCoverage);
        P(EV, "Environment", "Cloud Density", cloudDensity);
        P(EV, "Environment", "Fog Density", fogDensity);
        P(EV, "Environment", "Fog Falloff", fogHeightFalloff);
        P(EV, "Environment", "Wind Direction", windDirectionDeg);
        P(EV, "Environment", "Wind Strength", windStrength);
        P(EV, "Environment", "Bloom", bloomIntensity);
        P(EV, "Environment", "Contrast", contrast);
        P(EV, "Environment", "Saturation", saturation);
        P(EV, "Environment", "Vignette", vignette);
        P(EV, "Environment", "FXAA", fxaa);

        using RB = RigidbodyComponent;
        t.AddEnabled<RB>("Rigidbody");
        P(RB, "Rigidbody", "Mass", mass);
        P(RB, "Rigidbody", "Linear Damping", linearDamping);
        P(RB, "Rigidbody", "Angular Damping", angularDamping);
        P(RB, "Rigidbody", "Use Gravity", useGravity);
        P(RB, "Rigidbody", "Freeze Rotation", freezeRotation);

        auto material = [&]<typename C>(const char* group) {
            t.Add<C>(group, "Friction", [](C& c) -> auto& { return c.material.friction; });
            t.Add<C>(group, "Bounciness", [](C& c) -> auto& { return c.material.restitution; });
            t.Add<C>(group, "Is Trigger", [](C& c) -> auto& { return c.material.isTrigger; });
        };
        t.AddEnabled<BoxColliderComponent>("Box Collider");
        P(BoxColliderComponent, "Box Collider", "Size", size);
        P(BoxColliderComponent, "Box Collider", "Center", center);
        material.operator()<BoxColliderComponent>("Box Collider");
        t.AddEnabled<SphereColliderComponent>("Sphere Collider");
        P(SphereColliderComponent, "Sphere Collider", "Radius", radius);
        P(SphereColliderComponent, "Sphere Collider", "Center", center);
        material.operator()<SphereColliderComponent>("Sphere Collider");
        t.AddEnabled<CapsuleColliderComponent>("Capsule Collider");
        P(CapsuleColliderComponent, "Capsule Collider", "Radius", radius);
        P(CapsuleColliderComponent, "Capsule Collider", "Height", height);
        P(CapsuleColliderComponent, "Capsule Collider", "Center", center);
        material.operator()<CapsuleColliderComponent>("Capsule Collider");
        t.AddEnabled<MeshColliderComponent>("Mesh Collider");
        P(MeshColliderComponent, "Mesh Collider", "Convex", convex);
        material.operator()<MeshColliderComponent>("Mesh Collider");
        return t;
    }();
    return table;
}
#undef P
#undef COLOR

// ---------------------------------------------------------------- public script fields
const std::string kScriptPrefix = "Script:";

bool ScriptFieldShape(const std::string& type, int& dimension, char& kind) {
    if (type == "float") { dimension = 1; kind = 'f'; return true; }
    if (type == "int") { dimension = 1; kind = 'i'; return true; }
    if (type == "bool") { dimension = 1; kind = 'b'; return true; }
    if (type == "Vector2") { dimension = 2; kind = 'f'; return true; }
    if (type == "Vector3") { dimension = 3; kind = 'f'; return true; }
    if (type == "Color") { dimension = 4; kind = 'c'; return true; }
    return false;
}

struct ScriptTarget { ScriptSlot* slot = nullptr; size_t index = 0; const ScriptFieldInfo* field = nullptr; std::string name; };

ScriptTarget FindScriptField(Scene& scene, EntityID entity, const std::string& id) {
    ScriptTarget target;
    auto* scripts = scene.Reg().TryGet<ScriptComponent>(entity);
    auto dot = id.find('.', kScriptPrefix.size());
    if (!scripts || dot == std::string::npos) return target;
    const std::string className = id.substr(kScriptPrefix.size(), dot - kScriptPrefix.size());
    target.name = id.substr(dot + 1);
    for (size_t i = 0; i < scripts->Count(); ++i) {
        if (scripts->Slot(i).className != className) continue;
        const ScriptClassInfo* info = ScriptEngine::FindClass(className);
        if (!info) return target;
        for (const ScriptFieldInfo& field : info->fields)
            if (field.name == target.name) { target.slot = &scripts->Slot(i); target.index = i; target.field = &field; return target; }
    }
    return target;
}

} // namespace

namespace {
std::unordered_map<u32, std::string>& ReferenceTable() { static std::unordered_map<u32, std::string> table; return table; }
}
float AnimatedProperties::ReferenceKey(const std::string& path) {
    if (path.empty()) return 0;
    u32 hash = 2166136261u;
    for (unsigned char c : path) { hash ^= c; hash *= 16777619u; }
    hash &= 0xFFFFFF;
    if (hash == 0) hash = 1; // 0 means "none"
    ReferenceTable()[hash] = path;
    return (float)hash;
}
std::string AnimatedProperties::ReferencePath(float key) {
    auto found = ReferenceTable().find((u32)std::lround(key));
    return found != ReferenceTable().end() ? found->second : std::string();
}
void AnimatedProperties::RegisterReferences(const json& track) {
    auto references = track.find("references");
    if (references == track.end() || !references->is_object()) return;
    for (auto& [key, path] : references->items())
        if (path.is_string()) ReferenceTable()[(u32)std::strtoul(key.c_str(), nullptr, 10)] = path.get<std::string>();
}
void AnimatedProperties::StoreReference(json& track, float key) {
    if (key == 0) return;
    const std::string path = ReferencePath(key);
    if (!path.empty()) track["references"][std::to_string((u32)std::lround(key))] = path;
}

std::string AnimatedProperty::ChannelKey() const { return std::format("prop:{}{}:{}", dimension, kind, id); }

bool AnimatedProperties::ParseChannel(const std::string& key, int& dimension, char& kind, std::string& id) {
    if (!key.starts_with("prop:") || key.size() < 9 || key[7] != ':') return false;
    dimension = std::clamp(key[5] - '0', 1, 4);
    kind = key[6];
    id = key.substr(8);
    return true;
}

std::vector<AnimatedProperty> AnimatedProperties::List(Scene& scene, EntityID entity) {
    std::vector<AnimatedProperty> list;
    for (const Entry& entry : Properties().entries)
        if (entry.available(scene, entity)) list.push_back(entry.property);
    if (auto* scripts = scene.Reg().TryGet<ScriptComponent>(entity))
        for (size_t i = 0; i < scripts->Count(); ++i) {
            const std::string& className = scripts->Slot(i).className;
            const ScriptClassInfo* info = ScriptEngine::FindClass(className);
            if (!info) continue;
            for (const ScriptFieldInfo& field : info->fields) {
                AnimatedProperty property;
                if (!ScriptFieldShape(field.type, property.dimension, property.kind)) continue;
                property.id = kScriptPrefix + className + "." + field.name;
                property.group = className;
                property.label = field.name;
                list.push_back(std::move(property));
            }
        }
    return list;
}

bool AnimatedProperties::Get(Scene& scene, EntityID entity, const std::string& id, glm::vec4& value) {
    if (id.starts_with(kScriptPrefix)) {
        ScriptTarget target = FindScriptField(scene, entity, id);
        if (!target.field) return false;
        bool live = ScriptEngine::RuntimeScene() == &scene && ScriptEngine::HasInstance(entity, target.index);
        json fields = json::parse(live ? ScriptEngine::GetInstanceFields(entity, target.index) : target.slot->fieldsJson, nullptr, false);
        json current = fields.is_object() && fields.contains(target.name) ? fields[target.name] : json::parse(target.field->defaultJson, nullptr, false);
        value = glm::vec4(0);
        if (current.is_boolean()) value.x = current.get<bool>() ? 1.0f : 0.0f;
        else if (current.is_number()) value.x = current.get<float>();
        else if (current.is_array()) for (size_t i = 0; i < std::min<size_t>(4, current.size()); ++i) value[(int)i] = current[i].get<float>();
        else return false;
        return true;
    }
    auto found = Properties().byId.find(id);
    if (found == Properties().byId.end()) return false;
    const Entry& entry = Properties().entries[found->second];
    if (!entry.available(scene, entity)) return false;
    entry.get(scene, entity, value);
    return true;
}

bool AnimatedProperties::Set(Scene& scene, EntityID entity, const std::string& id, const glm::vec4& value) {
    if (id.starts_with(kScriptPrefix)) {
        ScriptTarget target = FindScriptField(scene, entity, id);
        if (!target.field) return false;
        int dimension = 1; char kind = 'f';
        ScriptFieldShape(target.field->type, dimension, kind);
        json out;
        if (kind == 'b') out = value.x >= 0.5f;
        else if (kind == 'i') out = (int)std::round(value.x);
        else if (dimension == 1) out = value.x;
        else { out = json::array(); for (int i = 0; i < dimension; ++i) out.push_back(value[i]); }
        if (ScriptEngine::RuntimeScene() == &scene && ScriptEngine::HasInstance(entity, target.index)) {
            ScriptEngine::SetInstanceField(entity, target.name, out.dump(), target.index);
        } else {
            json fields = json::parse(target.slot->fieldsJson, nullptr, false);
            if (!fields.is_object()) fields = json::object();
            fields[target.name] = out;
            target.slot->fieldsJson = fields.dump();
        }
        return true;
    }
    auto found = Properties().byId.find(id);
    if (found == Properties().byId.end()) return false;
    const Entry& entry = Properties().entries[found->second];
    if (!entry.available(scene, entity)) return false;
    entry.set(scene, entity, value);
    return true;
}

} // namespace Stardust
