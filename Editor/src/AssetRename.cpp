// Renaming assets and folders from the Project window, keeping every reference to them working
#include "EditorApp.h"
#include "Assets/AssetManager.h"
#include "AssetUI.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Project/Project.h"
#include "Scene/SceneSerializer.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <fstream>

namespace Stardust {
namespace fs = std::filesystem;

namespace {
std::string Lower(std::string text) { std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return text; }

// Asset references are relative paths stored as JSON strings: "Folder/File.png". A renamed file matches the whole
// string; a renamed folder (or a model's .fbm folder) matches the start of the paths inside it.
struct Replacement { std::string from, to; bool prefix; };
bool ApplyReplacements(std::string& text, const std::vector<Replacement>& replacements) {
    bool changed = false;
    for (const Replacement& r : replacements) {
        const std::string from = "\"" + r.from + (r.prefix ? "/" : "\""), to = "\"" + r.to + (r.prefix ? "/" : "\"");
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
            changed = true;
        }
    }
    return changed;
}
// Text assets that can point at other assets
bool HoldsReferences(const fs::path& file) {
    static const char* extensions[] = { ".scene", ".prefab", ".mat", ".controller", ".anim", ".sprite", ".meta", ".json", ".avatar",
                                         ".terrain", ".terrainlayer", ".rendertexture", ".overridecontroller", ".mask", ".tileset" };
    const std::string ext = Lower(FileSystem::Utf8(file.extension()));
    return std::any_of(std::begin(extensions), std::end(extensions), [&](const char* e) { return ext == e; });
}
void RenamePath(std::string& path, const std::vector<Replacement>& replacements) {
    for (const Replacement& r : replacements) {
        if (!r.prefix && path == r.from) { path = r.to; return; }
        if (r.prefix && path.starts_with(r.from + "/")) { path = r.to + path.substr(r.from.size()); return; }
    }
}
} // namespace

void EditorApp::BeginRenameAsset(const std::string& relPath, bool inTree) {
    const fs::path absolute = AssetManager::ToAbsolute(relPath);
    if (absolute == m_ProjectRoot || absolute == Project::Dir() / "Packages") return; // the roots keep their names
    m_RenamingAsset = relPath;
    // Files are edited without their extension, which is kept
    m_RenameText = FileSystem::Utf8(fs::is_directory(absolute) ? absolute.filename() : absolute.stem());
    m_RenameFocus = true;
    m_RenameInTree = inTree; // the field shows in one place only
}

