// Particle System inspector: modules (a foldout with an enable box each), colour gradient and size curve
// editors, burst list, and the playback controls of the preview that runs in the Scene view outside Play mode.
#include "InspectorWidgets.h"
#include "AssetUI.h"
#include "Scene/Scene.h"
#include "Scene/ParticleSystem.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <climits>
#include <format>

namespace Stardust {

namespace {
bool s_PreviewPaused = false;
float s_PreviewSpeed = 1.0f;
EntityID s_PreviewRoot = kNullEntity;

// A foldout for one module, with its enable box on the header line
bool Module(const char* name, bool* enabled, bool defaultOpen = false) {
    ImGui::PushID(name);
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID("##open");
    bool open = storage->GetBool(id, defaultOpen);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() + 2;
    ImGui::InvisibleButton("##header", { width, h });
    const bool hovered = ImGui::IsItemHovered();
    const float box = p.x + 22, cy = p.y + h * 0.5f;
    if (ImGui::IsItemClicked(0)) {
        const float x = ImGui::GetMousePos().x;
        if (enabled && x >= box - 2 && x <= box + 15) *enabled = !*enabled;
        else { open = !open; storage->SetBool(id, open); }
    }
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + width, p.y + h }, hovered ? IM_COL32(52, 55, 61, 255) : IM_COL32(40, 43, 48, 255), 3);
    const ImU32 arrow = IM_COL32(150, 150, 150, 255);
    if (open) d->AddTriangleFilled({ p.x + 6, cy - 2 }, { p.x + 14, cy - 2 }, { p.x + 10, cy + 3 }, arrow);
    else d->AddTriangleFilled({ p.x + 7, cy - 4 }, { p.x + 7, cy + 4 }, { p.x + 12, cy }, arrow);
    float textX = p.x + 22;
    if (enabled) {
        d->AddRectFilled({ box, cy - 6.5f }, { box + 13, cy + 6.5f }, *enabled ? IM_COL32(47, 128, 237, 255) : IM_COL32(28, 30, 34, 255), 3);
        d->AddRect({ box, cy - 6.5f }, { box + 13, cy + 6.5f }, IM_COL32(16, 17, 19, 255), 3);
        if (*enabled) { d->AddLine({ box + 3, cy }, { box + 5.5f, cy + 3 }, IM_COL32(255, 255, 255, 255), 1.6f); d->AddLine({ box + 5.5f, cy + 3 }, { box + 10, cy - 3.5f }, IM_COL32(255, 255, 255, 255), 1.6f); }
        textX = box + 20;
    }
    d->AddText({ textX, cy - ImGui::GetFontSize() * 0.5f }, enabled && !*enabled ? IM_COL32(140, 143, 150, 255) : IM_COL32(222, 225, 230, 255), name);
    ImGui::PopID();
    if (open) { ImGui::Indent(8); ImGui::PushID(name); }
    return open;
}
void EndModule() { ImGui::PopID(); ImGui::Unindent(8); ImGui::Spacing(); }

// Constant, or random between two values: the small button on the right switches
bool RangeField(const char* label, ParticleRange& r, float speed, float minimum = -FLT_MAX, float maximum = FLT_MAX) {
    ImGui::PushID(label);
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID id = ImGui::GetID("##random");
    bool random = storage->GetBool(id, r.min != r.max);
    Insp::LabelCell(label);
    const float width = std::max(40.0f, ImGui::CalcItemWidth() - 26);
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (random) changed = ImGui::DragFloat2("##v", &r.min, speed, minimum, maximum, "%.2f");
    else if (ImGui::DragFloat("##v", &r.min, speed, minimum, maximum, "%.2f")) { r.max = r.min; changed = true; }
    ImGui::SameLine(0, 4);
    if (ImGui::Button(random ? "R" : "C", { 22, 0 })) { random = !random; storage->SetBool(id, random); if (!random) { r.max = r.min; changed = true; } }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(random ? "Random Between Two Constants (click: Constant)" : "Constant (click: Random Between Two Constants)");
    ImGui::PopID();
    return changed;
}

