#include "EditorApp.h"
#include "Core/Log.h"
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    try {
        Stardust::EditorApp app;
        app.Run();
    } catch (const std::exception& e) {
        SD_ERROR("Fatal: {}", e.what());
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Stardust Editor - fatal error", MB_ICONERROR);
#endif
        return 1;
    }
    return 0;
}
