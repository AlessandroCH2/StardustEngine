#include "EditorApp.h"
#include "Core/Profiler.h"
#include "Version.h"
#include "Renderer/Quality.h"
#include "TitleBar.h"
#include "Core/FileSystem.h"
#include "EditorIcons.h"
#include "GizmoMath.h"
#include "SceneViewOverlay.h"
#include "ManagedEditorUI.h"
#include "Core/Log.h"
#include "Core/Input.h"
#include "Core/FileSystem.h"
#include "Scene/SceneSerializer.h"
#include "FileDialogs.h"
#include "AssetUI.h"
#include "Assets/AssetManager.h"
#include "Assets/Pak.h"
#include "Scripting/ScriptEngine.h"
#include "Physics/PhysicsWorld.h"
#include "Project/Project.h"
#include "Audio/AudioEngine.h"
#include "Scene/SceneManager.h"
#include "UI/CanvasRenderer.h"
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <fstream>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Stardust {

static ApplicationDesc MakeDesc() {
    ApplicationDesc d;
    d.window.title = "Stardust Editor";
    d.window.width = 1700;
    d.window.height = 950;
    d.window.maximized = true;
#ifndef NDEBUG
    d.device.validation = true;
#endif
    d.device.vsync = true;
    d.imgui = true;
    return d;
}

EditorApp::EditorApp() : Application(MakeDesc()) {}

void EditorApp::OnInit() {
    ScriptEngine::SetEditorMode(true);
    ApplyEditorTheme();
    MeshLibrary::Init(GetDevice());
    m_SceneRenderer = MakeScope<SceneRenderer>(GetDevice());
    m_GameRenderer = MakeScope<SceneRenderer>(GetDevice());
    m_SceneRenderer->drawGrid = true;

    PhysicsWorld::InitJolt();
    AudioEngine::Init();
    m_GizmoOp = ImGuizmo::TRANSLATE;

    // The bundled sample project is always offered in the hub
    auto sample = FileSystem::EngineRoot() / "SandboxProject" / "Sandbox.sdproject";
    if (std::filesystem::exists(sample)) {
        auto recent = Project::RecentProjects();
        if (std::find(recent.begin(), recent.end(), std::filesystem::absolute(sample)) == recent.end()) Project::AddRecent(sample);
    }
    // StardustEditor.exe <project.sdproject> or --project <file> opens it directly, otherwise the Project Hub is shown
#ifdef _WIN32
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::filesystem::path> arguments(argv + std::min(argc, 1), argv + argc);
    LocalFree(argv);
#else
    std::vector<std::filesystem::path> arguments;
    {
        std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
        std::string argument;
        for (bool first = true; std::getline(cmdline, argument, '\0'); first = false) if (!first) arguments.push_back(argument);
    }
#endif
    for (size_t i = 0; i < arguments.size(); ++i) {
        std::filesystem::path a = arguments[i];
        if (a == "--project" && i + 1 < arguments.size()) a = arguments[++i];
        if (a.extension() == ".sdproject" && std::filesystem::exists(a)) { OpenProject(a); break; }
    }
    InitWorkspaces();
    TitleBar::Install(GetWindow().NativeHandle());
    SD_INFO("Stardust Editor ready");
}

void EditorApp::OnShutdown() {
    if (!m_ShowHub && m_ActiveScene) CaptureWorkspace();
    StopAnimationPreview();
    SceneManager::CancelLoads();
    m_MaterialPreview.reset();
    ScriptEngine::SetEditorScene(nullptr);
    CanvasRenderer::Clear();
    if (m_PlayerBuildThread.joinable()) m_PlayerBuildThread.join();
    if (m_State != PlayState::Edit) Stop();
    SaveProjectState();
    AudioEngine::Shutdown();
    ScriptEngine::Shutdown();
    m_SceneRenderer.reset();
    m_GameRenderer.reset();
    m_ActiveScene.reset();
    m_EditorScene.reset();
    TerrainData::ClearCache();
    AssetManager::Shutdown();
    PhysicsWorld::ShutdownJolt();
    MeshLibrary::Shutdown();Pak::UnmountAll();
}

void EditorApp::NewScene() {
    StopAnimationPreview();
    if(!m_PrefabAsset.empty())ExitPrefabStage();
    if (m_State != PlayState::Edit) Stop();
    TerrainData::ClearCache();
    m_EditorScene = MakeRef<Scene>();
    m_EditorScene->SetName("Untitled");
    m_EditorScene->PopulateDefault();
    m_ActiveScene = m_EditorScene;
    m_Selected = kNullEntity;m_EntitySelection.clear();
    m_ScenePath.clear();
    m_Undo.clear(); m_Redo.clear(); m_UndoSnapshot.clear();
    m_SceneDirty = false;
    UpdateTitle();
}

void EditorApp::OpenScene() {
    if (auto p = FileDialogs::OpenFile(GetWindow().NativeHandle(), L"Stardust Scene (*.scene)\0*.scene\0", m_ProjectRoot / "Scenes"))
        OpenScene(*p);
}

void EditorApp::OpenScene(const std::filesystem::path& path) {
    StopAnimationPreview();
    if(!m_PrefabAsset.empty())ExitPrefabStage();
    if (m_State != PlayState::Edit) Stop();
    TerrainData::ClearCache();
    auto scene = SceneSerializer::Load(path);
    if (!scene) return;
    m_EditorScene = scene;
    m_ActiveScene = scene;
    m_ScenePath = path;
    m_Selected = kNullEntity;m_EntitySelection.clear();
    m_Undo.clear(); m_Redo.clear(); m_UndoSnapshot.clear();
    m_SceneDirty = false;
    SaveProjectState();
    UpdateTitle();
}

void EditorApp::SaveScene() {
    if(!m_PrefabAsset.empty()){SavePrefabStage();return;}
    if (m_ScenePath.empty()) { SaveSceneAs(); return; }
    SaveTerrains();
    AssetManager::SaveDirtyMaterials();
    if (SceneSerializer::Save(*m_EditorScene, m_ScenePath)) m_SceneDirty = false;
    SaveProjectState();
    UpdateTitle();
}

void EditorApp::SaveSceneAs() {
    if (auto p = FileDialogs::SaveFile(GetWindow().NativeHandle(), L"Stardust Scene (*.scene)\0*.scene\0", L"scene", m_ProjectRoot / "Scenes"))
        SaveSceneAs(*p);
}

void EditorApp::SaveSceneAs(const std::filesystem::path& path) {
    m_EditorScene->SetName(FileSystem::Utf8(path.stem()));
    SaveTerrains();
    AssetManager::SaveDirtyMaterials();
    if (SceneSerializer::Save(*m_EditorScene, path)) {
        m_ScenePath = path;
        m_SceneDirty = false;
        SaveProjectState();
        UpdateTitle();
    }
}

// Sculpted / painted terrains are stored next to the scene as binary .terrain files
void EditorApp::SaveTerrains() {
    Registry& reg = m_EditorScene->Reg();
    reg.Each<TerrainComponent>([&](EntityID e, TerrainComponent& tc) {
        u64 uuid = reg.Get<IDComponent>(e).uuid;
        auto data = TerrainData::Find(uuid);
        if (!data) return;
        if (tc.dataPath.empty()) tc.dataPath = std::format("Terrains/{}_{:016x}.terrain", m_EditorScene->Name(), uuid);
        if (data->Save(AssetManager::ToAbsolute(tc.dataPath))) SD_INFO("Terrain saved: {}", tc.dataPath);
    });
}

void EditorApp::ShellExecuteFile(const std::filesystem::path& path) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string quoted = FileSystem::Utf8(path);
    for (size_t at = 0; (at = quoted.find('\'', at)) != std::string::npos; at += 4) quoted.replace(at, 1, "'\\''");
    if (std::system(("xdg-open '" + quoted + "' >/dev/null 2>&1 &").c_str()) != 0) SD_WARN("Could not open {}", FileSystem::Utf8(path));
#endif
}

