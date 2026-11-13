#include "Core/Application.h"
#include "Core/Log.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioEngine.h"
#include <thread>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>
#include <chrono>
#include <filesystem>

namespace Stardust {

Application::Application(const ApplicationDesc& desc) : m_Desc(desc) {
    s_Instance = this;
    m_Window = MakeScope<Window>(desc.window);
    m_Device = RHI::Device::Create(desc.device, *m_Window);
    SD_INFO("GPU: {}", m_Device->GetAdapterName());
    if (desc.imgui) InitImGui();
}

Application::~Application() {
    m_Device->WaitIdle();
    if (m_Desc.imgui) {
        m_Device->ImGuiShutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    m_Device.reset();
    m_Window.reset();
    s_Instance = nullptr;
}

void Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    LoadUiFonts(1.f);
    ImGui_ImplGlfw_InitForVulkan(m_Window->Native(), true);
    m_Device->ImGuiInit();
}

void Application::LoadUiFonts(float scale) {
    auto& io=ImGui::GetIO();
    io.FontDefault=nullptr;
    io.Fonts->Clear();

#ifdef _WIN32
    const char* uiFont = "C:/Windows/Fonts/segoeui.ttf";
#else
    const char* uiFont = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
    if (std::filesystem::exists(uiFont)) io.Fonts->AddFontFromFileTTF(uiFont, 17.0f*scale);
    else { ImFontConfig config;config.SizePixels=17.f*scale;io.Fonts->AddFontDefault(&config); }

    // Merge system fallbacks for CJK names. Ranges persist until the atlas is destroyed.
    static const ImWchar cjk[]={0x2E80,0x30FF,0x31F0,0x31FF,0x3400,0x9FFF,0xAC00,0xD7AF,0xF900,0xFAFF,0xFF00,0xFFEF,0};
    for(const char* fallback:{"C:/Windows/Fonts/meiryo.ttc","C:/Windows/Fonts/msyh.ttc","C:/Windows/Fonts/malgun.ttf"})if(std::filesystem::exists(fallback)){ImFontConfig config;config.MergeMode=true;config.OversampleH=config.OversampleV=1;io.Fonts->AddFontFromFileTTF(fallback,17.f*scale,&config,cjk);}
    // Bold face for headings (scene name, titles); falls back to the regular font
#ifdef _WIN32
    const char* boldFont = "C:/Windows/Fonts/segoeuib.ttf";
#else
    const char* boldFont = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
#endif
    s_BoldFont = std::filesystem::exists(boldFont) ? io.Fonts->AddFontFromFileTTF(boldFont, 17.0f*scale) : nullptr;
    // Monospaced face for code (Script Editor)
#ifdef _WIN32
    const char* codeFont = "C:/Windows/Fonts/consola.ttf";
#else
    const char* codeFont = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
#endif
    s_CodeFont = std::filesystem::exists(codeFont) ? io.Fonts->AddFontFromFileTTF(codeFont, 16.0f*scale) : nullptr;
    io.FontDefault = io.Fonts->Fonts[0];
}

void Application::UpdateUiScale() {
    float x=1,y=1;glfwGetWindowContentScale(m_Window->Native(),&x,&y);
    float scale=std::clamp(std::max(x,y),.75f,4.f);
    if(std::abs(scale-m_UiScale)<.01f)return;
    // Called between frames: keep the UI atlas sharp rather than stretching it.
    m_Device->WaitIdle();
    ImGui::GetStyle()=*m_UiBaseStyle;
    ImGui::GetStyle().ScaleAllSizes(scale);
    LoadUiFonts(scale);
    m_Device->ImGuiRebuildFonts();
    m_UiScale=scale;
    SD_INFO("UI DPI scale: {:.0f}%",scale*100);
}

void Application::Run() {
    try {
        RunLoop();
    } catch (...) {
        // Release GPU resources owned by the app before the device is destroyed, then report
        m_Device->WaitIdle();
        try { OnShutdown(); } catch (...) {}
        throw;
    }
}

void Application::SetRunInBackground(bool run) {
    s_RunInBackground = run;
    // Exclusive fullscreen minimizes itself when it loses focus; a game that keeps running should stay up
    if (s_Instance && s_Instance->m_Window) glfwSetWindowAttrib(s_Instance->m_Window->Native(), GLFW_AUTO_ICONIFY, run ? GLFW_FALSE : GLFW_TRUE);
}

void Application::RunLoop() {
    OnInit();
    if(m_Desc.imgui){m_UiBaseStyle=MakeScope<ImGuiStyle>(ImGui::GetStyle());UpdateUiScale();}
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    float fpsAccum = 0; int fpsFrames = 0;

    while (m_Running && !m_Window->ShouldClose()) {
        m_Window->PollEvents();
        if (m_Desc.pauseWhenInactive) {
            const bool focused = glfwGetWindowAttrib(m_Window->Native(), GLFW_FOCUSED) == GLFW_TRUE;
            if (!s_RunInBackground && (!focused || m_Window->IsMinimized())) {
                // Paused: nothing runs and the time does not jump when the player comes back
                AudioEngine::SetPaused(true);
                glfwWaitEventsTimeout(0.25);
                last = clock::now();
                continue;
            }
            AudioEngine::SetPaused(false);
            if (m_Window->IsMinimized()) {
                // Running in the background with no surface to draw on: game logic only
                auto now = clock::now();
                m_DeltaTime = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
                last = now; m_Time += m_DeltaTime;
                OnUpdate(m_DeltaTime);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
        } else while (m_Window->IsMinimized() && !m_Window->ShouldClose()) glfwWaitEvents();
        if(m_Desc.imgui)UpdateUiScale();

        auto now = clock::now();
        m_DeltaTime = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
        last = now;
        m_Time += m_DeltaTime;
        fpsAccum += m_DeltaTime; ++fpsFrames;
        if (fpsAccum >= 0.5f) { m_FPS = fpsFrames / fpsAccum; fpsAccum = 0; fpsFrames = 0; }

        if (!m_Device->BeginFrame()) continue;
        auto cpuStart = clock::now();

        if (m_Desc.imgui) {
            OnBeforeNewFrame();
            m_Device->ImGuiNewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
        }

        OnUpdate(m_DeltaTime);
        RHI::CommandList& cmd = m_Device->Cmd();
        AssetManager::RefreshMounts();
        OnRender(cmd);

        if (m_Desc.imgui) {
            OnImGui();
            ImGui::Render();
            m_Device->ImGuiRender(m_Device->BackBuffer());
        } else {
            OnPresent(cmd, m_Device->BackBuffer());
        }
        m_CpuMs = std::chrono::duration<float, std::milli>(clock::now() - cpuStart).count();
        m_Device->EndFrame();
    }

    m_Device->WaitIdle();
    OnShutdown();
}

} // namespace Stardust
