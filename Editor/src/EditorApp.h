#pragma once
#include "Core/Application.h"
#include "Scene/Scene.h"
#include "Renderer/SceneRenderer.h"
#include "EditorCamera.h"
#include "Renderer/Nature/TerrainData.h"
#include <filesystem>
#include <atomic>
#include <thread>

namespace Stardust {

struct ViewportState {
    glm::vec2 size{ 1280, 720 };
    glm::vec2 min{ 0, 0 };      // screen-space top-left of the image
    bool hovered = false, focused = false, visible = true;
};

enum class PlayState { Edit, Play, Pause };

class EditorApp final : public Application {
public:
    EditorApp();

protected:
    void OnInit() override;
    void OnShutdown() override;
    void OnUpdate(float dt) override;
    void OnRender(RHI::CommandList& cmd) override;
    void OnImGui() override;

private:
    // layout & chrome
    void DrawDockspace();
    void DrawMenuBar();
    void DrawToolbar();
    void BuildDefaultLayout(unsigned int dockId);
    // panels (Panels.cpp)
    void DrawControllerEditor(nlohmann::json& asset,bool& dirty);
    void DrawHierarchy();
    void SelectHierarchy(EntityID e);
    void SelectProject(const std::string& path,const std::vector<std::string>& order);
    void CopyEntities();
    void PasteEntities();
    void DeleteEntities();
    void DuplicateEntities();
    void CopyAssets();
    void PasteAssets();
    void DrawInspector();
    void DrawAssetInspector();
    void RequestDeleteAsset(const std::string& path);
    void DrawDeleteAssetDialog();
    void DrawSpriteEditor(const std::string& path);
    void DrawSpriteEditorWindow();
    void DrawAnimationEditor();
    void DrawAnimationWindow();
    void DrawLightingWindow();
    struct WorkspaceLayout { std::string name; int preset = 0; bool builtin = false; std::string ini; u32 windows = 0; bool hasWindows = false; };
    void OnBeforeNewFrame() override;
    void InitWorkspaces();
    void CaptureWorkspace();
    void SwitchWorkspace(int workspace);
    void ResetWorkspace(int workspace);
    void DeleteWorkspace(int workspace);
    void SaveWorkspaceAs(const std::string& name);
    void DrawWorkspaceTabs();
    void DrawLayoutMenu();
    void DrawWindowButtons(float menusEnd, float dragEnd);
    std::vector<bool*> LayoutWindowFlags();
    std::vector<WorkspaceLayout> m_Workspaces;
    int m_Workspace=0,m_PendingWorkspace=-1;
    float m_LayoutFade=1;
    bool m_OpenSaveLayout=false,m_UISnapEnabled=true;
    std::string m_NewLayoutName;
    glm::vec2 m_UIStartMin{0},m_UIStartMax{0};
    bool m_ShowLighting=false,m_LightingFocusRequest=false,m_StatsFocusRequest=false,m_PrefabClosePending=false;
    void DrawAnimatorWindow();
    bool StepAnimationHistory(bool redo);
    bool m_ShowAnimatorEditor=false,m_AnimationFocusRequest=false,m_AnimatorFocusRequest=false;
    bool m_AnimationWindowFocused=false,m_AnimatorWindowFocused=false;
    std::string m_AnimatorAsset;EntityID m_AnimatorRoot=0;
    void StartAnimationPreview(EntityID root,const std::string& asset="");
    void StopAnimationPreview();void UpdateAnimationPreview(float dt);
    Ref<Scene> m_AnimationPreviewScene;EntityID m_AnimationPreviewRoot=0;
    unsigned int m_AnimationDockId=0;
    bool m_AnimationTransformChanged=false;
    bool m_AnimationRecording=false;float m_AnimationRecordTime=-1;
    bool m_AnimationPreviewPlaying=false;float m_AnimationPreviewTime=0,m_AnimationPreviewSpeed=1;
    std::string m_AnimationPreviewAsset,m_AnimationPreviewJson,m_AnimationPreviewState;

