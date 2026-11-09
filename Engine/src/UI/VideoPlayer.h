#pragma once
#include "Scene/Scene.h"
namespace Stardust {
class VideoPlayerSystem {
public:
    static void Update(Scene& scene,float dt);
    static void Remove(Scene& scene,EntityID entity);
    static void Clear(Scene& scene);
};
}
