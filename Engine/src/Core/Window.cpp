#include "Core/Window.h"
#include "Core/Input.h"
#include "Core/WindowIcon.h"
#include "Core/Log.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <stdexcept>

namespace Stardust {

static void SetDpiAwareness() {
#ifdef _WIN32
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto setContext = reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
            // A manifest or host may already have chosen the process context.
            if (GetLastError() == ERROR_ACCESS_DENIED) return;
        }
    }
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using SetAwareness = HRESULT(WINAPI*)(int);
        HRESULT result = E_NOTIMPL;
        if (auto setAwareness = reinterpret_cast<SetAwareness>(GetProcAddress(shcore, "SetProcessDpiAwareness")))
            result = setAwareness(2 /* PROCESS_PER_MONITOR_DPI_AWARE */);
        FreeLibrary(shcore);
        if (SUCCEEDED(result) || result == E_ACCESSDENIED) return;
    }
    SetProcessDPIAware();
#endif
}

Window::Window(const WindowDesc& desc) {
    SetDpiAwareness(); // Must precede GLFW initialization and every native window.
    glfwSetErrorCallback([](int code, const char* msg) { SD_ERROR("GLFW {}: {}", code, msg); });
    SD_VERIFY(glfwInit(), "glfwInit failed");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan / D3D12 own the surface
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, desc.maximized ? GLFW_TRUE : GLFW_FALSE);
    m_Window = glfwCreateWindow((int)desc.width, (int)desc.height, desc.title.c_str(), desc.fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);
    SD_VERIFY(m_Window, "glfwCreateWindow failed");
    WindowIcon::ApplyEmbedded(m_Window);

    int w, h;
    glfwGetFramebufferSize(m_Window, &w, &h);
    m_Width = (u32)w; m_Height = (u32)h;

    glfwSetWindowUserPointer(m_Window, this);
    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* wnd, int w, int h) {
        auto* self = (Window*)glfwGetWindowUserPointer(wnd);
        self->m_Width = (u32)w; self->m_Height = (u32)h;
        if (self->OnResize) self->OnResize(self->m_Width, self->m_Height);
    });
    Input::Attach(m_Window);
}

Window::~Window() {
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Window::PollEvents() {
    glfwPollEvents();
    Input::BeginFrame();
}

bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Window); }
void Window::SetTitle(const std::string& t) { glfwSetWindowTitle(m_Window, t.c_str()); }
void* Window::NativeHandle() const {
#ifdef _WIN32
 return glfwGetWin32Window(m_Window);
#else
 return m_Window;
#endif
}

} // namespace Stardust
