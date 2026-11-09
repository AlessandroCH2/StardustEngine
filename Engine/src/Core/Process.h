#pragma once
#include "Core/Base.h"
#include <filesystem>

namespace Stardust {

// Runs a command line (hidden window), streams stdout/stderr line by line and returns the exit code (-1 if it could not start)
int RunProcess(const std::wstring& commandLine, const std::filesystem::path& workingDir,
               const std::function<void(const std::string&)>& onLine);

// Starts a process without waiting (e.g. "Build And Run")
bool LaunchProcess(const std::filesystem::path& exe, const std::filesystem::path& workingDir);

} // namespace Stardust
