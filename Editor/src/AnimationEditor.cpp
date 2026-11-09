// Animation window (dopesheet + curves) and Animator window. Both follow the selected object, like Unity,
// autosave after each edit, and keep their own undo history.
#include "EditorApp.h"
#include "Animation/AnimatorSystem.h"
#include "Animation/AnimatedProperties.h"
#include "AssetUI.h"
#include "InspectorWidgets.h"
#include "Scripting/ScriptEngine.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace Stardust {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

const ImU32 kAxisColors[4] = { IM_COL32(236, 92, 92, 255), IM_COL32(112, 214, 112, 255), IM_COL32(96, 156, 245, 255), IM_COL32(205, 205, 205, 255) };

// Clips keep each channel under its own JSON key. Keys are interned so rows and selections can use small ids;
// 0, 1 and 2 are always the transform channels.
struct ChannelInfo { std::string key, label, id; int dimension = 3; char kind = 'f'; };
ChannelInfo MakeChannelInfo(const std::string& key) {
    ChannelInfo info;
    info.key = key;
    if (AnimatedProperties::ParseChannel(key, info.dimension, info.kind, info.id)) info.label = info.id.starts_with("Script:") ? info.id.substr(7) : info.id;
    else info.label = key == "position" ? "Position" : key == "rotation" ? "Rotation" : "Scale";
    return info;
}
std::deque<ChannelInfo>& ChannelTable() {
    static std::deque<ChannelInfo> table = { MakeChannelInfo("position"), MakeChannelInfo("rotation"), MakeChannelInfo("scale") };
    return table;
}
int ChannelId(const std::string& key) {
    auto& table = ChannelTable();
    for (int i = 0; i < (int)table.size(); ++i) if (table[i].key == key) return i;
    table.push_back(MakeChannelInfo(key));
    return (int)table.size() - 1;
}
const ChannelInfo& Channel(int id) { return ChannelTable()[id]; }
const char* ChannelKey(int id) { return Channel(id).key.c_str(); }
std::string AxisName(const ChannelInfo& info, int axis) {
    static const char xyzw[] = "xyzw", rgba[] = "rgba";
    return std::string(1, (info.kind == 'c' ? rgba : xyzw)[axis]);
}
std::vector<int> TrackChannels(const json& track) {
    std::vector<int> ids;
    for (int c = 0; c < 3; ++c) if (track.contains(ChannelKey(c)) && track[ChannelKey(c)].is_array()) ids.push_back(c);
    for (auto& [key, value] : track.items()) if (key.starts_with("prop:") && value.is_array()) ids.push_back(ChannelId(key));
    return ids;
}
std::string InterpolationOf(const json& track, int channel) {
    char kind = Channel(channel).kind;
    return kind == 'b' || kind == 'i' ? std::string("Constant") : track.value("interpolation", std::string("Linear"));
}
json KeyJson(float t, glm::vec4 value, int dimension) {
    json key = json::array({ t });
    for (int i = 0; i < dimension; ++i) key.push_back(value[i]);
    return key;
}

struct KeyRef {
    int track, channel, key;
    auto operator<=>(const KeyRef&) const = default;
};
struct Row { int track, channel, axis; }; // axis -1: the channel's header row
using RowId = std::tuple<int, int, int>;

// A JSON asset edited in place: autosaved, with undo snapshots
struct JsonDocument {
    std::string path, saved;
    json data = json::object();
    bool dirty = false;
    std::vector<std::string> undo, redo;

    void Load(const std::string& assetPath) {
        path = assetPath;
        try { data = json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(path)), nullptr, false); }
        catch (...) { data = json::object(); }
        if (!data.is_object()) data = json::object();
        saved = data.dump();
        dirty = false;
        undo.clear();
        redo.clear();
    }
    void Close() { path.clear(); data = json::object(); saved.clear(); dirty = false; undo.clear(); redo.clear(); }
    void Write() { FileSystem::WriteText(AssetManager::ToAbsolute(path), data.dump(2)); AssetManager::Invalidate(path); }
    void Commit() {
        if (!dirty || path.empty()) return;
        dirty = false;
        std::string now = data.dump();
        if (now == saved) return;
        undo.push_back(saved);
        if (undo.size() > 64) undo.erase(undo.begin());
        redo.clear();
        saved = std::move(now);
        Write();
    }
    bool Step(bool forward) {
        auto& from = forward ? redo : undo;
        auto& to = forward ? undo : redo;
        if (from.empty()) return false;
        to.push_back(saved);
        saved = from.back();
        from.pop_back();
        data = json::parse(saved, nullptr, false);
        dirty = false;
        Write();
        return true;
    }
};

struct ClipEditor {
    JsonDocument doc;
    std::string request, pendingClip;
    bool createClipPending = false;
    int removeTrack = -2, removeChannel = -1; // -2: nothing pending
    bool explicitClip = false, locked = false, visible = false, curves = false;
    EntityID lockedEntity = 0, root = 0;
    float time = 0, viewStart = -0.05f, pixelsPerSecond = 300, listWidth = 320, scrollY = 0;
    std::set<KeyRef> selection;
    std::set<RowId> selectedRows;
    std::set<std::string> expanded;
    std::vector<std::string> clips;
    std::string clipsController;
    fs::file_time_type clipsStamp{};

    // gestures
    bool draggingKeys = false, boxSelecting = false;
    float dragAnchor = 0;
    std::map<KeyRef, float> dragOrigin;
    ImVec2 boxStart{};
    int curveAxis = -1;
    bool curveDrag = false;
    int handleSide = 0; // -1 in tangent, +1 out tangent
    float valueMin = -1, valueMax = 1;

    int menuRow = -1;
    float menuTime = 0;
    bool menuOnKey = false;

    struct Copied { int track, channel; float offset; json key; };
    std::vector<Copied> clipboard;

    std::unordered_map<EntityID, std::array<glm::vec3, 3>> sampled;
    std::unordered_map<std::string, glm::vec4> sampledProperties;
    std::string propertyFilter;
    std::set<int> selectedEvents;
    std::map<int, float> eventOrigin;
    int editEvent = -1, menuEvent = -1, scaleSide = 0;
    float scaleMin = 0, scaleMax = 0;
    bool openEventEditor = false;
    float sampledTime = -2;
};
ClipEditor g_Clip;

struct ControllerEditor {
    JsonDocument doc;
    bool locked = false, visible = false;
    EntityID lockedEntity = 0;
};
ControllerEditor g_Controller;

float SnapToFrame(float t, float fps) { return std::round(t * fps) / fps; }

EntityID FindAnimatorRoot(Scene& scene, EntityID e) {
    for (; e; e = scene.GetParent(e))
        if (scene.Reg().Valid(e) && scene.Reg().Has<AnimatorComponent>(e)) return e;
    return 0;
}

std::string RelativePath(Scene& scene, EntityID root, EntityID e) {
    std::string path;
    for (EntityID node = e; node && node != root; node = scene.GetParent(node)) {
        const std::string& name = scene.Reg().Get<NameComponent>(node).name;
        path = path.empty() ? name : name + "/" + path;
    }
    return path;
}

EntityID ResolvePath(Scene& scene, EntityID root, const std::string& path) {
    EntityID target = root;
    std::stringstream parts(path);
    std::string segment;
    while (target && std::getline(parts, segment, '/')) {
        if (segment.empty()) continue;
        EntityID child = 0;
        for (EntityID c : scene.GetChildren(target))
            if (scene.Reg().Get<NameComponent>(c).name == segment) { child = c; break; }
        target = child;
    }
    return target;
}

glm::vec3 ChannelOf(const TransformComponent& t, int channel) { return channel == 0 ? t.position : channel == 1 ? t.eulerHint : t.scale; }

json* KeyAt(json& tracks, const KeyRef& r) {
    if (r.track < 0 || r.track >= (int)tracks.size()) return nullptr;
    auto& track = tracks[r.track];
    if (!track.contains(ChannelKey(r.channel))) return nullptr;
    auto& keys = track[ChannelKey(r.channel)];
    if (!keys.is_array() || r.key < 0 || r.key >= (int)keys.size()) return nullptr;
    auto& key = keys[r.key];
    return key.is_array() && key.size() >= (size_t)Channel(r.channel).dimension + 1 ? &key : nullptr;
}

// Replaces the key on this frame (keeping tangents) or adds a new one
void WriteKey(json& keys, float t, glm::vec4 value, int dimension, float fps) {
    for (auto& key : keys)
        if (key.is_array() && key.size() >= (size_t)dimension + 1 && std::abs(key[0].get<float>() - t) < 0.5f / fps) {
            for (int i = 0; i < dimension; ++i) key[i + 1] = value[i];
            return;
        }
    keys.push_back(KeyJson(t, value, dimension));
}

json& EnsureTrack(json& tracks, const std::string& path) {
    for (auto& track : tracks)
        if (track.value("path", std::string()) == path) return track;
    tracks.push_back({ { "path", path } });
    return tracks.back();
}

std::string SanitizeFileName(std::string name) {
    for (char& c : name) if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
    return name.empty() ? "Animation" : name;
}
fs::path UniquePath(const fs::path& dir, const std::string& stem, const std::string& ext) {
    fs::path path = dir / fs::u8path(stem + ext);
    for (int n = 1; fs::exists(path); ++n) path = dir / fs::u8path(std::format("{} {}{}", stem, n, ext));
    return path;
}
std::string CreateClipFile(const std::string& stem) {
    fs::path dir = AssetManager::Root() / "Animations";
    fs::create_directories(dir);
    fs::path path = UniquePath(dir, SanitizeFileName(stem), ".anim");
    FileSystem::WriteText(path, json({ { "sampleRate", 60 }, { "duration", 1.0f }, { "tracks", json::array() } }).dump(2));
    return AssetManager::ToRelative(path);
}

enum class Glyph { First, Previous, Play, Pause, Next, Last, Record, AddKey, Lock };

