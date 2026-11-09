#pragma once
#include "RHI/RHI.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"
#include <filesystem>
#include <unordered_map>

namespace Stardust {

enum class DefaultTexture : u8 { White, FlatNormal, Black };

// Loads and caches project assets. Paths stored in scenes/materials are relative to the project's Assets folder.
// Mesh references are either "builtin:<Primitive>" or a relative path to an .obj / .gltf / .glb file.
struct FbxContentItem {std::string path,name;u32 slot=0;bool material=false;};
struct FbxTakeDesc {std::string name;float duration=0;};
struct FbxBoneDesc {std::string name;u32 nodeId=0;int parent=-1;glm::mat4 local{1},world{1};};
class AssetManager {
public:
    static void Init(RHI::Device& device, const std::filesystem::path& assetsRoot);
    static void Shutdown();
    static void RefreshMounts();
    static RHI::Device* Device(){return s_Device;}

    static const std::filesystem::path& Root() { return s_Root; }
    static std::string ToRelative(const std::filesystem::path& absolute);
    static std::filesystem::path ToAbsolute(const std::string& relative);

    // Textures (PNG/JPG/TGA/BMP via stb_image, full mip chain). Returns the bindless SRV of the texture or a fallback.
    static Ref<RHI::Texture> GetTexture(const std::string& path, bool srgb);
    static void SetRuntimeTexture(const std::string& path,Ref<RHI::Texture> texture);
    static void SetRuntimeMesh(const std::string& path,Ref<Mesh> mesh);
    static u32 TextureSRV(const std::string& path, bool srgb, DefaultTexture fallback);
    static u32 DefaultSRV(DefaultTexture t);

    // Meshes
    static Ref<Mesh> GetMesh(const std::string& ref);
    static Ref<Mesh> GetAnimatedFbx(const std::string& ref, float time, const std::string& animation = {});
    static std::string BuiltinRef(PrimitiveType t) { return std::string("builtin:") + PrimitiveName(t); }

    // Materials and textures inside an FBX, written once to the hidden <model>.fbm folder beside it
    static std::vector<FbxContentItem> GetFbxContents(const std::string& asset);
    static std::vector<FbxTakeDesc> GetFbxTakes(const std::string& asset);
    static std::vector<FbxBoneDesc> GetFbxSkeleton(const std::string& asset,float time=-1,const std::string& animation="");
    static void FlushSkinning(RHI::CommandList& cmd); // deforms the meshes posed this frame; call outside render passes
    static Ref<Mesh> GetPosedFbx(const std::string& asset,u64 instance,const std::unordered_map<u32,glm::mat4>& pose);

    // Materials (.mat JSON)
    static Ref<Material> GetMaterial(const std::string& path);
    static Ref<Material> CreateMaterial(const std::string& path); // writes a new default material
    static bool SaveMaterial(Material& m);
    static void SaveDirtyMaterials();
    static void Invalidate(const std::string& path);

    // Project browsing helpers
    static std::vector<std::string> Find(const std::vector<std::string>& extensions);
    static bool IsMeshFile(const std::filesystem::path& p);
    static bool IsTextureFile(const std::filesystem::path& p);

private:
    static Ref<Mesh> ImportObj(const std::filesystem::path& path);
    static Ref<Mesh> ImportGltf(const std::filesystem::path& path);
    static Ref<Mesh> ImportFbx(const std::filesystem::path& path, float time = -1, const std::string& animation = {});

    static inline RHI::Device* s_Device = nullptr;
    static inline std::filesystem::path s_Root;
};

// Fills normals (if zero) and tangents of an indexed triangle mesh
void ComputeNormalsAndTangents(std::vector<Vertex>& v, const std::vector<u32>& idx, bool computeNormals);

} // namespace Stardust
