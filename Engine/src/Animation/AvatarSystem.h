#pragma once
#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include <nlohmann/json_fwd.hpp>

namespace Stardust {

// Per-FBX import choices, stored next to the model as <file>.rig.json
struct FbxImportSettings {
    enum Type { None = 0, Generic = 1, Humanoid = 2 };
    int animationType = Generic;
    std::string avatar;
    bool inPlace = false;
    std::unordered_map<std::string, bool> loop; // by take name, default true
    static FbxImportSettings Load(const std::string& asset);
    bool Save(const std::string& asset) const;
};

// Humanoid retargeting. An .avatar asset maps standard bone names to one skeleton and records its T-pose,
// so a clip baked from one humanoid plays on any other.
class AvatarSystem {
public:
    static const std::vector<std::string>& HumanBones(); // parents always come before their children
    static bool IsRequired(const std::string& bone);
    static std::vector<std::string> SkeletonPaths(const std::string& fbx);

    // Guesses the bone mapping from common naming conventions (Mixamo, Unreal, Blender, 3ds Max Biped...)
    static nlohmann::json AutoMap(const std::string& fbx);
    // Fills basis, hips height and T-pose from the "source" and "bones" fields. False if required bones are missing.
    static bool Rebuild(nlohmann::json& avatar, std::string* error = nullptr);
    // Writes <model>.avatar beside the FBX and returns its asset path, empty on failure.
    static std::string CreateFromFbx(const std::string& fbx, std::string* error = nullptr);

    // Mixamo files often carry an extra take that only holds the T-pose
    static bool TakeHasMotion(const std::string& fbx, const FbxTakeDesc& take);
    // Bakes the takes of the FBX to .anim files beside it (motionless takes are skipped if another one moves); returns their asset paths.
    static std::vector<std::string> ImportClips(const std::string& fbx, std::string* error = nullptr);

    static void Apply(Scene& scene, EntityID root, const nlohmann::json& clip, float time, bool loop);
};

} // namespace Stardust