    std::string m_AnimationAsset;
    bool m_ShowAnimationEditor=false;
    Scope<SceneRenderer> m_MaterialPreview;
    float m_MaterialPreviewYaw=.5f;
    std::string m_SpriteEditorPath;
    bool m_SpriteEditorOpen=false;
    void OpenPrefab(const std::filesystem::path& path);
    void SavePrefabStage();
    void ExitPrefabStage();
    void UpdatePrefabs();
    void DrawConsole();
    void DrawProject();
    void DrawProjectCreateItems();
    void DrawProjectFolderTree(const std::filesystem::path& dir, const std::string& label);
    // editor chrome (EditorChrome.cpp)
    void DecorateDockTabs();
    void DrawWindowShadows();
    bool UpdateManagedSceneTools(); // C# OnSceneGUI tools; true while one owns the mouse (ManagedSceneTools.cpp)
    void DrawStatusBar(float height);
    void* EngineIconTexture();
    Ref<RHI::Texture> m_EngineIcon;
    bool m_EngineIconLoaded = false, m_ConsoleFocusRequest = false;
    double m_ConsoleSelectTime = -1; // the status bar asks the Console to select this entry
    float m_ProjectTreeWidth = 190, m_ProjectIconSize = 92;
    // Renaming in the Project window (F2, context menu): the asset being edited and the text typed so far
    std::string m_RenamingAsset, m_RenameText;
    bool m_RenameFocus = false, m_RenameInTree = false, m_ProjectListingDirty = false, m_ProjectShowTree = true;
    void BeginRenameAsset(const std::string& relPath, bool inTree = false);
    void DrawRenameField(float width);
    bool RenameAsset(const std::string& relPath, std::string newName);
    // Moving assets keeps every reference to them (AssetRename.cpp)
    struct AssetPathChange { std::string from, to; bool folder = false; };
    bool RelocateAsset(const std::filesystem::path& oldPath, const std::filesystem::path& newPath, std::vector<AssetPathChange>& changes);
    int UpdateAssetReferences(const std::vector<AssetPathChange>& changes);
    bool MoveAssets(const std::vector<std::string>& relPaths, const std::filesystem::path& folder);
    void AcceptAssetMoveDrop(const std::filesystem::path& folder); // drop target for dragged assets and folders
    void DrawStats();
    void DrawSceneView();
    void DrawGameView();
    void DrawEntityContextItems();
    void DrawStatsOverlay(const ViewportState& vp);

    // actions
    void HandleShortcuts();
    void NewScene();
    void OpenScene();
    void OpenScene(const std::filesystem::path& path);
    void SaveScene();
    void SaveSceneAs();
    void SaveSceneAs(const std::filesystem::path& path);
    void UpdateTitle();
    void FocusSelected();
    void SaveTerrains();
    void ShellExecuteFile(const std::filesystem::path& path);
    void CreateScriptAsset(const std::filesystem::path& dir);
    // terrain tools (TerrainTools.cpp)
    TerrainBrushStamp m_BrushStamp;
    void DrawTerrainInspector(EntityID e, TerrainComponent& tc);
    void DrawProceduralTerrainInspector(EntityID e,ProceduralTerrainComponent& c);
    void DrawScriptInspector(EntityID e, ScriptSlot& sc, size_t slot = 0);
    void DrawColliderGizmos();
    void DrawCameraGizmos(); // view frustum and direction of every camera
    bool DrawCanvasTools();
    void DrawRectInspector(EntityID e,RectTransformComponent& rect);
    void DrawTextInspector(EntityID e,UITextComponent& text);
    void DrawButtonInspector(EntityID e,UIButtonComponent& button);
    void UpdateScripts();
    bool UpdateTerrainBrush(); // inside the Scene view window; returns true while the brush owns the mouse
    bool RaycastTerrain(const TerrainData& t, const glm::vec3& origin, const glm::vec3& ro, const glm::vec3& rd, glm::vec3& hit);
    void DrawEntityNode(EntityID e);
    void Play();
    void Stop();
    void SetSelected(EntityID e) { m_Selected = e; }
    // projects, build, undo (ProjectWindows.cpp)
    void DrawProjectHub();
    void OpenProject(const std::filesystem::path& file, bool isNew = false);
    void SaveProjectState();
    void DrawBuildSettings();
    void DrawProjectSettings();
    void BuildPlayer(bool run);
    void UpdateUndo();
    void Undo();
    void Redo();
    void RestoreSnapshot(const std::string& json);
    void LoadSceneInPlayMode(const std::string& assetPath);
    // drag & drop of assets onto entities / the scene (Panels.cpp)
    bool HandleAssetDropOnEntity(EntityID target, const std::string& assetPath);
    EntityID CreateFromAsset(const std::string& assetPath, EntityID parent, const glm::vec3* position);
    EntityID PickEntityCPU(const glm::vec3& ro, const glm::vec3& rd, float* distance = nullptr);
    void DrawSceneIcons(bool allowPicking=true);
    void DrawViewCube();
    Scene& ActiveScene() { return *m_ActiveScene; }

