#pragma once
#include "Core/Base.h"
#include <filesystem>

namespace Stardust {

// Project settings stored in <ProjectName>.sdproject (JSON) at the root of a project folder:
//   <Project>/<Name>.sdproject, <Project>/Assets/..., <Project>/<Assembly>.csproj (C# scripts)
struct ContentPackConfig {std::string name="content",folder;};
struct ProjectConfig {
    std::string name = "New Project";
    std::string company = "DefaultCompany";
    std::string version = "1.0";
    std::string assemblyName = "Game";
    std::vector<std::string> scenes;       // Scenes In Build (asset-relative paths); index 0 starts the game
    std::vector<bool> scenesEnabled;
    std::vector<std::string> tags = { "Untagged", "Player", "Enemy", "MainCamera", "Respawn", "Finish" };
    std::vector<std::string> layers={"Default","TransparentFX","Ignore Raycast","","Water","UI"};
    std::string lastOpenedScene;
    u32 windowWidth = 1600, windowHeight = 900;
    bool fullscreen = false;
    bool vsync = true;
    int quality = 2; // 0 Low, 1 Medium, 2 High (see Renderer/Quality.h)
    std::string buildTarget="Windows";
    std::string icon; // Asset-relative PNG/JPEG/TGA/BMP; empty uses the engine icon.
    std::string lastBuildFolder;
    std::vector<ContentPackConfig> contentPacks;
    std::string pakKey; // passphrase that encrypts the built paks; empty leaves them readable. Never written into a build.
};

class Project {
public:
    static bool Load(const std::filesystem::path& sdprojectFile);
    static bool Create(const std::filesystem::path& folder, const std::string& name); // new folder structure + C# project
    static bool Save();
    static bool WriteRuntime(const std::filesystem::path& file,const ProjectConfig& config);
    static bool LoadRuntime(const std::filesystem::path& dataDir);
    static bool IsLoaded() { return s_Loaded; }

    static ProjectConfig& Config() { return s_Config; }
    static const std::filesystem::path& Dir() { return s_Dir; }
    static const std::filesystem::path& File() { return s_File; }
    static std::filesystem::path AssetsDir() { return s_AssetsOverride.empty() ? s_Dir / "Assets" : s_AssetsOverride; }
    static std::filesystem::path ScriptProjectFile() { return s_Dir / (s_Config.assemblyName + ".csproj"); }
    static std::filesystem::path ScriptAssembly();   // compiled game scripts
    // Built players keep everything under Data/ next to the executable
    static void SetRuntimeLayout(const std::filesystem::path& assetsDir, const std::filesystem::path& assembly);

    // Scenes In Build helpers
    static std::vector<std::string> EnabledScenes();
    static std::string ResolveScene(const std::string& nameOrPath); // "Level2", "Scenes/Level2.scene" or index as text

    // Recently opened projects (for the Project Hub), stored in %APPDATA%/Stardust/projects.json
    static std::vector<std::filesystem::path> RecentProjects();
    static void AddRecent(const std::filesystem::path& file);
    static void RemoveRecent(const std::filesystem::path& file);

    static void WriteScriptProject(const std::filesystem::path& csproj, const std::string& assemblyName);
    static void WriteEditorScriptProject();

private:
    static inline bool s_Loaded = false;
    static inline ProjectConfig s_Config;
    static inline std::filesystem::path s_Dir, s_File, s_AssetsOverride, s_AssemblyOverride;
};

} // namespace Stardust
