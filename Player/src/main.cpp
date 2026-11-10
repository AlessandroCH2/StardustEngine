// Stardust Player: runs a scene full-screen without the editor.
#include "Renderer/Quality.h"
#include "Version.h"
#include "Core/Application.h"
#include "Core/Log.h"
#include "Scene/Scene.h"
#include "Renderer/SceneRenderer.h"
#include "Assets/AssetManager.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptEngine.h"
#include "Physics/PhysicsWorld.h"
#include "Core/FileSystem.h"
#include "Core/WindowIcon.h"
#include "Project/Project.h"
#include "Assets/Pak.h"
#include "Scene/SceneManager.h"
#include "Audio/AudioEngine.h"
#include "Renderer/Nature/TerrainData.h"
#include "UI/CanvasRenderer.h"
#include <imgui.h>
#include <stdexcept>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Stardust {

class PlayerApp final : public Application {
public:
    PlayerApp() : Application(MakeDesc()) {}

protected:
    static ApplicationDesc MakeDesc() {
        auto data=FileSystem::ExecutableDir()/"Data";
        if(!Pak::Mount(data/"game.pak",data/"Assets")||!Project::LoadRuntime(data))throw std::runtime_error("Missing or invalid runtime metadata in Data/game.pak. Rebuild the game.");
        const auto& cfg = Project::Config();
        ApplicationDesc d;
        d.window.title = cfg.name;
        d.window.width = cfg.windowWidth;
        d.window.height = cfg.windowHeight;
        d.window.fullscreen = cfg.fullscreen;
        d.device.vsync = cfg.vsync;
        d.imgui = true;
        return d;
    }

    void OnInit() override {
        MeshLibrary::Init(GetDevice());
        auto data = FileSystem::ExecutableDir() / "Data";
        if (std::filesystem::exists(data / "packages.pak") && !Pak::Mount(data / "packages.pak", Project::Dir() / "Packages"))
            throw std::runtime_error("Invalid Data/packages.pak");
        AssetManager::Init(GetDevice(), Project::AssetsDir());
        if(!Project::Config().icon.empty())WindowIcon::Apply(GetWindow().Native(),AssetManager::ToAbsolute(Project::Config().icon));
        m_Renderer = MakeScope<SceneRenderer>(GetDevice());
        PhysicsWorld::InitJolt();
        AudioEngine::Init();
        if (!std::filesystem::is_regular_file(Project::ScriptAssembly())) throw std::runtime_error("Missing Data/Managed game assembly. Build the player again from the editor.");
        if (!ScriptEngine::Init()) throw std::runtime_error("Cannot initialize .NET scripting. Install the .NET 8 runtime and check the player files.");
        auto scenes = Project::EnabledScenes();
        if (scenes.empty() || !LoadScene(scenes.front())) throw std::runtime_error("The first enabled build scene could not be loaded.");

        RHI::GraphicsPipelineDesc blit;
        blit.name = "PresentBlit";
        blit.vs = "Fullscreen.vs"; blit.ps = "Blit.ps";
        blit.colorFormats = { RHI::Format::BGRA8_UNORM };
        blit.cull = RHI::CullMode::None;
        m_Blit = GetDevice().CreateGraphicsPipeline(blit);
    }

    void OnShutdown() override {
        SceneManager::CancelLoads();
        CanvasRenderer::Clear();
        m_Blit.reset();
        if (m_Scene) m_Scene->OnRuntimeStop();
        AudioEngine::Shutdown();
        ScriptEngine::Shutdown();
        m_Renderer.reset();
        m_Scene.reset();
        TerrainData::ClearCache();
        AssetManager::Shutdown();
        PhysicsWorld::ShutdownJolt();
        MeshLibrary::Shutdown();
        Pak::UnmountAll();
    }

    void OnUpdate(float dt) override {
        ScriptEngine::SetScreenSize((float)GetWindow().Width(), (float)GetWindow().Height());
        m_Scene->OnUpdate(dt);
        std::string path;
        SceneManager::UpdateAsync([&](Ref<Scene> next){GetDevice().WaitIdle();CanvasRenderer::Clear();if(m_Scene)m_Scene->ReplaceContents(*next,GetDevice());SceneManager::SetActiveSceneName(next->Name());return true;});
        if (SceneManager::TakeLoadRequest(path)) LoadScene(path);
        if (SceneManager::TakeQuitRequest()) Close();
    }

    void OnRender(RHI::CommandList& cmd) override {
        m_Scene->RenderCameraTargets(GetDevice(),cmd,(float)TotalTime());
        auto& w = GetWindow();
        { float scale = GraphicsQuality::Current().renderScale; m_Renderer->Resize((u32)(w.Width() * scale), (u32)(w.Height() * scale)); }
        float aspect = (float)w.Width() / std::max(1u, w.Height());
        RenderCamera cam;
        if (!m_Scene->GetPrimaryCamera(cam, aspect)) cam.projection = PerspectiveReverseZ(cam.fovY, aspect, cam.nearPlane);
        m_Renderer->BeginScene(cam, m_Scene->GatherEnvironment(), (float)TotalTime());
        m_Scene->SubmitToRenderer(*m_Renderer, GetDevice());
        CanvasRenderer::Submit(*m_Scene,*m_Renderer,GetDevice(),{w.Width(),w.Height()},false);
        m_Renderer->Render(cmd);
    }

    void OnPresent(RHI::CommandList& cmd, RHI::Texture* backBuffer) override {
        RHI::RenderingDesc rd;
        rd.colors.push_back({ backBuffer, RHI::LoadOp::DontCare });
        cmd.BeginRendering(rd);
        cmd.SetPipeline(m_Blit.get());
        cmd.Push(m_Renderer->Output()->SRV());
        cmd.Draw(3);
        cmd.EndRendering();
    }
    void OnImGui() override {
        auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
        ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::Image((ImTextureID)GetDevice().ImGuiTexture(m_Renderer->Output()), viewport->Size);
        RenderCamera camera;m_Scene->GetPrimaryCamera(camera,viewport->Size.x/viewport->Size.y);
        CanvasRenderer::HandleInput(*m_Scene,{viewport->Pos.x,viewport->Pos.y},{viewport->Size.x,viewport->Size.y},camera,false,true);
        ImGui::End(); ImGui::PopStyleVar();
    }

private:
    bool LoadScene(const std::string& path) {
        auto next = SceneSerializer::Load(AssetManager::ToAbsolute(path));
        if (!next) { SD_ERROR("Cannot load player scene: {}", path); return false; }
        GetDevice().WaitIdle();
        CanvasRenderer::Clear();
        if(m_Scene)m_Scene->ReplaceContents(*next,GetDevice());
        else {m_Scene=std::move(next);m_Scene->PrepareTerrains(GetDevice());m_Scene->OnRuntimeStart();}
        SceneManager::SetActiveSceneName(m_Scene->Name());
        return true;
    }
    Ref<Scene> m_Scene;
    Scope<SceneRenderer> m_Renderer;
    Ref<RHI::Pipeline> m_Blit;
};

} // namespace Stardust

int main() {
    try {
        SD_INFO("Stardust Player {}", STARDUST_VERSION_FULL);
        Stardust::PlayerApp app;
        app.Run();
    } catch (const std::exception& e) {
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Stardust Player - fatal error", MB_ICONERROR);
#else
        SD_ERROR("Fatal: {}",e.what());
#endif
        return 1;
    }
    return 0;
}
