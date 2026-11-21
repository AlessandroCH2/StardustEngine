// Project Settings pages: Audio Mixer (Settings/AudioMixer.json) and Input Actions (Settings/InputActions.json)
#include "SettingsLayout.h"
#include "InspectorWidgets.h"
#include "Audio/AudioEngine.h"
#include "Project/Project.h"
#include "Core/FileSystem.h"
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <format>
#include <algorithm>

namespace Stardust {

void DrawAudioMixerSettings(float ui) {
    static std::string loadedFor;
    const std::string project = Project::Dir().string();
    if (loadedFor != project) { loadedFor = project; AudioEngine::LoadMixer(); }
    auto& groups = AudioEngine::MixerGroups();
    auto& snapshots = AudioEngine::MixerSnapshots();
    SettingsUI::Note("Audio Sources play into a group (Output in the Audio Source). Each group has a volume and effects and feeds its parent; Master feeds the speakers. Scripts change values with AudioMixer.SetFloat(group, parameter, value) and blend to a snapshot with AudioMixer.TransitionToSnapshot.");
    bool changed = false, save = false;
    static std::string newGroup;
    Insp::LabelCell("New Group");
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 70 * ui));
    ImGui::InputTextWithHint("##newGroup", "Music, SFX, Voice...", &newGroup);
    ImGui::SameLine(0, 4 * ui);
    if (ImGui::Button("Add##group", { 66 * ui, 0 }) && !newGroup.empty() && std::none_of(groups.begin(), groups.end(), [&](auto& g) { return g.name == newGroup; })) {
        AudioMixerGroup g; g.name = newGroup; g.parent = "Master"; groups.push_back(g); newGroup.clear(); changed = save = true;
    }
    SettingsUI::Section("Groups");
    int remove = -1;
    for (size_t i = 0; i < groups.size(); ++i) {
        auto& g = groups[i];
        ImGui::PushID((int)i);
        const bool master = g.name == "Master";
        if (ImGui::TreeNodeEx(g.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | (master ? ImGuiTreeNodeFlags_DefaultOpen : 0), "%s%s", g.name.c_str(), g.mute ? "  (muted)" : "")) {
            if (!master) {
                Insp::LabelCell("Parent");
                if (ImGui::BeginCombo("##parent", g.parent.empty() ? "Master" : g.parent.c_str())) {
                    for (auto& other : groups) if (&other != &g && ImGui::Selectable(other.name.c_str(), other.name == g.parent)) { g.parent = other.name; changed = save = true; }
                    ImGui::EndCombo();
                }
            }
            if (Insp::SliderFloat("Volume (dB)", &g.volume, -80.0f, 20.0f, "%.1f dB")) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (Insp::Checkbox("Mute", &g.mute)) changed = save = true;
            if (!master && Insp::Checkbox("Solo", &g.solo)) changed = save = true;
            if (Insp::SliderFloat("Pitch", &g.pitch, 0.1f, 3.0f, "%.2f")) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (Insp::DragFloat("Low-pass (Hz, 0 = off)", &g.lowpass, 10.0f, 0.0f, 22000.0f, "%.0f")) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (Insp::DragFloat("High-pass (Hz, 0 = off)", &g.highpass, 5.0f, 0.0f, 10000.0f, "%.0f")) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (Insp::SliderFloat("Reverb", &g.reverb, 0.0f, 1.0f)) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (g.reverb > 0 && Insp::SliderFloat("Room Size", &g.reverbRoom, 0.0f, 1.0f)) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (Insp::SliderFloat("Echo", &g.echo, 0.0f, 1.0f)) changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            if (g.echo > 0) {
                if (Insp::SliderFloat("Echo Delay (s)", &g.echoDelay, 0.01f, 2.0f)) changed = true;
                if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
                if (Insp::SliderFloat("Echo Feedback", &g.echoFeedback, 0.0f, 0.95f)) changed = true;
                if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
            }
            if (!master && ImGui::Button("Remove Group")) remove = (int)i;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        const std::string name = groups[remove].name;
        for (auto& g : groups) if (g.parent == name) g.parent = "Master";
        groups.erase(groups.begin() + remove); save = true;
        ImGui::TextDisabled("Removed groups stop being used after the next play.");
    }

    SettingsUI::Section("Snapshots");
    static std::string newSnapshot;
    Insp::LabelCell("New Snapshot");
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 150 * ui));
    ImGui::InputTextWithHint("##newSnapshot", "Underwater, Paused...", &newSnapshot);
    ImGui::SameLine(0, 4 * ui);
    if (ImGui::Button("Save Current", { 146 * ui, 0 }) && !newSnapshot.empty()) {
        auto it = std::find_if(snapshots.begin(), snapshots.end(), [&](auto& s) { return s.name == newSnapshot; });
        if (it == snapshots.end()) snapshots.push_back({ newSnapshot, groups }); else it->groups = groups;
        newSnapshot.clear(); save = true;
    }
    int removeSnapshot = -1;
    for (size_t i = 0; i < snapshots.size(); ++i) {
        ImGui::PushID(1000 + (int)i);
        ImGui::TextUnformatted(snapshots[i].name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200 * ui);
        if (ImGui::SmallButton("Apply")) AudioEngine::TransitionToSnapshot(snapshots[i].name, 0.0f);
        ImGui::SameLine(); if (ImGui::SmallButton("Overwrite")) { snapshots[i].groups = groups; save = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("Delete")) removeSnapshot = (int)i;
        ImGui::PopID();
    }
    if (removeSnapshot >= 0) { snapshots.erase(snapshots.begin() + removeSnapshot); save = true; }
    if (changed) AudioEngine::ApplyMixer();
    if (save) AudioEngine::SaveMixer();
}

