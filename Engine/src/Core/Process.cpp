#ifdef _WIN32
#include "Core/Process.h"
#include <windows.h>

namespace Stardust {

int RunProcess(const std::wstring& commandLine, const std::filesystem::path& workingDir,
               const std::function<void(const std::string&)>& onLine) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE readPipe, writePipe;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = si.hStdError = writePipe;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(0);
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!ok) { CloseHandle(readPipe); return -1; }
    std::string pending;
    char buf[4096];
    DWORD n;
    while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
        pending.append(buf, n);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine && line.find_first_not_of(" \t") != std::string::npos) onLine(line);
        }
    }
    if (onLine && !pending.empty()) onLine(pending);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readPipe);
    return (int)code;
}

bool LaunchProcess(const std::filesystem::path& exe, const std::filesystem::path& workingDir) {
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, workingDir.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

} // namespace Stardust

#else
#include "Core/Process.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>
#include <codecvt>
#include <locale>
namespace Stardust {
int RunProcess(const std::wstring& command,const std::filesystem::path& cwd,const std::function<void(const std::string&)>& onLine){
    std::string text=std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(command);std::vector<std::string> args;std::string word;bool quoted=false;
    for(char c:text){if(c=='"')quoted=!quoted;else if(c==' '&&!quoted){if(!word.empty()){args.push_back(word);word.clear();}}else word+=c;}if(!word.empty())args.push_back(word);if(args.empty())return -1;
    int pipes[2];if(pipe(pipes))return -1;pid_t pid=fork();if(pid<0){close(pipes[0]);close(pipes[1]);return -1;}
    if(pid==0){close(pipes[0]);dup2(pipes[1],STDOUT_FILENO);dup2(pipes[1],STDERR_FILENO);close(pipes[1]);if(!cwd.empty()&&chdir(cwd.c_str()))_exit(126);std::vector<char*> raw;for(auto& s:args)raw.push_back(s.data());raw.push_back(nullptr);execvp(raw[0],raw.data());_exit(127);}
    close(pipes[1]);char buffer[4096];std::string pending;ssize_t n;while((n=read(pipes[0],buffer,sizeof(buffer)))>0){pending.append(buffer,n);size_t end;while((end=pending.find('\n'))!=std::string::npos){if(onLine)onLine(pending.substr(0,end));pending.erase(0,end+1);}}close(pipes[0]);if(onLine&&!pending.empty())onLine(pending);int status=0;waitpid(pid,&status,0);return WIFEXITED(status)?WEXITSTATUS(status):-1;
}
bool LaunchProcess(const std::filesystem::path& executable,const std::filesystem::path& cwd){pid_t pid=fork();if(pid<0)return false;if(!pid){if(chdir(cwd.c_str()))_exit(126);execl(executable.c_str(),executable.filename().c_str(),nullptr);_exit(127);}return true;}
}
#endif