void EditorApp::CreateScriptAsset(const std::filesystem::path& dir) {
    std::filesystem::path p = dir / "NewBehaviour.cs";
    for (int n = 1; std::filesystem::exists(p); ++n) p = dir / std::format("NewBehaviour{}.cs", n);
    std::string cls = FileSystem::Utf8(p.stem());
    FileSystem::WriteText(p, std::format(R"(using Stardust;

public class {} : ScriptBehaviour
{{
    public float Speed = 1.0f;

    void OnCreate()
    {{
    }}

    void OnUpdate(float dt)
    {{
    }}
}}
)", cls));
    SD_INFO("Script created: {} (Scripts > Build to compile)", AssetManager::ToRelative(p));
}

void EditorApp::UpdateTitle() {
    std::string file = m_ScenePath.empty() ? "Untitled" : FileSystem::Utf8(m_ScenePath.filename());
    std::string project = Project::IsLoaded() ? Project::Config().name : "No project";
    GetWindow().SetTitle(std::format("Stardust Editor " STARDUST_VERSION_FULL " - {} - {}{} - Vulkan", project, file, m_SceneDirty ? "*" : ""));
}

void EditorApp::Play() {
    StopAnimationPreview();
    if (m_State != PlayState::Edit) return;
    m_ActiveScene = Scene::Copy(m_EditorScene);
    m_ActiveScene->PrepareTerrains(GetDevice());
    SceneManager::SetActiveSceneName(m_EditorScene->Name());
    m_ActiveScene->OnRuntimeStart();
    m_State = PlayState::Play;
    ImGui::SetWindowFocus("Game");
    SD_INFO("Play mode");
}

void EditorApp::Stop() {
    SceneManager::CancelLoads();
    if (m_State == PlayState::Edit) return;
    CanvasRenderer::Clear();
    m_ActiveScene->OnRuntimeStop();
    m_ActiveScene = m_EditorScene;
    m_State = PlayState::Edit;ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouse;
    Input::SetCursorMode(CursorMode::Normal);
    if (m_ReloadAfterStop) { m_ReloadAfterStop = false; ScriptEngine::ReloadGameAssembly(); }
    if (!m_ActiveScene->Reg().Valid(m_Selected)) m_Selected = kNullEntity;
    ImGui::SetWindowFocus("Scene");
    SD_INFO("Edit mode");
}

// ------------------------------------------------------------------------------------------------ frame

// Gameplay asked for another scene (SceneManager.LoadScene): swap the running copy, the edited scene is untouched
void EditorApp::LoadSceneInPlayMode(const std::string& assetPath) {
    auto scene = SceneSerializer::Load(AssetManager::ToAbsolute(assetPath));
    if (!scene) return;
    GetDevice().WaitIdle();CanvasRenderer::Clear();
    m_ActiveScene->ReplaceContents(*scene,GetDevice());
    SceneManager::SetActiveSceneName(scene->Name());
    m_Selected = kNullEntity;m_EntitySelection.clear();
    SD_INFO("Loaded scene '{}' (play mode)", scene->Name());
}

void EditorApp::OnUpdate(float dt) {
    Profiler::EndFrame();
    UpdateAnimationPreview(dt);
    if (m_ShowHub || !m_ActiveScene) return;
    if (m_State == PlayState::Play || (m_State == PlayState::Pause && m_StepFrame)) {
        auto& io=ImGui::GetIO();auto mouse=io.MousePos;auto* game=ImGui::FindWindowByName("Game");auto* hovered=ImGui::GetCurrentContext()->HoveredWindow;
        bool overGame=game&&hovered==game&&m_GameView.visible&&mouse.x>=m_GameView.min.x&&mouse.y>=m_GameView.min.y&&mouse.x<m_GameView.min.x+m_GameView.size.x&&mouse.y<m_GameView.min.y+m_GameView.size.y&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
        bool captured=Input::GetCursorMode()==CursorMode::Locked&&m_GamePointerOwned&&!m_Camera.IsFlying()&&Input::WindowFocused()&&m_GameView.visible;
        if(Input::IsKeyPressed(Key::Escape)){m_GamePointerOwned=false;captured=false;Input::SetCursorMode(CursorMode::Normal);}
        if(!captured&&(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1)||ImGui::IsMouseClicked(2)))m_GamePointerOwned=overGame;
        bool gameplayPointer=(captured||overGame)&&Input::WindowFocused();
        // GLFW cursor coordinates are relative to the main window's client area;
        // viewport rectangles are in desktop coordinates (including decorations).
        auto mainOrigin=ImGui::GetMainViewport()->Pos;
        Input::RuntimeMouse=gameplayPointer&&m_GamePointerOwned;Input::RuntimeKeyboard=gameplayPointer&&!io.WantTextInput;Input::RuntimeOrigin=m_GameView.min-glm::vec2(mainOrigin.x,mainOrigin.y);
        if(!gameplayPointer&&!m_Camera.IsFlying())Input::SetCursorMode(CursorMode::Normal);
        ScriptEngine::SetScreenSize(m_GameRenderSize.x, m_GameRenderSize.y);
        m_ActiveScene->OnUpdate(dt);
        std::string load;
        SceneManager::UpdateAsync([&](Ref<Scene> next){GetDevice().WaitIdle();CanvasRenderer::Clear();m_ActiveScene->ReplaceContents(*next,GetDevice());SceneManager::SetActiveSceneName(next->Name());m_Selected=0;return true;});
        if (SceneManager::TakeLoadRequest(load)) LoadSceneInPlayMode(load);
        if (SceneManager::TakeQuitRequest()) { SD_INFO("Application.Quit() called: leaving play mode"); Stop(); }
    }
    m_StepFrame = false;
    // Outside Play mode the selected Particle System (and its children) plays in the Scene view
    if (m_State == PlayState::Edit && !m_AnimationPreviewScene) UpdateParticlePreview(*m_ActiveScene, m_Selected, dt);
    UpdateUndo();
    if (m_SceneDirty) { static bool shown = false; if (!shown) { UpdateTitle(); shown = true; } }

    bool gameplayLocked=m_State==PlayState::Play&&Input::RuntimeMouse&&Input::GetCursorMode()==CursorMode::Locked&&!m_Camera.IsFlying();
    // Locked virtual cursor coordinates must not click editor widgets or start Scene navigation.
    if(gameplayLocked)ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NoMouse;else ImGui::GetIO().ConfigFlags&=~ImGuiConfigFlags_NoMouse;
    if(!gameplayLocked)m_Camera.Update(dt,m_SceneView.hovered,m_SceneView.focused);
    HandleShortcuts();
    UpdateScripts();
    UpdatePrefabs();
    ScriptEngine::SetEditorScene(m_EditorScene.get());

    u32 picked;
    if (m_SceneRenderer->PollPick(picked) && m_ScenePickPending) {
        m_Selected = m_ActiveScene->Reg().Valid(picked) ? picked : kNullEntity;
        m_EntitySelection=m_Selected?std::vector<EntityID>{m_Selected}:std::vector<EntityID>{};m_InspectAsset = false; m_ScenePickPending = false;
    }
    if (m_Selected != m_LastSelected) {
        if (m_Selected) m_InspectAsset = false;
        if (!m_ActiveScene->Reg().Valid(m_Selected) || !m_ActiveScene->Reg().Has<TerrainComponent>(m_Selected)) m_TerrainTool = TerrainBrush::None;
        m_LastSelected = m_Selected;
    }

    if (Log::Version() != m_LastLogVersion) m_LastLogVersion = Log::Version();
}