void Checkerboard(ImDrawList* d, ImVec2 a, ImVec2 b) {
    d->AddRectFilled(a, b, IM_COL32(200, 200, 200, 255));
    const float cell = 5;
    for (float y = a.y; y < b.y; y += cell)
        for (float x = a.x + (int((y - a.y) / cell) % 2) * cell; x < b.x; x += cell * 2)
            d->AddRectFilled({ x, y }, { std::min(x + cell, b.x), std::min(y + cell, b.y) }, IM_COL32(150, 150, 150, 255));
}

// Gradient bar with draggable key markers; the selected key's colour and time below
void GradientEditor(std::vector<ParticleColorKey>& keys) {
    if (keys.empty()) keys.push_back({ 0, glm::vec4(1) });
    auto* storage = ImGui::GetStateStorage();
    const ImGuiID selectedId = ImGui::GetID("##selectedKey");
    int selected = std::clamp(storage->GetInt(selectedId, 0), 0, (int)keys.size() - 1);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x - 4, barH = 18, markerH = 10;
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x + 6, p.y), b(p.x + width - 6, p.y + barH);
    Checkerboard(d, a, b);
    const int steps = 48;
    for (int i = 0; i < steps; ++i) {
        const float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
        auto eval = [&](float t) {
            glm::vec4 c = keys.front().color;
            for (size_t k = 1; k < keys.size(); ++k) if (t <= keys[k].time) { float s = keys[k].time - keys[k - 1].time; c = glm::mix(keys[k - 1].color, keys[k].color, s > 0 ? (t - keys[k - 1].time) / s : 1.0f); return c; }
            return t < keys.front().time ? keys.front().color : keys.back().color;
        };
        const glm::vec4 c0 = eval(t0), c1 = eval(t1);
        d->AddRectFilledMultiColor({ a.x + (b.x - a.x) * t0, a.y }, { a.x + (b.x - a.x) * t1, b.y },
            ImGui::ColorConvertFloat4ToU32({ c0.r, c0.g, c0.b, c0.a }), ImGui::ColorConvertFloat4ToU32({ c1.r, c1.g, c1.b, c1.a }),
            ImGui::ColorConvertFloat4ToU32({ c1.r, c1.g, c1.b, c1.a }), ImGui::ColorConvertFloat4ToU32({ c0.r, c0.g, c0.b, c0.a }));
    }
    d->AddRect(a, b, IM_COL32(16, 17, 19, 255));
    // Clicking the bar adds a key there
    ImGui::InvisibleButton("##bar", { width, barH });
    if (ImGui::IsItemClicked(0) && keys.size() < 8) {
        const float t = std::clamp((ImGui::GetMousePos().x - a.x) / (b.x - a.x), 0.0f, 1.0f);
        glm::vec4 c = keys.front().color;
        for (size_t k = 1; k < keys.size(); ++k) if (t <= keys[k].time) { float s = keys[k].time - keys[k - 1].time; c = glm::mix(keys[k - 1].color, keys[k].color, s > 0 ? (t - keys[k - 1].time) / s : 1.0f); break; }
        keys.push_back({ t, c });
        std::stable_sort(keys.begin(), keys.end(), [](auto& x, auto& y) { return x.time < y.time; });
        for (int k = 0; k < (int)keys.size(); ++k) if (keys[k].time == t) selected = k;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to add a key (up to 8); drag the markers below");
    // Markers
    const float markerY = p.y + barH + 1;
    for (int k = 0; k < (int)keys.size(); ++k) {
        const float x = a.x + (b.x - a.x) * keys[k].time;
        ImGui::SetCursorScreenPos({ x - 5, markerY });
        ImGui::PushID(k);
        ImGui::InvisibleButton("##key", { 10, markerH });
        if (ImGui::IsItemActivated()) selected = k;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 1)) {
            keys[k].time = std::clamp((ImGui::GetMousePos().x - a.x) / (b.x - a.x), 0.0f, 1.0f);
            const ParticleColorKey moved = keys[k];
            std::stable_sort(keys.begin(), keys.end(), [](auto& x, auto& y) { return x.time < y.time; });
            for (int i = 0; i < (int)keys.size(); ++i) if (keys[i].time == moved.time && keys[i].color == moved.color) selected = i;
        }
        ImGui::PopID();
        const glm::vec4 c = keys[std::min(k, (int)keys.size() - 1)].color;
        d->AddTriangleFilled({ x, markerY }, { x - 5, markerY + markerH }, { x + 5, markerY + markerH }, ImGui::ColorConvertFloat4ToU32({ c.r, c.g, c.b, 1 }));
        d->AddTriangle({ x, markerY }, { x - 5, markerY + markerH }, { x + 5, markerY + markerH }, k == selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(16, 17, 19, 255), k == selected ? 2.0f : 1.0f);
    }
    ImGui::SetCursorScreenPos({ p.x, markerY + markerH + 4 });
    selected = std::clamp(selected, 0, (int)keys.size() - 1);
    storage->SetInt(selectedId, selected);
    auto& key = keys[selected];
    float time = key.time * 100;
    ImGui::SetNextItemWidth(70);
    if (ImGui::DragFloat("##time", &time, 0.5f, 0, 100, "%.0f%%")) { key.time = std::clamp(time / 100, 0.0f, 1.0f); std::stable_sort(keys.begin(), keys.end(), [](auto& x, auto& y) { return x.time < y.time; }); }
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - 30));
    EditorIcons::ColorField("##color", &keys[selected].color.x, 4);
    ImGui::SameLine(0, 4);
    ImGui::BeginDisabled(keys.size() <= 1);
    if (ImGui::Button("-", { 22, 0 })) { keys.erase(keys.begin() + selected); storage->SetInt(selectedId, 0); }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Remove the selected key");
}

