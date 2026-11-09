#pragma once
#include "Core/Base.h"
#include "Core/Window.h"
#include "RHI/RHI.h"
struct ImGuiStyle;

namespace Stardust {

struct ApplicationDesc {
    WindowDesc window;
    RHI::DeviceDesc device;
    bool imgui = true;
};

// Base class for the editor and the standalone player.
class Application {
public:
    explicit Application(const ApplicationDesc& desc);
    virtual ~Application();

    void Run();
    void Close() { m_Running = false; }

    static Application& Get() { return *s_Instance; }
    Window& GetWindow() { return *m_Window; }
    RHI::Device& GetDevice() { return *m_Device; }

    float DeltaTime() const { return m_DeltaTime; }
    double TotalTime() const { return m_Time; }
    float FPS() const { return m_FPS; }
    float CpuFrameMs() const { return m_CpuMs; }
    float UiScale() const { return m_UiScale; }

protected:
    virtual void OnInit() {}
    virtual void OnShutdown() {}
    virtual void OnBeforeNewFrame() {}                                 // the only safe moment to replace ImGui layouts
    virtual void OnUpdate(float dt) {}
    virtual void OnRender(RHI::CommandList& cmd) {}                    // offscreen GPU work
    virtual void OnImGui() {}                                          // only when desc.imgui
    virtual void OnPresent(RHI::CommandList& cmd, RHI::Texture* backBuffer) {} // only when !desc.imgui

private:
    void InitImGui();
    void LoadUiFonts(float scale);
    void UpdateUiScale();
    void RunLoop();

    static inline Application* s_Instance = nullptr;
    ApplicationDesc m_Desc;
    Scope<Window> m_Window;
    Scope<RHI::Device> m_Device;
    Scope<ImGuiStyle> m_UiBaseStyle;
    float m_UiScale = 1;
    bool m_Running = true;
    float m_DeltaTime = 0, m_FPS = 0, m_CpuMs = 0;
    double m_Time = 0;
};

} // namespace Stardust
