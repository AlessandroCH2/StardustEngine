#pragma once
#include <string>
#include <mutex>
#include "Core/Base.h"
#include <functional>

namespace Stardust {
class Scene;

// Requests coming from gameplay code (C# SceneManager / Application) that the host app (editor or player) executes
// between frames: loading another scene and quitting.
class SceneManager {
public:
    static void RequestLoad(const std::string& sceneAssetPath) { std::lock_guard l(s_Mutex); s_Pending = sceneAssetPath; }
    static bool TakeLoadRequest(std::string& out) {
        std::lock_guard l(s_Mutex);
        if (s_Pending.empty()) return false;
        out = std::move(s_Pending);
        s_Pending.clear();
        return true;
    }
    static u64 LoadAsync(const std::string& path);
    static float LoadProgress(u64 id);
    static int LoadStatus(u64 id); // 0 loading, 1 complete, -1 failed
    static std::string LoadError(u64 id);
    static void AllowActivation(u64 id,bool allowed);
    static void UpdateAsync(const std::function<bool(Ref<Scene>)>& activate);
    static void CancelLoads();
    static void RequestQuit() { s_Quit = true; }
    static bool TakeQuitRequest() { bool q = s_Quit; s_Quit = false; return q; }
    static void SetActiveSceneName(const std::string& n) { s_ActiveName = n; }
    static const std::string& ActiveSceneName() { return s_ActiveName; }

private:
    static inline std::mutex s_Mutex;
    static inline std::string s_Pending, s_ActiveName;
    static inline bool s_Quit = false;
};

} // namespace Stardust