// Size curve: a small plot and the keys (time %, multiplier)
void CurveEditor(std::vector<ParticleFloatKey>& keys) {
    if (keys.empty()) keys.push_back({ 0, 1 });
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x - 4, h = 54;
    auto* d = ImGui::GetWindowDrawList();
    float top = 1;
    for (auto& k : keys) top = std::max(top, k.value);
    d->AddRectFilled(p, { p.x + width, p.y + h }, IM_COL32(28, 30, 34, 255), 3);
    for (int g = 1; g < 4; ++g) d->AddLine({ p.x + width * g / 4, p.y }, { p.x + width * g / 4, p.y + h }, IM_COL32(45, 48, 54, 255));
    auto point = [&](float t, float v) { return ImVec2(p.x + 4 + (width - 8) * t, p.y + h - 4 - (h - 8) * std::clamp(v / top, 0.0f, 1.0f)); };
    std::vector<ImVec2> line;
    line.push_back(point(0, keys.front().value));
    for (auto& k : keys) line.push_back(point(k.time, k.value));
    line.push_back(point(1, keys.back().value));
    d->AddPolyline(line.data(), (int)line.size(), IM_COL32(110, 200, 120, 255), 0, 2);
    for (auto& k : keys) d->AddCircleFilled(point(k.time, k.value), 3.5f, IM_COL32(230, 232, 236, 255));
    ImGui::Dummy({ width, h });
    int remove = -1;
    for (int i = 0; i < (int)keys.size(); ++i) {
        ImGui::PushID(i);
        float time = keys[i].time * 100;
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragFloat("##t", &time, 0.5f, 0, 100, "%.0f%%")) keys[i].time = std::clamp(time / 100, 0.0f, 1.0f);
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - 30));
        ImGui::DragFloat("##v", &keys[i].value, 0.01f, 0, 100, "x %.2f");
        ImGui::SameLine(0, 4);
        ImGui::BeginDisabled(keys.size() <= 1);
        if (ImGui::Button("-", { 22, 0 })) remove = i;
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (remove >= 0) keys.erase(keys.begin() + remove);
    if (ImGui::Button("Add Key") && keys.size() < 8) keys.push_back({ 1, keys.back().value });
    std::stable_sort(keys.begin(), keys.end(), [](auto& a, auto& b) { return a.time < b.time; });
}
} // namespace

