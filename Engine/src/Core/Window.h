#pragma once
#include "Core/Base.h"

struct GLFWwindow;

namespace Stardust {

struct WindowDesc {
    std::string title = "Stardust";
    u32 width = 1600;
    u32 height = 900;
    bool maximized = false;
    bool fullscreen = false;
    bool visible = true;
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    void PollEvents();
    bool ShouldClose() const;
    void SetTitle(const std::string& title);

    u32 Width() const { return m_Width; }
    u32 Height() const { return m_Height; }
    bool IsMinimized() const { return m_Width == 0 || m_Height == 0; }
    GLFWwindow* Native() const { return m_Window; }
    void* NativeHandle() const; // HWND on Windows

    std::function<void(u32, u32)> OnResize;

private:
    GLFWwindow* m_Window = nullptr;
    u32 m_Width = 0, m_Height = 0;
};

} // namespace Stardust