void EditorApp::OnRender(RHI::CommandList& cmd) {
    if(m_ActiveScene&&!m_ShowHub)ActiveScene().RenderCameraTargets(GetDevice(),cmd,(float)TotalTime());
    if (m_ShowHub || !m_ActiveScene) return;
    Scene& scene = m_AnimationPreviewScene?*m_AnimationPreviewScene:ActiveScene();
    EntityID selected=m_Selected;if(m_AnimationPreviewScene&&ActiveScene().Reg().Valid(m_Selected))selected=scene.FindByUUID(ActiveScene().Reg().Get<IDComponent>(m_Selected).uuid);
    m_SceneRenderer->selectedEntity=m_ShowGizmos&&scene.Reg().Valid(selected)?selected:0;if(scene.Reg().Has<RectTransformComponent>(selected)||scene.Reg().Has<CanvasComponent>(selected))m_SceneRenderer->selectedEntity=0;
    EnvironmentSettings env = scene.GatherEnvironment();
    float time = (float)TotalTime();

    if(m_InspectAsset&&m_SelectedAsset.ends_with(".mat"))if(auto material=AssetManager::GetMaterial(m_SelectedAsset)){
        if(!m_MaterialPreview)m_MaterialPreview=MakeScope<SceneRenderer>(GetDevice());m_MaterialPreview->Resize(256,256);m_MaterialPreview->drawGrid=false;RenderCamera camera;camera.position={std::sin(m_MaterialPreviewYaw)*1.7f,.6f,std::cos(m_MaterialPreviewYaw)*1.7f};camera.view=glm::lookAt(camera.position,glm::vec3(0),glm::vec3(0,1,0));camera.projection=PerspectiveReverseZ(camera.fovY,1,camera.nearPlane);EnvironmentSettings light;light.sunIntensity=3;light.exposure=1;light.bloomIntensity=0;light.fogDensity=0;light.cloudCoverage=0;m_MaterialPreview->BeginScene(camera,light,time);m_MaterialPreview->Submit(MeshLibrary::Get(PrimitiveType::Sphere),glm::mat4(1),*material,0,false);m_MaterialPreview->Render(cmd);
    }
    if (m_SceneView.visible) {
        m_SceneRenderer->Resize((u32)m_SceneView.size.x, (u32)m_SceneView.size.y);
        float aspect = m_SceneView.size.x / std::max(1.0f, m_SceneView.size.y);
        m_LastSceneCamera = m_Camera.GetRenderCamera(aspect); m_HasSceneCamera = true;
        m_SceneRenderer->BeginScene(m_LastSceneCamera, env, time);
        { SD_PROFILE("Scene view: submit"); scene.SubmitToRenderer(*m_SceneRenderer, GetDevice(),bool(m_AnimationPreviewScene));
          CanvasRenderer::Submit(scene,*m_SceneRenderer,GetDevice(),m_SceneView.size,true,m_Camera.Is2D()); }
        SD_PROFILE("Scene view: record");
        bool grid=m_SceneRenderer->drawGrid;
        m_SceneRenderer->grid2D=m_Camera.Is2D();
        m_SceneRenderer->Render(cmd);
        m_SceneRenderer->drawGrid=grid;
    }
    if (m_GameView.visible) {
        { float scale = GraphicsQuality::Current().renderScale; m_GameRenderer->Resize((u32)(m_GameRenderSize.x * scale), (u32)(m_GameRenderSize.y * scale)); }
        float aspect = m_GameRenderSize.x / std::max(1.0f, m_GameRenderSize.y);
        RenderCamera cam;
        if (!scene.GetPrimaryCamera(cam, aspect)) cam = m_Camera.GetRenderCamera(aspect);
        m_GameRenderer->BeginScene(cam, env, time);
        { SD_PROFILE("Game view: submit"); scene.SubmitToRenderer(*m_GameRenderer, GetDevice(),bool(m_AnimationPreviewScene));
          CanvasRenderer::Submit(scene,*m_GameRenderer,GetDevice(),m_GameRenderSize,false); }
        SD_PROFILE("Game view: record");
        m_GameRenderer->Render(cmd);
    }
}

void EditorApp::HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (m_Camera.IsFlying() || io.WantTextInput) return;
    bool ctrl = Input::IsKeyDown(Key::LeftControl) || Input::IsKeyDown(Key::RightControl);

    if (ctrl && Input::IsKeyPressed(Key::P)) { m_State == PlayState::Edit ? Play() : Stop(); return; }
    bool shift = Input::IsKeyDown(Key::LeftShift) || Input::IsKeyDown(Key::RightShift);
    if (ctrl && Input::IsKeyPressed(Key::N)) { NewScene(); return; }
    if (ctrl && Input::IsKeyPressed(Key::O)) { OpenScene(); return; }
    if (ctrl && shift && Input::IsKeyPressed(Key::B)) { m_ShowBuildSettings = true; return; }
    if (ctrl && Input::IsKeyPressed(Key::B)) { ScriptEngine::BuildAsync(); return; }
    bool animationFocus = m_AnimationWindowFocused || m_AnimatorWindowFocused;
    if (ctrl && Input::IsKeyPressed(Key::Z)) { shift ? Redo() : Undo(); return; }
    if (ctrl && Input::IsKeyPressed(Key::Y)) { Redo(); return; }
    if (ctrl && Input::IsKeyPressed(Key::S)) {
        if (m_State != PlayState::Edit) { SD_WARN("Exit play mode before saving the scene"); return; }
        shift ? SaveSceneAs() : SaveScene();
        return;
    }

    if (animationFocus) return; // those windows use Delete, F, A... for their own keys

    if (m_SceneView.hovered || m_SceneView.focused) {
        if (Input::IsKeyPressed(Key::W)) m_GizmoOp = ImGuizmo::TRANSLATE;
        if (Input::IsKeyPressed(Key::E)) m_GizmoOp = ImGuizmo::ROTATE;
        if (Input::IsKeyPressed(Key::R)) m_GizmoOp = ImGuizmo::SCALE;
    }
    Registry& reg = ActiveScene().Reg();
    if (reg.Valid(m_Selected)) {
        if (Input::IsKeyPressed(Key::F)) FocusSelected();
        if (!m_InspectAsset && Input::IsKeyPressed(Key::Delete)) { DeleteEntities(); }
        else if (!m_InspectAsset && ctrl && Input::IsKeyPressed(Key::D)) DuplicateEntities();
    }
}

// ------------------------------------------------------------------------------------------------ UI

void EditorApp::OnImGui() {
    AssetUI::SetEditorContext(&GetDevice(), m_ActiveScene.get());
    if (m_ShowHub || !m_ActiveScene) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 6));
        bool bar = ImGui::BeginMainMenuBar();
        ImGui::PopStyleVar();
        if (bar) { DrawWindowButtons(0, ImGui::GetWindowWidth()); ImGui::EndMainMenuBar(); }
        DrawProjectHub();
        return;
    }
    // Panels ease in after a layout change, which hides the rearrangement
    struct FadeScope { bool active; FadeScope(float alpha) : active(alpha < 1) { if (active) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha); } ~FadeScope() { if (active) ImGui::PopStyleVar(); } };
    if (m_LayoutFade < 1) m_LayoutFade = std::min(1.0f, m_LayoutFade + ImGui::GetIO().DeltaTime / 0.18f);
    FadeScope fade(m_LayoutFade * m_LayoutFade * (3 - 2 * m_LayoutFade));
    SD_PROFILE("Editor UI");
    DrawDockspace();
    DrawBuildSettings();
    DrawProjectSettings();
    DrawPackageManager();
    if (m_ShowHierarchy) DrawHierarchy();
    if (m_ShowInspector) DrawInspector();
    if (m_ShowProject) DrawProject();
    if (m_ShowConsole) DrawConsole();
    if (m_ShowStats) DrawStats();
    DrawLightingWindow();
    DrawGameView();
    DrawSceneView();
    DrawManagedWindows();
    if (m_ShowDemo) ImGui::ShowDemoWindow(&m_ShowDemo);
    DecorateDockTabs(); // last: every tab bar of this frame has been submitted
    DrawWindowShadows();
}

void EditorApp::DrawDockspace() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 6)); // sets the height of the menu / title bar
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(14 / 255.0f, 15 / 255.0f, 17 / 255.0f, 1));
    ImGui::Begin("##EditorRoot", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    ImGui::GetCurrentWindow()->DC.MenuBarOffset.y = 4;

    DrawMenuBar();
    DrawToolbar();
    DrawDeleteAssetDialog();
    DrawSpriteEditorWindow();

    ImGuiID dockId = ImGui::GetID("EditorDockspace");
    if (m_FirstLayout || m_ResetLayout) {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
        if (m_ResetLayout || !node || node->IsLeafNode()) BuildDefaultLayout(dockId);
        m_FirstLayout = m_ResetLayout = false;
    }
    const float statusHeight = std::round(ImGui::GetFontSize() + 9 * UiScale());
    // The status bar follows after the item spacing: leave room for both so nothing overflows
    ImGui::DockSpace(dockId, ImVec2(0, -(statusHeight + ImGui::GetStyle().ItemSpacing.y)), ImGuiDockNodeFlags_None);
    DrawStatusBar(statusHeight);
    DrawAnimationEditor(); // after DockSpace: ImGui undocks windows submitted before their host
    ImGui::End();
}