bool GlyphButton(const char* id, Glyph glyph, bool active, const char* tooltip, ImU32 activeColor = IM_COL32(47, 128, 237, 255)) {
    float h = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_Button, active ? activeColor : IM_COL32(34, 36, 39, 255));
    bool clicked = ImGui::Button(id, { h + 3, h });
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(), c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    auto* d = ImGui::GetWindowDrawList();
    const float r = h * 0.24f;
    const ImU32 col = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled ? IM_COL32(120, 120, 120, 255) : IM_COL32(222, 222, 222, 255);
    auto right = [&](float x) { d->AddTriangleFilled({ x, c.y - r }, { x + r * 1.3f, c.y }, { x, c.y + r }, col); };
    auto left = [&](float x) { d->AddTriangleFilled({ x + r * 1.3f, c.y - r }, { x + r * 1.3f, c.y + r }, { x, c.y }, col); };
    auto bar = [&](float x) { d->AddRectFilled({ x, c.y - r }, { x + 2, c.y + r }, col); };
    switch (glyph) {
    case Glyph::First: bar(c.x - r * 1.2f); left(c.x - r * 0.6f); break;
    case Glyph::Previous: bar(c.x + r * 0.9f); left(c.x - r); break;
    case Glyph::Play: right(c.x - r * 0.6f); break;
    case Glyph::Pause: bar(c.x - r * 0.7f); bar(c.x + r * 0.3f); break;
    case Glyph::Next: bar(c.x - r * 1.1f); right(c.x - r * 0.4f); break;
    case Glyph::Last: right(c.x - r * 0.9f); bar(c.x + r * 0.8f); break;
    case Glyph::Record: d->AddCircleFilled(c, r * 1.1f, active ? IM_COL32(255, 255, 255, 255) : IM_COL32(232, 66, 66, 255)); break;
    case Glyph::AddKey:
        d->AddQuadFilled({ c.x - r, c.y }, { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, col);
        d->AddLine({ c.x + r * 1.1f, c.y - r }, { c.x + r * 2.1f, c.y - r }, col, 1.5f);
        d->AddLine({ c.x + r * 1.6f, c.y - r * 1.5f }, { c.x + r * 1.6f, c.y - r * 0.5f }, col, 1.5f);
        break;
    case Glyph::Lock:
        d->AddRectFilled({ c.x - r, c.y - r * 0.1f }, { c.x + r, c.y + r }, col, 1);
        d->PathArcTo({ c.x, c.y - r * 0.1f }, r * 0.65f, 3.14159f, 6.28318f, 10);
        d->PathStroke(col, 0, 1.5f);
        break;
    }
    return clicked;
}

void Diamond(ImDrawList* d, ImVec2 c, float r, ImU32 color) {
    d->AddQuadFilled({ c.x - r, c.y }, { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, color);
    d->AddQuad({ c.x - r, c.y }, { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, IM_COL32(14, 16, 19, 255));
}

// When opened, the window joins the Project / Console tabs unless the user already docked it somewhere
void DockBesideProject(const char* name, bool opening, bool focus) {
    auto* self = ImGui::FindWindowByName(name);
    auto* saved = ImGui::FindWindowSettingsByID(ImHashStr(name)); // a workspace may already have placed it
    if (auto* project = ImGui::FindWindowByName("Project"); opening && project && project->DockId && (!self || !self->DockId) && (!saved || !saved->DockId))
        ImGui::SetNextWindowDockID(project->DockId, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ 1100, 320 }, ImGuiCond_FirstUseEver);
    if (focus) ImGui::SetNextWindowFocus();
}

void CenteredText(const char* text, float y) {
    ImGui::SetCursorPos({ (ImGui::GetWindowWidth() - ImGui::CalcTextSize(text).x) * 0.5f, y });
    ImGui::TextDisabled("%s", text);
}

} // namespace

bool EditorApp::StepAnimationHistory(bool redo) {
    if (m_AnimationWindowFocused || m_AnimationRecording) {
        if (!g_Clip.doc.Step(redo)) return false;
        g_Clip.selection.clear();
        m_AnimationRecordTime = -1;
        if (m_AnimationPreviewScene && m_AnimationPreviewAsset == g_Clip.doc.path) m_AnimationPreviewJson = g_Clip.doc.saved;
        return true;
    }
    return m_AnimatorWindowFocused && g_Controller.doc.Step(redo);
}

void EditorApp::DrawAnimationEditor() {
    // Open requests from the Project window and inspectors are routed by asset type
    if (!m_AnimationAsset.empty()) {
        std::string asset = std::exchange(m_AnimationAsset, {});
        if (asset.ends_with(".controller")) {
            m_AnimatorAsset = asset;
            m_ShowAnimationEditor = g_Clip.visible;
            m_ShowAnimatorEditor = true;
            m_AnimatorFocusRequest = true;
        } else {
            g_Clip.request = asset;
            m_ShowAnimationEditor = true;
            m_AnimationFocusRequest = true;
        }
    }
    DrawAnimationWindow();
    DrawAnimatorWindow();
}

void EditorApp::DrawAnimatorWindow() {
    ControllerEditor& s = g_Controller;
    if (!m_ShowAnimatorEditor) {
        if (s.visible && m_AnimationPreviewAsset.ends_with(".controller")) StopAnimationPreview();
        s.visible = false;
        m_AnimatorWindowFocused = false;
        return;
    }
    Scene& scene = ActiveScene();
    auto& reg = scene.Reg();
    if (s.locked && !reg.Valid(s.lockedEntity)) s.locked = false;
    EntityID selected = s.locked ? s.lockedEntity : (reg.Valid(m_Selected) ? m_Selected : 0);
    EntityID root = selected ? FindAnimatorRoot(scene, selected) : 0;
    m_AnimatorRoot = root;
    if (root) {
        const std::string& controller = reg.Get<AnimatorComponent>(root).controller;
        if (!controller.empty()) m_AnimatorAsset = controller;
    }
    if (s.doc.path != m_AnimatorAsset) {
        s.doc.Commit();
        if (m_AnimationPreviewAsset.ends_with(".controller")) StopAnimationPreview();
        if (m_AnimatorAsset.empty()) s.doc.Close(); else s.doc.Load(m_AnimatorAsset);
    }

    DockBesideProject("Animator", !s.visible, !s.visible || std::exchange(m_AnimatorFocusRequest, false));
    bool open = true;
    bool shown = ImGui::Begin("Animator", &open);
    s.visible = true;
    m_AnimatorWindowFocused = shown && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    if (shown) {
        if (s.doc.path.empty()) {
            float y = ImGui::GetWindowHeight() * 0.38f;
            if (root && m_State == PlayState::Edit) {
                CenteredText(std::format("{} has an Animator without a Controller.", reg.Get<NameComponent>(root).name).c_str(), y);
                ImGui::SetCursorPos({ (ImGui::GetWindowWidth() - 150) * 0.5f, y + 30 });
                if (ImGui::Button("Create Controller", { 150, 0 })) {
                    fs::path dir = AssetManager::Root() / "Animations";
                    fs::create_directories(dir);
                    fs::path controller = UniquePath(dir, SanitizeFileName(reg.Get<NameComponent>(root).name), ".controller");
                    FileSystem::WriteText(controller, json({ { "defaultState", "" }, { "states", json::object() }, { "transitions", json::array() }, { "parameters", json::array() } }).dump(2));
                    reg.Get<AnimatorComponent>(root).controller = AssetManager::ToRelative(controller);
                }
            } else CenteredText("Select an object with an Animator, or double-click an Animator Controller asset.", y);
        } else {
            bool previewing = m_AnimationPreviewScene && m_AnimationPreviewAsset == s.doc.path;
            if (GlyphButton("##lock", Glyph::Lock, s.locked, "Lock this window to the current object")) {
                s.locked = !s.locked;
                s.lockedEntity = s.locked ? selected : 0;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_State != PlayState::Edit || !root);
            if (previewing) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(47, 128, 237, 255));
            bool toggled = ImGui::Button("Preview");
            if (previewing) ImGui::PopStyleColor();
            if (toggled) { if (previewing) StopAnimationPreview(); else StartAnimationPreview(root, s.doc.path); previewing = bool(m_AnimationPreviewScene); }
            ImGui::SameLine();
            ImGui::BeginDisabled(!previewing);
            if (GlyphButton("##play", m_AnimationPreviewPlaying ? Glyph::Pause : Glyph::Play, m_AnimationPreviewPlaying, "Play / pause the preview")) m_AnimationPreviewPlaying = !m_AnimationPreviewPlaying;
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", root ? std::format("{}  -  {}", reg.Get<NameComponent>(root).name, s.doc.path).c_str() : s.doc.path.c_str());
            DrawControllerEditor(s.doc.data, s.doc.dirty);
        }
    }
    ImGui::End();
    if (!ImGui::IsAnyMouseDown()) s.doc.Commit();
    if (!open) {
        s.doc.Commit();
        m_ShowAnimatorEditor = false;
    }
}

