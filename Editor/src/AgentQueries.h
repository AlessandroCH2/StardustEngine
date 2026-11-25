// Scene, console and build requests of the AI Agent's tools ("agent..." Native.Query kinds).
// Kept apart from the window so the runtime tests can run the same code without the editor.
#pragma once
#include "Scene/Scene.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Stardust {
struct AgentContext {
    Scene* scene = nullptr;
    std::vector<EntityID>* selection = nullptr;
    EntityID* selected = nullptr;
    std::filesystem::path scenePath;
    std::string selectedAsset;
    bool playing = false;
    std::function<void()> save;
    bool edited = false;          // set when a request changed the scene (the editor records an undo step)
};
std::string AgentSceneQuery(AgentContext& ctx, const std::string& request);
}