void EditorApp::BuildDefaultLayout(unsigned int dockId) {
    const int preset = m_Workspaces.empty() ? 0 : m_Workspaces[m_Workspace].preset;
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockId, bottom = 0, right = 0, left = 0, assets = 0, side = 0;
    auto split = [&](ImGuiDir direction, float size) { return ImGui::DockBuilderSplitNode(center, direction, size, nullptr, &center); };
    if (preset == 1) {        // timeline across the whole width
        bottom = split(ImGuiDir_Down, 0.42f);
        right = split(ImGuiDir_Right, 0.24f);
        left = split(ImGuiDir_Left, 0.22f);
        assets = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f, nullptr, &left);
    } else if (preset == 2) { // Lighting gets its own column
        right = split(ImGuiDir_Right, 0.22f);
        side = split(ImGuiDir_Right, 0.26f);
        left = split(ImGuiDir_Left, 0.2f);
        assets = bottom = split(ImGuiDir_Down, 0.28f);
    } else if (preset == 3) { // measurements get half the screen
        bottom = split(ImGuiDir_Down, 0.5f);
        right = split(ImGuiDir_Right, 0.24f);
        left = split(ImGuiDir_Left, 0.2f);
        assets = bottom;
    } else {
        right = split(ImGuiDir_Right, 0.24f);
        left = split(ImGuiDir_Left, 0.2f);
        assets = bottom = split(ImGuiDir_Down, 0.3f);
    }
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Project", assets);
    ImGui::DockBuilderDockWindow("Console", assets);
    ImGui::DockBuilderDockWindow("Animation", bottom);
    ImGui::DockBuilderDockWindow("Animator", bottom);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Stats", preset == 3 ? bottom : right);
    ImGui::DockBuilderDockWindow("Lighting", side ? side : right);
    ImGui::DockBuilderFinish(dockId);
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    // Engine icon before the menus
    if (void* icon = EngineIconTexture()) {
        const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();
        const float size = std::round(bar.GetHeight() - 12 * UiScale());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4 * UiScale());
        // Centered on the menu labels' line, which sits lower than the bar's middle (MenuBarOffset)
        const float textCenter = ImGui::GetCursorScreenPos().y + ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset + ImGui::GetFontSize() * 0.5f;
        const ImVec2 at(ImGui::GetCursorScreenPos().x, std::round(textCenter - size * 0.5f));
        ImGui::Dummy({ size, 1 });
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)icon, at, { at.x + size, at.y + size });
        ImGui::SameLine(0, 4 * UiScale());
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project / Open Project...")) { SaveProjectState(); m_ShowHub = true; }
        ImGui::Separator();
        if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) OpenScene();
        ImGui::Separator();
        bool editing = m_State == PlayState::Edit;
        if (ImGui::MenuItem("Save", "Ctrl+S", false, editing)) SaveScene();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, editing)) SaveSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B")) m_ShowBuildSettings = true;
        if (ImGui::MenuItem("Project Settings...")) m_ShowProjectSettings = true;
        if (ImGui::MenuItem("Show Project Folder")) ShellExecuteFile(Project::Dir());
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) Close();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_State == PlayState::Edit && !m_Undo.empty())) Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_State == PlayState::Edit && !m_Redo.empty())) Redo();
        ImGui::Separator();
        Registry& reg = ActiveScene().Reg();
        bool sel = reg.Valid(m_Selected);
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, sel)) DuplicateEntities();
        if (ImGui::MenuItem("Delete", "Del", false, sel)) { DeleteEntities(); }
        if (ImGui::MenuItem("Frame Selected", "F", false, sel)) FocusSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Play", "Ctrl+P", m_State != PlayState::Edit)) m_State == PlayState::Edit ? Play() : Stop();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        DrawEntityContextItems();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scripts")) {
        if (ImGui::MenuItem("Build Scripts", "Ctrl+B", false, !ScriptEngine::IsBuilding())) ScriptEngine::BuildAsync();
        if (ImGui::MenuItem("Reload Scripts", nullptr, false, m_State == PlayState::Edit)) ScriptEngine::ReloadGameAssembly();
        ImGui::Separator();
        if (ImGui::MenuItem("New C# Script")) CreateScriptAsset(m_ProjectRoot / "Scripts");
        if (ImGui::MenuItem("Open Scripts Folder")) ShellExecuteFile(m_ProjectRoot / "Scripts");
        for (const auto& command : ScriptEngine::EditorCommands()) if (ImGui::MenuItem(command.c_str())) ScriptEngine::RunEditorCommand(command);
        if (ImGui::MenuItem("Open Project in IDE")) ShellExecuteFile(ScriptEngine::ProjectDir() / "Sandbox.csproj");
        ImGui::Separator();
        ImGui::TextDisabled("%s, %zu script classes", ScriptEngine::IsReady() ? ".NET ready" : ".NET unavailable", ScriptEngine::Classes().size());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
        ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
        ImGui::MenuItem("Project", nullptr, &m_ShowProject);
        ImGui::MenuItem("Console", nullptr, &m_ShowConsole);
        ImGui::MenuItem("Stats", nullptr, &m_ShowStats);
        ImGui::MenuItem("Package Manager", nullptr, &m_ShowPackageManager);
        if (ImGui::MenuItem("Lighting", nullptr, m_ShowLighting)) { m_ShowLighting = !m_ShowLighting; m_LightingFocusRequest = m_ShowLighting; }
        if(ImGui::MenuItem("Animation",nullptr,m_ShowAnimationEditor)){m_ShowAnimationEditor=!m_ShowAnimationEditor;m_AnimationFocusRequest=m_ShowAnimationEditor;}
        if(ImGui::MenuItem("Animator",nullptr,m_ShowAnimatorEditor)){m_ShowAnimatorEditor=!m_ShowAnimatorEditor;m_AnimatorFocusRequest=m_ShowAnimatorEditor;}
        ImGui::Separator();
        DrawLayoutMenu();
        ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::TextDisabled("Stardust Engine " STARDUST_VERSION_FULL " - Vulkan 1.3");
        ImGui::TextDisabled("%s", GetDevice().GetAdapterName());
        ImGui::EndMenu();
    }

    // Project and scene in the middle, transport controls on the right
    {
        // "Project - Scene - Stardust 0.6.4" with a chevron like Unity's title; it opens the project info
        std::string title = (Project::IsLoaded() ? Project::Config().name + " - " : std::string()) + ActiveScene().Name() + (m_SceneDirty ? "*" : "") + " - Stardust " STARDUST_VERSION;
        float windowWidth = ImGui::GetWindowWidth(), buttonSize = ImGui::GetFrameHeight();
        const float menusEnd = ImGui::GetCursorScreenPos().x + 8, transportX = windowWidth - 46 * 3 - 28 - (buttonSize + 6) * 3;
        {
            const float textWidth = ImGui::CalcTextSize(title.c_str()).x, chevron = 16 * UiScale(), width = textWidth + chevron + 20 * UiScale();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 12, (windowWidth - width) * 0.5f));
            const ImRect bar = ImGui::GetCurrentWindow()->MenuBarRect();
            const ImVec2 a(ImGui::GetCursorScreenPos().x, bar.Min.y + 4 * UiScale()), b(a.x + width, bar.Max.y - 4 * UiScale());
            ImGui::SetCursorScreenPos(a);
            const bool clicked = ImGui::InvisibleButton("##projectTitle", { width, b.y - a.y });
            const bool hovered = ImGui::IsItemHovered(), open = ImGui::IsPopupOpen("##projectInfo");
            auto* draw = ImGui::GetWindowDrawList();
            if (hovered || open) draw->AddRectFilled(a, b, IM_COL32(44, 47, 53, 255), 4);
            const float textY = (a.y + b.y - ImGui::GetFontSize()) * 0.5f;
            draw->AddText({ a.x + 10 * UiScale(), textY }, IM_COL32(200, 204, 211, 255), title.c_str());
            const ImVec2 c(a.x + 10 * UiScale() + textWidth + chevron * 0.5f + 2, (a.y + b.y) * 0.5f + 1);
            const float r = 3.5f * UiScale();
            draw->AddLine({ c.x - r, c.y - r * 0.5f }, { c.x, c.y + r * 0.5f }, IM_COL32(170, 174, 182, 255), 1.5f);
            draw->AddLine({ c.x, c.y + r * 0.5f }, { c.x + r, c.y - r * 0.5f }, IM_COL32(170, 174, 182, 255), 1.5f);
            TitleBar::SetDragHole(a.x, b.x);
            if (clicked) ImGui::OpenPopup("##projectInfo");
            ImGui::SetNextWindowPos({ (a.x + b.x) * 0.5f, b.y + 4 }, ImGuiCond_Appearing, { 0.5f, 0 });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            if (ImGui::BeginPopup("##projectInfo")) {
                ImFont* bold = Application::BoldFont();
                if (bold) ImGui::PushFont(bold);
                ImGui::TextUnformatted(Project::IsLoaded() ? Project::Config().name.c_str() : "No project");
                if (bold) ImGui::PopFont();
                if (Project::IsLoaded()) ImGui::TextDisabled("%s", FileSystem::Utf8(Project::Dir()).c_str());
                ImGui::Spacing();
                ImGui::Text("Scene: %s%s", ActiveScene().Name().c_str(), m_SceneDirty ? " (unsaved)" : "");
                ImGui::TextDisabled("Stardust Engine " STARDUST_VERSION_FULL);
                ImGui::TextDisabled("%s - Vulkan 1.3", GetDevice().GetAdapterName());
                ImGui::Separator();
                if (ImGui::MenuItem("Show Project Folder")) ShellExecuteFile(Project::Dir());
                if (ImGui::MenuItem("Project Settings...")) m_ShowProjectSettings = true;
                if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B")) m_ShowBuildSettings = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Switch Project...")) { SaveProjectState(); m_ShowHub = true; }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
        }
        ImGui::SetCursorPosX(transportX);
        auto transport = [&](const char* id, int glyph, bool active, const char* tip) {
            ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(47 / 255.0f, 128 / 255.0f, 237 / 255.0f, 1) : ImVec4(0, 0, 0, 0));
            bool pressed = ImGui::Button(id, { buttonSize, buttonSize });
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(), c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            const float r = buttonSize * 0.24f;
            ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            auto* draw = ImGui::GetWindowDrawList();
            if (glyph == 0) draw->AddTriangleFilled({ c.x - r * 0.8f, c.y - r }, { c.x + r, c.y }, { c.x - r * 0.8f, c.y + r }, color);
            if (glyph == 1) { draw->AddRectFilled({ c.x - r, c.y - r }, { c.x - r * 0.25f, c.y + r }, color); draw->AddRectFilled({ c.x + r * 0.25f, c.y - r }, { c.x + r, c.y + r }, color); }
            if (glyph == 2) { draw->AddTriangleFilled({ c.x - r, c.y - r }, { c.x + r * 0.5f, c.y }, { c.x - r, c.y + r }, color); draw->AddRectFilled({ c.x + r * 0.5f, c.y - r }, { c.x + r, c.y + r }, color); }
            if (glyph == 3) draw->AddRectFilled({ c.x - r, c.y - r }, { c.x + r, c.y + r }, color);
            return pressed;
        };
        bool playing = m_State != PlayState::Edit;
        if (transport("##play", playing ? 3 : 0, playing, playing ? "Stop (Ctrl+P)" : "Play (Ctrl+P)")) playing ? Stop() : Play();
        ImGui::BeginDisabled(!playing);
        if (transport("##pause", 1, m_State == PlayState::Pause, "Pause")) m_State = m_State == PlayState::Pause ? PlayState::Play : PlayState::Pause;
        if (transport("##step", 2, false, "Step one frame") && m_State == PlayState::Pause) m_StepFrame = true;
        ImGui::EndDisabled();
        DrawWindowButtons(menusEnd, transportX - 8);
    }
    ImGui::EndMenuBar();
}

