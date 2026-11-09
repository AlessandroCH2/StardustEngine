// FBX import settings (Rig / Animation) and the .avatar bone mapping
#include "AssetUI.h"
#include "InspectorWidgets.h"
#include "Animation/AvatarSystem.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <imgui.h>

namespace Stardust::AssetUI {

using json = nlohmann::json;

void FbxImportInspector(const std::string& asset, std::string& openAsset) {
    static std::string loaded, message;
    static FbxImportSettings settings;
    static std::vector<FbxTakeDesc> takes;
    static std::vector<bool> takeMoves;
    if (loaded != asset) { loaded = asset; settings = FbxImportSettings::Load(asset); takes = AssetManager::GetFbxTakes(asset); takeMoves.clear(); for (const FbxTakeDesc& take : takes) takeMoves.push_back(AvatarSystem::TakeHasMotion(asset, take)); message.clear(); }
    bool changed = false;

    ImGui::SeparatorText("Rig");
    changed |= Insp::Combo("Animation Type", &settings.animationType, "None\0Generic\0Humanoid\0");
    if (settings.animationType == FbxImportSettings::Humanoid) {
        changed |= AssetField("Avatar", settings.avatar, { ".avatar" });
        if (ImGui::Button("Create From This Model")) {
            std::string error, created = AvatarSystem::CreateFromFbx(asset, &error);
            if (created.empty()) message = error;
            else { settings.avatar = created; changed = true; message = "Avatar created: " + created; }
        }
        if (!settings.avatar.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Configure...")) openAsset = settings.avatar;
        }
    }

    if (settings.animationType != FbxImportSettings::None) {
        ImGui::SeparatorText("Animation");
        if (takes.empty()) ImGui::TextDisabled("This model contains no animation.");
        const bool anyMotion = std::find(takeMoves.begin(), takeMoves.end(), true) != takeMoves.end();
        for (size_t index = 0; index < takes.size(); ++index) {
            const FbxTakeDesc& take = takes[index];
            if (anyMotion && !takeMoves[index]) { ImGui::TextDisabled("%s  (%.2f s): no motion, not imported", take.name.c_str(), take.duration); continue; }
            ImGui::PushID(take.name.c_str());
            bool loop = !settings.loop.count(take.name) || settings.loop[take.name];
            std::string label = std::format("{}  ({:.2f} s) Loop", take.name, take.duration);
            if (Insp::Checkbox(label.c_str(), &loop)) { settings.loop[take.name] = loop; changed = true; }
            ImGui::PopID();
        }
        if (settings.animationType == FbxImportSettings::Humanoid) changed |= Insp::Checkbox("Remove Root Motion XZ", &settings.inPlace);
        ImGui::BeginDisabled(takes.empty() || (settings.animationType == FbxImportSettings::Humanoid && settings.avatar.empty()));
        if (ImGui::Button("Import Clips", { -FLT_MIN, 0 })) {
            settings.Save(asset);
            std::string error;
            auto written = AvatarSystem::ImportClips(asset, &error);
            message = written.empty() ? error : std::format("Imported {} clip(s) beside the model.", written.size());
        }
        ImGui::EndDisabled();
    }
    if (changed) settings.Save(asset);
    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
}

void AvatarInspector(const std::string& asset) {
    static std::string loaded, status;
    static json avatar;
    static std::vector<std::string> paths;
    auto path = AssetManager::ToAbsolute(asset);
    auto rebuild = [&] {
        std::string error;
        if (AvatarSystem::Rebuild(avatar, &error)) { FileSystem::WriteText(path, avatar.dump(2)); status = "Avatar is valid."; }
        else status = error;
    };
    if (loaded != asset) {
        loaded = asset;
        avatar = json::parse(FileSystem::ReadText(path), nullptr, false);
        if (!avatar.is_object()) avatar = json::object();
        if (!avatar.contains("bones")) avatar["bones"] = json::object();
        paths = AvatarSystem::SkeletonPaths(avatar.value("source", std::string()));
        status.clear();
    }

    ImGui::SeparatorText("Avatar");
    ImGui::TextDisabled("Source: %s", avatar.value("source", std::string("(none)")).c_str());
    int mapped = 0, missing = 0;
    for (const std::string& bone : AvatarSystem::HumanBones()) {
        if (avatar["bones"].contains(bone)) ++mapped;
        else if (AvatarSystem::IsRequired(bone)) ++missing;
    }
    ImGui::Text("%d bones mapped", mapped);
    if (missing) { ImGui::SameLine(); ImGui::TextColored({ 1, 0.6f, 0.3f, 1 }, "%d required missing", missing); }
    if (ImGui::Button("Automap")) { avatar["bones"] = AvatarSystem::AutoMap(avatar.value("source", std::string())); rebuild(); }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { avatar["bones"] = json::object(); status.clear(); }
    if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());

    auto row = [&](const std::string& bone) {
        ImGui::PushID(bone.c_str());
        bool has = avatar["bones"].contains(bone);
        std::string current = has ? avatar["bones"][bone].get<std::string>() : std::string();
        std::string preview = current.empty() ? "None" : current.substr(current.rfind('/') == std::string::npos ? 0 : current.rfind('/') + 1);
        std::string label = bone + (AvatarSystem::IsRequired(bone) ? " *" : "");
        if (Insp::BeginCombo(label.c_str(), preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("None", current.empty())) { avatar["bones"].erase(bone); rebuild(); }
            for (const std::string& option : paths) {
                std::string name = option.substr(option.rfind('/') == std::string::npos ? 0 : option.rfind('/') + 1);
                if (ImGui::Selectable(std::format("{}##{}", name, option).c_str(), option == current)) { avatar["bones"][bone] = option; rebuild(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", option.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    };
    const auto& bones = AvatarSystem::HumanBones();
    const int bodyCount = 6 + 8 + 8; // spine + arms + legs
    ImGui::SeparatorText("Body");
    for (int i = 0; i < bodyCount; ++i) row(bones[i]);
    if (ImGui::TreeNode("Fingers")) {
        for (size_t i = bodyCount; i < bones.size(); ++i) row(bones[i]);
        ImGui::TreePop();
    }
}

} // namespace Stardust::AssetUI
