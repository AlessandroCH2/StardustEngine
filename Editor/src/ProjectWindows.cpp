// Project Hub, Build Settings, Project Settings, player builds and undo/redo
#include "Renderer/Quality.h"
#include "EditorApp.h"
#include "EditorIcons.h"
#include "Project/BuildCooker.h"
#include "AssetUI.h"
#include "FileDialogs.h"
#include "Project/Project.h"
#include "Project/PackageManager.h"
#include "Assets/AssetManager.h"
#include "Assets/Pak.h"
#include "UI/CanvasRenderer.h"
#include "Scripting/ScriptEngine.h"
#include "Scene/SceneSerializer.h"
#include "Core/FileSystem.h"
#include "Core/Process.h"
#include "Core/Log.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <imgui_stdlib.h>
#include <thread>
#include <fstream>

namespace Stardust {

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------------ project hub

void EditorApp::DrawProjectHub() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("Stardust Hub");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Choose a project to open, or create a new one.");
    ImGui::Separator();

    float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##recent", ImVec2(w * 0.62f, 0), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Recent Projects");
    auto recent = Project::RecentProjects();
    if (recent.empty()) ImGui::TextDisabled("No recent projects.");
    for (auto& p : recent) {
        ImGui::PushID(p.string().c_str());
        if (ImGui::Selectable(std::format("{}\n{}", p.stem().string(), p.parent_path().string()).c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 42*UiScale()))) {
            if (ImGui::IsMouseDoubleClicked(0)) OpenProject(p);
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open")) OpenProject(p);
            if (ImGui::MenuItem("Show in Explorer")) ShellExecuteFile(p.parent_path());
            if (ImGui::MenuItem("Remove from list")) Project::RemoveRecent(p);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Double-click to open, right-click for options.");
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##new", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("New Project");
    static std::string name = "My Game";
    static std::string location;
    if (location.empty()) location = (fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "C:/") / "Documents" / "Stardust Projects").string();
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", &name);
    ImGui::TextUnformatted("Location");
    ImGui::SetNextItemWidth(-70);
    ImGui::InputText("##loc", &location);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) if (auto f = FileDialogs::PickFolder(GetWindow().NativeHandle(), location)) location = f->string();
    ImGui::TextDisabled("Folder: %s", (fs::path(location) / name).string().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Create Project", ImVec2(-1, 34*UiScale())) && !name.empty()) {
        fs::path dir = fs::path(location) / name;
        if (Project::Create(dir, name)) OpenProject(Project::File(), true);
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Existing Project");
    if (ImGui::Button("Open Project...", ImVec2(-1, 34*UiScale())))
        if (auto p = FileDialogs::OpenFile(GetWindow().NativeHandle(), L"Stardust Project (*.sdproject)\0*.sdproject\0", location))
            OpenProject(*p);
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::OpenProject(const fs::path& file, bool isNew) {
    StopAnimationPreview();
    if(!m_PrefabAsset.empty())ExitPrefabStage();
    if (m_State != PlayState::Edit) Stop();
    if (Project::IsLoaded() && m_EditorScene) SaveProjectState();
    if (!Project::Load(file)) return;
    PackageManager::EnsureDefault();
    Project::WriteEditorScriptProject();
    Project::AddRecent(file);
    m_ProjectRoot = Project::AssetsDir();
    m_ProjectCurrentDir = m_ProjectRoot;
    m_SelectedAsset.clear();
    CanvasRenderer::Clear();
    AssetManager::Shutdown();
    AssetManager::Init(GetDevice(), m_ProjectRoot);
    TerrainData::ClearCache();

    if (!ScriptEngine::IsReady()) ScriptEngine::Init();
    else ScriptEngine::ReloadGameAssembly();
    ScriptEngine::CheckForSourceChange();
    if (ScriptEngine::IsReady() && ScriptEngine::NeedsBuild()) { SD_INFO("Compiling project scripts..."); ScriptEngine::BuildAsync(); }

    // Last scene, else the first scene in build, else a new default scene
    std::string scene = Project::Config().lastOpenedScene;
    if (scene.empty() || !fs::exists(m_ProjectRoot / fs::u8path(scene))) scene = Project::EnabledScenes().empty() ? "" : Project::EnabledScenes()[0];
    if (!scene.empty() && fs::exists(m_ProjectRoot / fs::u8path(scene))) OpenScene(m_ProjectRoot / fs::u8path(scene));
    else {
        NewScene();
        SaveSceneAs(m_ProjectRoot / "Scenes" / "Main.scene");
        auto& cfg = Project::Config();
        if (cfg.scenes.empty()) { cfg.scenes.push_back("Scenes/Main.scene"); cfg.scenesEnabled.push_back(true); }
        Project::Save();
    }
    m_Undo.clear(); m_Redo.clear(); m_UndoSnapshot.clear();
    m_ShowHub = false;
    UpdateTitle();
    (void)isNew;
}

void EditorApp::SaveProjectState() {
    if (!Project::IsLoaded()) return;
    if (!m_ScenePath.empty()) Project::Config().lastOpenedScene = AssetManager::ToRelative(m_ScenePath);
    Project::Save();
}

// ------------------------------------------------------------------------------------------------ build settings

void EditorApp::DrawBuildSettings() {
    if (!m_ShowBuildSettings) return;
    ImGui::SetNextWindowSize(ImVec2(620*UiScale(), 560*UiScale()), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Build Settings", &m_ShowBuildSettings,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    auto& cfg = Project::Config();
    cfg.scenesEnabled.resize(cfg.scenes.size(), true);

    static int page=0;const char* pages[]={"Scenes","Content Paks","Player","Export"};EditorIcons::SettingsTabs("BuildTabs",page,pages,4);ImGui::BeginChild("BuildSettingsBody",{0,0});
    if(page==0){
    ImGui::SeparatorText("Scenes In Build");
    ImGui::TextDisabled("Index 0 is loaded first. Drag .scene files here from the Project panel, drag rows to reorder.");
    ImGui::BeginChild("##scenes", ImVec2(0, 200*UiScale()), ImGuiChildFlags_Borders);
    int moveFrom = -1, moveTo = -1, removeAt = -1;
    int index = 0;
    for (size_t i = 0; i < cfg.scenes.size(); ++i) {
        ImGui::PushID((int)i);
        bool en = cfg.scenesEnabled[i];
        if (ImGui::Checkbox("##en", &en)) { cfg.scenesEnabled[i] = en; Project::Save(); }
        ImGui::SameLine();
        ImGui::Selectable(std::format("{}   {}", en ? std::to_string(index) : std::string(" "), cfg.scenes[i]).c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 30, 0));
        if (en) ++index;
        if (ImGui::BeginDragDropSource()) {
            int src = (int)i;
            ImGui::SetDragDropPayload("STARDUST_SCENE_ROW", &src, sizeof(int));
            ImGui::Text("%s", cfg.scenes[i].c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STARDUST_SCENE_ROW")) { moveFrom = *(const int*)p->Data; moveTo = (int)i; }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = (int)i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    // dropping assets on the list
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)) {
            std::string path((const char*)p->Data, p->DataSize - 1);
            if (path.ends_with(".scene") && std::find(cfg.scenes.begin(), cfg.scenes.end(), path) == cfg.scenes.end()) {
                cfg.scenes.push_back(path); cfg.scenesEnabled.push_back(true); Project::Save();
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
        std::string s = cfg.scenes[moveFrom]; bool e = cfg.scenesEnabled[moveFrom];
        cfg.scenes.erase(cfg.scenes.begin() + moveFrom); cfg.scenesEnabled.erase(cfg.scenesEnabled.begin() + moveFrom);
        cfg.scenes.insert(cfg.scenes.begin() + moveTo, s); cfg.scenesEnabled.insert(cfg.scenesEnabled.begin() + moveTo, e);
        Project::Save();
    }
    if (removeAt >= 0) { cfg.scenes.erase(cfg.scenes.begin() + removeAt); cfg.scenesEnabled.erase(cfg.scenesEnabled.begin() + removeAt); Project::Save(); }
    if (ImGui::Button("Add Open Scene") && !m_ScenePath.empty()) {
        std::string rel = AssetManager::ToRelative(m_ScenePath);
        if (std::find(cfg.scenes.begin(), cfg.scenes.end(), rel) == cfg.scenes.end()) { cfg.scenes.push_back(rel); cfg.scenesEnabled.push_back(true); Project::Save(); }
    }

    }
    if(page==1){
    ImGui::SeparatorText("Optional Content Paks");ImGui::TextWrapped("Folders are relative to Assets. Scene dependencies always go in game.pak. Remaining assets and their dependencies go in Data/Optional and are mounted by script.");
    int removePack=-1;for(int i=0;i<(int)cfg.contentPacks.size();++i){ImGui::PushID(i);bool change=ImGui::InputText("Pak name",&cfg.contentPacks[i].name);change|=ImGui::InputText("Asset folder",&cfg.contentPacks[i].folder);if(ImGui::SmallButton("Remove"))removePack=i;if(change)Project::Save();ImGui::PopID();}
    if(removePack>=0){cfg.contentPacks.erase(cfg.contentPacks.begin()+removePack);Project::Save();}if(ImGui::Button("+ Add Optional Pak")){cfg.contentPacks.push_back({"content"+std::to_string(cfg.contentPacks.size()+1),"DLC"});Project::Save();}
    }
    if(page==2){
    ImGui::SeparatorText("Player");
    bool changed = false;
    int platform=cfg.buildTarget=="Linux"?1:0;if(ImGui::Combo("Target Platform",&platform,"Windows x64\0Linux x64\0")){cfg.buildTarget=platform?"Linux":"Windows";cfg.lastBuildFolder=(Project::Dir()/"Build"/cfg.buildTarget).string();changed=true;}
    changed |= ImGui::InputText("Product Name", &cfg.name);
    changed |= ImGui::InputText("Company", &cfg.company);
    changed |= ImGui::InputText("Version", &cfg.version);
    changed |= AssetUI::AssetField("Game Icon",cfg.icon,{".png",".jpg",".jpeg",".bmp",".tga"},"Stardust (default)");
    ImGui::TextDisabled("Used for the game window, taskbar and exported .exe. Transparent square PNG recommended.");
    int res[2] = { (int)cfg.windowWidth, (int)cfg.windowHeight };
    if (ImGui::InputInt2("Resolution", res)) { cfg.windowWidth = (u32)std::max(320, res[0]); cfg.windowHeight = (u32)std::max(240, res[1]); changed = true; }
    changed |= ImGui::Checkbox("Fullscreen", &cfg.fullscreen);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("VSync", &cfg.vsync);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Graphics Quality", &cfg.quality, "Low (older GPUs)\0Medium\0High\0")) { GraphicsQuality::Apply(cfg.quality); changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Low and Medium render the Game view and the built player at a lower 3D resolution,\nwith smaller shadow maps, cheaper clouds and sparser grass.");
    if (changed) Project::Save();

    }
    if(page==3){
    ImGui::SeparatorText("Build");
    ImGui::Text("Target: %s",cfg.buildTarget.c_str());
    if (cfg.lastBuildFolder.empty()) cfg.lastBuildFolder = (Project::Dir() / "Build").string();
    ImGui::SetNextItemWidth(-80);
    ImGui::InputText("##out", &cfg.lastBuildFolder);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) if (auto f = FileDialogs::PickFolder(GetWindow().NativeHandle(), cfg.lastBuildFolder)) { cfg.lastBuildFolder = f->string(); Project::Save(); }
    ImGui::BeginDisabled(m_Building || m_State != PlayState::Edit || Project::EnabledScenes().empty());
    if (ImGui::Button("Build", ImVec2(140*UiScale(), 30*UiScale()))) BuildPlayer(false);
    ImGui::SameLine();
    if (ImGui::Button("Build And Run", ImVec2(140*UiScale(), 30*UiScale()))) BuildPlayer(true);
    ImGui::EndDisabled();
    if (m_Building) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Building... (see Console)"); }
    if (Project::EnabledScenes().empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Add at least one scene to build.");
    ImGui::TextDisabled("The build needs the .NET 8 runtime on the target PC (C# scripts).");
    }
    ImGui::EndChild();
    ImGui::End();
}

// Copies the player executable, runtime files, compiled scripts and the project assets into a standalone folder
void EditorApp::BuildPlayer(bool run) {
    if(!m_PrefabAsset.empty())ExitPrefabStage();
    if (m_Building) return;
    if (m_PlayerBuildThread.joinable()) m_PlayerBuildThread.join();
    SaveScene();
    if (m_ScenePath.empty() || m_SceneDirty) { SD_ERROR("Save the scene before building the player"); return; }
    SaveProjectState();
    m_Building = true;
    ProjectConfig cfg = Project::Config();
    fs::path out = fs::absolute(cfg.lastBuildFolder).lexically_normal();
    fs::path exeDir = FileSystem::ExecutableDir();
    fs::path projectDir = Project::Dir(), assets = Project::AssetsDir(), csproj = Project::ScriptProjectFile(), projectFile = Project::File();
    // Reject layouts where replacing Data/Assets could touch project sources or recurse into the build.
    auto within = [](const fs::path& child, const fs::path& parent) {
        auto rel = fs::weakly_canonical(child).lexically_relative(fs::weakly_canonical(parent));
        return !rel.empty() && *rel.begin() != "..";
    };
    if (within(out, assets) || within(assets, out / "Data" / "Assets") || within(exeDir, out)) {
        SD_ERROR("Choose a build folder outside Assets and the engine binaries"); m_Building = false; return;
    }
    for (const auto& scene : Project::EnabledScenes()) {
        if (!fs::is_regular_file(assets / fs::u8path(scene))) { SD_ERROR("Build scene is missing: {}", scene); m_Building = false; return; }
    }
    m_PlayerBuildThread = std::thread([this, cfg, out, exeDir, projectDir, assets, csproj, projectFile, run, within] {
        auto fail = [&](const std::string& why) { SD_ERROR("Build failed: {}", why); m_Building = false; };
        try {
        std::error_code ec;
        SD_INFO("Building player '{}' into {}", cfg.name, out.string());
        fs::create_directories(out / "Data" / "Managed", ec);
        if (ec) return fail(ec.message());

        // 1. C# scripts (Release)
        if (fs::exists(csproj)) {
            std::wstring cmd = L"dotnet build \"" + csproj.wstring() + L"\" -c Release -o \"" + (out / "Data" / "Managed").wstring() +
                               L"\" -p:StardustBin=\"" + exeDir.wstring() + L"\" --nologo -v:q -clp:NoSummary";
            int code = RunProcess(cmd, projectDir, [](const std::string& l) {
                if (l.find(": error") != std::string::npos) SD_ERROR("{}", l); else SD_INFO("{}", l);
            });
            if (code != 0) return fail("script compilation failed");
        }
        // 2. runtime binaries
        std::string exeName = cfg.name;
        std::erase_if(exeName, [](char c) { return std::string("\\/:*?\"<>|").find(c) != std::string::npos; });
        if (exeName.empty()) exeName = "Game";
        bool linuxTarget=cfg.buildTarget=="Linux";
#ifdef _WIN32
        auto runtimeDir=linuxTarget?exeDir/"Linux":exeDir;
#else
        auto runtimeDir=exeDir; // a Linux editor can only export for Linux, with the player next to it
        if(!linuxTarget)return fail("Windows players can only be built from the Windows editor.");
#endif

        if(linuxTarget&&!fs::is_regular_file(runtimeDir/"StardustPlayer"))return fail("Linux runtime missing. Run Tools/BuildLinuxRuntime.sh on Linux and copy its output to build/bin/Release/Linux.");
        if(linuxTarget){std::ifstream runtime(runtimeDir/"StardustPlayer",std::ios::binary);unsigned char header[20]{};runtime.read((char*)header,sizeof(header));if(runtime.gcount()!=sizeof(header)||header[0]!=0x7f||header[1]!='E'||header[2]!='L'||header[3]!='F'||header[4]!=2||header[18]!=62||header[19]!=0)return fail("Invalid Linux runtime: expected a native ELF x86-64 executable.");}
        auto playerFile=out/(linuxTarget?"Game.x86_64":exeName+".exe");
        fs::copy_file(runtimeDir / (linuxTarget?"StardustPlayer":"StardustPlayer.exe"), playerFile, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("StardustPlayer.exe not found next to the editor (" + ec.message() + ")");
        for (const char* f : { "Stardust.ScriptCore.dll", "Stardust.ScriptCore.runtimeconfig.json", "Stardust.ScriptCore.deps.json" })
        {
            fs::copy_file(exeDir / f, out / f, fs::copy_options::overwrite_existing, ec);
            if (ec) return fail(std::string("cannot copy ") + f + ": " + ec.message());
        }
        fs::copy_file(runtimeDir/(linuxTarget?"libnethost.so":"nethost.dll"),out/(linuxTarget?"libnethost.so":"nethost.dll"),fs::copy_options::overwrite_existing,ec);if(ec)return fail("Missing native .NET host: "+ec.message());
        if(linuxTarget){
            fs::permissions(playerFile,fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec,fs::perm_options::add,ec);
            if(fs::exists(runtimeDir/"engineIcon.png"))fs::copy_file(runtimeDir/"engineIcon.png",out/"engineIcon.png",fs::copy_options::overwrite_existing);
            FileSystem::WriteText(out/"Launch.sh",R"launch(#!/bin/sh
cd -- "$(dirname -- "$0")"
exec ./Game.x86_64 "$@"
)launch");
        }
        fs::copy(exeDir / "shaders", out / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("cannot copy shaders: " + ec.message());
        // 3. assets (without C# sources) and project settings
        if(!BuildCooker::Cook(projectDir,out,cfg))return fail("Asset dependency cooking failed (see Console)");
        auto loose=out/"Data/Assets";if(fs::exists(loose)){if(!within(loose,out/"Data"))return fail("Old loose asset folder is outside export");fs::remove_all(loose,ec);if(ec)return fail("Cannot remove old loose assets");}
        auto legacy=out/"Data/game.sdproject";if(fs::is_regular_file(legacy))fs::remove(legacy,ec);
        auto license=FileSystem::EngineRoot()/"docs/THIRD_PARTY_UFBX.txt";
        if (fs::exists(license)) fs::copy_file(license,out/"THIRD_PARTY_UFBX.txt",fs::copy_options::overwrite_existing);
        auto videoLicense=FileSystem::EngineRoot()/"docs/THIRD_PARTY_PL_MPEG.txt";if(fs::exists(videoLicense))fs::copy_file(videoLicense,out/"THIRD_PARTY_PL_MPEG.txt",fs::copy_options::overwrite_existing);
        SD_INFO("Build complete: {} (Data/game.pak)", playerFile.string());
        if (run&&!linuxTarget) LaunchProcess(playerFile, out);
        else if(run&&linuxTarget) SD_INFO("Linux build ready; run Launch.sh on Linux.");
        else ShellExecuteFile(out);
        m_Building = false;
        } catch (const std::exception& e) { fail(e.what()); }
    });
}

// ------------------------------------------------------------------------------------------------ project settings

void EditorApp::DrawProjectSettings() {
    if (!m_ShowProjectSettings) return;
    ImGui::SetNextWindowSize(ImVec2(480*UiScale(), 420*UiScale()), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &m_ShowProjectSettings,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
    auto& cfg = Project::Config();
    bool changed = false;
    static int section=0;const char* categories[]={"General","Packages","Input","Layers","Tags"};EditorIcons::SettingsTabs("ProjectTabs",section,categories,5);ImGui::BeginChild("ProjectSettingsBody",{0,0});
    if(section==0){
    ImGui::SeparatorText("Project");
    changed |= ImGui::InputText("Name", &cfg.name);
    changed |= ImGui::InputText("Company", &cfg.company);
    changed |= ImGui::InputText("Version", &cfg.version);
    ImGui::TextDisabled("Scripts assembly: %s", cfg.assemblyName.c_str());
    ImGui::TextDisabled("Folder: %s", Project::Dir().string().c_str());
    }
    if(section==1){
    ImGui::SeparatorText("Packages");
    for (const auto& package : PackageManager::Installed())
        ImGui::BulletText("%s  %s%s", package.displayName.c_str(), package.version.c_str(), package.engine ? " (Engine)" : "");
    if (ImGui::Button("Install Local Package...")) {
        if (auto folder = FileDialogs::PickFolder(GetWindow().NativeHandle(), Project::Dir()))
            if (PackageManager::InstallLocal(*folder)) { Project::WriteScriptProject(Project::ScriptProjectFile(), cfg.assemblyName); ScriptEngine::BuildAsync(); }
    }
    }
    if(section==2){
    ImGui::SeparatorText("Input Manager");
        auto file=Project::AssetsDir()/"Settings/Input.json";static std::string loadedInput;static nlohmann::json input;
        if(loadedInput!=file.string()){loadedInput=file.string();input=nlohmann::json::parse(FileSystem::ReadText(file),nullptr,false);if(!input.is_object())input={{"Horizontal",{{"Positive",68},{"Negative",65},{"AlternatePositive",262},{"AlternateNegative",263},{"GamepadAxis",0}}},{"Vertical",{{"Positive",87},{"Negative",83},{"AlternatePositive",265},{"AlternateNegative",264},{"GamepadAxis",1}}},{"Jump",{{"Positive",32},{"GamepadButton",0}}},{"Submit",{{"Positive",257},{"GamepadButton",0}}},{"Cancel",{{"Positive",256},{"GamepadButton",1}}}};}
        bool save=false;static std::string newAction;ImGui::InputText("Action Name",&newAction);ImGui::SameLine();if(ImGui::Button("Add Action")&&!newAction.empty()){input[newAction]={{"Positive",32},{"Negative",0},{"GamepadAxis",-1},{"GamepadButton",-1},{"Deadzone",.15f},{"Invert",false}};save=true;}
        std::string removeInput,renameFrom,renameTo;for(auto& [name,action]:input.items())if(ImGui::TreeNode(name.c_str())){
            auto renamed=name;if(ImGui::InputText("Name",&renamed,ImGuiInputTextFlags_EnterReturnsTrue)&&!renamed.empty()&&renamed!=name&&!input.contains(renamed)){renameFrom=name;renameTo=renamed;}
            for(const char* key:{"Positive","Negative","AlternatePositive","AlternateNegative"}){int value=action.value(key,0);if(ImGui::InputInt(key,&value)){action[key]=std::clamp(value,0,348);save=true;}}
            int controller=action.value("GamepadIndex",0);if(ImGui::SliderInt("Controller",&controller,0,15)){action["GamepadIndex"]=controller;save=true;}
            int axis=action.value("GamepadAxis",-1)+1;if(ImGui::Combo("Gamepad Axis",&axis,"None\0Left X\0Left Y\0Right X\0Right Y\0Left Trigger\0Right Trigger\0")){action["GamepadAxis"]=axis-1;save=true;}
            int button=action.value("GamepadButton",-1);if(ImGui::SliderInt("Gamepad Button",&button,-1,14)){action["GamepadButton"]=button;save=true;}
            float deadzone=action.value("Deadzone",.15f);if(ImGui::SliderFloat("Deadzone",&deadzone,0,.95f)){action["Deadzone"]=deadzone;save=true;}bool invert=action.value("Invert",false);if(ImGui::Checkbox("Invert",&invert)){action["Invert"]=invert;save=true;}
            ImGui::TextDisabled("Keyboard codes: Space 32, A-Z 65-90, Enter 257, arrows 262-265.");if(ImGui::Button("Remove Action"))removeInput=name;ImGui::TreePop();}
        if(!renameFrom.empty()){input[renameTo]=input[renameFrom];input.erase(renameFrom);save=true;}
        if(!removeInput.empty()){input.erase(removeInput);save=true;}if(save)FileSystem::WriteText(file,input.dump(2));
    }
    if(section==3){cfg.layers.resize(32);for(int i=0;i<32;++i){ImGui::PushID(i);if(ImGui::InputText(std::format("Layer {}",i).c_str(),&cfg.layers[i]))changed=true;ImGui::PopID();}}
    if(section==4){
    ImGui::SeparatorText("Tags");
    int remove = -1;
    for (size_t i = 0; i < cfg.tags.size(); ++i) {
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(-40);
        bool fixed = i == 0; // "Untagged"
        ImGui::BeginDisabled(fixed);
        changed |= ImGui::InputText("##tag", &cfg.tags[i]);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = (int)i;
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (remove > 0) { cfg.tags.erase(cfg.tags.begin() + remove); changed = true; }
    if (ImGui::Button("Add Tag")) { cfg.tags.push_back("NewTag"); changed = true; }
    }
    if (changed) Project::Save();
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------ undo / redo
// The scene is snapshotted as JSON whenever it settles after a change (no mouse button held, no active widget).
// This catches every kind of edit (inspector, gizmo, hierarchy, drag & drop) without instrumenting each widget.

void EditorApp::UpdateUndo() {
    if (m_State != PlayState::Edit || !m_EditorScene) return;
    // Drawing a freshly restored or selected object can tidy its data (material slots...). That is not an edit.
    if (m_UndoRebase > 0) {
        --m_UndoRebase;
        m_UndoSnapshot = SceneSerializer::ToJson(*m_EditorScene);
        return;
    }
    const bool busy = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || m_Stroking;
    const bool gestureEnded = m_UndoWasBusy && !busy;
    const bool selectionChanged = m_UndoSelected != m_Selected;
    m_UndoWasBusy = busy;
    m_UndoTimer += DeltaTime();
    if (busy || (!gestureEnded && !selectionChanged && m_UndoTimer < 0.25f)) return;
    m_UndoTimer = 0.0f;
    m_UndoSelected = m_Selected;
    std::string now = SceneSerializer::ToJson(*m_EditorScene);
    if (m_UndoSnapshot.empty()) { m_UndoSnapshot = std::move(now); m_UndoRebase = selectionChanged ? 2 : 0; return; }
    if (now != m_UndoSnapshot) {
        m_Undo.push_back(std::move(m_UndoSnapshot));
        if (m_Undo.size() > 100) m_Undo.erase(m_Undo.begin());
        m_Redo.clear();
        m_UndoSnapshot = std::move(now);
        m_SceneDirty = true;
        if (m_AnimationPreviewScene && !m_AnimationRecording) RefreshAnimationPreview();
    }
    if (selectionChanged) m_UndoRebase = 2;
}

// Previews run on a copy of the scene: rebuild the copy so they show the current values
void EditorApp::RefreshAnimationPreview() {
    if (!m_AnimationPreviewScene || !m_EditorScene) return;
    const u64 uuid = m_AnimationPreviewScene->Reg().Valid(m_AnimationPreviewRoot) ? m_AnimationPreviewScene->Reg().Get<IDComponent>(m_AnimationPreviewRoot).uuid : 0;
    const std::string asset = m_AnimationPreviewAsset, clip = m_AnimationPreviewJson, state = m_AnimationPreviewState;
    const float time = m_AnimationPreviewTime, speed = m_AnimationPreviewSpeed;
    const bool playing = m_AnimationPreviewPlaying, recording = m_AnimationRecording;
    EntityID root = uuid ? m_EditorScene->FindByUUID(uuid) : kNullEntity;
    StopAnimationPreview();
    if (!root) return;
    StartAnimationPreview(root, asset);
    m_AnimationPreviewJson = clip;
    m_AnimationPreviewState = state;
    m_AnimationPreviewTime = time;
    m_AnimationPreviewSpeed = speed;
    m_AnimationPreviewPlaying = playing;
    m_AnimationRecording = recording;
}

void EditorApp::RestoreSnapshot(const std::string& json) {
    auto& oldRegistry = m_EditorScene->Reg();
    u64 selectedUuid = oldRegistry.Valid(m_Selected) ? oldRegistry.Get<IDComponent>(m_Selected).uuid : 0;
    std::vector<u64> selection;
    for (EntityID e : m_EntitySelection) if (oldRegistry.Valid(e)) selection.push_back(oldRegistry.Get<IDComponent>(e).uuid);
    std::string name = m_EditorScene->Name();
    auto scene = SceneSerializer::FromJson(json);
    if (!scene) return;
    scene->SetName(name);
    m_EditorScene = scene;
    m_ActiveScene = scene;
    m_Selected = selectedUuid ? scene->FindByUUID(selectedUuid) : kNullEntity;
    m_EntitySelection.clear();
    for (u64 uuid : selection) if (EntityID e = scene->FindByUUID(uuid)) m_EntitySelection.push_back(e);
    m_UndoSelected = m_Selected;
    ScriptEngine::SetEditorScene(scene.get());
    // The reloaded scene can serialize slightly differently; compare against what it writes from now on
    m_UndoSnapshot = SceneSerializer::ToJson(*scene);
    m_UndoRebase = 2;
    m_UndoWasBusy = false;
    m_SceneDirty = true;
    RefreshAnimationPreview();
}

void EditorApp::Undo() {
    if ((m_AnimationWindowFocused || m_AnimatorWindowFocused || m_AnimationRecording) && StepAnimationHistory(false)) return;
    if (m_State != PlayState::Edit) return;
    std::string now = SceneSerializer::ToJson(*m_EditorScene);
    if (!m_UndoSnapshot.empty() && now != m_UndoSnapshot) { // commit an edit newer than the last check
        m_Undo.push_back(m_UndoSnapshot);
        m_Redo.clear();
        m_UndoSnapshot = std::move(now);
    }
    if (m_Undo.empty()) return;
    std::string current = m_UndoSnapshot;
    std::string previous = std::move(m_Undo.back());
    m_Undo.pop_back();
    m_Redo.push_back(std::move(current));
    RestoreSnapshot(previous);
}

void EditorApp::Redo() {
    if ((m_AnimationWindowFocused || m_AnimatorWindowFocused || m_AnimationRecording) && StepAnimationHistory(true)) return;
    if (m_State != PlayState::Edit || m_Redo.empty()) return;
    m_Undo.push_back(m_UndoSnapshot);
    std::string next = std::move(m_Redo.back());
    m_Redo.pop_back();
    RestoreSnapshot(next);
}

} // namespace Stardust