void EditorApp::DrawToolbar() {
    // Unity-style toolbar: transform tools on the left, play controls centered
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6*UiScale(), 4*UiScale()));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(19 / 255.0f, 20 / 255.0f, 23 / 255.0f, 1));
    ImGui::BeginChild("##Toolbar", ImVec2(0, ImGui::GetFrameHeight()+10*UiScale()), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    DrawWorkspaceTabs();

    ImGui::SameLine(ImGui::GetWindowWidth() - 230.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s  |  %.0f fps", ActiveScene().Name().c_str(), FPS());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ------------------------------------------------------------------------------------------------ viewports

static void BeginViewportWindow(const char* name, ViewportState& vp, bool& open) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    open = ImGui::Begin(name);
    ImGui::PopStyleVar();
    vp.visible = open;
    if (!open) return;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    vp.size = { std::max(avail.x, 8.0f), std::max(avail.y, 8.0f) };
    ImVec2 p = ImGui::GetCursorScreenPos();
    vp.min = { p.x, p.y };
    vp.focused = ImGui::IsWindowFocused();
    vp.hovered = ImGui::IsWindowHovered();
}

void EditorApp::DrawSceneView() {
    bool open;
    BeginViewportWindow("Scene", m_SceneView, open);
    if (!open) { m_SceneView.hovered = false; ImGui::End(); return; }

    auto toolButton = [this](const char* label, bool active, const char* tip) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool pressed = ImGui::Button(label, ImVec2(0, ImGui::GetFrameHeight()));
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return pressed;
    };
    if (!m_PrefabAsset.empty()) {
        bool validRoot = ActiveScene().GetRoots().size() == 1;
        ImGui::BeginDisabled(!validRoot);
        if (ImGui::Button("<##closePrefab")) m_PrefabClosePending = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Save and return to the scene");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Scene  >");
        ImGui::SameLine();
        ImGui::TextUnformatted(FileSystem::Utf8(m_PrefabAsset.stem()).c_str());
        if (!validRoot) { ImGui::SameLine(); ImGui::TextColored({ 1, 0.65f, 0.3f, 1 }, "A prefab needs exactly one root object"); }
        float buttons = ImGui::CalcTextSize("Save Prefab").x + ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 4 + 8;
        ImGui::SameLine(ImGui::GetWindowWidth() - buttons - 8);
        ImGui::BeginDisabled(!validRoot);
        if (ImGui::Button("Save Prefab")) SavePrefabStage();
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Close")) m_PrefabClosePending = true;
        ImGui::EndDisabled();
    }
    if (EditorIcons::ToolButton("##MoveTool",EditorIcons::Tool::Move,m_GizmoOp == ImGuizmo::TRANSLATE,"Move (W)")) m_GizmoOp = ImGuizmo::TRANSLATE;
    ImGui::SameLine(0, 2);
    if (EditorIcons::ToolButton("##RotateTool",EditorIcons::Tool::Rotate,m_GizmoOp == ImGuizmo::ROTATE,"Rotate (E)")) m_GizmoOp = ImGuizmo::ROTATE;
    ImGui::SameLine(0, 2);
    if (EditorIcons::ToolButton("##ScaleTool",EditorIcons::Tool::Scale,m_GizmoOp == ImGuizmo::SCALE,"Scale (R)")) m_GizmoOp = ImGuizmo::SCALE;
    ImGui::SameLine(0, 10);
    if (toolButton(m_GizmoLocal ? " Local " : " Global ", false, "Toggle gizmo space")) m_GizmoLocal = !m_GizmoLocal;
    ImGui::SameLine();ImGui::Checkbox("Gizmos",&m_ShowGizmos);ImGui::SameLine();ImGui::Checkbox("Handles",&m_ShowTransformTools);
    ImGui::SameLine(0, 10);
    if (toolButton(" UI Snap ", m_UISnapEnabled, "Snap UI rectangles to the canvas, parent and siblings (hold Alt to bypass)")) m_UISnapEnabled = !m_UISnapEnabled;
    ImGui::SameLine(0, 4);
    if (toolButton(" Grid ", m_SceneRenderer->drawGrid, "Show the ground grid in the Scene view")) m_SceneRenderer->drawGrid = !m_SceneRenderer->drawGrid;
    ImGui::SameLine(0, 10);
    if (toolButton(" 2D ", m_Camera.Is2D(), "Orthographic XY view: right/middle mouse pans, wheel zooms")) m_Camera.Set2D(!m_Camera.Is2D());
    ImVec2 sceneAvail=ImGui::GetContentRegionAvail(),sceneOrigin=ImGui::GetCursorScreenPos();
    m_SceneView.size={std::max(8.f,sceneAvail.x),std::max(8.f,sceneAvail.y)};m_SceneView.min={sceneOrigin.x,sceneOrigin.y};
    RHI::Texture* out = m_SceneRenderer->Output();
    ImGui::Image((ImTextureID)GetDevice().ImGuiTexture(out), ImVec2(m_SceneView.size.x, m_SceneView.size.y));
    m_SceneView.hovered=ImGui::IsItemHovered();
    // Assets dropped into the Scene view: meshes / prefabs are created under the cursor, materials go onto the object hit
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(AssetUI::kAssetPayload)) {
            std::string asset((const char*)p->Data, p->DataSize - 1);
            ImVec2 m = ImGui::GetMousePos();
            RenderCamera cam = m_Camera.GetRenderCamera(m_SceneView.size.x / m_SceneView.size.y);
            glm::mat4 invVP = glm::inverse(cam.projection * cam.view);
            glm::vec2 ndc((m.x - m_SceneView.min.x) / m_SceneView.size.x * 2.0f - 1.0f, 1.0f - (m.y - m_SceneView.min.y) / m_SceneView.size.y * 2.0f);
            glm::vec4 a4 = invVP * glm::vec4(ndc, 1.0f, 1.0f), b4 = invVP * glm::vec4(ndc, 0.5f, 1.0f);
            glm::vec3 rd = glm::normalize(glm::vec3(b4) / b4.w - glm::vec3(a4) / a4.w);
            glm::vec3 ro=m_Camera.Is2D() ? glm::vec3(a4)/a4.w : cam.position;
            float dist = 0;
            EntityID hitEntity = PickEntityCPU(ro, rd, &dist);
            glm::vec3 point = ro + rd * 8.0f;
            Registry& r = ActiveScene().Reg();
            bool terrainHit = false;
            r.Each<TerrainComponent>([&](EntityID te, TerrainComponent&) {
                auto td = TerrainData::Find(r.Get<IDComponent>(te).uuid);
                glm::vec3 h;
                if (!terrainHit && td && RaycastTerrain(*td, glm::vec3(ActiveScene().GetWorldMatrix(te)[3]), ro, rd, h)) { point = h; terrainHit = true; }
            });
            if (hitEntity && (!terrainHit || dist < glm::length(point - ro))) point = ro + rd * dist;
            if (!(hitEntity && HandleAssetDropOnEntity(hitEntity, asset))) {
                EntityID created = CreateFromAsset(asset, kNullEntity, &point);
                if (created) m_Selected = created;
            }
        }
        ImGui::EndDragDropTarget();
    }

    {
    SceneViewOverlayClip overlayClip({m_SceneView.min.x,m_SceneView.min.y},{m_SceneView.min.x+m_SceneView.size.x,m_SceneView.min.y+m_SceneView.size.y});
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(m_SceneView.min.x,m_SceneView.min.y,m_SceneView.size.x,m_SceneView.size.y);
    // Terrain brush (owns the mouse while a terrain tool is active)
    bool brushActive = !m_AnimationPreviewScene&&UpdateTerrainBrush();
    brushActive |= UpdateManagedSceneTools();

    // Gizmo
    Scene& gizmoScene=m_AnimationRecording&&m_AnimationPreviewScene?*m_AnimationPreviewScene:ActiveScene();
    Registry& reg = gizmoScene.Reg();
    bool gizmoActive = brushActive;
    bool rectTools=m_ShowGizmos&&m_ShowTransformTools&&!m_AnimationPreviewScene&&DrawCanvasTools();gizmoActive|=rectTools;
    if (m_ShowGizmos && m_ShowTransformTools && (!m_AnimationPreviewScene||m_AnimationRecording) && !brushActive && reg.Valid(m_Selected) && !reg.Has<RectTransformComponent>(m_Selected) && !m_Camera.IsFlying()) {
        ImGuizmo::SetOrthographic(m_Camera.Is2D());
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_SceneView.min.x, m_SceneView.min.y, m_SceneView.size.x, m_SceneView.size.y);
        glm::mat4 view = m_Camera.View();
        glm::mat4 proj = m_Camera.GizmoProjection(m_SceneView.size.x / m_SceneView.size.y);
        glm::mat4 model = gizmoScene.GetWorldMatrix(m_Selected);
        glm::mat4 previousModel=model;
        bool snap = Input::IsKeyDown(Key::LeftControl);
        float snapValue = m_GizmoOp == ImGuizmo::ROTATE ? 15.0f : m_GizmoOp == ImGuizmo::SCALE ? 0.1f : 0.5f;
        float snaps[3] = { snapValue, snapValue, snapValue };
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), (ImGuizmo::OPERATION)m_GizmoOp,
            m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(model), nullptr, snap ? snaps : nullptr);
        if (ImGuizmo::IsUsing()) {
            glm::mat4 delta=model*glm::inverse(previousModel);
            std::vector<std::pair<EntityID,glm::mat4>> changes;
            auto selected=m_EntitySelection;if(std::find(selected.begin(),selected.end(),m_Selected)==selected.end())selected={m_Selected};
            for(auto target:selected)if(reg.Valid(target)){bool child=false;for(auto parent=gizmoScene.GetParent(target);parent;parent=gizmoScene.GetParent(parent))if(std::find(selected.begin(),selected.end(),parent)!=selected.end()){child=true;break;}if(!child)changes.push_back({target,delta*gizmoScene.GetWorldMatrix(target)});}
            for(auto& [target,matrix]:changes)gizmoScene.SetWorldMatrix(target,matrix);
            if(m_AnimationRecording)m_AnimationTransformChanged=true;
        }
        gizmoActive |= ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        if(gizmoActive)m_ScenePickPending=false;
    }

    // Click to select (entity id buffer readback)
    if (!m_AnimationPreviewScene && m_SceneView.hovered && !gizmoActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 m = ImGui::GetMousePos();
        float x = m.x - m_SceneView.min.x, y = m.y - m_SceneView.min.y;
        if (x >= 0 && y >= 0 && x < m_SceneView.size.x && y < m_SceneView.size.y)
        { m_SceneRenderer->RequestPick((u32)x, (u32)y); m_ScenePickPending = true; }
    }

    if(m_ShowGizmos){DrawColliderGizmos();DrawCameraGizmos();}
    if (!m_AnimationPreviewScene && !gizmoActive && !m_UIRectEntity && !ImGui::IsDragDropActive()) if (auto clicked = CanvasRenderer::HandleInput(ActiveScene(),m_SceneView.min,m_SceneView.size,m_Camera.GetRenderCamera(m_SceneView.size.x/m_SceneView.size.y),true,false)) { bool occluded=false;
        if(!m_Camera.Is2D()){
            auto camera=m_Camera.GetRenderCamera(m_SceneView.size.x/m_SceneView.size.y);auto mouse=ImGui::GetMousePos();glm::vec2 ndc{(mouse.x-m_SceneView.min.x)/m_SceneView.size.x*2-1,1-(mouse.y-m_SceneView.min.y)/m_SceneView.size.y*2};auto inv=glm::inverse(camera.projection*camera.view);auto p=inv*glm::vec4(ndc,.5f,1);auto ray=glm::normalize(glm::vec3(p)/p.w-camera.position);float distance;auto hit=PickEntityCPU(camera.position,ray,&distance);
            for(auto& rect:CanvasRenderer::Layout(ActiveScene(),m_SceneView.size,true))if(rect.entity==clicked){glm::vec2 point;if(CanvasRenderer::MouseOnCanvas(ActiveScene(),rect.canvas,camera,m_SceneView.min,m_SceneView.size,{mouse.x,mouse.y},point,rect.canvasSize)){auto world=glm::vec3(CanvasRenderer::CanvasWorld(ActiveScene(),rect.canvas,rect.canvasSize)*glm::vec4(point,0,1));occluded=hit&&distance<glm::length(world-camera.position)-.01f;}break;}
        }
        if(!occluded){m_Selected=clicked;m_EntitySelection={clicked};m_InspectAsset=false;m_ScenePickPending=false;} }
    if(m_ShowGizmos)DrawSceneIcons(!gizmoActive&&!ImGuizmo::IsUsing()&&!ImGuizmo::IsOver()&&!ImGui::IsDragDropActive());
    if (m_ShowGizmos && !m_Camera.Is2D()) DrawViewCube();
    DrawStatsOverlay(m_SceneView);
    if (ScriptEngine::IsBuilding()) {
        ImGui::SetCursorScreenPos(ImVec2(m_SceneView.min.x + 10, m_SceneView.min.y + 10));
        ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Compiling scripts...");
    }
    } // scene image overlay clip
    ImGui::End();
}

