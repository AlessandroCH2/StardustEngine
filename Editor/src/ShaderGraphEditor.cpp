// Shader Graph window: a node canvas (pan with the middle mouse button or Alt+drag, zoom with the wheel), the
// Blackboard with the graph settings and material properties, and Save (Ctrl+S), which compiles the graph.
#include "ShaderGraphEditor.h"
#include "ShaderGraph.h"
#include "ShaderAssets.h"
#include "InspectorWidgets.h"
#include "EditorIcons.h"
#include "AssetUI.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

namespace Stardust::ShaderGraphEditor {

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Stardust::ShaderGraph;

namespace {

struct State {
    bool open = false, focus = false;
    std::string asset;            // asset-relative path
    json graph;
    std::string committed;        // last state put in the undo history
    std::vector<std::string> undo, redo;
    bool touched = false, dirty = false;
    float zoom = 1.0f;
    ImVec2 pan{ 40, 40 };
    int selected = 0;
    // Link being dragged
    bool linking = false; int linkNode = 0; std::string linkPin; bool linkFromOutput = true;
    // Create-node popup
    bool requestCreate = false; ImVec2 createAt; std::string createSearch;
    int pendingNode = 0; std::string pendingPin; bool pendingFromOutput = true;
    bool showCode = false;
    bool confirmClose = false;
};
State s;

struct PinRef { int node; std::string pin; bool output; ImVec2 pos; int dims; };
struct NodeLayout { ImVec2 min, max; std::vector<PinRef> pins; };

ImU32 CategoryColor(const std::string& c) {
    if (c == "Output") return IM_COL32(60, 120, 70, 255);
    if (c == "Input") return IM_COL32(150, 85, 50, 255);
    if (c == "Texture") return IM_COL32(150, 60, 90, 255);
    if (c == "Math") return IM_COL32(55, 85, 140, 255);
    if (c == "Vector") return IM_COL32(75, 95, 150, 255);
    if (c == "Channel") return IM_COL32(110, 80, 140, 255);
    if (c == "UV") return IM_COL32(50, 120, 125, 255);
    if (c == "Procedural") return IM_COL32(120, 110, 50, 255);
    return IM_COL32(90, 90, 100, 255);
}
ImU32 DimsColor(int dims) {
    switch (dims) { case 1: return IM_COL32(132, 228, 231, 255); case 2: return IM_COL32(154, 239, 146, 255); case 3: return IM_COL32(246, 255, 154, 255); default: return IM_COL32(251, 203, 244, 255); }
}

json* NodeById(int id) { for (auto& n : s.graph["nodes"]) if (n.value("id", 0) == id) return &n; return nullptr; }
int NextId() { int id = 0; for (auto& n : s.graph["nodes"]) id = std::max(id, n.value("id", 0)); return id + 1; }
void Touch() { s.touched = true; }

void RemoveLinksTo(int node, const std::string& pin) {
    auto& links = s.graph["links"];
    for (size_t i = links.size(); i-- > 0;) if (links[i]["to"][0] == node && links[i]["to"][1] == pin) links.erase(i);
}
const json* LinkTo(int node, const std::string& pin) {
    for (auto& l : s.graph["links"]) if (l["to"][0] == node && l["to"][1] == pin) return &l;
    return nullptr;
}
void Connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
    if (fromNode == toNode) return;
    RemoveLinksTo(toNode, toPin);
    s.graph["links"].push_back({ { "from", { fromNode, fromPin } }, { "to", { toNode, toPin } } });
    Touch();
}
void DeleteNode(int id) {
    auto* n = NodeById(id);
    if (!n || n->value("type", std::string()) == "Output") return;
    auto& nodes = s.graph["nodes"];
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].value("id", 0) == id) { nodes.erase(i); break; }
    auto& links = s.graph["links"];
    for (size_t i = links.size(); i-- > 0;) if (links[i]["to"][0] == id || links[i]["from"][0] == id) links.erase(i);
    if (s.selected == id) s.selected = 0;
    Touch();
}
int AddNode(const std::string& type, ImVec2 pos, const std::string& property = {}) {
    json n = { { "id", NextId() }, { "type", type }, { "pos", { pos.x, pos.y } } };
    if (!property.empty()) n["property"] = property;
    if (const NodeDef* def = Find(type); def && def->valueDims) n["value"] = def->colorValue ? json::array({ 1, 1, 1, 1 }) : json::array({ 0, 0, 0, 0 });
    s.graph["nodes"].push_back(n);
    Touch();
    return n["id"].get<int>();
}