// ------------------------------------------------------------------------------------------------ Input Actions

namespace {
const char* kCommonPaths[] = {
    "<Keyboard>/space", "<Keyboard>/enter", "<Keyboard>/escape", "<Keyboard>/tab", "<Keyboard>/leftShift", "<Keyboard>/leftCtrl", "<Keyboard>/e", "<Keyboard>/f", "<Keyboard>/r",
    "<Mouse>/leftButton", "<Mouse>/rightButton", "<Mouse>/middleButton", "<Mouse>/delta", "<Mouse>/position", "<Mouse>/scroll",
    "<Gamepad>/buttonSouth", "<Gamepad>/buttonEast", "<Gamepad>/buttonWest", "<Gamepad>/buttonNorth", "<Gamepad>/leftShoulder", "<Gamepad>/rightShoulder",
    "<Gamepad>/leftTrigger", "<Gamepad>/rightTrigger", "<Gamepad>/leftStick", "<Gamepad>/rightStick", "<Gamepad>/start", "<Gamepad>/select",
    "<Gamepad>/dpad", "<Gamepad>/dpad/up", "<Gamepad>/dpad/down", "<Gamepad>/dpad/left", "<Gamepad>/dpad/right", "<Gamepad>/leftStickPress", "<Gamepad>/rightStickPress",
};

// Path of the key pressed this frame in the editor ("" = none)
std::string PressedKeyPath() {
    struct K { ImGuiKey key; const char* name; };
    static const K keys[] = {
        { ImGuiKey_Space, "space" }, { ImGuiKey_Enter, "enter" }, { ImGuiKey_Escape, "escape" }, { ImGuiKey_Tab, "tab" }, { ImGuiKey_Backspace, "backspace" },
        { ImGuiKey_LeftShift, "leftShift" }, { ImGuiKey_RightShift, "rightShift" }, { ImGuiKey_LeftCtrl, "leftCtrl" }, { ImGuiKey_RightCtrl, "rightCtrl" },
        { ImGuiKey_LeftAlt, "leftAlt" }, { ImGuiKey_RightAlt, "rightAlt" }, { ImGuiKey_UpArrow, "upArrow" }, { ImGuiKey_DownArrow, "downArrow" },
        { ImGuiKey_LeftArrow, "leftArrow" }, { ImGuiKey_RightArrow, "rightArrow" }, { ImGuiKey_Insert, "insert" }, { ImGuiKey_Delete, "delete" },
        { ImGuiKey_Minus, "minus" }, { ImGuiKey_Period, "period" }, { ImGuiKey_Comma, "comma" }, { ImGuiKey_Slash, "slash" }, { ImGuiKey_Apostrophe, "quote" },
    };
    for (auto& k : keys) if (ImGui::IsKeyPressed(k.key, false)) return std::string("<Keyboard>/") + k.name;
    for (int i = 0; i < 26; ++i) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + i), false)) return std::string("<Keyboard>/") + (char)('a' + i);
    for (int i = 0; i < 10; ++i) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + i), false)) return std::string("<Keyboard>/") + (char)('0' + i);
    for (int i = 0; i < 12; ++i) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_F1 + i), false)) return std::format("<Keyboard>/f{}", i + 1);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) return "<Mouse>/rightButton";
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) return "<Mouse>/middleButton";
    struct G { ImGuiKey key; const char* name; };
    static const G pads[] = {
        { ImGuiKey_GamepadFaceDown, "buttonSouth" }, { ImGuiKey_GamepadFaceRight, "buttonEast" }, { ImGuiKey_GamepadFaceLeft, "buttonWest" }, { ImGuiKey_GamepadFaceUp, "buttonNorth" },
        { ImGuiKey_GamepadL1, "leftShoulder" }, { ImGuiKey_GamepadR1, "rightShoulder" }, { ImGuiKey_GamepadL2, "leftTrigger" }, { ImGuiKey_GamepadR2, "rightTrigger" },
        { ImGuiKey_GamepadStart, "start" }, { ImGuiKey_GamepadBack, "select" }, { ImGuiKey_GamepadDpadUp, "dpad/up" }, { ImGuiKey_GamepadDpadDown, "dpad/down" },
        { ImGuiKey_GamepadDpadLeft, "dpad/left" }, { ImGuiKey_GamepadDpadRight, "dpad/right" },
    };
    for (auto& p : pads) if (ImGui::IsKeyPressed(p.key, false)) return std::string("<Gamepad>/") + p.name;
    return {};
}

