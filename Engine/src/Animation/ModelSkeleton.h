#pragma once
#include "Scene/Scene.h"
namespace Stardust {class ModelSkeleton {public:static bool Import(Scene& scene,EntityID root,const std::string& asset);static void ApplyPose(Scene& scene,EntityID root,float time,const std::string& take);static std::unordered_map<u32,glm::mat4> Pose(Scene& scene,EntityID root);};}
