#pragma once
#include "Scene/SceneSerializer.h"
#include <nlohmann/json.hpp>
namespace Stardust {
class PrefabSystem {
public:
    static void Link(Scene& scene,EntityID root,const std::string& asset,const std::string& source);
    static nlohmann::json Resolve(nlohmann::json scene);
    static bool Refresh(Ref<Scene>& scene);
    static void Revert(Scene& scene,EntityID root);
};
}