void EditorApp::DrawAnimationWindow() {
    ClipEditor& s = g_Clip;
    JsonDocument& doc = s.doc;
    if (!m_ShowAnimationEditor) {
        if (s.visible && m_AnimationPreviewAsset.ends_with(".anim")) StopAnimationPreview();
        s.visible = false;
        m_AnimationWindowFocused = false;
        return;
    }
    Scene& scene = ActiveScene();
    auto& reg = scene.Reg();

    auto stopClipPreview = [&] { if (m_AnimationPreviewScene && m_AnimationPreviewAsset.ends_with(".anim")) StopAnimationPreview(); };
    auto loadClip = [&](const std::string& path) {
        doc.Commit();
        stopClipPreview();
        doc.Load(path);
        s.selection.clear();
        s.selectedRows.clear();
        s.selectedEvents.clear();
        s.time = 0;
    };

    // Which object and clip are we looking at?
    if (s.locked && !reg.Valid(s.lockedEntity)) s.locked = false;
    EntityID selected = s.locked ? s.lockedEntity : (reg.Valid(m_Selected) ? m_Selected : 0);
    EntityID root = selected ? FindAnimatorRoot(scene, selected) : 0;
    if (root != s.root) { s.root = root; if (root) s.explicitClip = false; }
    EntityID previewRoot = root ? root : selected;

    std::string controllerPath = root ? reg.Get<AnimatorComponent>(root).controller : std::string();
    {
        std::error_code ec;
        auto stamp = controllerPath.empty() ? fs::file_time_type{} : fs::last_write_time(AssetManager::ToAbsolute(controllerPath), ec);
        if (controllerPath != s.clipsController || stamp != s.clipsStamp) {
            s.clipsController = controllerPath;
            s.clipsStamp = stamp;
            s.clips.clear();
            if (!controllerPath.empty() && !ec) {
                json data = json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(controllerPath)), nullptr, false);
                if (data.is_object()) {
                    auto states = data.value("states", json::object());
                    auto add = [&](const std::string& clip) { if (!clip.empty() && std::find(s.clips.begin(), s.clips.end(), clip) == s.clips.end()) s.clips.push_back(clip); };
                    add(states.value(data.value("defaultState", std::string()), json::object()).value("clip", std::string()));
                    for (auto& [name, state] : states.items()) add(state.value("clip", std::string()));
                }
            }
        }
    }

    if (!s.pendingClip.empty()) { loadClip(std::exchange(s.pendingClip, {})); s.explicitClip = false; }
    if (!s.request.empty()) { loadClip(std::exchange(s.request, {})); s.explicitClip = true; }
    else if (!s.explicitClip) {
        if (root && !s.clips.empty()) { if (std::find(s.clips.begin(), s.clips.end(), doc.path) == s.clips.end()) loadClip(s.clips[0]); }
        else if (!doc.path.empty()) { doc.Commit(); stopClipPreview(); doc.Close(); }
    }

    DockBesideProject("Animation", !s.visible, !s.visible || std::exchange(m_AnimationFocusRequest, false));
    bool open = true;
    bool shown = ImGui::Begin("Animation", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    s.visible = true;
    m_AnimationWindowFocused = shown && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;

    auto createClipForRoot = [&] {
        std::string clip = CreateClipFile(reg.Get<NameComponent>(root).name);
        auto& animator = reg.Get<AnimatorComponent>(root);
        json data = json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(animator.controller)), nullptr, false);
        if (!data.is_object()) data = json::object();
        if (!data.contains("states")) data["states"] = json::object();
        std::string stem = FileSystem::Utf8(fs::u8path(clip).stem()), state = stem;
        for (int n = 1; data["states"].contains(state); ++n) state = std::format("{} {}", stem, n);
        data["states"][state] = { { "clip", clip }, { "loop", true } };
        if (data.value("defaultState", std::string()).empty()) data["defaultState"] = state;
        FileSystem::WriteText(AssetManager::ToAbsolute(animator.controller), data.dump(2));
        if (g_Controller.doc.path == animator.controller) g_Controller.doc.Load(animator.controller);
        s.clipsController.clear();
        loadClip(clip);
        s.explicitClip = true;
    };

    if (s.createClipPending && root && !controllerPath.empty()) createClipForRoot();
    s.createClipPending = false;

    if (shown && doc.path.empty()) {
        float y = ImGui::GetWindowHeight() * 0.38f;
        if (!selected) CenteredText("No animatable object selected.", y);
        else if (m_State != PlayState::Edit) CenteredText("Exit Play mode to create animations.", y);
        else {
            const std::string& name = reg.Get<NameComponent>(selected).name;
            bool needsController = !root || controllerPath.empty();
            CenteredText(std::format("To begin animating {}, create {}.", name, needsController ? "an Animator and an Animation Clip" : "an Animation Clip").c_str(), y);
            ImGui::SetCursorPos({ (ImGui::GetWindowWidth() - 90) * 0.5f, y + 30 });
            if (ImGui::Button("Create", { 90, 0 })) {
                if (needsController) {
                    std::string stem = SanitizeFileName(name);
                    std::string clip = CreateClipFile(stem);
                    fs::path dir = AssetManager::Root() / "Animations";
                    fs::path controller = UniquePath(dir, stem, ".controller");
                    std::string stateName = FileSystem::Utf8(fs::u8path(clip).stem());
                    FileSystem::WriteText(controller, json({ { "defaultState", stateName }, { "states", { { stateName, { { "clip", clip }, { "loop", true } } } } },
                                                             { "transitions", json::array() }, { "parameters", json::array() } }).dump(2));
                    auto& animator = reg.Has<AnimatorComponent>(selected) ? reg.Get<AnimatorComponent>(selected) : reg.Add<AnimatorComponent>(selected);
                    animator.controller = AssetManager::ToRelative(controller);
                    loadClip(clip);
                    s.root = selected;
                } else createClipForRoot();
            }
        }
    } else if (shown) {
        const float fps = (float)std::clamp(doc.data.value("sampleRate", 60), 1, 240);
        const float duration = std::max(1.0f / fps, doc.data.value("duration", 1.0f));
        if (!doc.data.contains("tracks") || !doc.data["tracks"].is_array()) doc.data["tracks"] = json::array();
        json& tracks = doc.data["tracks"];
        if (!doc.data.contains("events") || !doc.data["events"].is_array()) doc.data["events"] = json::array();
        json& events = doc.data["events"];

        bool previewing = m_AnimationPreviewScene && m_AnimationPreviewAsset == doc.path;
        if (previewing && m_AnimationPreviewRoot != previewRoot) { StopAnimationPreview(); previewing = false; }
        if (previewing) s.time = m_AnimationPreviewTime;
        const bool canPreview = m_State == PlayState::Edit && reg.Valid(previewRoot);

        auto startPreview = [&] {
            if (!canPreview) return;
            StartAnimationPreview(previewRoot, doc.path);
            m_AnimationPreviewPlaying = false;
            m_AnimationPreviewTime = s.time;
            m_AnimationPreviewJson = doc.data.dump();
            previewing = bool(m_AnimationPreviewScene);
        };
        auto setTime = [&](float t) {
            s.time = std::max(0.0f, t);
            if (!previewing) startPreview();
            if (previewing) { m_AnimationPreviewTime = s.time; m_AnimationPreviewPlaying = false; }
        };

        // Current transform of a track's target; identity-like defaults when the object is missing
        auto currentValue = [&](const json& track, int channel) -> glm::vec4 {
            Scene& source = previewing ? *m_AnimationPreviewScene : scene;
            EntityID target = reg.Valid(previewRoot) ? ResolvePath(source, previewRoot, track.value("path", std::string())) : 0;
            if (channel < 3) {
                if (target) return glm::vec4(ChannelOf(source.Reg().Get<TransformComponent>(target), channel), 0);
                return channel == 2 ? glm::vec4(1, 1, 1, 0) : glm::vec4(0);
            }
            glm::vec4 value(0);
            if (target) AnimatedProperties::Get(source, target, Channel(channel).id, value);
            return value;
        };
        auto sampleChannel = [&](const json& track, int channel, float t) {
            return AnimatorSystem::EvaluateN(track[ChannelKey(channel)], t, currentValue(track, channel), InterpolationOf(track, channel), Channel(channel).dimension);
        };
        auto addKey = [&](int track, int channel, float t) {
            t = std::max(0.0f, SnapToFrame(t, fps));
            WriteKey(tracks[track][ChannelKey(channel)], t, sampleChannel(tracks[track], channel, t), Channel(channel).dimension, fps);
            doc.dirty = true;
        };
        auto addChannel = [&](const std::string& path, int channel, glm::vec4 value) {
            json& track = EnsureTrack(tracks, path);
            if (!track.contains(ChannelKey(channel))) {
                const int dimension = Channel(channel).dimension;
                track[ChannelKey(channel)] = json::array({ KeyJson(0.0f, value, dimension), KeyJson(duration, value, dimension) });
                doc.dirty = true;
            }
        };

        // Record: anything changed on the preview (gizmo or Inspector) becomes keys on the current frame
        if (m_AnimationRecording && previewing && !m_AnimationPreviewPlaying) {
            Scene& preview = *m_AnimationPreviewScene;
            bool rebuild = s.sampled.empty() || s.sampledTime != m_AnimationRecordTime;
            s.sampledTime = m_AnimationRecordTime;
            for (EntityID e : preview.Reg().AllEntities()) {
                if (e != m_AnimationPreviewRoot && !preview.IsDescendantOf(e, m_AnimationPreviewRoot)) continue;
                const auto& transform = preview.Reg().Get<TransformComponent>(e);
                std::array<glm::vec3, 3> now{ transform.position, transform.eulerHint, transform.scale };
                auto found = s.sampled.find(e);
                if (rebuild || found == s.sampled.end()) { s.sampled[e] = now; continue; }
                for (int channel = 0; channel < 3; ++channel) {
                    if (glm::all(glm::epsilonEqual(found->second[channel], now[channel], 1e-4f))) continue;
                    json& track = EnsureTrack(tracks, RelativePath(preview, m_AnimationPreviewRoot, e));
                    json& keys = track[ChannelKey(channel)];
                    if (!keys.is_array()) keys = json::array();
                    float frame = SnapToFrame(m_AnimationPreviewTime, fps);
                    if (keys.empty() && frame > 0) {
                        glm::vec3 original = ChannelOf(reg.Get<TransformComponent>(e), channel);
                        keys.push_back({ 0.0f, original.x, original.y, original.z });
                    }
                    WriteKey(keys, frame, glm::vec4(now[channel], 0), 3, fps);
                    doc.dirty = true;
                }
                found->second = now;
            }
            // The same for every other component property
            for (EntityID e : preview.Reg().AllEntities()) {
                if (e != m_AnimationPreviewRoot && !preview.IsDescendantOf(e, m_AnimationPreviewRoot)) continue;
                for (const AnimatedProperty& property : AnimatedProperties::List(preview, e)) {
                    glm::vec4 now(0);
                    if (!AnimatedProperties::Get(preview, e, property.id, now)) continue;
                    std::string cacheKey = std::format("{}|{}", e, property.id);
                    auto found = s.sampledProperties.find(cacheKey);
                    if (rebuild || found == s.sampledProperties.end()) { s.sampledProperties[cacheKey] = now; continue; }
                    if (glm::all(glm::epsilonEqual(found->second, now, 1e-4f))) continue;
                    json& track = EnsureTrack(tracks, RelativePath(preview, m_AnimationPreviewRoot, e));
                    const std::string channelKey = property.ChannelKey();
                    json& keys = track[channelKey];
                    if (!keys.is_array()) keys = json::array();
                    float frame = SnapToFrame(m_AnimationPreviewTime, fps);
                    if (keys.empty() && frame > 0) {
                        glm::vec4 original(0);
                        AnimatedProperties::Get(scene, e, property.id, original);
                        keys.push_back(KeyJson(0.0f, original, property.dimension));
                    }
                    WriteKey(keys, frame, now, property.dimension, fps);
                    doc.dirty = true;
                    found->second = now;
                }
            }
        }

        // Rows
        std::vector<Row> rows;
        for (int t = 0; t < (int)tracks.size(); ++t)
            for (int c : TrackChannels(tracks[t])) {
                rows.push_back({ t, c, -1 });
                if (s.expanded.count(std::format("{}|{}", tracks[t].value("path", std::string()), c)))
                    for (int a = 0; Channel(c).dimension > 1 && a < Channel(c).dimension; ++a) rows.push_back({ t, c, a });
            }

        auto selectedOrAllKeyRows = [&] {
            std::set<std::pair<int, int>> out;
            for (auto& [t, c, a] : s.selectedRows) out.insert({ t, c });
            if (out.empty()) for (const Row& r : rows) out.insert({ r.track, r.channel });
            return out;
        };
        auto deleteSelection = [&] {
            for (auto it = s.selection.rbegin(); it != s.selection.rend(); ++it)
                if (KeyAt(tracks, *it)) { auto& keys = tracks[it->track][ChannelKey(it->channel)]; keys.erase(keys.begin() + it->key); }
            s.selection.clear();
            doc.dirty = true;
        };
        auto removeRows = [&](int onlyTrack, int onlyChannel) {
            std::set<std::pair<int, int>> targets;
            if (onlyTrack >= 0 && !s.selectedRows.count({ onlyTrack, onlyChannel, -1 })) targets.insert({ onlyTrack, onlyChannel });
            else for (auto& [t, c, a] : s.selectedRows) targets.insert({ t, c });
            for (auto& [t, c] : targets) tracks[t].erase(ChannelKey(c));
            for (int t = (int)tracks.size() - 1; t >= 0; --t)
                if (TrackChannels(tracks[t]).empty()) tracks.erase(tracks.begin() + t);
            s.selectedRows.clear();
            s.selection.clear();
            doc.dirty = true;
        };
        auto addEvent = [&](float t) {
            events.push_back({ { "time", std::clamp(SnapToFrame(t, fps), 0.0f, duration) }, { "function", "" }, { "script", "" }, { "signature", "" }, { "arguments", json::array() } });
            s.selectedEvents = { (int)events.size() - 1 };
            s.editEvent = (int)events.size() - 1;
            s.openEventEditor = true;
            doc.dirty = true;
        };
        auto deleteEvents = [&] {
            for (auto it = s.selectedEvents.rbegin(); it != s.selectedEvents.rend(); ++it)
                if (*it < (int)events.size()) events.erase(events.begin() + *it);
            s.selectedEvents.clear();
            s.editEvent = -1;
            doc.dirty = true;
        };
        auto frameRange = [&](float a, float b, float width) {
            float span = std::max(4.0f / fps, b - a);
            s.pixelsPerSecond = std::clamp((width - 40.0f) / span, 8.0f, 12000.0f);
            s.viewStart = a - 20.0f / s.pixelsPerSecond;
        };
        auto stepToKey = [&](int direction) {
            float best = s.time;
            bool found = false;
            for (auto& track : tracks) for (int channel : TrackChannels(track)) for (auto& key : track[ChannelKey(channel)]) {
                float t = key[0];
                if (direction * (t - s.time) > 0.5f / fps && (!found || direction * (t - best) < 0)) { best = t; found = true; }
            }
            if (found) setTime(best);
        };

        const float lineH = ImGui::GetFrameHeight(), rowH = lineH + 2.0f, headerH = lineH * 3 + 14.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float leftW = std::clamp(s.listWidth, 260.0f, std::max(261.0f, avail.x - 200.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2, 2 });

        // ---------------------------------------------------- left: controls and property list
        ImGui::BeginChild("##left", { leftW, avail.y }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPos({ 3, 3 });
        ImGui::BeginDisabled(!canPreview);
        if (previewing) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(47, 128, 237, 255));
        bool previewClicked = ImGui::Button("Preview");
        if (previewing) ImGui::PopStyleColor();
        if (previewClicked) { if (previewing) { StopAnimationPreview(); previewing = false; } else startPreview(); }
        ImGui::SameLine();
        if (GlyphButton("##record", Glyph::Record, m_AnimationRecording, "Record: changes to the object become keys on the current frame", IM_COL32(190, 50, 50, 255))) {
            if (m_AnimationRecording) m_AnimationRecording = false;
            else {
                if (!previewing) startPreview();
                m_AnimationRecording = previewing;
                m_AnimationPreviewPlaying = false;
                m_AnimationRecordTime = -1;
                s.sampled.clear();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (GlyphButton("##first", Glyph::First, false, "First frame")) setTime(0);
        ImGui::SameLine();
        if (GlyphButton("##prev", Glyph::Previous, false, "Previous frame  ( , )")) setTime(s.time - 1.0f / fps);
        ImGui::SameLine();
        ImGui::BeginDisabled(!canPreview);
        bool playing = previewing && m_AnimationPreviewPlaying;
        auto togglePlay = [&] {
            if (!previewing) startPreview();
            if (previewing) { m_AnimationPreviewPlaying = !m_AnimationPreviewPlaying; if (m_AnimationPreviewPlaying) m_AnimationRecording = false; }
        };
        if (GlyphButton("##play", playing ? Glyph::Pause : Glyph::Play, playing, "Play / pause  (Space)")) togglePlay();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (GlyphButton("##next", Glyph::Next, false, "Next frame  ( . )")) setTime(s.time + 1.0f / fps);
        ImGui::SameLine();
        if (GlyphButton("##last", Glyph::Last, false, "Last frame")) setTime(duration);
        ImGui::SameLine();
        int frame = (int)std::round(s.time * fps);
        ImGui::SetNextItemWidth(std::max(36.0f, leftW - ImGui::GetCursorPosX() - lineH - 12));
        if (ImGui::DragInt("##frame", &frame, 0.2f, 0, 1000000)) setTime(frame / fps);
        ImGui::SameLine();
        auto addKeysAtPlayhead = [&] {
            for (auto& [t, c] : selectedOrAllKeyRows()) addKey(t, c, s.time);
        };
        if (GlyphButton("##addKey", Glyph::AddKey, false, "Add keyframe  (K)")) addKeysAtPlayhead();

        ImGui::SetCursorPos({ 3, lineH + 7 });
        const float comboW = leftW - 62 - lineH - 8;
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##clip", FileSystem::Utf8(fs::u8path(doc.path).stem()).c_str())) {
            for (const std::string& clip : s.clips)
                if (ImGui::Selectable(FileSystem::Utf8(fs::u8path(clip).stem()).c_str(), clip == doc.path)) s.pendingClip = clip;
            if (s.explicitClip && std::find(s.clips.begin(), s.clips.end(), doc.path) == s.clips.end()) ImGui::Selectable(FileSystem::Utf8(fs::u8path(doc.path).stem()).c_str(), true);
            if (root && !controllerPath.empty()) {
                ImGui::Separator();
                if (ImGui::Selectable("Create New Clip...")) s.createClipPending = true;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        int samples = (int)fps;
        ImGui::SetNextItemWidth(58);
        if (ImGui::DragInt("##samples", &samples, 0.5f, 1, 240, "%d fps")) { doc.data["sampleRate"] = std::clamp(samples, 1, 240); doc.dirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Samples per second");
        ImGui::SameLine();
        if (GlyphButton("##lock", Glyph::Lock, s.locked, "Lock this window to the current object")) {
            s.locked = !s.locked;
            s.lockedEntity = s.locked ? selected : 0;
        }

        ImGui::SetCursorPos({ 8, lineH * 2 + 13 });
        ImGui::TextDisabled("Events");
        ImGui::SameLine(leftW - 96);
        if (ImGui::SmallButton("Add Event")) addEvent(s.time);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Call a script method at the current frame");

        ImGui::SetCursorPos({ 0, headerH });
        ImGui::BeginChild("##rowsList", { leftW, avail.y - headerH - lineH - 6 }, ImGuiChildFlags_None);
        s.scrollY = ImGui::GetScrollY();
        const float listInner = ImGui::GetContentRegionAvail().x;
        for (int i = 0; i < (int)rows.size(); ++i) {
            const Row row = rows[i];
            ImGui::PushID(i);
            const json& track = tracks[row.track];
            const float y = i * rowH;
            std::string expandKey = std::format("{}|{}", track.value("path", std::string()), row.channel);
            RowId id{ row.track, row.channel, row.axis };
            ImGui::SetCursorPos({ 0, y });
            ImVec2 rowPos = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##row", s.selectedRows.count(id) > 0, ImGuiSelectableFlags_AllowOverlap, { listInner, rowH - 1 })) {
                if (row.axis < 0 && ImGui::GetMousePos().x < rowPos.x + 22) {
                    if (!s.expanded.erase(expandKey)) s.expanded.insert(expandKey);
                } else {
                    if (!ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl) s.selectedRows.clear();
                    s.selectedRows.insert(id);
                }
            }
            if (ImGui::BeginPopupContextItem("##rowMenu")) {
                if (ImGui::MenuItem("Add Key")) addKey(row.track, row.channel, s.time);
                if (ImGui::BeginMenu("Interpolation")) {
                    std::string mode = track.value("interpolation", std::string("Linear"));
                    for (const char* option : { "Linear", "Constant", "Cubic" })
                        if (ImGui::MenuItem(option, nullptr, mode == option)) { tracks[row.track]["interpolation"] = option; doc.dirty = true; }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Properties")) { s.removeTrack = row.track; s.removeChannel = row.channel; }
                ImGui::EndPopup();
            }
            auto* draw = ImGui::GetWindowDrawList();
            float textY = rowPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            if (row.axis < 0) {
                const ChannelInfo& info = Channel(row.channel);
                bool isOpen = s.expanded.count(expandKey) > 0;
                float cx = rowPos.x + 11, cy = rowPos.y + rowH * 0.5f;
                if (info.dimension > 1) {
                    if (isOpen) draw->AddTriangleFilled({ cx - 4, cy - 2 }, { cx + 4, cy - 2 }, { cx, cy + 3 }, IM_COL32(170, 170, 170, 255));
                    else draw->AddTriangleFilled({ cx - 2, cy - 4 }, { cx - 2, cy + 4 }, { cx + 3, cy }, IM_COL32(170, 170, 170, 255));
                }
                std::string path = track.value("path", std::string());
                bool missing = reg.Valid(previewRoot) && !ResolvePath(scene, previewRoot, path);
                std::string label = path.empty() ? info.label : std::format("{} : {}", path, info.label);
                if (missing) label += " (Missing!)";
                ImVec4 textClip(rowPos.x + 22, rowPos.y, rowPos.x + listInner - (info.dimension == 1 ? 84 : 4), rowPos.y + rowH);
                draw->AddText(nullptr, 0, { rowPos.x + 22, textY }, missing ? IM_COL32(235, 190, 80, 255) : IM_COL32(215, 215, 215, 255), label.c_str(), nullptr, 0, &textClip);
                if (info.dimension == 1) {
                    glm::vec4 value = sampleChannel(track, row.channel, s.time);
                    const float frameTime = std::max(0.0f, SnapToFrame(s.time, fps));
                    bool edited = false;
                    ImGui::SetCursorPos({ listInner - 78, y });
                    ImGui::SetNextItemWidth(74);
                    if (info.kind == 'b') { bool on = value.x >= 0.5f; if (ImGui::Checkbox("##value", &on)) { value.x = on ? 1.0f : 0.0f; edited = true; } }
                    else if (info.kind == 'i') { int number = (int)std::round(value.x); if (ImGui::DragInt("##value", &number)) { value.x = (float)number; edited = true; } }
                    else edited = ImGui::DragFloat("##value", &value.x, 0.01f, 0, 0, "%.3f");
                    if (edited) { WriteKey(tracks[row.track][ChannelKey(row.channel)], frameTime, value, 1, fps); doc.dirty = true; }
                }
            } else {
                const ChannelInfo& info = Channel(row.channel);
                draw->AddText({ rowPos.x + 40, textY }, kAxisColors[row.axis], std::format("{}.{}", info.label.substr(info.label.rfind('.') == std::string::npos ? 0 : info.label.rfind('.') + 1), AxisName(info, row.axis)).c_str());
                glm::vec4 value = sampleChannel(track, row.channel, s.time);
                ImGui::SetCursorPos({ listInner - 78, y });
                ImGui::SetNextItemWidth(74);
                if (ImGui::DragFloat("##value", &value[row.axis], row.channel == 1 ? 0.5f : 0.01f, 0, 0, "%.3f")) {
                    WriteKey(tracks[row.track][ChannelKey(row.channel)], std::max(0.0f, SnapToFrame(s.time, fps)), value, info.dimension, fps);
                    doc.dirty = true;
                }
            }
            ImGui::PopID();
        }
        if (doc.data.value("humanoid", false)) {
            ImGui::SetCursorPos({ 8, 6 });
            ImGui::TextDisabled("Humanoid clip from FBX: keys are read-only.");
        }
        ImGui::SetCursorPos({ (listInner - 150) * 0.5f, (doc.data.value("humanoid", false) ? 1 : rows.size()) * rowH + 6 });
        if (ImGui::Button("Add Property", { 150, 0 })) ImGui::OpenPopup("##addProperty");
        ImGui::SetNextWindowSizeConstraints({ 340, 0 }, { 340, 440 });
        if (ImGui::BeginPopup("##addProperty")) {
            if (ImGui::IsWindowAppearing()) { s.propertyFilter.clear(); ImGui::SetKeyboardFocusHere(); }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##propertySearch", "Search properties...", &s.propertyFilter);
            ImGuiTextFilter filter(s.propertyFilter.c_str());
            const bool searching = filter.IsActive();
            if (reg.Valid(previewRoot)) {
                auto hasChannel = [&](const std::string& path, const std::string& key) {
                    for (const auto& track : tracks) if (track.value("path", std::string()) == path) return track.contains(key);
                    return false;
                };
                auto entry = [&](const std::string& path, const std::string& group, const std::string& label, const std::string& key, glm::vec4 value) {
                    if (searching && !filter.PassFilter(label.c_str()) && !filter.PassFilter(group.c_str())) return;
                    if (hasChannel(path, key)) { ImGui::Indent(); ImGui::TextDisabled("%s", label.c_str()); ImGui::Unindent(); return; }
                    ImGui::Indent();
                    if (ImGui::Selectable(label.c_str())) {
                        addChannel(path, ChannelId(key), value);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Unindent();
                };
                std::function<void(EntityID)> tree = [&](EntityID e) {
                    if (searching) ImGui::SetNextItemOpen(true);
                    if (!ImGui::TreeNodeEx((void*)(uintptr_t)e, e == previewRoot ? ImGuiTreeNodeFlags_DefaultOpen : 0, "%s", reg.Get<NameComponent>(e).name.c_str())) return;
                    const std::string path = RelativePath(scene, previewRoot, e);
                    if (searching) ImGui::SetNextItemOpen(true);
                    if (ImGui::TreeNode("Transform")) {
                        for (int c = 0; c < 3; ++c) entry(path, "Transform", Channel(c).label, ChannelKey(c), glm::vec4(ChannelOf(reg.Get<TransformComponent>(e), c), 0));
                        ImGui::TreePop();
                    }
                    auto properties = AnimatedProperties::List(scene, e);
                    for (size_t i = 0; i < properties.size();) {
                        const std::string group = properties[i].group;
                        size_t end = i;
                        while (end < properties.size() && properties[end].group == group) ++end;
                        if (searching) ImGui::SetNextItemOpen(true);
                        if (ImGui::TreeNode(group.c_str())) {
                            for (size_t p = i; p < end; ++p) {
                                glm::vec4 value(0);
                                AnimatedProperties::Get(scene, e, properties[p].id, value);
                                entry(path, group, properties[p].label, properties[p].ChannelKey(), value);
                            }
                            ImGui::TreePop();
                        }
                        i = end;
                    }
                    for (EntityID child : std::vector<EntityID>(scene.GetChildren(e))) tree(child);
                    ImGui::TreePop();
                };
                tree(previewRoot);
            } else {
                ImGui::TextDisabled("Select the object to animate");
                for (int c = 0; c < 3; ++c) if (ImGui::Selectable(Channel(c).label.c_str())) addChannel("", c, c == 2 ? glm::vec4(1, 1, 1, 0) : glm::vec4(0));
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        ImGui::SetCursorPos({ 3, avail.y - lineH - 3 });
        const char* views[] = { "Dopesheet", "Curves" };
        for (int i = 0; i < 2; ++i) {
            if (i) ImGui::SameLine(0, 2);
            bool active = s.curves == (i == 1);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(46, 48, 51, 255) : IM_COL32(27, 29, 32, 255));
            if (ImGui::Button(views[i], { 90, 0 })) s.curves = i == 1;
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        // Splitter
        ImGui::SameLine(0, 0);
        ImGui::InvisibleButton("##split", { 4, avail.y });
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) s.listWidth = leftW + ImGui::GetIO().MouseDelta.x;
        ImGui::GetWindowDrawList()->AddLine({ ImGui::GetItemRectMin().x + 2, ImGui::GetItemRectMin().y }, { ImGui::GetItemRectMin().x + 2, ImGui::GetItemRectMax().y }, IM_COL32(14, 16, 19, 255));
        ImGui::SameLine(0, 0);

        // ---------------------------------------------------- right: ruler, summary and keys
        ImGui::BeginChild("##right", { 0, avail.y }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 rightPos = ImGui::GetCursorScreenPos();
        const float rightW = ImGui::GetContentRegionAvail().x, rightH = ImGui::GetContentRegionAvail().y;
        const float originX = rightPos.x;
        auto X = [&](float t) { return originX + (t - s.viewStart) * s.pixelsPerSecond; };
        auto T = [&](float x) { return (x - originX) / s.pixelsPerSecond + s.viewStart; };
        const ImVec2 mouse = ImGui::GetMousePos();
        auto& io = ImGui::GetIO();

        auto navigate = [&] { // wheel zooms around the mouse, middle drag pans
            if (io.MouseWheel != 0) {
                float anchor = T(mouse.x);
                s.pixelsPerSecond = std::clamp(s.pixelsPerSecond * std::pow(1.15f, io.MouseWheel), 8.0f, 12000.0f);
                s.viewStart = anchor - (mouse.x - originX) / s.pixelsPerSecond;
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) s.viewStart -= io.MouseDelta.x / s.pixelsPerSecond;
        };

        float majorFrames = 1;
        for (float candidate : { 1.f, 2.f, 5.f, 10.f, 15.f, 30.f, 60.f, 120.f, 300.f, 600.f, 1200.f, 3000.f, 6000.f }) {
            majorFrames = candidate;
            if (candidate / fps * s.pixelsPerSecond >= 64.0f) break;
        }
        const float majorStep = majorFrames / fps, minorStep = majorStep / 5.0f;
        auto forEachMajor = [&](auto&& fn) {
            float t = std::floor(std::max(0.0f, s.viewStart) / majorStep) * majorStep;
            for (; X(t) < originX + rightW; t += majorStep) fn(t);
        };
        auto* header = ImGui::GetWindowDrawList();
        const float rulerH = lineH + 4, eventH = lineH + 4, laneH = headerH - rulerH - eventH;
        header->AddRectFilled(rightPos, { rightPos.x + rightW, rightPos.y + rulerH }, IM_COL32(31, 33, 36, 255));
        header->AddRectFilled({ rightPos.x, rightPos.y + rulerH }, { rightPos.x + rightW, rightPos.y + headerH }, IM_COL32(26, 28, 31, 255));
        for (float m = std::floor(std::max(0.0f, s.viewStart) / minorStep) * minorStep; X(m) < originX + rightW; m += minorStep)
            header->AddLine({ X(m), rightPos.y + rulerH - 4 }, { X(m), rightPos.y + rulerH }, IM_COL32(66, 68, 71, 255));
        forEachMajor([&](float t) {
            int total = (int)std::round(t * fps), seconds = total / (int)fps, frames = total % (int)fps;
            header->AddLine({ X(t), rightPos.y + rulerH - 9 }, { X(t), rightPos.y + rulerH }, IM_COL32(150, 150, 150, 255));
            header->AddText({ X(t) + 3, rightPos.y + 2 }, IM_COL32(190, 190, 190, 255), std::format("{}:{:02}", seconds, frames).c_str());
        });

        ImGui::SetCursorScreenPos(rightPos);
        ImGui::InvisibleButton("##ruler", { rightW, rulerH });
        if (ImGui::IsItemHovered()) navigate();
        if (ImGui::IsItemActive()) setTime(SnapToFrame(T(mouse.x), fps));

        auto beginKeyDrag = [&](float anchor) {
            s.draggingKeys = true;
            s.dragAnchor = anchor;
            s.dragOrigin.clear();
            for (const KeyRef& r : s.selection) if (json* key = KeyAt(tracks, r)) s.dragOrigin[r] = (*key)[0].get<float>();
            s.eventOrigin.clear();
            for (int i : s.selectedEvents) if (i < (int)events.size()) s.eventOrigin[i] = events[i].value("time", 0.0f);
        };

        // Summary lane: one diamond per frame that has keys
        std::map<int, std::vector<KeyRef>> frames;
        for (int t = 0; t < (int)tracks.size(); ++t)
            for (int c : TrackChannels(tracks[t])) {
                auto& keys = tracks[t][ChannelKey(c)];
                for (int k = 0; k < (int)keys.size(); ++k) if (KeyAt(tracks, { t, c, k })) frames[(int)std::round(keys[k][0].get<float>() * fps)].push_back({ t, c, k });
            }
        // Events lane
        {
            const float top = rightPos.y + rulerH + laneH;
            ImGui::SetCursorScreenPos({ rightPos.x, top });
            ImGui::InvisibleButton("##events", { rightW, eventH }, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool laneHovered = ImGui::IsItemHovered();
            if (laneHovered) navigate();
            header->AddRectFilled({ rightPos.x, top }, { rightPos.x + rightW, top + eventH }, IM_COL32(21, 23, 27, 255));
            int hitEvent = -1;
            for (int i = 0; i < (int)events.size(); ++i) {
                const float x = X(events[i].value("time", 0.0f));
                if (x < originX - 12 || x > originX + rightW + 12) continue;
                const bool chosen = s.selectedEvents.count(i) > 0, named = !events[i].value("function", std::string()).empty();
                const ImU32 color = chosen ? IM_COL32(95, 170, 240, 255) : named ? IM_COL32(215, 215, 215, 255) : IM_COL32(235, 180, 80, 255);
                header->AddRectFilled({ x - 1, top + 3 }, { x + 1, top + eventH - 2 }, color);
                header->AddTriangleFilled({ x + 1, top + 3 }, { x + 10, top + 7.5f }, { x + 1, top + 12 }, color);
                if (laneHovered && mouse.x >= x - 4 && mouse.x <= x + 11) hitEvent = i;
            }
            if (ImGui::IsItemClicked(0)) {
                if (hitEvent >= 0) {
                    if (!s.selectedEvents.count(hitEvent)) { if (!io.KeyShift) { s.selectedEvents.clear(); s.selection.clear(); } s.selectedEvents.insert(hitEvent); }
                    beginKeyDrag(events[hitEvent].value("time", 0.0f));
                } else if (!io.KeyShift) s.selectedEvents.clear();
            }
            if (laneHovered && ImGui::IsMouseDoubleClicked(0)) {
                if (hitEvent >= 0) { s.editEvent = hitEvent; s.openEventEditor = true; }
                else addEvent(T(mouse.x));
            }
            if (laneHovered && ImGui::IsMouseClicked(1)) {
                s.menuEvent = hitEvent;
                s.menuTime = T(mouse.x);
                if (hitEvent >= 0 && !s.selectedEvents.count(hitEvent)) s.selectedEvents = { hitEvent };
                ImGui::OpenPopup("##eventMenu");
            }
            if (hitEvent >= 0) {
                const std::string function = events[hitEvent].value("function", std::string());
                ImGui::SetTooltip("%s", function.empty() ? "No function chosen (double-click to edit)" : function.c_str());
            }
            if (ImGui::BeginPopup("##eventMenu")) {
                if (s.menuEvent < 0 && ImGui::MenuItem("Add Event")) addEvent(s.menuTime);
                if (s.menuEvent >= 0 && ImGui::MenuItem("Edit Event")) { s.editEvent = s.menuEvent; s.openEventEditor = true; }
                if (s.menuEvent >= 0 && ImGui::MenuItem("Delete Event")) deleteEvents();
                ImGui::EndPopup();
            }
            if (std::exchange(s.openEventEditor, false)) ImGui::OpenPopup("##eventEditor");
            ImGui::SetNextWindowSizeConstraints({ 340, 0 }, { 340, 420 });
            if (ImGui::BeginPopup("##eventEditor")) {
                if (s.editEvent >= 0 && s.editEvent < (int)events.size()) {
                    json& event = events[s.editEvent];
                    ImGui::SeparatorText("Animation Event");
                    float eventTime = event.value("time", 0.0f);
                    if (Insp::DragFloat("Time", &eventTime, 0.5f / fps, 0.0f, duration, "%.3f s")) { event["time"] = SnapToFrame(eventTime, fps); doc.dirty = true; }
                    const std::string function = event.value("function", std::string()), eventScript = event.value("script", std::string()), signature = event.value("signature", std::string());
                    std::string preview = function.empty() ? std::string("(choose a method)") : eventScript.empty() ? function : eventScript + "." + function;
                    auto* scripts = reg.Valid(previewRoot) ? reg.TryGet<ScriptComponent>(previewRoot) : nullptr;
                    if (Insp::BeginCombo("Function", preview.c_str())) {
                        bool any = false;
                        for (size_t slot = 0; scripts && slot < scripts->Count(); ++slot) {
                            const ScriptClassInfo* info = ScriptEngine::FindClass(scripts->Slot(slot).className);
                            if (!info) continue;
                            for (const ScriptMethodInfo& method : info->methods) {
                                any = true;
                                if (!ImGui::Selectable(std::format("{}.{}##{}", info->name, method.name, method.signature).c_str(), method.signature == signature)) continue;
                                json arguments = json::array();
                                for (const auto& parameter : method.parameters) arguments.push_back(json::parse(parameter.defaultJson, nullptr, false));
                                event["function"] = method.name; event["script"] = info->name; event["signature"] = method.signature; event["arguments"] = arguments;
                                doc.dirty = true;
                            }
                        }
                        if (!any) ImGui::TextDisabled("No public methods on this object's scripts");
                        ImGui::EndCombo();
                    }
                    std::string typed = function;
                    if (Insp::InputText("Method Name", &typed)) { event["function"] = typed; event["script"] = ""; event["signature"] = ""; event["arguments"] = json::array(); doc.dirty = true; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Any method without parameters, private ones included");
                    const ScriptClassInfo* eventClass = eventScript.empty() ? nullptr : ScriptEngine::FindClass(eventScript);
                    const ScriptMethodInfo* method = nullptr;
                    if (eventClass) for (const ScriptMethodInfo& candidate : eventClass->methods) if (candidate.signature == signature) method = &candidate;
                    if (method) {
                        json& arguments = event["arguments"];
                        while (arguments.size() < method->parameters.size()) arguments.push_back(json::parse(method->parameters[arguments.size()].defaultJson, nullptr, false));
                        for (int a = 0; a < (int)method->parameters.size(); ++a) {
                            const auto& parameter = method->parameters[a];
                            json& value = arguments[a];
                            ImGui::PushID(a);
                            bool edited = false;
                            if (parameter.type == "float") { float v = value.is_number() ? value.get<float>() : 0; if ((edited = Insp::DragFloat(parameter.name.c_str(), &v, 0.05f))) value = v; }
                            else if (parameter.type == "int") { int v = value.is_number() ? value.get<int>() : 0; if ((edited = Insp::DragInt(parameter.name.c_str(), &v))) value = v; }
                            else if (parameter.type == "bool") { bool v = value.is_boolean() && value.get<bool>(); if ((edited = Insp::Checkbox(parameter.name.c_str(), &v))) value = v; }
                            else if (parameter.type == "string") { std::string v = value.is_string() ? value.get<std::string>() : ""; if ((edited = Insp::InputText(parameter.name.c_str(), &v))) value = v; }
                            else if (parameter.type == "Vector2" || parameter.type == "Vector3" || parameter.type == "Color") {
                                float v[4] = { 0, 0, 0, 1 };
                                int count = parameter.type == "Vector2" ? 2 : parameter.type == "Vector3" ? 3 : 4;
                                if (value.is_array()) for (int k = 0; k < count && k < (int)value.size(); ++k) v[k] = value[k].get<float>();
                                edited = count == 2 ? Insp::DragFloat2(parameter.name.c_str(), v, 0.05f) : count == 3 ? Insp::DragFloat3(parameter.name.c_str(), v, 0.05f) : Insp::ColorEdit4(parameter.name.c_str(), v);
                                if (edited) { value = json::array(); for (int k = 0; k < count; ++k) value.push_back(v[k]); }
                            } else {
                                if (!value.is_string() && !value.is_object()) value = "0";
                                Insp::LabelCell(parameter.name.c_str());
                                edited = AssetUI::EntityField(scene, parameter.type, value);
                            }
                            doc.dirty |= edited;
                            ImGui::PopID();
                        }
                    }
                    if (ImGui::Button("Delete Event")) { s.selectedEvents = { s.editEvent }; deleteEvents(); ImGui::CloseCurrentPopup(); }
                } else ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        ImGui::SetCursorScreenPos({ rightPos.x, rightPos.y + rulerH });
        ImGui::InvisibleButton("##summary", { rightW, laneH });
        bool laneHover = ImGui::IsItemHovered();
        if (laneHover) navigate();
        const float laneY = rightPos.y + rulerH + laneH * 0.5f;
        if (doc.data.value("humanoid", false) && doc.data.contains("hips")) // imported keys: shown, not editable
            for (const json& key : doc.data["hips"]) Diamond(header, { X(key[0].get<float>()), laneY }, 4.0f, IM_COL32(120, 124, 132, 255));
        for (auto& [f, refs] : frames) {
            float x = X(f / fps);
            if (x < originX - 8 || x > originX + rightW + 8) continue;
            bool isSelected = std::all_of(refs.begin(), refs.end(), [&](const KeyRef& r) { return s.selection.count(r) > 0; });
            Diamond(header, { x, laneY }, 5.5f, isSelected ? IM_COL32(95, 170, 240, 255) : IM_COL32(170, 170, 170, 255));
            if (laneHover && ImGui::IsMouseClicked(0) && std::abs(mouse.x - x) < 7) {
                if (!isSelected) { if (!io.KeyShift) s.selection.clear(); s.selection.insert(refs.begin(), refs.end()); }
                beginKeyDrag(f / fps);
            }
        }
        if (X(s.time) >= originX) {
            header->AddLine({ X(s.time), rightPos.y }, { X(s.time), rightPos.y + headerH }, IM_COL32(255, 255, 255, 230), 1.0f);
            header->AddTriangleFilled({ X(s.time) - 5, rightPos.y }, { X(s.time) + 5, rightPos.y }, { X(s.time), rightPos.y + 7 }, IM_COL32(255, 255, 255, 230));
        }

        // Body
        ImGui::SetCursorScreenPos({ rightPos.x, rightPos.y + headerH });
        ImGui::BeginChild("##body", { rightW, rightH - headerH }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (!s.curves) ImGui::SetScrollY(s.scrollY);
        const ImVec2 bodyPos = ImGui::GetCursorScreenPos();
        const float bodyH = rightH - headerH;
        const float contentH = s.curves ? bodyH : std::max(bodyH, rows.size() * rowH + lineH + 16);
        auto* body = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##canvas", { rightW, contentH }, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) navigate();
        body->AddRectFilled(bodyPos, { bodyPos.x + rightW, bodyPos.y + contentH }, IM_COL32(30, 32, 35, 255));
        forEachMajor([&](float t) { body->AddLine({ X(t), bodyPos.y }, { X(t), bodyPos.y + contentH }, IM_COL32(38, 40, 43, 255)); });

        std::optional<KeyRef> hit;
        if (s.curves) {
            std::vector<std::tuple<int, int, int>> curves; // track, channel, axis
            std::set<std::tuple<int, int, int>> seen;
            auto addCurve = [&](int t, int c, int a) { if (seen.insert({ t, c, a }).second) curves.push_back({ t, c, a }); };
            for (auto& [t, c, a] : s.selectedRows) {
                if (t >= (int)tracks.size() || !tracks[t].contains(ChannelKey(c))) continue;
                if (a < 0) { for (int i = 0; i < Channel(c).dimension; ++i) addCurve(t, c, i); } else addCurve(t, c, a);
            }
            if (curves.empty()) for (const Row& r : rows) for (int i = 0; i < Channel(r.channel).dimension; ++i) addCurve(r.track, r.channel, i);

            if (!s.curveDrag) {
                float lo = FLT_MAX, hi = -FLT_MAX;
                for (auto& [t, c, a] : curves) for (auto& key : tracks[t][ChannelKey(c)]) if (key.is_array() && key.size() > (size_t)Channel(c).dimension) { float v = key[a + 1]; lo = std::min(lo, v); hi = std::max(hi, v); }
                if (lo > hi) { lo = -1; hi = 1; }
                float pad = std::max(0.25f, (hi - lo) * 0.15f);
                s.valueMin = lo - pad;
                s.valueMax = hi + pad;
            }
            auto Y = [&](float v) { return bodyPos.y + bodyH - 10 - (v - s.valueMin) / (s.valueMax - s.valueMin) * (bodyH - 20); };
            auto V = [&](float y) { return s.valueMin + (bodyPos.y + bodyH - 10 - y) / (bodyH - 20) * (s.valueMax - s.valueMin); };

            float gridStep = std::pow(10.0f, std::floor(std::log10(std::max(1e-4f, s.valueMax - s.valueMin))) );
            if ((s.valueMax - s.valueMin) / gridStep < 4) gridStep *= 0.5f;
            for (float v = std::ceil(s.valueMin / gridStep) * gridStep; v <= s.valueMax; v += gridStep) {
                body->AddLine({ bodyPos.x, Y(v) }, { bodyPos.x + rightW, Y(v) }, std::abs(v) < gridStep * 0.01f ? IM_COL32(60, 62, 65, 255) : IM_COL32(38, 40, 43, 255));
                body->AddText({ bodyPos.x + 4, Y(v) - ImGui::GetTextLineHeight() }, IM_COL32(135, 135, 135, 255), std::format("{:.2f}", v).c_str());
            }

            int hitAxis = -1, hitHandle = 0;
            for (auto& [t, c, a] : curves) {
                const json& track = tracks[t];
                const json& keys = track[ChannelKey(c)];
                if (keys.empty()) continue;
                const int dimension = Channel(c).dimension;
                std::string mode = InterpolationOf(track, c);
                float first = FLT_MAX, last = -FLT_MAX;
                for (auto& key : keys) if (key.is_array() && key.size() > (size_t)dimension) { first = std::min(first, key[0].get<float>()); last = std::max(last, key[0].get<float>()); }
                if (first > last) continue;
                ImVec2 previous{};
                bool hasPrevious = false;
                const float startX = std::max(X(first), originX), endX = std::min(X(last), originX + rightW);
                for (float x = startX;; x += 3.0f) {
                    float px = std::min(x, endX);
                    glm::vec4 value = AnimatorSystem::EvaluateN(keys, T(px), glm::vec4(0), mode, dimension);
                    ImVec2 point(px, Y(value[a]));
                    if (hasPrevious) body->AddLine(previous, point, kAxisColors[a], 1.6f);
                    previous = point;
                    hasPrevious = true;
                    if (px >= endX) break;
                }
                for (int k = 0; k < (int)keys.size(); ++k) {
                    if (!KeyAt(tracks, { t, c, k })) continue;
                    ImVec2 p(X(keys[k][0].get<float>()), Y(keys[k][a + 1].get<float>()));
                    KeyRef ref{ t, c, k };
                    bool isSelected = s.selection.count(ref) > 0;
                    if (isSelected && mode == "Cubic") {
                        const float dt = 40.0f / s.pixelsPerSecond;
                        for (int side : { -1, 1 }) {
                            int index = (side < 0 ? 1 + dimension : 1 + 2 * dimension) + a;
                            float slope = (int)keys[k].size() > index ? keys[k][index].get<float>() : 0.0f;
                            ImVec2 handle(X(keys[k][0].get<float>() + side * dt), Y(keys[k][a + 1].get<float>() + side * dt * slope));
                            body->AddLine(p, handle, IM_COL32(190, 190, 190, 255));
                            body->AddCircleFilled(handle, 3.5f, IM_COL32(230, 230, 230, 255));
                            if (hovered && std::hypot(mouse.x - handle.x, mouse.y - handle.y) < 6) { hit = ref; hitAxis = a; hitHandle = side; }
                        }
                    }
                    body->AddCircleFilled(p, 4.0f, isSelected ? IM_COL32(255, 255, 255, 255) : kAxisColors[a]);
                    if (hovered && !hitHandle && std::hypot(mouse.x - p.x, mouse.y - p.y) < 6) { hit = ref; hitAxis = a; }
                }
            }

            if (ImGui::IsItemClicked(0)) {
                if (hit) {
                    if (!s.selection.count(*hit)) { if (!io.KeyShift) s.selection.clear(); s.selection.insert(*hit); }
                    s.curveDrag = true;
                    s.curveAxis = hitAxis;
                    s.handleSide = hitHandle;
                } else if (!io.KeyShift) s.selection.clear();
            }
            if (hovered && ImGui::IsMouseDoubleClicked(0) && !hit && !curves.empty()) {
                float t = T(mouse.x), best = FLT_MAX;
                std::tuple<int, int, int> nearest = curves[0];
                for (auto& curve : curves) {
                    auto& [ct, cc, ca] = curve;
                    float distance = std::abs(Y(sampleChannel(tracks[ct], cc, t)[ca]) - mouse.y);
                    if (distance < best) { best = distance; nearest = curve; }
                }
                addKey(std::get<0>(nearest), std::get<1>(nearest), t);
            }
            if (s.curveDrag) {
                if (!ImGui::IsMouseDown(0)) s.curveDrag = false;
                else if (ImGui::IsMouseDragging(0, 1.0f)) {
                    for (const KeyRef& r : s.selection) {
                        json* key = KeyAt(tracks, r);
                        if (!key) continue;
                        if (s.handleSide) {
                            const int dimension = Channel(r.channel).dimension;
                            while ((int)key->size() < 1 + 3 * dimension) key->push_back(0.0f);
                            float dt = T(mouse.x) - (*key)[0].get<float>();
                            if (dt * s.handleSide < 1e-3f) dt = 1e-3f * s.handleSide;
                            (*key)[(s.handleSide < 0 ? 1 + dimension : 1 + 2 * dimension) + s.curveAxis] = (V(mouse.y) - (*key)[s.curveAxis + 1].get<float>()) / dt;
                        } else {
                            (*key)[0] = std::max(0.0f, SnapToFrame(T(mouse.x), fps));
                            (*key)[s.curveAxis + 1] = V(mouse.y);
                        }
                        doc.dirty = true;
                        if (s.selection.size() > 1) break;
                    }
                }
            }
        } else {
            int hoveredRow = -1;
            const float visibleTop = bodyPos.y + ImGui::GetScrollY(), visibleBottom = visibleTop + bodyH;
            if (hovered) hoveredRow = (int)std::floor((mouse.y - bodyPos.y) / rowH);
            for (int i = 0; i < (int)rows.size(); ++i) {
                const float y = bodyPos.y + i * rowH;
                if (y + rowH < visibleTop || y > visibleBottom) continue;
                const Row& row = rows[i];
                bool rowSelected = s.selectedRows.count({ row.track, row.channel, row.axis }) > 0;
                body->AddRectFilled({ bodyPos.x, y }, { bodyPos.x + rightW, y + rowH - 1 }, rowSelected ? IM_COL32(58, 66, 76, 255) : row.axis < 0 ? IM_COL32(34, 36, 39, 255) : IM_COL32(31, 33, 36, 255));
                const auto& keys = tracks[row.track][ChannelKey(row.channel)];
                std::vector<std::pair<float, int>> ordered;
                for (int k = 0; k < (int)keys.size(); ++k) if (KeyAt(tracks, { row.track, row.channel, k })) ordered.push_back({ keys[k][0].get<float>(), k });
                std::sort(ordered.begin(), ordered.end());
                for (auto& [t, k] : ordered) {
                    float x = X(t);
                    if (x < originX - 8 || x > originX + rightW + 8) continue;
                    KeyRef ref{ row.track, row.channel, k };
                    bool isSelected = s.selection.count(ref) > 0;
                    Diamond(body, { x, y + rowH * 0.5f }, row.axis < 0 ? 5.5f : 4.5f, isSelected ? IM_COL32(95, 170, 240, 255) : row.axis < 0 ? IM_COL32(205, 205, 205, 255) : IM_COL32(160, 160, 160, 255));
                    if (hoveredRow == i && std::abs(mouse.x - x) < 6.5f) hit = ref;
                }
            }
            // A selection spanning time gets a box: side handles stretch it, the middle moves it
            float selectionMin = FLT_MAX, selectionMax = -FLT_MAX;
            int firstRow = INT_MAX, lastRow = -1;
            for (const KeyRef& r : s.selection) if (json* key = KeyAt(tracks, r)) { float t = (*key)[0]; selectionMin = std::min(selectionMin, t); selectionMax = std::max(selectionMax, t); }
            for (int i : s.selectedEvents) if (i < (int)events.size()) { float t = events[i].value("time", 0.0f); selectionMin = std::min(selectionMin, t); selectionMax = std::max(selectionMax, t); }
            for (int i = 0; i < (int)rows.size(); ++i)
                for (const KeyRef& r : s.selection) if (r.track == rows[i].track && r.channel == rows[i].channel) { firstRow = std::min(firstRow, i); lastRow = std::max(lastRow, i); break; }
            const bool scalable = s.selection.size() + s.selectedEvents.size() >= 2 && selectionMax - selectionMin > 0.5f / fps;
            int scaleHandle = 0;
            bool insideBox = false;
            if (scalable) {
                if (lastRow < 0) { firstRow = 0; lastRow = 0; }
                const float boxLeft = X(selectionMin), boxRight = X(selectionMax), boxTop = bodyPos.y + firstRow * rowH, boxBottom = bodyPos.y + (lastRow + 1) * rowH;
                const bool inHeight = hovered && mouse.y >= boxTop && mouse.y <= boxBottom;
                if (inHeight && mouse.x >= boxLeft - 15 && mouse.x <= boxLeft - 5) scaleHandle = -1;
                if (inHeight && mouse.x >= boxRight + 5 && mouse.x <= boxRight + 15) scaleHandle = 1;
                insideBox = inHeight && mouse.x > boxLeft - 5 && mouse.x < boxRight + 5;
                if (scaleHandle || s.scaleSide) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                body->AddRectFilled({ boxLeft - 5, boxTop }, { boxRight + 5, boxBottom }, IM_COL32(95, 170, 240, 32));
                const ImU32 leftColor = scaleHandle < 0 || s.scaleSide < 0 ? IM_COL32(150, 205, 255, 255) : IM_COL32(95, 170, 240, 220);
                const ImU32 rightColor = scaleHandle > 0 || s.scaleSide > 0 ? IM_COL32(150, 205, 255, 255) : IM_COL32(95, 170, 240, 220);
                body->AddRectFilled({ boxLeft - 13, boxTop }, { boxLeft - 7, boxBottom }, leftColor, 2);
                body->AddRectFilled({ boxRight + 7, boxTop }, { boxRight + 13, boxBottom }, rightColor, 2);
            }
            if (ImGui::IsItemClicked(0) && scaleHandle) {
                s.scaleSide = scaleHandle;
                s.scaleMin = selectionMin;
                s.scaleMax = selectionMax;
                beginKeyDrag(0.0f);
                s.draggingKeys = false;
            } else if (ImGui::IsItemClicked(0) && !hit && insideBox) {
                beginKeyDrag(T(mouse.x));
            } else if (ImGui::IsItemClicked(0)) {
                if (hit) {
                    if (!s.selection.count(*hit)) { if (!io.KeyShift) { s.selection.clear(); s.selectedEvents.clear(); } s.selection.insert(*hit); }
                    json* key = KeyAt(tracks, *hit);
                    beginKeyDrag(key ? (*key)[0].get<float>() : 0.0f);
                } else {
                    if (!io.KeyShift) { s.selection.clear(); s.selectedEvents.clear(); }
                    s.boxSelecting = true;
                    s.boxStart = mouse;
                }
            }
            if (hovered && ImGui::IsMouseDoubleClicked(0) && !hit && hoveredRow >= 0 && hoveredRow < (int)rows.size())
                addKey(rows[hoveredRow].track, rows[hoveredRow].channel, T(mouse.x));
            if (hovered && ImGui::IsMouseClicked(1)) {
                s.menuRow = hoveredRow < (int)rows.size() ? hoveredRow : -1;
                s.menuTime = T(mouse.x);
                s.menuOnKey = hit.has_value();
                if (hit && !s.selection.count(*hit)) s.selection = { *hit };
                ImGui::OpenPopup("##keyMenu");
            }
            if (s.boxSelecting) {
                ImVec2 a(std::min(s.boxStart.x, mouse.x), std::min(s.boxStart.y, mouse.y)), b(std::max(s.boxStart.x, mouse.x), std::max(s.boxStart.y, mouse.y));
                body->AddRectFilled(a, b, IM_COL32(95, 170, 240, 40));
                body->AddRect(a, b, IM_COL32(95, 170, 240, 200));
                if (!ImGui::IsMouseDown(0)) {
                    s.boxSelecting = false;
                    if (b.x - a.x > 2 || b.y - a.y > 2)
                        for (int i = 0; i < (int)rows.size(); ++i) {
                            float cy = bodyPos.y + i * rowH + rowH * 0.5f;
                            if (cy < a.y || cy > b.y) continue;
                            auto& keys = tracks[rows[i].track][ChannelKey(rows[i].channel)];
                            for (int k = 0; k < (int)keys.size(); ++k) {
                                KeyRef ref{ rows[i].track, rows[i].channel, k };
                                if (!KeyAt(tracks, ref)) continue;
                                float x = X(keys[k][0].get<float>());
                                if (x >= a.x && x <= b.x) s.selection.insert(ref);
                            }
                        }
                }
            }
        }

        if (s.draggingKeys) {
            if (!ImGui::IsMouseDown(0)) s.draggingKeys = false;
            else if (ImGui::IsMouseDragging(0, 2.0f)) {
                float delta = SnapToFrame(T(mouse.x), fps) - SnapToFrame(s.dragAnchor, fps);
                for (auto& [ref, origin] : s.dragOrigin)
                    if (json* key = KeyAt(tracks, ref)) (*key)[0] = std::max(0.0f, SnapToFrame(origin + delta, fps));
                for (auto& [index, origin] : s.eventOrigin)
                    if (index < (int)events.size()) events[index]["time"] = std::clamp(SnapToFrame(origin + delta, fps), 0.0f, duration);
                doc.dirty = true;
            }
        }
        if (s.scaleSide) {
            if (!ImGui::IsMouseDown(0)) s.scaleSide = 0;
            else if (ImGui::IsMouseDragging(0, 1.0f)) {
                const float pivot = s.scaleSide < 0 ? s.scaleMax : s.scaleMin, edge = s.scaleSide < 0 ? s.scaleMin : s.scaleMax;
                float target = std::max(0.0f, SnapToFrame(T(mouse.x), fps));
                target = s.scaleSide < 0 ? std::min(target, pivot - 1.0f / fps) : std::max(target, pivot + 1.0f / fps);
                const float factor = (target - pivot) / (edge - pivot);
                auto stretched = [&](float time) { return std::max(0.0f, SnapToFrame(pivot + (time - pivot) * factor, fps)); };
                for (auto& [ref, origin] : s.dragOrigin) if (json* key = KeyAt(tracks, ref)) (*key)[0] = stretched(origin);
                for (auto& [index, origin] : s.eventOrigin) if (index < (int)events.size()) events[index]["time"] = stretched(origin);
                doc.dirty = true;
            }
        }

        if (ImGui::BeginPopup("##keyMenu")) {
            if (s.menuOnKey && ImGui::MenuItem("Delete Keys", "Del")) deleteSelection();
            if (!s.menuOnKey && s.menuRow >= 0 && s.menuRow < (int)rows.size() && ImGui::MenuItem("Add Key"))
                addKey(rows[s.menuRow].track, rows[s.menuRow].channel, s.menuTime);
            if (!s.menuOnKey && s.menuRow < 0) ImGui::TextDisabled("Double-click a row to add a key");
            ImGui::EndPopup();
        }

        // Clip end shading and playhead, over the keys
        if (X(duration) < originX + rightW)
            body->AddRectFilled({ std::max(originX, X(duration)), bodyPos.y }, { originX + rightW, bodyPos.y + contentH }, IM_COL32(0, 0, 0, 55));
        if (X(s.time) >= originX) body->AddLine({ X(s.time), bodyPos.y }, { X(s.time), bodyPos.y + contentH }, m_AnimationRecording ? IM_COL32(245, 80, 80, 255) : IM_COL32(255, 255, 255, 230));
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        if (m_AnimationWindowFocused) {
            auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
            if (pressed(ImGuiKey_Space) && canPreview) togglePlay();
            if (ImGui::IsKeyPressed(ImGuiKey_Comma)) { if (io.KeyAlt) stepToKey(-1); else setTime(s.time - 1.0f / fps); }
            if (ImGui::IsKeyPressed(ImGuiKey_Period)) { if (io.KeyAlt) stepToKey(1); else setTime(s.time + 1.0f / fps); }
            if (pressed(ImGuiKey_K)) addKeysAtPlayhead();
            if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) {
                if (!s.selectedEvents.empty()) deleteEvents();
                if (!s.selection.empty()) deleteSelection();
                else if (!s.selectedRows.empty()) { s.removeTrack = -1; s.removeChannel = -1; }
            }
            if (io.KeyCtrl && pressed(ImGuiKey_A)) { s.selection.clear(); for (auto& [frameNumber, refs] : frames) s.selection.insert(refs.begin(), refs.end()); }
            else if (pressed(ImGuiKey_A)) frameRange(0, duration, rightW);
            if (pressed(ImGuiKey_F)) {
                float lo = FLT_MAX, hi = -FLT_MAX;
                for (const KeyRef& r : s.selection) if (json* key = KeyAt(tracks, r)) { float t = (*key)[0]; lo = std::min(lo, t); hi = std::max(hi, t); }
                if (lo <= hi) frameRange(lo, hi, rightW); else frameRange(0, duration, rightW);
            }
            if (io.KeyCtrl && pressed(ImGuiKey_C) && !s.selection.empty()) {
                s.clipboard.clear();
                float earliest = FLT_MAX;
                for (const KeyRef& r : s.selection) if (json* key = KeyAt(tracks, r)) earliest = std::min(earliest, (*key)[0].get<float>());
                for (const KeyRef& r : s.selection) if (json* key = KeyAt(tracks, r)) s.clipboard.push_back({ r.track, r.channel, (*key)[0].get<float>() - earliest, *key });
            }
            if (io.KeyCtrl && pressed(ImGuiKey_V)) {
                for (auto& copied : s.clipboard) {
                    if (copied.track >= (int)tracks.size() || !tracks[copied.track].contains(ChannelKey(copied.channel))) continue;
                    const int dimension = Channel(copied.channel).dimension;
                    glm::vec4 value(0);
                    for (int i = 0; i < dimension; ++i) value[i] = copied.key[i + 1].get<float>();
                    WriteKey(tracks[copied.track][ChannelKey(copied.channel)], SnapToFrame(s.time + copied.offset, fps), value, dimension, fps);
                    doc.dirty = true;
                }
            }
        }

        if (s.removeTrack != -2) { removeRows(s.removeTrack, s.removeChannel); s.removeTrack = -2; }

        // Autosave once the mouse is up: sort keys, merge keys on one frame, update the clip length
        if (doc.dirty && !ImGui::IsAnyMouseDown()) {
            struct Kept { int track, channel; float time; };
            std::vector<Kept> kept;
            for (const KeyRef& r : s.selection) if (json* key = KeyAt(tracks, r)) kept.push_back({ r.track, r.channel, (*key)[0].get<float>() });
            float last = 0;
            for (auto& track : tracks)
                for (int channel : TrackChannels(track)) {
                    auto& keys = track[ChannelKey(channel)];
                    std::stable_sort(keys.begin(), keys.end(), [](const json& a, const json& b) { return a[0].get<float>() < b[0].get<float>(); });
                    for (int k = (int)keys.size() - 1; k > 0; --k)
                        if (std::abs(keys[k][0].get<float>() - keys[k - 1][0].get<float>()) < 0.5f / fps) keys.erase(keys.begin() + k - 1);
                    if (!keys.empty()) last = std::max(last, keys.back()[0].get<float>());
                }
            doc.data["duration"] = std::max(1.0f / fps, last > 0 ? last : duration);
            {   // Events stay in time order; the selection and the open editor follow them
                std::vector<int> order(events.size());
                for (int i = 0; i < (int)order.size(); ++i) order[i] = i;
                std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return events[a].value("time", 0.0f) < events[b].value("time", 0.0f); });
                json sorted = json::array();
                std::map<int, int> moved;
                for (int i = 0; i < (int)order.size(); ++i) { sorted.push_back(events[order[i]]); moved[order[i]] = i; }
                events = std::move(sorted);
                std::set<int> selected;
                for (int i : s.selectedEvents) if (moved.count(i)) selected.insert(moved[i]);
                s.selectedEvents = std::move(selected);
                if (moved.count(s.editEvent)) s.editEvent = moved[s.editEvent];
            }
            s.selection.clear();
            for (const Kept& k : kept) {
                auto& keys = tracks[k.track][ChannelKey(k.channel)];
                for (int i = 0; i < (int)keys.size(); ++i)
                    if (std::abs(keys[i][0].get<float>() - k.time) < 1e-5f) { s.selection.insert({ k.track, k.channel, i }); break; }
            }
            doc.Commit();
            m_AnimationRecordTime = -1;
        }
        if (previewing && m_AnimationPreviewJson != doc.saved && !doc.dirty) m_AnimationPreviewJson = doc.saved;
        if (previewing && doc.dirty) m_AnimationPreviewJson = doc.data.dump();
    }
    ImGui::End();
    if (!open) {
        doc.Commit();
        stopClipPreview();
        m_ShowAnimationEditor = false;
    }
}

} // namespace Stardust
