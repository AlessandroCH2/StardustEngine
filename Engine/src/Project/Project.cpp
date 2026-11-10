#include "Renderer/Quality.h"
#include "Project/Project.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <charconv>
#include <fstream>
#include "Assets/Pak.h"

namespace Stardust {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

bool Project::Load(const fs::path& file) {
    std::string text;if(file.extension()==".sdruntime"){auto bytes=FileSystem::ReadBinary(file);text.assign(bytes.begin(),bytes.end());}else text=FileSystem::ReadText(file);
    if (text.empty()) { SD_ERROR("Cannot read project: {}", file.string()); return false; }
    json j = file.extension()==".sdruntime"?json::from_cbor(std::vector<u8>(text.begin(),text.end()),true,false):json::parse(text, nullptr, false);
    if (!j.is_object()) { SD_ERROR("Invalid project file: {}", file.string()); return false; }
    try {
    ProjectConfig c;
    c.name = j.value("name", c.name);
    c.company = j.value("company", c.company);
    c.version = j.value("version", c.version);
    c.assemblyName = j.value("assemblyName", c.assemblyName);
    c.lastOpenedScene = j.value("lastOpenedScene", c.lastOpenedScene);
    c.lastBuildFolder = j.value("lastBuildFolder", c.lastBuildFolder);
    if (j.contains("scenes"))
        for (auto& s : j["scenes"]) {
            if (s.is_string()) { c.scenes.push_back(s.get<std::string>()); c.scenesEnabled.push_back(true); }
            else { c.scenes.push_back(s.value("path", "")); c.scenesEnabled.push_back(s.value("enabled", true)); }
        }
    if (j.contains("tags")) c.tags = j["tags"].get<std::vector<std::string>>();
    if (j.contains("player")) {
        const json& p = j["player"];
        c.windowWidth = p.value("width", c.windowWidth);
        c.windowHeight = p.value("height", c.windowHeight);
        c.fullscreen = p.value("fullscreen", c.fullscreen);
        c.vsync = p.value("vsync", c.vsync);
        c.quality = p.value("quality", c.quality);
        GraphicsQuality::Apply(c.quality);
        c.icon = p.value("icon", c.icon);
        c.buildTarget=p.value("target",std::string("Windows"));
    }
    for(auto& p:j.value("contentPacks",json::array()))c.contentPacks.push_back({p.value("name","content"),p.value("folder","")});
    c.layers=j.value("layers",c.layers);c.layers.resize(32);
    s_Config = std::move(c);
    s_File = fs::absolute(file);
    s_Dir = s_File.parent_path();
    s_AssetsOverride.clear();
    s_AssemblyOverride.clear();
    s_Loaded = true;
    SD_INFO("Project opened: {} ({})", s_Config.name, s_Dir.string());
    return true;
    } catch (const json::exception& e) {
        SD_ERROR("Invalid project settings in {}: {}", file.string(), e.what());
        return false;
    }
}

bool Project::Save() {
    if (!s_Loaded) return false;
    json j;
    j["name"] = s_Config.name;
    j["company"] = s_Config.company;
    j["version"] = s_Config.version;
    j["assemblyName"] = s_Config.assemblyName;
    json scenes = json::array();
    for (size_t i = 0; i < s_Config.scenes.size(); ++i)
        scenes.push_back({ { "path", s_Config.scenes[i] }, { "enabled", i < s_Config.scenesEnabled.size() ? (bool)s_Config.scenesEnabled[i] : true } });
    j["scenes"] = scenes;
    j["contentPacks"]=json::array();for(auto& p:s_Config.contentPacks)j["contentPacks"].push_back({{"name",p.name},{"folder",p.folder}});
    j["tags"] = s_Config.tags;
    j["layers"]=s_Config.layers;
    j["lastOpenedScene"] = s_Config.lastOpenedScene;
    j["lastBuildFolder"] = s_Config.lastBuildFolder;
    j["player"] = { { "width", s_Config.windowWidth }, { "height", s_Config.windowHeight },
                    { "fullscreen", s_Config.fullscreen }, { "vsync", s_Config.vsync }, { "quality", s_Config.quality }, {"icon", s_Config.icon},{"target",s_Config.buildTarget} };
    return FileSystem::WriteText(s_File, j.dump(2));
}

bool Project::WriteRuntime(const fs::path& file,const ProjectConfig& c){json j={{"name",c.name},{"company",c.company},{"version",c.version},{"assemblyName",c.assemblyName},{"tags",c.tags},{"layers",c.layers},{"player",{{"width",c.windowWidth},{"height",c.windowHeight},{"fullscreen",c.fullscreen},{"vsync",c.vsync},{"quality",c.quality},{"icon",c.icon},{"target",c.buildTarget}}}};j["scenes"]=json::array();for(size_t i=0;i<c.scenes.size();++i)j["scenes"].push_back({{"path",c.scenes[i]},{"enabled",i>=c.scenesEnabled.size()||c.scenesEnabled[i]}});auto bytes=json::to_cbor(j);fs::create_directories(file.parent_path());std::ofstream out(file,std::ios::binary);out.write((const char*)bytes.data(),bytes.size());return out.good();}
bool Project::LoadRuntime(const fs::path& dataDir){if(!Load(dataDir/"Assets/__runtime.sdruntime"))return false;s_Dir=fs::absolute(dataDir);SetRuntimeLayout(s_Dir/"Assets",s_Dir/"Managed"/(s_Config.assemblyName+".dll"));return true;}
void Project::WriteScriptProject(const fs::path& csproj, const std::string& assembly) {
    std::string bin = FileSystem::ExecutableDir().generic_string();
    FileSystem::WriteText(csproj, std::format(R"(<Project Sdk="Microsoft.NET.Sdk">

  <!-- C# scripts of this Stardust project. The editor compiles them automatically when a .cs file is saved. -->
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <AssemblyName>{0}</AssemblyName>
    <RootNamespace>{0}</RootNamespace>
    <Nullable>disable</Nullable>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <GenerateRuntimeConfigurationFiles>false</GenerateRuntimeConfigurationFiles>
    <AssemblySearchPaths>{{HintPathFromItem}};$(AssemblySearchPaths)</AssemblySearchPaths>
    <DefaultItemExcludes>$(DefaultItemExcludes);Build/**</DefaultItemExcludes>
    <StardustBin Condition="'$(StardustBin)' == ''">{1}</StardustBin>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include="Assets/**/*.cs;Packages/**/*.cs" Exclude="Assets/**/Editor/**/*.cs;Packages/**/Editor/**/*.cs" />
  </ItemGroup>

  <ItemGroup>
    <Reference Include="Stardust.ScriptCore">
      <HintPath>$(StardustBin)/Stardust.ScriptCore.dll</HintPath>
      <Private>false</Private>
    </Reference>
  </ItemGroup>

</Project>
)", assembly, bin));
}

void Project::WriteEditorScriptProject() {
    auto file = Dir() / (Config().assemblyName + ".Editor.csproj");
    if (fs::exists(file)) return;
    FileSystem::WriteText(file, std::format(R"(<Project>
  <PropertyGroup><BaseIntermediateOutputPath>obj/Editor/</BaseIntermediateOutputPath><MSBuildProjectExtensionsPath>obj/Editor/</MSBuildProjectExtensionsPath></PropertyGroup>
  <Import Project="Sdk.props" Sdk="Microsoft.NET.Sdk" />
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework><EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath><Nullable>disable</Nullable>
    <AssemblyName>{0}.Editor</AssemblyName>
    <AssemblySearchPaths>{{HintPathFromItem}};$(AssemblySearchPaths)</AssemblySearchPaths>
    <StardustBin Condition="'$(StardustBin)' == ''">{1}</StardustBin>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="Assets/**/Editor/**/*.cs;Packages/**/Editor/**/*.cs" />
    <Reference Include="Stardust.ScriptCore"><HintPath>$(StardustBin)/Stardust.ScriptCore.dll</HintPath><Private>false</Private></Reference>
    <Reference Include="{0}"><HintPath>bin/{0}.dll</HintPath><Private>false</Private></Reference>
  </ItemGroup>
  <Import Project="Sdk.targets" Sdk="Microsoft.NET.Sdk" />
</Project>)", Config().assemblyName, FileSystem::ExecutableDir().generic_string()));
}

bool Project::Create(const fs::path& folder, const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.find_first_of("\\/:*?\"<>|") != std::string::npos ||
        name.back() == '.' || name.back() == ' ') {
        SD_ERROR("Choose a valid project name without path separators or reserved filename characters");
        return false;
    }
    std::error_code ec;
    if (fs::exists(folder / (name + ".sdproject"))) { SD_ERROR("A project already exists in {}", folder.string()); return false; }
    for (const char* sub : { "Assets/Scenes", "Assets/Scripts", "Assets/Materials", "Assets/Textures", "Assets/Models", "Assets/Prefabs", "Assets/Audio" })
        fs::create_directories(folder / sub, ec);
    if (ec) { SD_ERROR("Cannot create project folder: {}", ec.message()); return false; }
    std::string assembly;
    for (char ch : name) if (std::isalnum((unsigned char)ch)) assembly += ch;
    if (assembly.empty() || std::isdigit((unsigned char)assembly[0])) assembly = "Game" + assembly;

    s_Config = ProjectConfig{};
    s_Config.name = name;
    s_Config.assemblyName = assembly;
    s_Dir = fs::absolute(folder);
    s_File = s_Dir / (name + ".sdproject");
    s_AssetsOverride.clear();
    s_AssemblyOverride.clear();
    s_Loaded = true;
    WriteScriptProject(s_Dir / (assembly + ".csproj"), assembly);
    WriteEditorScriptProject();
    FileSystem::WriteText(s_Dir / ".gitignore", "bin/\nobj/\nBuild/\n");
    FileSystem::WriteText(s_Dir / "Assets/Scripts/PlayerController.cs", R"(using Stardust;

// Starter script: WASD moves the object, Space jumps when it has a Rigidbody.
public class PlayerController : ScriptBehaviour
{
    public float Speed = 5.0f;
    public float JumpImpulse = 5.0f;

    Rigidbody m_Body;

    void OnCreate()
    {
        m_Body = GetComponent<Rigidbody>();
    }

    void OnUpdate(float dt)
    {
        Vector3 input = new Vector3(Input.GetAxis("Horizontal"), 0, -Input.GetAxis("Vertical"));
        if (m_Body == null)
        {
            transform.Translate(input * Speed * dt, Space.World);
            return;
        }
        Vector3 v = m_Body.velocity;
        m_Body.velocity = new Vector3(input.x * Speed, v.y, input.z * Speed);
        if (Input.GetKeyDown(KeyCode.Space)) m_Body.AddForce(Vector3.up * JumpImpulse, ForceMode.Impulse);
    }
}
)");
    Save();
    AddRecent(s_File);
    SD_INFO("Project created: {}", s_Dir.string());
    return true;
}

fs::path Project::ScriptAssembly() {
    if (!s_AssemblyOverride.empty()) return s_AssemblyOverride;
    return s_Dir / "bin" / (s_Config.assemblyName + ".dll");
}

void Project::SetRuntimeLayout(const fs::path& assetsDir, const fs::path& assembly) {
    s_AssetsOverride = assetsDir;
    s_AssemblyOverride = assembly;
}

std::vector<std::string> Project::EnabledScenes() {
    std::vector<std::string> out;
    for (size_t i = 0; i < s_Config.scenes.size(); ++i)
        if (i >= s_Config.scenesEnabled.size() || s_Config.scenesEnabled[i]) out.push_back(s_Config.scenes[i]);
    return out;
}

std::string Project::ResolveScene(const std::string& nameOrPath) {
    auto scenes = EnabledScenes();
    if (!nameOrPath.empty() && std::all_of(nameOrPath.begin(), nameOrPath.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        size_t i = 0;
        auto result = std::from_chars(nameOrPath.data(), nameOrPath.data() + nameOrPath.size(), i);
        return result.ec == std::errc{} && i < scenes.size() ? scenes[i] : std::string();
    }
    for (auto& s : scenes)
        if (s == nameOrPath || FileSystem::Utf8(fs::u8path(s).stem()) == nameOrPath) return s;
    // Explicit paths are independent from the startup/build scene list.
    auto extra=fs::u8path(nameOrPath).lexically_normal();
    if(extra.extension()==".scene"){
        auto path=extra.is_absolute()?extra:AssetsDir()/extra;
        if(FileSystem::Exists(path))return FileSystem::Utf8(extra);
    }
    return {};
}

// ------------------------------------------------------------------------------------------------ recent projects

static fs::path RecentFile() {
    const char* appdata = std::getenv("APPDATA");
    return fs::path(appdata ? appdata : ".") / "Stardust" / "projects.json";
}

std::vector<fs::path> Project::RecentProjects() {
    std::vector<fs::path> out;
    json j = json::parse(FileSystem::ReadText(RecentFile()), nullptr, false);
    if (j.is_array())
        for (auto& p : j) if (p.is_string() && fs::exists(fs::path(p.get<std::string>()))) out.push_back(p.get<std::string>());
    return out;
}

void Project::AddRecent(const fs::path& file) {
    auto list = RecentProjects();
    std::erase(list, fs::absolute(file));
    list.insert(list.begin(), fs::absolute(file));
    if (list.size() > 12) list.resize(12);
    json j = json::array();
    for (auto& p : list) j.push_back(p.string());
    FileSystem::WriteText(RecentFile(), j.dump(2));
}

void Project::RemoveRecent(const fs::path& file) {
    auto list = RecentProjects();
    std::erase(list, fs::absolute(file));
    json j = json::array();
    for (auto& p : list) j.push_back(p.string());
    FileSystem::WriteText(RecentFile(), j.dump(2));
}

} // namespace Stardust