// Script build results and hot reload when the game assembly changes on disk
void EditorApp::UpdateScripts() {
    bool ok;
    if (ScriptEngine::TakeBuildFinished(ok)) {
        if (ok) {
            if (m_State == PlayState::Edit) ScriptEngine::ReloadGameAssembly();
            else { m_ReloadAfterStop = true; SD_INFO("Scripts will reload when play mode stops"); }
        }
        if (m_BuildPending) { m_BuildPending = false; ScriptEngine::BuildAsync(); } // saved again while compiling
    }
    m_ScriptPollTimer += DeltaTime();
    if (m_ScriptPollTimer > 1.0f) {
        m_ScriptPollTimer = 0.0f;
        // Saving a .cs file recompiles automatically (like Unity); the reload follows when the build succeeds
        SD_PROFILE("Script file check");
        if (ScriptEngine::IsReady() && ScriptEngine::CheckForSourceChange()) {
            if (ScriptEngine::IsBuilding()) m_BuildPending = true;
            else { SD_INFO("Script change detected, compiling..."); ScriptEngine::BuildAsync(); }
        } else if (!ScriptEngine::IsBuilding() && ScriptEngine::CheckForAssemblyChange()) {
            if (m_State == PlayState::Edit) ScriptEngine::ReloadGameAssembly();
            else m_ReloadAfterStop = true;
        }
    }
}