// Text field + menu of common paths + Listen (press a key)
bool PathField(const char* label, nlohmann::json& binding, const char* key, std::string& listening, const std::string& id) {
    std::string path = binding.value(key, "");
    bool changed = false;
    const float ui = SettingsUI::Scale();
    Insp::LabelCell(label);
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 120 * ui));
    if (listening == id) {
        ImGui::TextColored(ImVec4(1, .8f, .3f, 1), "Press a key or button...");
        std::string pressed = PressedKeyPath();
        if (!pressed.empty()) { if (pressed != "<Keyboard>/escape") { binding[key] = pressed; changed = true; } listening.clear(); }
    } else if (ImGui::InputText("##path", &path)) { binding[key] = path; changed = true; }
    ImGui::SameLine(0, 4 * ui);
    if (ImGui::Button("...", { 26 * ui, 0 })) ImGui::OpenPopup("paths");
    if (ImGui::BeginPopup("paths")) {
        for (const char* p : kCommonPaths) if (ImGui::Selectable(p)) { binding[key] = p; changed = true; }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0, 4 * ui);
    if (ImGui::Button(listening == id ? "Cancel" : "Listen", { 80 * ui, 0 })) listening = listening == id ? std::string() : id;
    return changed;
}

nlohmann::json DefaultActions() {
    using json = nlohmann::json;
    return { { "maps", json::array({ {
        { "name", "Player" },
        { "actions", json::array({
            { { "name", "Move" }, { "type", "Value" }, { "valueType", "Vector2" }, { "bindings", json::array({
                { { "composite", "2DVector" }, { "up", "<Keyboard>/w" }, { "down", "<Keyboard>/s" }, { "left", "<Keyboard>/a" }, { "right", "<Keyboard>/d" } },
                { { "composite", "2DVector" }, { "up", "<Keyboard>/upArrow" }, { "down", "<Keyboard>/downArrow" }, { "left", "<Keyboard>/leftArrow" }, { "right", "<Keyboard>/rightArrow" } },
                { { "path", "<Gamepad>/leftStick" } } }) } },
            { { "name", "Look" }, { "type", "Value" }, { "valueType", "Vector2" }, { "bindings", json::array({ { { "path", "<Mouse>/delta" } }, { { "path", "<Gamepad>/rightStick" }, { "scale", 10.0 } } }) } },
            { { "name", "Jump" }, { "type", "Button" }, { "bindings", json::array({ { { "path", "<Keyboard>/space" } }, { { "path", "<Gamepad>/buttonSouth" } } }) } },
            { { "name", "Fire" }, { "type", "Button" }, { "bindings", json::array({ { { "path", "<Mouse>/leftButton" } }, { { "path", "<Gamepad>/rightTrigger" } } }) } },
            { { "name", "Sprint" }, { "type", "Button" }, { "bindings", json::array({ { { "path", "<Keyboard>/leftShift" } }, { { "path", "<Gamepad>/leftStickPress" } } }) } },
        }) } } }) } };
}
}

