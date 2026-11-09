#ifdef _WIN32
#include "FileDialogs.h"
#include "Core/Log.h"
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <thread>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace Stardust::FileDialogs {
namespace {
using Microsoft::WRL::ComPtr;
using Selection = std::optional<std::filesystem::path>;

void Report(const char* operation, HRESULT hr) {
    SD_ERROR("File dialog {} failed (HRESULT 0x{:08X})", operation, (unsigned long)hr);
}

// Audio and the hosted CLR can initialize the editor thread as MTA. Shell UI requires
// STA: never continue after RPC_E_CHANGED_MODE or change the caller's COM apartment.
// Pump the owner thread while waiting: Show() sends synchronous messages to its HWND.
// Joining the worker without this message pump would deadlock the modal dialog.
template<class Function>
Selection OnDialogThread(Function function) {
    HANDLE complete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!complete) { Report("CreateEvent", HRESULT_FROM_WIN32(GetLastError())); return {}; }
    Selection result;
    std::thread worker;
    try {
        worker = std::thread([&] {
            SD_TRACE("File dialog: initializing STA");
            HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            SD_TRACE("File dialog: COM result 0x{:08X}",(unsigned long)init);
            if (SUCCEEDED(init)) {
                try { result = function(); }
                catch (const std::exception& e) { SD_ERROR("File dialog failed: {}", e.what()); }
                catch (...) { SD_ERROR("File dialog failed unexpectedly"); }
                CoUninitialize();
            } else Report("STA initialization", init);
            SetEvent(complete);
        });
    } catch (const std::exception& e) {
        CloseHandle(complete); SD_ERROR("Cannot start file dialog: {}", e.what()); return {};
    }
    bool quit = false; int quitCode = 0;
    while (MsgWaitForMultipleObjectsEx(1, &complete, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE) != WAIT_OBJECT_0) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { quit = true; quitCode = (int)message.wParam; continue; }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    worker.join();
    CloseHandle(complete);
    if (quit) PostQuitMessage(quitCode);
    return result;
}

Selection Run(bool save, void* owner, const wchar_t* filter, const wchar_t* defExt,
              const std::filesystem::path& initialDir) {
    return OnDialogThread([&]() -> Selection {
        wchar_t file[32768] = {};
        std::wstring dir = initialDir.wstring();
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (HWND)owner;
        ofn.lpstrFile = file;
        ofn.nMaxFile = (DWORD)std::size(file);
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
        ofn.lpstrDefExt = defExt;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER |
                    (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
        if (!ok) { auto error=CommDlgExtendedError(); if(error)Report("open/save",HRESULT_FROM_WIN32(error)); return {}; }
        return std::filesystem::path(file);
    });
}
}

std::optional<std::filesystem::path> OpenFile(void* owner, const wchar_t* filter, const std::filesystem::path& initialDir) {
    return Run(false, owner, filter, nullptr, initialDir);
}

std::optional<std::filesystem::path> PickFolder(void* owner, const std::filesystem::path& initialDir) {
    return OnDialogThread([&]() -> Selection {
        ComPtr<IFileOpenDialog> dialog;
        SD_TRACE("Folder dialog: creating shell dialog");
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        SD_TRACE("Folder dialog: create result 0x{:08X}",(unsigned long)hr);
        if (FAILED(hr)) { Report("create folder picker", hr); return {}; }
        DWORD options = 0;
        hr = dialog->GetOptions(&options);
        if (FAILED(hr)) { Report("folder options", hr); return {}; }
        hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        if (FAILED(hr)) { Report("set folder options", hr); return {}; }
        SD_TRACE("Folder dialog: setting start folder");
        ComPtr<IShellItem> start;
        if (!initialDir.empty() && SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&start))))
            dialog->SetFolder(start.Get());
        SD_TRACE("Folder dialog: showing");
        hr = dialog->Show((HWND)owner);
        SD_TRACE("Folder dialog: show result 0x{:08X}",(unsigned long)hr);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
        if (FAILED(hr)) { Report("show folder picker", hr); return {}; }
        ComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);
        if (FAILED(hr)) { Report("folder result", hr); return {}; }
        PWSTR path = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (FAILED(hr)) { Report("folder path", hr); return {}; }
        Selection result;
        try { result = std::filesystem::path(path); }
        catch (...) { CoTaskMemFree(path); throw; }
        CoTaskMemFree(path);
        return result;
    });
}