std::string PropertyType(const std::string& name) {
    for (auto& p : s.graph["properties"]) if (p.value("name", std::string()) == name) return p.value("type", std::string("float"));
    return {};
}

bool Save() {
    if (s.asset.empty()) return false;
    const fs::path path = AssetManager::ToAbsolute(s.asset);
    if (!FileSystem::WriteText(path, s.graph.dump(2))) { SD_ERROR("Cannot save {}", s.asset); return false; }
    s.dirty = false;
    ShaderAssets::CompileAsync(path);
    return true;
}

void Normalize(json& g) {
    if (!g.is_object()) g = ShaderAssets::GraphTemplate(true);
    if (!g.contains("settings") || !g["settings"].is_object()) g["settings"] = json::object();
    if (!g.contains("properties") || !g["properties"].is_array()) g["properties"] = json::array();
    if (!g.contains("nodes") || !g["nodes"].is_array()) g["nodes"] = json::array();
    if (!g.contains("links") || !g["links"].is_array()) g["links"] = json::array();
    bool hasOutput = false;
    for (auto& n : g["nodes"]) if (n.value("type", std::string()) == "Output") hasOutput = true;
    if (!hasOutput) {
        int id = 1; for (auto& n : g["nodes"]) id = std::max(id, n.value("id", 0) + 1);
        g["nodes"].push_back({ { "id", id }, { "type", "Output" }, { "pos", { 500, 80 } } });
    }
}

std::string SanitizeName(std::string name) {
    std::string out;
    for (char c : name) if (std::isalnum((unsigned char)c) || c == '_') out += c;
    if (out.empty() || std::isdigit((unsigned char)out[0])) out = "_" + out;
    return out;
}

// ------------------------------------------------------------------------------------------------ blackboard