void DrawInputActionsSettings(float ui) {
    using json = nlohmann::json;
    const auto file = Project::AssetsDir() / "Settings/InputActions.json";
    static std::string loadedFor; static json data; static std::string listening;
    if (loadedFor != file.string()) {
        loadedFor = file.string();
        data = FileSystem::Exists(file) ? json::parse(FileSystem::ReadText(file), nullptr, false) : json();
        if (!data.is_object() || !data.contains("maps")) data = json::object({ { "maps", json::array() } });
    }
    SettingsUI::Note("Actions grouped in maps, each bound to keys, mouse, gamepad or composites (WASD as a 2D vector). Scripts read them with InputSystem.actions[\"Player/Move\"].ReadValue<Vector2>() or listen to performed / canceled; players can rebind them at run time.");
    bool save = false;
    if (data["maps"].empty()) {
        ImGui::Dummy({ 0, 4 * ui });
        if (ImGui::Button("Create Default Actions (Move, Look, Jump, Fire, Sprint)", { 0, 28 * ui })) { data = DefaultActions(); save = true; }
    }
    static std::string newMap;
    Insp::LabelCell("New Action Map");
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 70 * ui));
    ImGui::InputTextWithHint("##newMap", "Player, UI, Vehicle...", &newMap);
    ImGui::SameLine(0, 4 * ui);
    if (ImGui::Button("Add##map", { 66 * ui, 0 }) && !newMap.empty()) { data["maps"].push_back({ { "name", newMap }, { "actions", json::array() } }); newMap.clear(); save = true; }

    int removeMap = -1;
    for (size_t m = 0; m < data["maps"].size(); ++m) {
        auto& map = data["maps"][m];
        ImGui::PushID((int)m);
        SettingsUI::Section(map.value("name", "Map").c_str());
        std::string name = map.value("name", "");
        if (Insp::InputText("Map Name", &name)) { map["name"] = name; save = true; }
        static std::string newAction;
        Insp::LabelCell("New Action");
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 70 * ui));
        ImGui::InputTextWithHint("##newAction", "Name", &newAction);
        ImGui::SameLine(0, 4 * ui);
        if (ImGui::Button("Add##action", { 66 * ui, 0 }) && !newAction.empty()) { map["actions"].push_back({ { "name", newAction }, { "type", "Button" }, { "bindings", json::array() } }); newAction.clear(); save = true; }
        int removeAction = -1;
        for (size_t a = 0; a < map["actions"].size(); ++a) {
            auto& action = map["actions"][a];
            ImGui::PushID((int)a);
            if (ImGui::TreeNodeEx("action", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "%s", action.value("name", "Action").c_str())) {
                std::string actionName = action.value("name", "");
                if (Insp::InputText("Name", &actionName)) { action["name"] = actionName; save = true; }
                static const char* types[] = { "Button", "Value", "PassThrough" };
                int type = 0; for (int i = 0; i < 3; ++i) if (action.value("type", "Button") == types[i]) type = i;
                if (Insp::Combo("Action Type", &type, "Button\0Value\0Pass Through\0")) { action["type"] = types[type]; save = true; }
                if (type != 0) {
                    static const char* values[] = { "Axis", "Vector2", "Button" };
                    int value = 0; for (int i = 0; i < 3; ++i) if (action.value("valueType", "Axis") == values[i]) value = i;
                    if (Insp::Combo("Value Type", &value, "Axis (float)\0Vector2\0Button\0")) { action["valueType"] = values[value]; save = true; }
                }
                static const char* interactions[] = { "", "Hold", "Tap", "MultiTap" };
                int interaction = 0; for (int i = 0; i < 4; ++i) if (action.value("interaction", "") == interactions[i]) interaction = i;
                if (Insp::Combo("Interaction", &interaction, "Press (default)\0Hold\0Tap\0Multi Tap\0")) { action["interaction"] = interactions[interaction]; save = true; }
                if (interaction == 1) { float t = action.value("holdTime", 0.4f); if (Insp::DragFloat("Hold Time (s)", &t, 0.01f, 0.05f, 10.0f)) { action["holdTime"] = t; save = true; } }
                if (interaction == 2) { float t = action.value("tapTime", 0.2f); if (Insp::DragFloat("Max Tap Time (s)", &t, 0.01f, 0.05f, 2.0f)) { action["tapTime"] = t; save = true; } }
                if (interaction == 3) { int n = action.value("tapCount", 2); if (Insp::SliderInt("Tap Count", &n, 2, 5)) { action["tapCount"] = n; save = true; } }
                ImGui::TextDisabled("Bindings");
                int removeBinding = -1;
                auto& bindings = action["bindings"];
                if (!bindings.is_array()) bindings = json::array();
                for (size_t b = 0; b < bindings.size(); ++b) {
                    auto& binding = bindings[b];
                    ImGui::PushID((int)b);
                    const std::string id = std::format("{}/{}/{}", m, a, b);
                    const std::string composite = binding.value("composite", "");
                    if (composite == "2DVector") {
                        ImGui::TextDisabled("2D Vector");
                        for (const char* part : { "up", "down", "left", "right" }) save |= PathField(part, binding, part, listening, id + part);
                    } else if (composite == "1DAxis") {
                        ImGui::TextDisabled("1D Axis");
                        for (const char* part : { "negative", "positive" }) save |= PathField(part, binding, part, listening, id + part);
                    } else if (composite == "ButtonWithModifier") {
                        ImGui::TextDisabled("Button with modifier");
                        for (const char* part : { "modifier", "button" }) save |= PathField(part, binding, part, listening, id + part);
                    } else save |= PathField("Path", binding, "path", listening, id);
                    float scale = binding.value("scale", 1.0f);
                    bool invert = binding.value("invert", false);
                    ImGui::SetNextItemWidth(120 * ui);
                    if (ImGui::DragFloat("Scale", &scale, 0.01f, -100, 100)) { binding["scale"] = scale; save = true; }
                    ImGui::SameLine(); if (ImGui::Checkbox("Invert", &invert)) { binding["invert"] = invert; save = true; }
                    ImGui::SameLine(); if (ImGui::SmallButton("Remove")) removeBinding = (int)b;
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (removeBinding >= 0) { bindings.erase(removeBinding); save = true; }
                if (ImGui::SmallButton("+ Binding")) { bindings.push_back({ { "path", "" } }); save = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("+ 2D Vector (WASD)")) { bindings.push_back({ { "composite", "2DVector" }, { "up", "<Keyboard>/w" }, { "down", "<Keyboard>/s" }, { "left", "<Keyboard>/a" }, { "right", "<Keyboard>/d" } }); save = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("+ 1D Axis")) { bindings.push_back({ { "composite", "1DAxis" }, { "negative", "" }, { "positive", "" } }); save = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("+ With Modifier")) { bindings.push_back({ { "composite", "ButtonWithModifier" }, { "modifier", "<Keyboard>/leftCtrl" }, { "button", "" } }); save = true; }
                if (ImGui::Button("Remove Action")) removeAction = (int)a;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (removeAction >= 0) { map["actions"].erase(removeAction); save = true; }
        if (ImGui::SmallButton("Remove Map")) removeMap = (int)m;
        ImGui::PopID();
    }
    if (removeMap >= 0) { data["maps"].erase(removeMap); save = true; }
    if (save) FileSystem::WriteText(file, data.dump(2));
}

} // namespace Stardust