// Wireframe colliders of the selected entity (green, like Unity)
// Each camera shows where it looks: a short frustum for every camera, a longer one with the near plane and a
// direction arrow for the selected one. A triangle above the frustum marks the camera's up side, as in Unity.
void EditorApp::DrawCameraGizmos() {
    Scene& scene = ActiveScene();
    Registry& reg = scene.Reg();
    const RenderCamera view = m_Camera.GetRenderCamera(m_SceneView.size.x / std::max(1.0f, m_SceneView.size.y));
    const glm::mat4 vp = view.projection * view.view;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect({ m_SceneView.min.x, m_SceneView.min.y }, { m_SceneView.min.x + m_SceneView.size.x, m_SceneView.min.y + m_SceneView.size.y }, true);
    const ImVec2 viewMin(m_SceneView.min.x, m_SceneView.min.y), viewSize(m_SceneView.size.x, m_SceneView.size.y);
    auto project = [&](const glm::vec3& p, ImVec2& out) { return Gizmo::ProjectPoint(vp, p, viewMin, viewSize, out); };
    reg.Each<CameraComponent>([&](EntityID e, CameraComponent& c) {
        if (!scene.IsActiveInHierarchy(e)) return;
        const bool selected = e == m_Selected || std::find(m_EntitySelection.begin(), m_EntitySelection.end(), e) != m_EntitySelection.end();
        const ImU32 color = selected ? IM_COL32(255, 255, 255, 210) : IM_COL32(200, 205, 215, 110);
        auto line = [&](const glm::vec3& a, const glm::vec3& b, float width = 1.2f) { ImVec2 pa, pb; if (Gizmo::ProjectSegment(vp, a, b, viewMin, viewSize, pa, pb)) dl->AddLine(pa, pb, color, width); };
        glm::vec3 pos, scale, skew; glm::vec4 perspective; glm::quat rot;
        glm::decompose(scene.GetWorldMatrix(e), scale, rot, pos, skew, perspective);
        rot = glm::normalize(rot);
        const glm::vec3 forward = rot * glm::vec3(0, 0, -1), up = rot * glm::vec3(0, 1, 0), right = rot * glm::vec3(1, 0, 0);
        // Same aspect as what the camera renders: its render texture, or the Game view
        const float aspect = !c.targetTexture.empty() ? (float)std::max(1, c.targetWidth) / std::max(1, c.targetHeight)
                                                      : m_GameRenderSize.x / std::max(1.0f, m_GameRenderSize.y);
        const float nearD = std::max(0.01f, c.nearPlane);
        const float farD = selected ? std::min(c.farPlane, std::max(nearD + 1.0f, c.orthographic ? 20.0f : 12.0f)) : std::min(c.farPlane, nearD + 1.5f);
        auto halfSize = [&](float d) {
            const float h = c.orthographic ? c.orthographicSize : std::tan(glm::radians(c.fovDegrees) * 0.5f) * d;
            return glm::vec2(h * aspect, h);
        };
        auto corners = [&](float d, glm::vec3* out) {
            const glm::vec2 h = halfSize(d);
            const glm::vec3 center = pos + forward * d;
            out[0] = center - right * h.x - up * h.y; out[1] = center + right * h.x - up * h.y;
            out[2] = center + right * h.x + up * h.y; out[3] = center - right * h.x + up * h.y;
        };
        glm::vec3 nearC[4], farC[4];
        corners(nearD, nearC); corners(farD, farC);
        for (int i = 0; i < 4; ++i) {
            line(farC[i], farC[(i + 1) % 4], selected ? 1.6f : 1.2f);
            line(c.orthographic ? nearC[i] : pos, farC[i]);
            if (selected || c.orthographic) line(nearC[i], nearC[(i + 1) % 4]);
        }
        // Up marker above the far rectangle
        const glm::vec2 h = halfSize(farD);
        const glm::vec3 topCenter = pos + forward * farD + up * h.y;
        ImVec2 a, b, t;
        if (project(topCenter - right * h.x * 0.25f, a) && project(topCenter + right * h.x * 0.25f, b) && project(topCenter + up * h.y * 0.25f, t))
            dl->AddTriangleFilled(a, b, t, selected ? IM_COL32(255, 255, 255, 120) : IM_COL32(200, 205, 215, 60));
        // Direction from the camera, with an arrow head
        if (selected) {
            const glm::vec3 tip = pos + forward * std::min(farD, nearD + 3.0f);
            line(pos, tip, 2.0f);
            ImVec2 pt, pl, pr;
            if (project(tip, pt) && project(tip - forward * 0.35f + right * 0.15f, pl) && project(tip - forward * 0.35f - right * 0.15f, pr))
                dl->AddTriangleFilled(pt, pl, pr, color);
        }
    });
    dl->PopClipRect();
}

