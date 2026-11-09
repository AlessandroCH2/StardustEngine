#pragma once
#include "Core/Base.h"
#include "Scene/Registry.h"
#include <filesystem>

namespace Stardust {

class Scene;

struct ScriptFieldInfo {
    std::string name;
    std::string type;          // float, int, bool, string, Vector2, Vector3, Color
    std::string defaultJson;   // JSON value
    std::string schemaJson="{}";
};

struct ScriptMethodInfo { std::string name,signature; std::vector<ScriptFieldInfo> parameters; };
struct ScriptClassInfo {
    std::string name;
    std::vector<ScriptFieldInfo> fields;
    std::vector<ScriptMethodInfo> methods;
};

// Hosts the .NET 8 runtime (hostfxr), loads Stardust.ScriptCore and the game assembly into a collectible
// AssemblyLoadContext (hot reload), and drives ScriptBehaviour instances during play mode.
class ScriptEngine {
public:
    static bool Init();
    static void Shutdown();
    static bool IsReady() { return s_Ready; }
    static void SetEditorMode(bool value) { s_EditorMode = value; }
    static void SetEditorScene(Scene* scene) { s_EditorScene = scene; }
    static Scene* ContextScene() { return s_Scene ? s_Scene : (s_EditorMode ? s_EditorScene : nullptr); }
    static const std::vector<std::string>& EditorCommands() { return s_EditorCommands; }
    static void RunEditorCommand(const std::string& path);

    // Game assembly (compiled from SandboxProject/Assets/**/*.cs)
    static std::filesystem::path ProjectDir();
    static std::filesystem::path GameAssemblyPath();
    static bool ReloadGameAssembly();          // unload + load + refresh classes (edit mode only)
    static bool CheckForAssemblyChange();      // true when the DLL on disk changed since the last load
    static bool CheckForSourceChange();        // true when a .cs file under Assets was added, removed or saved
    static std::string DrawEditorUI(const std::string& request);
    static bool NeedsBuild();                  // DLL missing or older than the newest .cs file
    static const std::vector<ScriptClassInfo>& Classes() { return s_Classes; }
    static std::string RemapFieldReferences(const std::string& className,const std::string& fields,const std::string& mapping);
    static const ScriptClassInfo* FindClass(const std::string& name);

    // Build the game scripts with `dotnet build` on a background thread (output goes to the log)
    static void BuildAsync();
    static bool IsBuilding() { return s_Building; }
    static bool TakeBuildFinished(bool& success);

    // Play mode
    static void OnRuntimeStart(Scene* scene,bool resetTime=true);
    static void OnRuntimeStop();
    static void Instantiate(EntityID e);       // create the instance and call OnCreate
    static void DestroyInstance(EntityID e);
    static void Update(float dt);
    static void FixedUpdate(float dt);
    static void OnContact(EntityID a, EntityID b, int kind); // 0 collision enter, 1 exit, 2 trigger enter, 3 exit
    static void OnUIButton(EntityID e);
    static void OnUIInput(EntityID e,const std::string& text,bool submit);
    static std::vector<ScriptMethodInfo> ActionMethods(const std::string& component,const std::string& scriptClass="");
    static float CurrentDeltaTime();
    static void SetScreenSize(float w, float h);
    static bool HasInstance(EntityID e, size_t slot = 0);
    static std::string GetInstanceFields(EntityID e, size_t slot = 0);
    static void SetInstanceField(EntityID e, const std::string& name, const std::string& json, size_t slot = 0);
    static Scene* RuntimeScene() { return s_Scene; }
    static void FireAnimationEvent(EntityID e, const std::string& script, const std::string& function, const std::string& signature, const std::string& argumentsJson);
    static bool SlotRunnable(EntityID e, size_t slot);
    static void StartSlots(EntityID e);

private:
    static bool CreateRaw(EntityID e);
    static inline bool s_Ready = false;
    static inline bool s_EditorMode = false;
    static inline std::vector<std::string> s_EditorCommands;
    static inline bool s_Building = false;
    static inline Scene* s_Scene = nullptr;
    static inline Scene* s_EditorScene = nullptr;
    static inline std::vector<ScriptClassInfo> s_Classes;
};

} // namespace Stardust