    Ref<Scene> m_EditorScene, m_ActiveScene;
    Scope<SceneRenderer> m_SceneRenderer, m_GameRenderer;
    EditorCamera m_Camera;
    RenderCamera m_LastSceneCamera; // what the Scene view drew last, for the LOD Group inspector
    bool m_HasSceneCamera = false;
    EntityID m_Selected = kNullEntity;
    std::vector<EntityID> m_EntitySelection,m_HierarchyRows,m_PreviousHierarchyRows;
    std::vector<std::string> m_AssetSelection,m_AssetClipboard;
    std::string m_EntityClipboard,m_AssetAnchor;
    EntityID m_EntityAnchor=0;
    PlayState m_State = PlayState::Edit;
    bool m_StepFrame = false;

    ViewportState m_SceneView, m_GameView;
    glm::vec2 m_GameRenderSize{1280,720};
    int m_GizmoOp = 0;          // ImGuizmo::OPERATION
    bool m_GizmoLocal = false;
    bool m_ShowGizmos=true,m_ShowTransformTools=true,m_GamePointerOwned=true;
    bool m_FirstLayout = true, m_ResetLayout = false;

    bool m_ShowHierarchy = true, m_ShowInspector = true, m_ShowConsole = true, m_ShowProject = true, m_ShowStats = false;
    bool m_ShowDemo = false;
    std::filesystem::path m_ProjectRoot, m_ProjectCurrentDir, m_ScenePath;
    std::string m_SelectedAsset;
    std::string m_DeleteAsset;
    std::filesystem::path m_PrefabAsset,m_PrefabReturnPath;
    Ref<Scene> m_PrefabReturnScene;
    u64 m_PrefabReturnSelection=0,m_PrefabTarget=0;
    int m_PrefabAction=0;
    float m_PrefabPoll=0;
    bool m_InspectAsset = false;
    bool m_ScenePickPending = false;
    EntityID m_UIRectEntity=0;
    int m_UIRectHandle=-1;
    glm::vec2 m_UIStartMouse{0},m_UIStartPosition{0},m_UIStartSize{0},m_UIStartDelta{0};
    float m_UIStartRotation=0,m_UIStartAngle=0;
    RectTransformComponent m_UIAnchorOriginal;
    EntityID m_LastSelected = kNullEntity;

    // terrain editing
    TerrainBrush m_TerrainTool = TerrainBrush::None;
    float m_BrushSize = 12.0f, m_BrushStrength = 0.5f, m_BrushHardness = 0.3f, m_BrushHeight = 0.0f;
    float m_FlatHeight = 0.0f;
    int m_PaintLayer = 0;
    bool m_Stroking = false;
    bool m_ReloadAfterStop = false;
    bool m_BuildPending = false;
    float m_ScriptPollTimer = 0.0f;
    float m_StrokeHeight = 0.0f;
    EntityID m_StrokeTerrain = kNullEntity;
    u64 m_LastLogVersion = 0;

    // projects / build / undo
    bool m_ShowHub = true, m_ShowBuildSettings = false, m_ShowProjectSettings = false, m_ShowPackageManager = false;
    void DrawPackageManager(); // PackageManagerWindow.cpp
    std::atomic<bool> m_Building{ false };
    std::thread m_PlayerBuildThread;
    std::vector<std::string> m_Undo, m_Redo;
    std::string m_UndoSnapshot;
    float m_UndoTimer = 0.0f;
    int m_UndoRebase = 0;
    bool m_UndoWasBusy = false;
    EntityID m_UndoSelected = 0;
    void RefreshAnimationPreview();
    bool m_SceneDirty = false;
};

void ApplyEditorTheme();

} // namespace Stardust