std::optional<std::filesystem::path> SaveFile(void* owner, const wchar_t* filter, const wchar_t* defaultExt,
                                              const std::filesystem::path& initialDir) {
    return Run(true, owner, filter, defaultExt, initialDir);
}
}

#else
// Linux: native dialogs come from zenity or kdialog, whichever the desktop has
#include "FileDialogs.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Stardust::FileDialogs {
namespace {
using Selection = std::optional<std::filesystem::path>;

std::string Quote(std::string text) {
    for (size_t at = 0; (at = text.find('\'', at)) != std::string::npos; at += 4) text.replace(at, 1, "'\\''");
    return "'" + text + "'";
}
std::string Narrow(const wchar_t* text) { std::string out; for (; text && *text; ++text) out += (char)*text; return out; }

// "Name (*.ext)\0*.ext\0" becomes the name and pattern pairs of the Windows filter format
std::vector<std::pair<std::string, std::string>> ParseFilter(const wchar_t* filter) {
    std::vector<std::pair<std::string, std::string>> out;
    while (filter && *filter) {
        std::string name = Narrow(filter);
        filter += name.size() + 1;
        if (!*filter) break;
        std::string pattern = Narrow(filter);
        filter += pattern.size() + 1;
        for (char& c : pattern) if (c == ';') c = ' ';
        out.push_back({ name, pattern });
    }
    return out;
}
bool HasProgram(const char* name) { return std::system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0; }

Selection Run(const std::string& command) {
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (!pipe) return std::nullopt;
    std::string output;
    char buffer[4096];
    while (size_t n = fread(buffer, 1, sizeof(buffer), pipe)) output.append(buffer, n);
    if (pclose(pipe) != 0) return std::nullopt; // cancelled
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    if (output.empty()) return std::nullopt;
    return std::filesystem::u8path(output);
}

enum class Mode { Open, Save, Folder };
Selection Show(Mode mode, const wchar_t* filter, const std::filesystem::path& start) {
    const auto filters = ParseFilter(filter);
    std::string startPath = FileSystem::Utf8(start);
    if (mode != Mode::Folder && !startPath.empty() && startPath.back() != '/') startPath += '/';
    if (HasProgram("zenity")) {
        std::string command = "zenity --file-selection --filename=" + Quote(startPath);
        if (mode == Mode::Save) command += " --save --confirm-overwrite";
        if (mode == Mode::Folder) command += " --directory";
        for (auto& [name, pattern] : filters) command += " --file-filter=" + Quote(name + " | " + pattern);
        return Run(command);
    }
    if (HasProgram("kdialog")) {
        std::string patterns;
        for (auto& [name, pattern] : filters) patterns += (patterns.empty() ? "" : "\n") + pattern + "|" + name;
        const char* action = mode == Mode::Open ? "--getopenfilename" : mode == Mode::Save ? "--getsavefilename" : "--getexistingdirectory";
        std::string command = std::string("kdialog ") + action + " " + Quote(startPath);
        if (mode != Mode::Folder && !patterns.empty()) command += " " + Quote(patterns);
        return Run(command);
    }
    SD_ERROR("File dialogs need zenity or kdialog: install one of them");
    return std::nullopt;
}
}

std::optional<std::filesystem::path> OpenFile(void*, const wchar_t* filter, const std::filesystem::path& initialDir) { return Show(Mode::Open, filter, initialDir); }
std::optional<std::filesystem::path> PickFolder(void*, const std::filesystem::path& initialDir) { return Show(Mode::Folder, nullptr, initialDir); }
std::optional<std::filesystem::path> SaveFile(void*, const wchar_t* filter, const wchar_t* defaultExt, const std::filesystem::path& initialDir) {
    auto selected = Show(Mode::Save, filter, initialDir);
    if (selected && !selected->has_extension() && defaultExt && *defaultExt) *selected += "." + Narrow(defaultExt);
    return selected;
}
} // namespace Stardust::FileDialogs
#endif
