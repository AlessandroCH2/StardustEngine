#pragma once
// Label-first versions of the ImGui widgets used by inspectors: name in a left column, control filling
// the rest of the row. A "##id" label, or a widget placed after SameLine(), keeps its own layout.
#include "EditorIcons.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Stardust::Insp {

inline float LabelWidth() { return std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 110.0f, 200.0f); }

inline void LabelCell(const char* label) {
    float width = LabelWidth(), height = ImGui::GetFrameHeight();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const char* end = ImGui::FindRenderedTextEnd(label);
    float textWidth = ImGui::CalcTextSize(label, end).x, clipX = pos.x + width - 6.0f;
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), { pos.x, pos.y + ImGui::GetStyle().FramePadding.y },
                              { clipX, pos.y + height }, clipX, clipX, label, end, nullptr);
    ImGui::Dummy({ width, height });
    if (textWidth > width - 6.0f && ImGui::IsItemHovered()) ImGui::SetTooltip("%.*s", (int)(end - label), label);
    ImGui::SameLine(0, 0);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

inline bool KeepsLayout(const char* label) {
    return (label[0] == '#' && label[1] == '#') || ImGui::GetCurrentWindow()->DC.IsSameLine;
}
inline std::string HiddenId(const char* label) { return std::string("##") + label; }

#define SD_INSPECTOR_WIDGET(Name)                                                                   \
    template<typename... Args> bool Name(const char* label, Args&&... args) {                       \
        if (KeepsLayout(label)) return ImGui::Name(label, std::forward<Args>(args)...);             \
        LabelCell(label);                                                                           \
        return ImGui::Name(HiddenId(label).c_str(), std::forward<Args>(args)...);                   \
    }
SD_INSPECTOR_WIDGET(DragFloat)
SD_INSPECTOR_WIDGET(DragFloat2)
SD_INSPECTOR_WIDGET(DragFloat3)
SD_INSPECTOR_WIDGET(DragFloat4)
SD_INSPECTOR_WIDGET(DragInt)
SD_INSPECTOR_WIDGET(DragInt2)
SD_INSPECTOR_WIDGET(SliderInt)
SD_INSPECTOR_WIDGET(InputInt)
SD_INSPECTOR_WIDGET(InputFloat)
SD_INSPECTOR_WIDGET(InputText)
SD_INSPECTOR_WIDGET(InputTextWithHint)
SD_INSPECTOR_WIDGET(Checkbox)
SD_INSPECTOR_WIDGET(Combo)
SD_INSPECTOR_WIDGET(BeginCombo)
#undef SD_INSPECTOR_WIDGET

inline bool SliderBody(const char* id, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags) {
    const float width = ImGui::CalcItemWidth(), height = ImGui::GetFrameHeight(), box = std::min(62.0f, width * 0.36f), track = std::max(20.0f, width - box - 6.0f);
    const bool logarithmic = (flags & ImGuiSliderFlags_Logarithmic) && minimum > 0 && maximum > minimum;
    auto toT = [&](float v) { return std::clamp(logarithmic ? std::log(v / minimum) / std::log(maximum / minimum) : (v - minimum) / (maximum - minimum), 0.0f, 1.0f); };
    auto fromT = [&](float t) { return logarithmic ? minimum * std::pow(maximum / minimum, t) : minimum + (maximum - minimum) * t; };
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float radius = 6.0f, midY = p.y + height * 0.5f, left = p.x + radius, span = track - radius * 2;
    ImGui::InvisibleButton("##track", { track, height });
    bool changed = false;
    if (ImGui::IsItemActive()) {
        float next = fromT(std::clamp((ImGui::GetMousePos().x - left) / span, 0.0f, 1.0f));
        if (next != *value) { *value = next; changed = true; }
    }
    const float knob = left + span * toT(*value);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({ left, midY - 1.5f }, { left + span, midY + 1.5f }, IM_COL32(66, 69, 75, 255), 1.5f);
    draw->AddRectFilled({ left, midY - 1.5f }, { knob, midY + 1.5f }, IM_COL32(47, 128, 237, 255), 1.5f);
    draw->AddCircleFilled({ knob, midY }, radius, ImGui::IsItemHovered() || ImGui::IsItemActive() ? IM_COL32(120, 175, 250, 255) : IM_COL32(82, 150, 245, 255));
    ImGui::SameLine(0, 6);
    ImGui::SetNextItemWidth(box);
    changed |= ImGui::DragFloat("##value", value, (maximum - minimum) / 200.0f, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopID();
    return changed;
}

inline bool SliderFloat(const char* label, float* value, float minimum, float maximum, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    if (label[0] == '#' && label[1] == '#') return SliderBody(label, value, minimum, maximum, format, flags);
    if (ImGui::GetCurrentWindow()->DC.IsSameLine) return ImGui::SliderFloat(label, value, minimum, maximum, format, flags);
    LabelCell(label);
    return SliderBody(HiddenId(label).c_str(), value, minimum, maximum, format, flags);
}

inline bool ColorEdit3(const char* label, float* color, ImGuiColorEditFlags = 0) {
    if (KeepsLayout(label)) return EditorIcons::ColorField(label, color, 3);
    LabelCell(label);
    return EditorIcons::ColorField(HiddenId(label).c_str(), color, 3);
}
inline bool ColorEdit4(const char* label, float* color, ImGuiColorEditFlags = 0) {
    if (KeepsLayout(label)) return EditorIcons::ColorField(label, color, 4);
    LabelCell(label);
    return EditorIcons::ColorField(HiddenId(label).c_str(), color, 4);
}

} // namespace Stardust::Insp