void DrawBlackboard() {
    const float ui = ImGui::GetFontSize() / 14.0f;
    json& settings = s.graph["settings"];
    ImGui::TextDisabled("GRAPH SETTINGS");
    int type = settings.value("type", std::string("Lit")) == "Unlit" ? 1 : 0;
    if (Insp::Combo("Material", &type, "Lit\0Unlit\0")) { settings["type"] = type ? "Unlit" : "Lit"; Touch(); }
    int surface = settings.value("surface", std::string("Opaque")) == "Transparent" ? 1 : 0;
    if (Insp::Combo("Surface", &surface, "Opaque\0Transparent\0")) { settings["surface"] = surface ? "Transparent" : "Opaque"; Touch(); }
    if (surface) {
        const std::string b = settings.value("blend", std::string("Alpha"));
        int blend = b == "Additive" ? 1 : b == "Premultiplied" ? 2 : 0;
        if (Insp::Combo("Blend", &blend, "Alpha\0Additive\0Premultiplied\0")) { settings["blend"] = blend == 1 ? "Additive" : blend == 2 ? "Premultiplied" : "Alpha"; Touch(); }
    }
    bool twoSided = settings.value("twoSided", false), clip = settings.value("alphaClip", false);
    if (Insp::Checkbox("Two Sided", &twoSided)) { settings["twoSided"] = twoSided; Touch(); }
    if (Insp::Checkbox("Alpha Clipping", &clip)) { settings["alphaClip"] = clip; Touch(); }

    ImGui::Dummy({ 0, 8 * ui });
    ImGui::TextDisabled("PROPERTIES");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 18 * ui);
    if (ImGui::SmallButton("+")) ImGui::OpenPopup("AddProperty");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a material property");
    if (ImGui::BeginPopup("AddProperty")) {
        const char* types[] = { "float", "range", "int", "bool", "color", "vector4", "texture", "normal map" };
        const char* labels[] = { "Float", "Range", "Integer", "Boolean", "Color", "Vector 4", "Texture 2D", "Normal Map" };
        for (int i = 0; i < 8; ++i) if (ImGui::MenuItem(labels[i])) {
            std::string base = std::string("_") + labels[i]; base.erase(std::remove(base.begin(), base.end(), ' '), base.end());
            std::string name = base; int n = 1;
            auto exists = [&](const std::string& candidate) { for (auto& p : s.graph["properties"]) if (p.value("name", std::string()) == candidate) return true; return false; };
            while (exists(name)) name = base + std::to_string(n++);
            json p = { { "name", name }, { "label", labels[i] } };
            const std::string t = types[i];
            if (t == "range") { p["type"] = "float"; p["min"] = 0; p["max"] = 1; p["default"] = 0.5f; }
            else if (t == "normal map") { p["type"] = "texture"; p["fallback"] = "normal"; p["default"] = ""; }
            else if (t == "texture") { p["type"] = "texture"; p["default"] = ""; }
            else if (t == "color") { p["type"] = "color"; p["default"] = { 1, 1, 1, 1 }; }
            else if (t == "vector4") { p["type"] = "vector4"; p["default"] = { 0, 0, 0, 0 }; }
            else if (t == "bool") { p["type"] = "bool"; p["default"] = false; }
            else if (t == "int") { p["type"] = "int"; p["default"] = 0; }
            else { p["type"] = "float"; p["default"] = 0; }
            s.graph["properties"].push_back(p);
            Touch();
        }
        ImGui::EndPopup();
    }
    int remove = -1;
    auto& properties = s.graph["properties"];
    for (int i = 0; i < (int)properties.size(); ++i) {
        auto& p = properties[i];
        ImGui::PushID(i);
        const std::string type = p.value("type", std::string("float"));
        const std::string name = p.value("name", std::string());
        const bool open = ImGui::TreeNodeEx("##property", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "%s   %s", p.value("label", name).c_str(), type.c_str());
        // Drag a property onto the canvas to use it
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("SG_PROPERTY", name.c_str(), name.size() + 1);
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }
        if (open) {
            std::string label = p.value("label", name);
            if (Insp::InputText("Label", &label)) { p["label"] = label; Touch(); }
            std::string renamed = name;
            if (Insp::InputText("Reference", &renamed, ImGuiInputTextFlags_EnterReturnsTrue)) {
                renamed = SanitizeName(renamed);
                bool unique = true; for (auto& o : properties) if (&o != &p && o.value("name", std::string()) == renamed) unique = false;
                if (unique && renamed != name) {
                    for (auto& n : s.graph["nodes"]) if (n.value("property", std::string()) == name) n["property"] = renamed;
                    p["name"] = renamed; Touch();
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Name used by materials and scripts (SetFloat, SetColor...). Enter to apply.");
            json& def = p["default"];
            if (type == "float") {
                float v = def.is_number() ? def.get<float>() : 0;
                bool range = p.contains("min") && p.contains("max");
                if (range ? Insp::SliderFloat("Default", &v, p.value("min", 0.0f), p.value("max", 1.0f)) : Insp::DragFloat("Default", &v, 0.01f)) { def = v; Touch(); }
                if (Insp::Checkbox("Range", &range)) { if (range) { p["min"] = 0; p["max"] = 1; } else { p.erase("min"); p.erase("max"); } Touch(); }
                if (range) { float mm[2] = { p.value("min", 0.0f), p.value("max", 1.0f) }; if (Insp::DragFloat2("Min / Max", mm, 0.01f)) { p["min"] = mm[0]; p["max"] = mm[1]; Touch(); } }
            } else if (type == "int") { int v = def.is_number() ? def.get<int>() : 0; if (Insp::DragInt("Default", &v)) { def = v; Touch(); } }
            else if (type == "bool") { bool v = def.is_boolean() && def.get<bool>(); if (Insp::Checkbox("Default", &v)) { def = v; Touch(); } }
            else if (type == "color" || type == "vector4") {
                float v[4] = { 0, 0, 0, 1 };
                if (def.is_array()) for (int k = 0; k < 4 && (size_t)k < def.size(); ++k) v[k] = def[k].get<float>();
                bool changed = type == "color" ? (Insp::LabelCell("Default"), EditorIcons::ColorField("##default", v, 4)) : Insp::DragFloat4("Default", v, 0.01f);
                if (changed) { def = { v[0], v[1], v[2], v[3] }; Touch(); }
            } else if (type == "texture") {
                std::string path = def.is_string() ? def.get<std::string>() : std::string();
                if (AssetUI::AssetField("Default", path, { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".rendertexture" })) { def = path; Touch(); }
                bool normal = p.value("fallback", std::string("white")) == "normal";
                if (Insp::Checkbox("Normal Map", &normal)) { p["fallback"] = normal ? "normal" : "white"; Touch(); }
            }
            if (ImGui::Button("Remove Property")) remove = i;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        const std::string name = properties[remove].value("name", std::string());
        properties.erase(remove);
        for (auto& n : s.graph["nodes"]) if (n.value("property", std::string()) == name) n["property"] = "";
        Touch();
    }
    if (properties.empty()) ImGui::TextDisabled("Add properties with +, then drag them onto the graph.");
    ImGui::Dummy({ 0, 8 * ui });
    ImGui::TextWrapped("Right-click the graph to add nodes. Drag from a pin to connect; drag a connected input to move its link. Delete removes the selected node.");
}

// ------------------------------------------------------------------------------------------------ canvas

void DrawCreatePopup(ImVec2 canvasOrigin) {
    if (s.requestCreate) { ImGui::OpenPopup("CreateNode"); s.requestCreate = false; s.createSearch.clear(); }
    ImGui::SetNextWindowSizeConstraints({ 240, 0 }, { 300, 420 });
    if (!ImGui::BeginPopup("CreateNode")) { return; }
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool enter = ImGui::InputTextWithHint("##search", "Search nodes", &s.createSearch, ImGuiInputTextFlags_EnterReturnsTrue);
    std::string needle = s.createSearch; std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    std::string chosen, category;
    ImGui::BeginChild("##nodes", { 0, 320 });
    for (const auto& def : Library()) {
        if (def.type == "Output") continue;
        std::string title = def.title; std::transform(title.begin(), title.end(), title.begin(), ::tolower);
        std::string cat = def.category; std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
        if (!needle.empty() && title.find(needle) == std::string::npos && cat.find(needle) == std::string::npos) continue;
        if (def.category != category) { category = def.category; ImGui::SeparatorText(category.c_str()); }
        if (ImGui::Selectable(def.title.c_str()) || (enter && chosen.empty())) chosen = def.type;
    }
    ImGui::EndChild();
    if (!chosen.empty()) {
        const ImVec2 graphPos((s.createAt.x - canvasOrigin.x) / s.zoom - s.pan.x, (s.createAt.y - canvasOrigin.y) / s.zoom - s.pan.y);
        int id = AddNode(chosen, graphPos);
        s.selected = id;
        // Opened by dropping a link on empty space: connect it to the first input (or output) of the new node
        if (s.pendingNode) {
            const NodeDef* def = Find(chosen);
            if (s.pendingFromOutput && !def->inputs.empty()) Connect(s.pendingNode, s.pendingPin, id, def->inputs[0].name);
            else if (!s.pendingFromOutput && !def->outputs.empty()) Connect(id, def->outputs[0].name, s.pendingNode, s.pendingPin);
            s.pendingNode = 0;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawCanvas() {
    const float ui = ImGui::GetFontSize() / 14.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 31, 35, 255));
    ImGui::BeginChild("##canvas", { 0, 0 }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    const ImVec2 origin = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    auto* d = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Zoom around the mouse, pan with the middle button (or Alt + left)
    if (canvasHovered && io.MouseWheel != 0 && !ImGui::IsAnyItemActive()) {
        const float old = s.zoom;
        s.zoom = std::clamp(s.zoom * (io.MouseWheel > 0 ? 1.1f : 1 / 1.1f), 0.35f, 1.8f);
        const ImVec2 m = io.MousePos;
        s.pan.x += (m.x - origin.x) / s.zoom - (m.x - origin.x) / old;
        s.pan.y += (m.y - origin.y) / s.zoom - (m.y - origin.y) / old;
    }
    if (canvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()))) {
        s.pan.x += io.MouseDelta.x / s.zoom; s.pan.y += io.MouseDelta.y / s.zoom;
    }
    auto toScreen = [&](float x, float y) { return ImVec2(origin.x + (x + s.pan.x) * s.zoom, origin.y + (y + s.pan.y) * s.zoom); };

    // Grid
    const float step = 24 * s.zoom;
    for (float x = std::fmod(s.pan.x * s.zoom, step); x < size.x; x += step) d->AddLine({ origin.x + x, origin.y }, { origin.x + x, origin.y + size.y }, IM_COL32(40, 42, 47, 255));
    for (float y = std::fmod(s.pan.y * s.zoom, step); y < size.y; y += step) d->AddLine({ origin.x, origin.y + y }, { origin.x + size.x, origin.y + y }, IM_COL32(40, 42, 47, 255));

    ImGui::SetWindowFontScale(s.zoom);
    const float rowH = ImGui::GetFrameHeight() + 4 * ui * s.zoom, headerH = ImGui::GetFontSize() + 10 * ui * s.zoom;

    // Dynamic sizes for pin colours: computed from the links (one pass, good enough for colouring)
    std::map<int, NodeLayout> layouts;
    for (auto& n : s.graph["nodes"]) {
        const NodeDef* def = Find(n.value("type", std::string()));
        if (!def) continue;
        const int id = n.value("id", 0);
        std::vector<const PinDef*> inputs;
        for (const auto& pin : def->inputs) if (def->type != "Output" || OutputPinVisible(s.graph, pin.name)) inputs.push_back(&pin);
        const bool wide = def->type == "Output" || !inputs.empty() || def->valueDims >= 2 || def->propertyKind;
        const float width = (wide ? 230.0f : 150.0f) * ui * s.zoom;
        int extra = (def->propertyKind ? 1 : 0) + (def->valueDims ? 1 : 0);
        const int rows = extra + (int)std::max(inputs.size(), def->outputs.size());
        const ImVec2 pos = toScreen(n["pos"][0].get<float>(), n["pos"][1].get<float>());
        NodeLayout layout;
        layout.min = pos; layout.max = { pos.x + width, pos.y + headerH + rows * rowH + 6 * ui * s.zoom };
        for (size_t i = 0; i < inputs.size(); ++i)
            layout.pins.push_back({ id, inputs[i]->name, false, { pos.x, pos.y + headerH + (extra + i + 0.5f) * rowH }, inputs[i]->dims });
        for (size_t i = 0; i < def->outputs.size(); ++i)
            layout.pins.push_back({ id, def->outputs[i].name, true, { pos.x + width, pos.y + headerH + (extra + i + 0.5f) * rowH }, OutputDims(*def, i, 1, s.graph, n) });
        layouts[id] = layout;
    }
    auto findPin = [&](int node, const std::string& pin, bool output) -> const PinRef* {
        auto it = layouts.find(node); if (it == layouts.end()) return nullptr;
        for (auto& p : it->second.pins) if (p.pin == pin && p.output == output) return &p;
        return nullptr;
    };
    auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 color, float thickness) {
        const float dx = std::max(40.0f * s.zoom, std::abs(b.x - a.x) * 0.5f);
        d->AddBezierCubic(a, { a.x + dx, a.y }, { b.x - dx, b.y }, b, color, thickness * s.zoom);
    };
    // Links
    for (auto& l : s.graph["links"]) {
        const PinRef* a = findPin(l["from"][0].get<int>(), l["from"][1].get<std::string>(), true);
        const PinRef* b = findPin(l["to"][0].get<int>(), l["to"][1].get<std::string>(), false);
        if (a && b) bezier(a->pos, b->pos, DimsColor(a->dims), 2.5f);
    }

    // Nodes
    int pressedPinNode = 0; std::string pressedPin; bool pressedOutput = false;
    int duplicateNode = 0, deleteNode = 0; // applied after the loop (the node list must not change while drawing it)
    for (auto& n : s.graph["nodes"]) {
        const NodeDef* def = Find(n.value("type", std::string()));
        if (!def) continue;
        const int id = n.value("id", 0);
        const NodeLayout& layout = layouts[id];
        ImGui::PushID(id);
        // Body: selects and drags the node; widgets drawn after it stay usable
        ImGui::SetCursorScreenPos(layout.min);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##node", { layout.max.x - layout.min.x, layout.max.y - layout.min.y });
        if (ImGui::IsItemActivated()) s.selected = id;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyAlt) {
            n["pos"][0] = n["pos"][0].get<float>() + io.MouseDelta.x / s.zoom;
            n["pos"][1] = n["pos"][1].get<float>() + io.MouseDelta.y / s.zoom;
            Touch();
        }
        if (ImGui::BeginPopupContextItem("##nodeMenu")) {
            s.selected = id;
            if (def->type != "Output" && ImGui::MenuItem("Duplicate", "Ctrl+D")) duplicateNode = id;
            if (def->type != "Output" && ImGui::MenuItem("Delete", "Del")) deleteNode = id;
            if (ImGui::MenuItem("Disconnect All")) { auto& links = s.graph["links"]; for (size_t i = links.size(); i-- > 0;) if (links[i]["to"][0] == id || links[i]["from"][0] == id) links.erase(i); Touch(); }
            ImGui::EndPopup();
        }
        const float r = 6 * ui * s.zoom;
        d->AddRectFilled({ layout.min.x + 3, layout.min.y + 4 }, { layout.max.x + 3, layout.max.y + 4 }, IM_COL32(0, 0, 0, 70), r);
        d->AddRectFilled(layout.min, layout.max, IM_COL32(48, 50, 56, 245), r);
        d->AddRectFilled(layout.min, { layout.max.x, layout.min.y + headerH }, CategoryColor(def->category), r, ImDrawFlags_RoundCornersTop);
        d->AddText({ layout.min.x + 8 * ui * s.zoom, layout.min.y + (headerH - ImGui::GetFontSize()) * 0.5f }, IM_COL32(240, 242, 246, 255), def->title.c_str());
        d->AddRect(layout.min, layout.max, s.selected == id ? IM_COL32(255, 180, 60, 255) : IM_COL32(20, 21, 24, 255), r, 0, s.selected == id ? 2.0f : 1.0f);

        float y = layout.min.y + headerH;
        const float padX = 10 * ui * s.zoom, innerW = layout.max.x - layout.min.x - padX * 2;
        // Property chooser
        if (def->propertyKind) {
            ImGui::SetCursorScreenPos({ layout.min.x + padX, y + 2 * s.zoom });
            ImGui::SetNextItemWidth(innerW);
            const std::string current = n.value("property", std::string());
            if (ImGui::BeginCombo("##property", current.empty() ? "(property)" : current.c_str())) {
                for (auto& p : s.graph["properties"]) {
                    const std::string name = p.value("name", std::string()), type = p.value("type", std::string());
                    if ((def->propertyKind == 2) != (type == "texture")) continue;
                    if (ImGui::Selectable(name.c_str(), name == current)) { n["property"] = name; Touch(); }
                }
                ImGui::EndCombo();
            }
            y += rowH;
        }
        // Constant value
        if (def->valueDims) {
            ImGui::SetCursorScreenPos({ layout.min.x + padX, y + 2 * s.zoom });
            ImGui::SetNextItemWidth(innerW - 18 * ui * s.zoom);
            float v[4] = { 0, 0, 0, 0 };
            json& stored = n["value"];
            if (stored.is_array()) for (int k = 0; k < 4 && (size_t)k < stored.size(); ++k) v[k] = stored[k].get<float>();
            bool changed = false;
            if (def->colorValue) changed = EditorIcons::ColorField("##value", v, 4);
            else if (def->valueDims == 1) changed = ImGui::DragFloat("##value", v, 0.01f);
            else if (def->valueDims == 2) changed = ImGui::DragFloat2("##value", v, 0.01f);
            else if (def->valueDims == 3) changed = ImGui::DragFloat3("##value", v, 0.01f);
            else changed = ImGui::DragFloat4("##value", v, 0.01f);
            if (changed) { stored = { v[0], v[1], v[2], v[3] }; Touch(); }
            y += rowH;
        }
        // Pins
        for (const auto& pin : layout.pins) {
            ImGui::PushID(pin.pin.c_str());
            ImGui::PushID(pin.output);
            const bool linked = pin.output ? false : LinkTo(id, pin.pin) != nullptr;
            bool hasLink = linked;
            if (pin.output) for (auto& l : s.graph["links"]) if (l["from"][0] == id && l["from"][1] == pin.pin) hasLink = true;
            const ImU32 color = DimsColor(pin.dims == 0 ? 1 : pin.dims);
            d->AddCircleFilled(pin.pos, r * 0.9f, IM_COL32(30, 31, 35, 255));
            if (hasLink) d->AddCircleFilled(pin.pos, r * 0.65f, color); else d->AddCircle(pin.pos, r * 0.65f, color, 0, 1.6f * s.zoom);
            ImGui::SetCursorScreenPos({ pin.pos.x - r * 1.4f, pin.pos.y - r * 1.4f });
            ImGui::InvisibleButton("##pin", { r * 2.8f, r * 2.8f });
            if (ImGui::IsItemActivated()) { pressedPinNode = id; pressedPin = pin.pin; pressedOutput = pin.output; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)", pin.pin.c_str(), pin.dims == 0 ? "dynamic" : pin.dims == 1 ? "float" : ("float" + std::to_string(pin.dims)).c_str());
            const ImVec2 text = ImGui::CalcTextSize(pin.pin.c_str());
            if (pin.output) d->AddText({ pin.pos.x - r * 1.6f - text.x, pin.pos.y - text.y * 0.5f }, IM_COL32(210, 213, 220, 255), pin.pin.c_str());
            else {
                d->AddText({ pin.pos.x + r * 1.6f, pin.pos.y - text.y * 0.5f }, IM_COL32(210, 213, 220, 255), pin.pin.c_str());
                // Value of an unconnected input, edited in place
                const PinDef* pd = nullptr;
                for (const auto& p : def->inputs) if (p.name == pin.pin) pd = &p;
                if (!linked && pd && pd->defaultExpr.empty()) {
                    const int dims = pd->dims == 0 ? 1 : pd->dims;
                    const float x0 = pin.pos.x + r * 1.6f + text.x + 6 * ui * s.zoom, w = layout.max.x - padX - x0;
                    if (w > 20 * s.zoom) {
                        float v[4] = { pd->value[0], pd->value[1], pd->value[2], pd->value[3] };
                        json& inputs = n["inputs"];
                        if (!inputs.is_object()) inputs = json::object();
                        if (inputs.contains(pd->name) && inputs[pd->name].is_array()) for (int k = 0; k < 4 && (size_t)k < inputs[pd->name].size(); ++k) v[k] = inputs[pd->name][k].get<float>();
                        ImGui::SetCursorScreenPos({ x0, pin.pos.y - ImGui::GetFrameHeight() * 0.5f });
                        ImGui::SetNextItemWidth(w);
                        bool changed = false;
                        if (pd->color) changed = EditorIcons::ColorField("##v", v, dims == 4 ? 4 : 3);
                        else if (dims == 1) changed = ImGui::DragFloat("##v", v, 0.01f, 0, 0, "%.2f");
                        else if (dims == 2) changed = ImGui::DragFloat2("##v", v, 0.01f, 0, 0, "%.2f");
                        else if (dims == 3) changed = ImGui::DragFloat3("##v", v, 0.01f, 0, 0, "%.2f");
                        else changed = ImGui::DragFloat4("##v", v, 0.01f, 0, 0, "%.2f");
                        if (changed) { inputs[pd->name] = { v[0], v[1], v[2], v[3] }; Touch(); }
                    }
                }
            }
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    if (deleteNode) DeleteNode(deleteNode);
    if (duplicateNode) if (json* n = NodeById(duplicateNode)) {
        json copy = *n; copy["id"] = NextId(); copy["pos"][0] = copy["pos"][0].get<float>() + 30; copy["pos"][1] = copy["pos"][1].get<float>() + 30;
        s.graph["nodes"].push_back(copy); s.selected = copy["id"]; Touch();
    }
    // Start dragging a link: from an output, or from an input (a connected input hands its link over)
    if (pressedPinNode) {
        s.linking = true;
        if (!pressedOutput) {
            if (const json* l = LinkTo(pressedPinNode, pressedPin)) {
                s.linkNode = (*l)["from"][0].get<int>(); s.linkPin = (*l)["from"][1].get<std::string>(); s.linkFromOutput = true;
                RemoveLinksTo(pressedPinNode, pressedPin); Touch();
            } else { s.linkNode = pressedPinNode; s.linkPin = pressedPin; s.linkFromOutput = false; }
        } else { s.linkNode = pressedPinNode; s.linkPin = pressedPin; s.linkFromOutput = true; }
    }
    if (s.linking) {
        const PinRef* from = findPin(s.linkNode, s.linkPin, s.linkFromOutput);
        if (from) {
            if (s.linkFromOutput) bezier(from->pos, io.MousePos, DimsColor(from->dims), 2.5f);
            else bezier(io.MousePos, from->pos, DimsColor(from->dims), 2.5f);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Released: connect to the pin under the mouse, or offer the node list on empty space
            const PinRef* target = nullptr;
            for (auto& [id, layout] : layouts)
                for (auto& p : layout.pins)
                    if (p.output != s.linkFromOutput && std::hypot(p.pos.x - io.MousePos.x, p.pos.y - io.MousePos.y) < 12 * ui * s.zoom) target = &p;
            if (target) {
                if (s.linkFromOutput) Connect(s.linkNode, s.linkPin, target->node, target->pin);
                else Connect(target->node, target->pin, s.linkNode, s.linkPin);
            } else if (canvasHovered && from && std::hypot(io.MousePos.x - from->pos.x, io.MousePos.y - from->pos.y) > 20) {
                s.requestCreate = true; s.createAt = io.MousePos;
                s.pendingNode = s.linkNode; s.pendingPin = s.linkPin; s.pendingFromOutput = s.linkFromOutput;
            }
            s.linking = false;
        }
    }

    // Background: click deselects, right-click opens the node list
    if (canvasHovered && !ImGui::IsAnyItemHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) s.selected = 0;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0) { s.requestCreate = true; s.createAt = io.MousePos; s.pendingNode = 0; }
    }
    // Properties dropped from the Blackboard
    if (ImGui::BeginDragDropTargetCustom({ origin, { origin.x + size.x, origin.y + size.y } }, ImGui::GetID("##canvasDrop"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SG_PROPERTY")) {
            const std::string name((const char*)payload->Data);
            const ImVec2 graphPos((io.MousePos.x - origin.x) / s.zoom - s.pan.x, (io.MousePos.y - origin.y) / s.zoom - s.pan.y);
            const std::string type = PropertyType(name);
            s.selected = AddNode(type == "texture" ? "Sample Texture 2D" : "Property", graphPos, name);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SetWindowFontScale(1.0f);
    DrawCreatePopup(origin);

    // Keys
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
        if (s.selected && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) DeleteNode(s.selected);
        if (s.selected && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) if (json* n = NodeById(s.selected); n && n->value("type", std::string()) != "Output") {
            json copy = *n; copy["id"] = NextId(); copy["pos"][0] = copy["pos"][0].get<float>() + 30; copy["pos"][1] = copy["pos"][1].get<float>() + 30;
            s.graph["nodes"].push_back(copy); s.selected = copy["id"]; Touch();
        }
    }
    ImGui::EndChild();
}

void CommitHistory() {
    if (!s.touched || ImGui::IsAnyItemActive() || s.linking) return;
    s.touched = false;
    std::string now = s.graph.dump();
    if (now == s.committed) return;
    s.undo.push_back(s.committed);
    if (s.undo.size() > 100) s.undo.erase(s.undo.begin());
    s.redo.clear();
    s.committed = now;
    s.dirty = true;
}
void Restore(std::vector<std::string>& from, std::vector<std::string>& to) {
    if (from.empty()) return;
    to.push_back(s.committed);
    s.committed = from.back(); from.pop_back();
    s.graph = json::parse(s.committed, nullptr, false);
    Normalize(s.graph);
    s.dirty = true; s.selected = 0;
}

} // namespace