// Inline text field for the asset in m_RenamingAsset: Enter or clicking away applies, Escape cancels
void EditorApp::DrawRenameField(float width) {
    ImGui::SetNextItemWidth(width);
    if (m_RenameFocus) { ImGui::SetKeyboardFocusHere(); m_RenameFocus = false; }
    const bool enter = ImGui::InputText("##rename", &m_RenameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool leave = ImGui::IsItemDeactivated();
    if (cancel) { m_RenamingAsset.clear(); return; }
    if (enter || leave) {
        const std::string asset = std::exchange(m_RenamingAsset, std::string());
        RenameAsset(asset, m_RenameText);
    }
}

bool EditorApp::RenameAsset(const std::string& relPath, std::string newName) {
    // Trim and check the name
    while (!newName.empty() && std::isspace((unsigned char)newName.back())) newName.pop_back();
    while (!newName.empty() && std::isspace((unsigned char)newName.front())) newName.erase(newName.begin());
    if (newName.empty() || newName == "." || newName == ".." || newName.find_first_of("/\\:*?\"<>|") != std::string::npos) {
        SD_WARN("Cannot rename to \"{}\": names cannot be empty or contain / \\ : * ? \" < > |", newName);
        return false;
    }
    if (m_State != PlayState::Edit) { SD_WARN("Exit play mode before renaming assets"); return false; }
    if (!m_PrefabAsset.empty()) { SD_WARN("Close the prefab before renaming assets"); return false; }

    const fs::path oldPath = AssetManager::ToAbsolute(relPath);
    std::error_code ec;
    if (!fs::exists(oldPath, ec)) { SD_WARN("Cannot rename {}: it no longer exists", relPath); return false; }
    const bool directory = fs::is_directory(oldPath, ec);
    const std::string extension = directory ? std::string() : FileSystem::Utf8(oldPath.extension());
    if (!extension.empty() && !Lower(newName).ends_with(Lower(extension))) newName += extension;
    const fs::path newPath = oldPath.parent_path() / fs::u8path(newName);
    if (newPath == oldPath) return true;
    // Windows paths ignore case: a case-only change is the same file, anything else must be free
    if (fs::exists(newPath, ec) && !fs::equivalent(oldPath, newPath, ec)) { SD_WARN("Cannot rename {}: {} already exists", relPath, newName); return false; }

    std::vector<AssetPathChange> changes;
    if (!RelocateAsset(oldPath, newPath, changes)) return false;
    const int updated = UpdateAssetReferences(changes);
    if (m_EditorScene && !m_ScenePath.empty() && m_ScenePath == newPath) m_EditorScene->SetName(FileSystem::Utf8(newPath.stem()));
    SD_INFO("Renamed {} to {}{}", relPath, AssetManager::ToRelative(newPath), updated ? std::format(" ({} files updated)", updated) : std::string());
    return true;
}

// Moves a file or folder on disk with its .meta and, for models, the <name>.fbm folder; records the path changes
bool EditorApp::RelocateAsset(const fs::path& oldPath, const fs::path& newPath, std::vector<AssetPathChange>& changes) {
    std::error_code ec;
    const bool directory = fs::is_directory(oldPath, ec);
    try {
        fs::rename(oldPath, newPath);
        changes.push_back({ AssetManager::ToRelative(oldPath), AssetManager::ToRelative(newPath), directory });
        fs::path oldMeta = oldPath, newMeta = newPath;
        oldMeta += ".meta"; newMeta += ".meta";
        if (fs::exists(oldMeta, ec)) fs::rename(oldMeta, newMeta);
        if (!directory) {
            const fs::path oldFolder = oldPath.parent_path() / fs::u8path(FileSystem::Utf8(oldPath.stem()) + ".fbm");
            const fs::path newFolder = newPath.parent_path() / fs::u8path(FileSystem::Utf8(newPath.stem()) + ".fbm");
            if (fs::is_directory(oldFolder, ec) && !fs::exists(newFolder, ec)) {
                fs::rename(oldFolder, newFolder);
                changes.push_back({ AssetManager::ToRelative(oldFolder), AssetManager::ToRelative(newFolder), true });
            }
        }
    } catch (const std::exception& e) {
        SD_ERROR("Cannot move {}: {}", FileSystem::Utf8(oldPath), e.what());
        return false;
    }
    AssetManager::Invalidate(AssetManager::ToRelative(oldPath));
    return true;
}

// Points every reference at the new paths: text assets, project settings, the open scene and its history, editor state.
// Returns how many asset files changed.
int EditorApp::UpdateAssetReferences(const std::vector<AssetPathChange>& changes) {
    if (changes.empty()) return 0;
    std::vector<Replacement> replacements;
    for (const auto& change : changes) replacements.push_back({ change.from, change.to, change.folder });
    std::error_code ec;
    int updated = 0;
    for (auto it = fs::recursive_directory_iterator(m_ProjectRoot, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || !HoldsReferences(it->path())) continue;
        std::string text = FileSystem::ReadText(it->path());
        if (text.empty() || !ApplyReplacements(text, replacements)) continue;
        std::ofstream out(it->path(), std::ios::binary);
        out.write(text.data(), (std::streamsize)text.size());
        ++updated;
    }
    // Project settings: scenes in build, last scene, icon, content packs
    {
        auto& config = Project::Config();
        for (auto& scene : config.scenes) RenamePath(scene, replacements);
        RenamePath(config.lastOpenedScene, replacements);
        RenamePath(config.icon, replacements);
        for (auto& pack : config.contentPacks) RenamePath(pack.folder, replacements);
        Project::Save();
    }
    // The open scene and its undo history, without marking the scene changed
    if (m_EditorScene) {
        std::string json = SceneSerializer::ToJson(*m_EditorScene);
        if (ApplyReplacements(json, replacements)) {
            const bool dirty = m_SceneDirty;
            RestoreSnapshot(json);
            m_SceneDirty = dirty;
        }
        for (auto* history : { &m_Undo, &m_Redo }) for (std::string& snapshot : *history) ApplyReplacements(snapshot, replacements);
    }
    // Editor state that holds paths
    auto renameAbsolute = [&](fs::path& path) {
        if (path.empty()) return;
        std::string relative = AssetManager::ToRelative(path);
        const std::string before = relative;
        RenamePath(relative, replacements);
        if (relative != before) path = AssetManager::ToAbsolute(relative);
    };
    renameAbsolute(m_ScenePath);
    renameAbsolute(m_ProjectCurrentDir);
    RenamePath(m_SelectedAsset, replacements);
    for (std::string& asset : m_AssetSelection) RenamePath(asset, replacements);
    RenamePath(m_AnimationAsset, replacements);
    RenamePath(m_SpriteEditorPath, replacements);
    m_ProjectListingDirty = true;
    return updated;
}

// Moves assets and folders into another folder of the project, keeping references
bool EditorApp::MoveAssets(const std::vector<std::string>& relPaths, const fs::path& folder) {
    if (m_State != PlayState::Edit) { SD_WARN("Exit play mode before moving assets"); return false; }
    if (!m_PrefabAsset.empty()) { SD_WARN("Close the prefab before moving assets"); return false; }
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return false;
    const fs::path target = fs::weakly_canonical(folder, ec);
    std::vector<AssetPathChange> changes;
    int moved = 0;
    for (const std::string& relPath : relPaths) {
        const fs::path source = AssetManager::ToAbsolute(relPath);
        if (!fs::exists(source, ec) || source == m_ProjectRoot || source == Project::Dir() / "Packages") continue;
        const fs::path canonical = fs::weakly_canonical(source, ec);
        if (canonical.parent_path() == target) continue; // already there
        // A folder cannot go inside itself
        const auto inside = fs::relative(target, canonical, ec);
        if (fs::is_directory(source, ec) && !inside.empty() && *inside.begin() != fs::path("..")) { SD_WARN("Cannot move {} into itself", relPath); continue; }
        const fs::path destination = folder / source.filename();
        if (fs::exists(destination, ec)) { SD_WARN("Cannot move {}: {} already has {}", relPath, AssetManager::ToRelative(folder), FileSystem::Utf8(source.filename())); continue; }
        if (RelocateAsset(source, destination, changes)) ++moved;
    }
    if (!moved) return false;
    const int updated = UpdateAssetReferences(changes);
    SD_INFO("Moved {} asset{} to {}{}", moved, moved == 1 ? "" : "s", AssetManager::ToRelative(folder).empty() ? "Assets" : AssetManager::ToRelative(folder),
            updated ? std::format(" ({} files updated)", updated) : std::string());
    return true;
}

// Drop target for assets (STARDUST_ASSET) and folders (STARDUST_FOLDER) dragged from the Project window.
// Dragging one of several selected assets moves the whole selection.
void EditorApp::AcceptAssetMoveDrop(const fs::path& folder) {
    if (!ImGui::BeginDragDropTarget()) return;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload);
    if (!payload) payload = ImGui::AcceptDragDropPayload("STARDUST_FOLDER");
    if (payload) {
        const std::string dragged((const char*)payload->Data);
        const bool inSelection = std::find(m_AssetSelection.begin(), m_AssetSelection.end(), dragged) != m_AssetSelection.end();
        MoveAssets(inSelection ? m_AssetSelection : std::vector<std::string>{ dragged }, folder);
    }
    ImGui::EndDragDropTarget();
}

} // namespace Stardust
