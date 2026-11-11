#pragma once
#include "Scene/Scene.h"
#include <filesystem>

namespace Stardust {

// Human-readable JSON scene format (.scene). Entities are referenced by UUID so files stay stable across sessions.
class SceneSerializer {
public:
    static bool Save(Scene& scene, const std::filesystem::path& path);
    static Ref<Scene> Load(const std::filesystem::path& path);   // nullptr on failure
    static std::string ToJson(Scene& scene, bool compact = false);   // compact: no indentation (undo snapshots)
    static std::vector<EntityID> AppendScene(Scene& scene,const std::string& json);
    static Ref<Scene> FromJson(const std::string& json);

    // Prefabs (.prefab = JSON of one entity subtree) and in-memory copies
    static std::string EntityToJson(Scene& scene, EntityID root);
    static EntityID InstantiateJson(Scene& scene, const std::string& json, EntityID parent = kNullEntity);
    static std::string SelectionToJson(Scene& scene,const std::vector<EntityID>& roots);
    static std::vector<EntityID> InstantiateSelection(Scene& scene,const std::string& json,EntityID parent=kNullEntity);
    static bool SavePrefab(Scene& scene, EntityID root, const std::filesystem::path& path);
    static EntityID InstantiatePrefab(Scene& scene, const std::filesystem::path& path, EntityID parent = kNullEntity);
};

} // namespace Stardust