void EditorApp::DrawColliderGizmos() {
    Registry& reg = ActiveScene().Reg();
    if (!reg.Valid(m_Selected)) return;
    glm::mat4 world = ActiveScene().GetWorldMatrix(m_Selected);
    RenderCamera cam = m_Camera.GetRenderCamera(m_SceneView.size.x / m_SceneView.size.y);
    glm::mat4 vp = cam.projection * cam.view;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = IM_COL32(120, 255, 120, 200);
    auto line = [&](const glm::vec3& a, const glm::vec3& b) {
        ImVec2 pa, pb;
        if (Gizmo::ProjectSegment(vp, a, b, { m_SceneView.min.x, m_SceneView.min.y }, { m_SceneView.size.x, m_SceneView.size.y }, pa, pb)) dl->AddLine(pa, pb, col, 1.5f);
    };
    auto circle = [&](const glm::vec3& c, const glm::vec3& u, const glm::vec3& v, float r) {
        glm::vec3 prev = c + u * r;
        for (int i = 1; i <= 40; ++i) {
            float a = i / 40.0f * 6.2831853f;
            glm::vec3 p = c + (u * std::cos(a) + v * std::sin(a)) * r;
            line(prev, p);
            prev = p;
        }
    };
    glm::vec3 pos = glm::vec3(world[3]);
    glm::vec3 ax = glm::vec3(world[0]), ay = glm::vec3(world[1]), az = glm::vec3(world[2]);
    if (auto* b = reg.TryGet<BoxColliderComponent>(m_Selected)) {
        glm::vec3 c = pos + ax * b->center.x + ay * b->center.y + az * b->center.z;
        glm::vec3 hx = ax * b->size.x * 0.5f, hy = ay * b->size.y * 0.5f, hz = az * b->size.z * 0.5f;
        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i) corners[i] = c + hx * ((i & 1) ? 1.f : -1.f) + hy * ((i & 2) ? 1.f : -1.f) + hz * ((i & 4) ? 1.f : -1.f);
        const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
        for (auto& e : edges) line(corners[e[0]], corners[e[1]]);
    }
    // 2D colliders: outlines on the object's XY plane
    if (auto* b = reg.TryGet<BoxCollider2DComponent>(m_Selected)) {
        const glm::vec3 c = pos + ax * b->offset.x + ay * b->offset.y, hx = ax * b->size.x * 0.5f, hy = ay * b->size.y * 0.5f;
        line(c - hx - hy, c + hx - hy); line(c + hx - hy, c + hx + hy); line(c + hx + hy, c - hx + hy); line(c - hx + hy, c - hx - hy);
    }
    if (auto* s = reg.TryGet<CircleCollider2DComponent>(m_Selected))
        circle(pos + ax * s->offset.x + ay * s->offset.y, glm::normalize(ax), glm::normalize(ay), s->radius * std::max(glm::length(ax), glm::length(ay)));
    if (auto* cap = reg.TryGet<CapsuleCollider2DComponent>(m_Selected)) {
        const glm::vec3 c = pos + ax * cap->offset.x + ay * cap->offset.y, nx = glm::normalize(ax), ny = glm::normalize(ay);
        const float r = cap->size.x * 0.5f * glm::length(ax), half = std::max(0.0f, cap->size.y * 0.5f * glm::length(ay) - r);
        circle(c + ny * half, nx, ny, r); circle(c - ny * half, nx, ny, r);
        line(c + nx * r + ny * half, c + nx * r - ny * half); line(c - nx * r + ny * half, c - nx * r - ny * half);
    }
    float maxScale = std::max({ glm::length(ax), glm::length(ay), glm::length(az) });
    glm::vec3 nx = glm::normalize(ax), ny = glm::normalize(ay), nz = glm::normalize(az);
    if (auto* s = reg.TryGet<SphereColliderComponent>(m_Selected)) {
        glm::vec3 c = pos + ax * s->center.x + ay * s->center.y + az * s->center.z;
        float r = s->radius * maxScale;
        circle(c, nx, ny, r); circle(c, nx, nz, r); circle(c, ny, nz, r);
    }
    if (auto* cap = reg.TryGet<CapsuleColliderComponent>(m_Selected)) {
        glm::vec3 c = pos + ax * cap->center.x + ay * cap->center.y + az * cap->center.z;
        float r = cap->radius * std::max(glm::length(ax), glm::length(az));
        float half = std::max(0.0f, cap->height * 0.5f * glm::length(ay) - r);
        glm::vec3 top = c + ny * half, bottom = c - ny * half;
        circle(top, nx, nz, r); circle(bottom, nx, nz, r);
        circle(top, nx, ny, r); circle(bottom, nx, ny, r);
        circle(top, nz, ny, r); circle(bottom, nz, ny, r);
        line(top + nx * r, bottom + nx * r); line(top - nx * r, bottom - nx * r);
        line(top + nz * r, bottom + nz * r); line(top - nz * r, bottom - nz * r);
    }
    // Particle System emitter shape, in light blue; the emitter's up axis is its forward
    if (auto* ps = reg.TryGet<ParticleSystemComponent>(m_Selected); ps && ps->shapeEnabled) {
        col = IM_COL32(120, 190, 255, 210);
        const glm::vec3 c = pos + ax * ps->shapeOffset.x + ay * ps->shapeOffset.y + az * ps->shapeOffset.z;
        const float r = ps->radius * std::max(glm::length(ax), glm::length(az));
        switch (ps->shape) {
        case ParticleShape::Sphere: circle(c, nx, ny, r); circle(c, nx, nz, r); circle(c, ny, nz, r); break;
        case ParticleShape::Hemisphere: {
            circle(c, nx, nz, r);
            auto arc = [&](const glm::vec3& u) { glm::vec3 prev = c + u * r; for (int i = 1; i <= 20; ++i) { float a = glm::pi<float>() * i / 20; glm::vec3 next = c + (u * std::cos(a) + ny * std::sin(a)) * r; line(prev, next); prev = next; } };
            arc(nx); arc(nz);
            break;
        }
        case ParticleShape::Cone: {
            const float length = std::max(1.0f, r * 2.0f + 1.0f) * std::max(0.01f, glm::length(ay));
            const float top = r + std::tan(glm::radians(std::clamp(ps->angle, 0.0f, 89.0f))) * length;
            circle(c, nx, nz, r); circle(c + ny * length, nx, nz, top);
            for (int i = 0; i < 4; ++i) { const float a = glm::half_pi<float>() * i; const glm::vec3 dir = nx * std::cos(a) + nz * std::sin(a); line(c + dir * r, c + ny * length + dir * top); }
            break;
        }
        case ParticleShape::Box: {
            const glm::vec3 hx = ax * ps->boxSize.x * 0.5f, hy = ay * ps->boxSize.y * 0.5f, hz = az * ps->boxSize.z * 0.5f;
            for (int sy : { -1, 1 }) for (int sz : { -1, 1 }) line(c - hx + hy * (float)sy + hz * (float)sz, c + hx + hy * (float)sy + hz * (float)sz);
            for (int sx : { -1, 1 }) for (int sz : { -1, 1 }) line(c + hx * (float)sx - hy + hz * (float)sz, c + hx * (float)sx + hy + hz * (float)sz);
            for (int sx : { -1, 1 }) for (int sy : { -1, 1 }) line(c + hx * (float)sx + hy * (float)sy - hz, c + hx * (float)sx + hy * (float)sy + hz);
            break;
        }
        case ParticleShape::Circle: circle(c, nx, nz, r); break;
        case ParticleShape::Edge: line(c - nx * ps->radius * glm::length(ax), c + nx * ps->radius * glm::length(ax)); break;
        case ParticleShape::Point: line(c, c + ny * 0.5f); break;
        }
        if (ps->collision) {
            col = IM_COL32(255, 200, 90, 160);
            const glm::vec3 base(pos.x, ps->collisionPlane, pos.z);
            for (int i = -2; i <= 2; ++i) { line(base + glm::vec3(-2, 0, i), base + glm::vec3(2, 0, i)); line(base + glm::vec3(i, 0, -2), base + glm::vec3(i, 0, 2)); }
        }
    }
}

void EditorApp::DrawGameView() {
    bool open;
    BeginViewportWindow("Game", m_GameView, open);
    if (!open) { ImGui::End(); return; }
    static int preset=0;static int resolution[2]={1280,720};ImGui::SetNextItemWidth(150);ImGui::Combo("##Aspect",&preset,"Free Aspect\0 16:9\0 16:10\0 4:3\0 1:1\0 9:16\0 Fixed Resolution\0");ImGui::SameLine();ImGui::SetNextItemWidth(150);ImGui::InputInt2("##Resolution",resolution);resolution[0]=std::clamp(resolution[0],64,8192);resolution[1]=std::clamp(resolution[1],64,8192);
    auto avail=ImGui::GetContentRegionAvail();auto origin=ImGui::GetCursorScreenPos();float ratios[]={0,16.f/9,1.6f,4.f/3,1,9.f/16,float(resolution[0])/resolution[1]};float ratio=ratios[preset];glm::vec2 viewSize{std::max(8.f,avail.x),std::max(8.f,avail.y)};if(ratio>0){if(viewSize.x/viewSize.y>ratio)viewSize.x=viewSize.y*ratio;else viewSize.y=viewSize.x/ratio;}m_GameView.min={origin.x+(avail.x-viewSize.x)*.5f,origin.y+(avail.y-viewSize.y)*.5f};m_GameView.size=viewSize;m_GameRenderSize=preset==6?glm::vec2(resolution[0],resolution[1]):viewSize;ImGui::SetCursorScreenPos({m_GameView.min.x,m_GameView.min.y});
    ImGui::Image((ImTextureID)GetDevice().ImGuiTexture(m_GameRenderer->Output()), ImVec2(m_GameView.size.x, m_GameView.size.y));
    RenderCamera uiCamera;
    ActiveScene().GetPrimaryCamera(uiCamera,m_GameView.size.x/m_GameView.size.y);
    CanvasRenderer::HandleInput(ActiveScene(),m_GameView.min,m_GameView.size,uiCamera,false,m_State!=PlayState::Edit);
    RenderCamera dummy;
    if (!ActiveScene().GetPrimaryCamera(dummy, 1.0f)) {
        ImGui::SetCursorScreenPos(ImVec2(m_GameView.min.x + 10, m_GameView.min.y + 10));
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "No primary camera in scene - showing the editor camera");
    }
    DrawStatsOverlay(m_GameView);
    ImGui::End();
}

void EditorApp::FocusSelected() {
    Scene& scene = ActiveScene();
    if (!scene.Reg().Valid(m_Selected)) return;
    for(auto& r:CanvasRenderer::Layout(scene,m_SceneView.size,true))if(r.entity==m_Selected){auto world=CanvasRenderer::CanvasWorld(scene,r.canvas)*r.localToCanvas;auto center=glm::vec3(world*glm::vec4(r.size*.5f,0,1));float extent=std::max(glm::length(glm::vec3(world[0]))*r.size.x,glm::length(glm::vec3(world[1]))*r.size.y);m_Camera.Frame(center,extent);return;}
    glm::mat4 w = scene.GetWorldMatrix(m_Selected);
    float size = glm::length(glm::vec3(w[0])) + glm::length(glm::vec3(w[1])) + glm::length(glm::vec3(w[2]));
    m_Camera.Focus(glm::vec3(w[3]), 3.0f + size);
}

void EditorApp::DrawStatsOverlay(const ViewportState& vp) {
    float gpuMs = 0;
    for (auto& t : GetDevice().GpuTimings()) if (t.name == "Scene") gpuMs += t.ms;
    char buf[128];
    snprintf(buf, sizeof(buf), "%.1f fps   cpu %.2f ms   gpu %.2f ms", FPS(), CpuFrameMs(), gpuMs);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    ImVec2 p(vp.min.x + vp.size.x - ts.x - 14, vp.min.y + 8);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x - 6, p.y - 3), ImVec2(p.x + ts.x + 6, p.y + ts.y + 3), IM_COL32(20, 20, 20, 170), 3.0f);
    dl->AddText(p, IM_COL32(230, 230, 230, 255), buf);
}

} // namespace Stardust