void UpdateParticlePreview(Scene& scene, EntityID selected, float dt) {
    // The selected object and its children preview their particles; the others go back to rest
    auto& reg = scene.Reg();
    if (selected != s_PreviewRoot) { s_PreviewRoot = selected; s_PreviewPaused = false; }
    reg.Each<ParticleSystemComponent>([&](EntityID e, ParticleSystemComponent& c) {
        const bool previewed = reg.Valid(selected) && (e == selected || scene.IsDescendantOf(e, selected)) && reg.IsEnabled<ParticleSystemComponent>(e) && scene.IsActiveInHierarchy(e);
        if (!previewed) { if (c.started) ParticleSystems::ResetRuntime(c); return; }
        if (!s_PreviewPaused) ParticleSystems::Simulate(scene, e, c, dt * s_PreviewSpeed);
    });
}

void DrawParticleSystemInspector(Scene& scene, EntityID e, ParticleSystemComponent& c) {
    // Playback: in Play mode it drives the system, otherwise the editor preview
    const bool running = scene.IsRunning();
    {
        const bool paused = running ? c.paused : s_PreviewPaused;
        if (ImGui::Button(paused || (running && !c.playing) ? "Play" : "Pause", { 64, 0 })) {
            if (running) { if (c.playing && !c.paused) ParticleSystems::Pause(c); else ParticleSystems::Play(c); }
            else { s_PreviewPaused = !s_PreviewPaused; if (!s_PreviewPaused && c.started && !c.playing) ParticleSystems::Play(c); }
        }
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Restart", { 64, 0 })) { ParticleSystems::ResetRuntime(c); if (running) ParticleSystems::Play(c); else s_PreviewPaused = false; }
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Stop", { 64, 0 })) { ParticleSystems::Stop(c, true); if (!running) s_PreviewPaused = true; }
        if (!running) {
            ImGui::SameLine(0, 8);
            ImGui::SetNextItemWidth(std::max(50.0f, ImGui::GetContentRegionAvail().x));
            ImGui::DragFloat("##previewSpeed", &s_PreviewSpeed, 0.01f, 0.0f, 10.0f, "Speed %.2fx");
        }
        ImGui::TextDisabled("%s  %zu particles   %.2f s", running ? "Play mode" : "Preview", c.particles.size(), c.time);
    }

    ImGui::Spacing();
    if (Module("Main", nullptr, true)) {
        Insp::DragFloat("Duration", &c.duration, 0.05f, 0.05f, 1000.0f, "%.2f");
        Insp::Checkbox("Looping", &c.looping);
        if (c.looping) Insp::Checkbox("Prewarm", &c.prewarm);
        RangeField("Start Delay", c.startDelay, 0.01f, 0.0f);
        RangeField("Start Lifetime", c.startLifetime, 0.05f, 0.01f);
        RangeField("Start Speed", c.startSpeed, 0.05f);
        RangeField("Start Size", c.startSize, 0.01f, 0.0f);
        RangeField("Start Rotation", c.startRotation, 1.0f);
        Insp::LabelCell("Start Color");
        {
            const float w = (ImGui::CalcItemWidth() - 4) * 0.5f;
            ImGui::SetNextItemWidth(w); EditorIcons::ColorField("##start1", &c.startColor.x, 4);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(w); EditorIcons::ColorField("##start2", &c.startColor2.x, 4);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each particle takes a random colour between these two");
        }
        Insp::DragFloat("Gravity Modifier", &c.gravityModifier, 0.01f, -100.0f, 100.0f, "%.2f");
        Insp::Combo("Simulation Space", &c.simulationSpace, "Local\0World\0");
        Insp::DragFloat("Simulation Speed", &c.simulationSpeed, 0.01f, 0.0f, 100.0f, "%.2f");
        Insp::Checkbox("Play On Awake", &c.playOnAwake);
        Insp::DragInt("Max Particles", &c.maxParticles, 10, 0, 100000);
        int seed = (int)c.seed;
        if (Insp::DragInt("Random Seed", &seed, 1, 0, INT_MAX)) c.seed = (u32)std::max(0, seed);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = different every time; any other value replays the same effect");
        EndModule();
    }
    if (Module("Emission", &c.emission)) {
        Insp::DragFloat("Rate over Time", &c.rateOverTime, 0.1f, 0.0f, 100000.0f, "%.1f");
        Insp::DragFloat("Rate over Distance", &c.rateOverDistance, 0.1f, 0.0f, 100000.0f, "%.1f");
        ImGui::TextDisabled("Bursts");
        if (!c.bursts.empty() && ImGui::BeginTable("##bursts", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerH)) {
            for (const char* name : { "Time", "Count", "Cycles", "Interval", "Chance" }) ImGui::TableSetupColumn(name);
            ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, 22);
            ImGui::TableHeadersRow();
            int remove = -1;
            for (int i = 0; i < (int)c.bursts.size(); ++i) {
                auto& b = c.bursts[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragFloat("##time", &b.time, 0.01f, 0, 1000, "%.2f");
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragInt("##count", &b.count, 1, 0, 100000);
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragInt("##cycles", &b.cycles, 0.1f, 0, 1000, b.cycles <= 0 ? "Infinite" : "%d");
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragFloat("##interval", &b.interval, 0.01f, 0.01f, 1000, "%.2f");
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::DragFloat("##probability", &b.probability, 0.01f, 0, 1, "%.2f");
                ImGui::TableNextColumn(); if (ImGui::Button("-", { 22, 0 })) remove = i;
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (remove >= 0) c.bursts.erase(c.bursts.begin() + remove);
        }
        if (ImGui::Button("Add Burst")) c.bursts.push_back({});
        EndModule();
    }
    if (Module("Shape", &c.shapeEnabled)) {
        int shape = (int)c.shape;
        if (Insp::Combo("Shape", &shape, "Sphere\0Hemisphere\0Cone\0Box\0Circle\0Edge\0Point\0")) c.shape = (ParticleShape)shape;
        if (c.shape == ParticleShape::Cone) Insp::DragFloat("Angle", &c.angle, 0.5f, 0.0f, 89.9f, "%.1f");
        if (c.shape != ParticleShape::Box && c.shape != ParticleShape::Point) Insp::DragFloat("Radius", &c.radius, 0.01f, 0.0f, 10000.0f, "%.2f");
        if (c.shape == ParticleShape::Sphere || c.shape == ParticleShape::Hemisphere || c.shape == ParticleShape::Cone || c.shape == ParticleShape::Circle)
            Insp::SliderFloat("Radius Thickness", &c.radiusThickness, 0, 1, "%.2f");
        if (c.shape == ParticleShape::Cone || c.shape == ParticleShape::Circle) Insp::DragFloat("Arc", &c.arc, 1.0f, 0.0f, 360.0f, "%.0f");
        if (c.shape == ParticleShape::Box) Insp::DragFloat3("Box Size", &c.boxSize.x, 0.01f, 0.0f, 10000.0f, "%.2f");
        Insp::DragFloat3("Position", &c.shapeOffset.x, 0.01f, 0, 0, "%.2f");
        Insp::SliderFloat("Randomize Direction", &c.randomizeDirection, 0, 1, "%.2f");
        ImGui::TextDisabled("Emits along the object's up axis (+Y)");
        EndModule();
    }
    if (Module("Velocity over Lifetime", &c.velocityOverLifetime)) { Insp::DragFloat3("Velocity", &c.velocity.x, 0.05f, 0, 0, "%.2f"); EndModule(); }
    if (Module("Force over Lifetime", &c.forceOverLifetime)) { Insp::DragFloat3("Force", &c.force.x, 0.05f, 0, 0, "%.2f"); EndModule(); }
    if (Module("Limit Velocity", &c.limitVelocity)) {
        Insp::DragFloat("Speed Limit", &c.speedLimit, 0.05f, 0.0f, 10000.0f, "%.2f");
        Insp::DragFloat("Drag", &c.drag, 0.01f, 0.0f, 100.0f, "%.2f");
        EndModule();
    }
    if (Module("Color over Lifetime", &c.colorOverLifetime)) { GradientEditor(c.colorKeys); EndModule(); }
    if (Module("Size over Lifetime", &c.sizeOverLifetime)) { CurveEditor(c.sizeKeys); EndModule(); }
    if (Module("Rotation over Lifetime", &c.rotationOverLifetime)) { RangeField("Angular Velocity", c.angularVelocity, 1.0f); EndModule(); }
    if (Module("Noise", &c.noise)) {
        Insp::DragFloat("Strength", &c.noiseStrength, 0.01f, 0.0f, 100.0f, "%.2f");
        Insp::DragFloat("Frequency", &c.noiseFrequency, 0.01f, 0.0f, 100.0f, "%.2f");
        Insp::DragFloat("Scroll Speed", &c.noiseScrollSpeed, 0.01f, 0.0f, 100.0f, "%.2f");
        EndModule();
    }
    if (Module("Collision", &c.collision)) {
        Insp::DragFloat("Plane Height", &c.collisionPlane, 0.05f, 0, 0, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Particles bounce on a horizontal plane at this world height");
        Insp::SliderFloat("Bounce", &c.bounce, 0, 2, "%.2f");
        Insp::SliderFloat("Dampen", &c.dampen, 0, 1, "%.2f");
        Insp::SliderFloat("Lifetime Loss", &c.lifetimeLoss, 0, 1, "%.2f");
        EndModule();
    }
    if (Module("Texture Sheet Animation", &c.textureSheet)) {
        int tiles[2] = { c.tilesX, c.tilesY };
        if (Insp::DragInt2("Tiles", tiles, 0.1f, 1, 64)) { c.tilesX = std::clamp(tiles[0], 1, 64); c.tilesY = std::clamp(tiles[1], 1, 64); }
        Insp::DragFloat("Frames per Second", &c.sheetFps, 0.1f, 0.0f, 240.0f, c.sheetFps > 0 ? "%.1f" : "Over lifetime");
        if (c.sheetFps <= 0) Insp::DragFloat("Cycles", &c.sheetCycles, 0.05f, 0.0f, 100.0f, "%.2f");
        Insp::Checkbox("Random Start Frame", &c.randomStartFrame);
        EndModule();
    }
    if (Module("Renderer", nullptr, true)) {
        AssetUI::AssetField("Texture", c.texture, { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".rendertexture" }, "Default (soft circle)");
        AssetUI::AssetField("Material", c.material, { ".mat" }, "None (unlit)");
        Insp::Combo("Blend Mode", &c.blendMode, "Alpha\0Additive\0");
        Insp::Combo("Render Mode", &c.renderMode, "Billboard\0Stretched Billboard\0Horizontal Billboard\0Vertical Billboard\0");
        if (c.renderMode == 1) {
            Insp::DragFloat("Length Scale", &c.lengthScale, 0.01f, 0.0f, 100.0f, "%.2f");
            Insp::DragFloat("Speed Scale", &c.speedScale, 0.005f, 0.0f, 100.0f, "%.3f");
        }
        Insp::DragFloat("Intensity", &c.intensity, 0.01f, 0.0f, 100.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Colour multiplier: above 1 the particles glow with bloom");
        Insp::Checkbox("Sort by Distance", &c.sortByDistance);
        Insp::InputInt("Sorting Order", &c.sortingOrder);
        EndModule();
    }
}

} // namespace Stardust