void Open(const std::string& asset) {
    if (s.open && s.asset == asset) { s.focus = true; return; }
    if (s.open && s.dirty) Save();
    s = State();
    s.asset = asset;
    s.graph = json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(asset)), nullptr, false);
    Normalize(s.graph);
    s.committed = s.graph.dump();
    s.open = true; s.focus = true;
}

bool IsOpen() { return s.open; }

void Draw() {
    if (!s.open) return;
    const float ui = ImGui::GetFontSize() / 14.0f;
    ImGui::SetNextWindowSize({ 1100 * ui, 680 * ui }, ImGuiCond_FirstUseEver);
    if (s.focus) { ImGui::SetNextWindowFocus(); s.focus = false; }
    bool open = true;
    const std::string title = "Shader Graph - " + fs::path(s.asset).stem().string() + (s.dirty ? " *" : "") + "###ShaderGraph";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); if (!open) s.open = false; return; }
    if (!open) { if (s.dirty) s.confirmClose = true; else s.open = false; }

    // Toolbar
    if (ImGui::Button(s.dirty ? "Save *" : "Save")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Show Code")) s.showCode = !s.showCode;
    ImGui::SameLine();
    ImGui::BeginDisabled(s.undo.empty()); if (ImGui::Button("Undo")) Restore(s.undo, s.redo); ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.redo.empty()); if (ImGui::Button("Redo")) Restore(s.redo, s.undo); ImGui::EndDisabled();
    ImGui::SameLine();
    {
        const std::string error = ShaderAssets::LastError(AssetManager::ToAbsolute(s.asset));
        ImGui::AlignTextToFramePadding();
        if (ShaderAssets::IsCompiling()) ImGui::TextColored({ 1, 0.85f, 0.3f, 1 }, "Compiling...");
        else if (!error.empty()) { ImGui::TextColored({ 1, 0.45f, 0.4f, 1 }, "Error: %s", error.c_str()); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", error.c_str()); }
        else if (s.dirty) ImGui::TextDisabled("Unsaved changes (Ctrl+S saves and compiles)");
        else ImGui::TextDisabled("Compiled. Use it in a material: Custom Shader = %s", fs::path(s.asset).filename().string().c_str());
    }
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_S)) Save();
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) Restore(s.redo, s.undo); else Restore(s.undo, s.redo); }
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) Restore(s.redo, s.undo);
    }

    const float blackboard = 260 * ui;
    ImGui::BeginChild("##blackboard", { blackboard, 0 }, ImGuiChildFlags_Borders);
    DrawBlackboard();
    ImGui::EndChild();
    ImGui::SameLine(0, 4 * ui);
    if (s.showCode) {
        ImGui::BeginChild("##code", { 0, 0 }, ImGuiChildFlags_Borders);
        std::string error;
        std::string code = ShaderAssets::GraphToShader(s.graph, "Shader Graphs/" + fs::path(s.asset).stem().string(), error);
        if (!error.empty()) ImGui::TextColored({ 1, 0.45f, 0.4f, 1 }, "%s", error.c_str());
        ImGui::InputTextMultiline("##generated", &code, { -1, -1 }, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    } else DrawCanvas();
    CommitHistory();

    if (s.confirmClose) { ImGui::OpenPopup("Unsaved Shader Graph"); s.confirmClose = false; }
    if (ImGui::BeginPopupModal("Unsaved Shader Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save the changes to %s?", s.asset.c_str());
        if (ImGui::Button("Save", { 90 * ui, 0 })) { Save(); s.open = false; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Discard", { 90 * ui, 0 })) { s.open = false; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 90 * ui, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace Stardust::ShaderGraphEditor
