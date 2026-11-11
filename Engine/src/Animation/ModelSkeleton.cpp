#include "Animation/ModelSkeleton.h"
#include "Assets/AssetManager.h"
#include "Animation/AvatarSystem.h"
namespace Stardust {
bool ModelSkeleton::Import(Scene& scene,EntityID root,const std::string& asset){if(scene.Reg().Has<FbxRigComponent>(root))return false;auto bones=AssetManager::GetFbxSkeleton(asset);if(bones.empty())return false;auto rootWorld=scene.GetWorldMatrix(root);u64 uuid=scene.Reg().Get<IDComponent>(root).uuid;std::vector<EntityID> entities;for(auto& bone:bones){auto child=scene.CreateEntity(bone.name);scene.SetParent(child,bone.parent<0?root:entities[bone.parent],false);scene.SetWorldMatrix(child,rootWorld*bone.world);scene.Reg().Add<BoneComponent>(child)=BoneComponent{uuid,bone.nodeId};entities.push_back(child);}scene.Reg().Add<FbxRigComponent>(root).source=asset;if(!scene.Reg().Has<AnimatorComponent>(root))scene.Reg().Add<AnimatorComponent>(root);auto settings=FbxImportSettings::Load(asset);if(settings.animationType==FbxImportSettings::Humanoid)scene.Reg().Get<AnimatorComponent>(root).avatar=settings.avatar;return true;}
// Bones of a rig are found by walking the root's hierarchy (parents before children), accumulating
// matrices on the way: no search through every bone of the scene, no recursive world matrices.
void ModelSkeleton::ApplyPose(Scene& scene,EntityID root,float time,const std::string& take){
    auto* rig=scene.Reg().TryGet<FbxRigComponent>(root);if(!rig)return;
    auto bones=AssetManager::GetFbxSkeleton(rig->source,std::floor(time*30)/30,take);
    std::unordered_map<u32,glm::mat4> matrices;for(auto& bone:bones)matrices[bone.nodeId]=bone.local;
    auto& reg=scene.Reg();const u64 uuid=reg.Get<IDComponent>(root).uuid;
    std::vector<std::pair<EntityID,glm::mat4>> stack;for(EntityID c:scene.GetChildren(root))stack.push_back({c,scene.GetWorldMatrix(root)});
    while(!stack.empty()){
        auto [e,parentWorld]=stack.back();stack.pop_back();
        auto* bone=reg.TryGet<BoneComponent>(e);
        if(bone&&bone->root==uuid){auto found=matrices.find(bone->nodeId);if(found!=matrices.end())scene.SetWorldMatrix(e,parentWorld*found->second);}
        glm::mat4 world=parentWorld*reg.Get<TransformComponent>(e).Matrix();
        for(EntityID c:scene.GetChildren(e))stack.push_back({c,world});
    }
}
std::unordered_map<u32,glm::mat4> ModelSkeleton::Pose(Scene& scene,EntityID root){
    std::unordered_map<u32,glm::mat4> pose;auto& reg=scene.Reg();const u64 uuid=reg.Get<IDComponent>(root).uuid;
    // Matrices relative to the root (what the skinning needs), built top-down
    std::vector<std::pair<EntityID,glm::mat4>> stack;for(EntityID c:scene.GetChildren(root))stack.push_back({c,glm::mat4(1)});
    while(!stack.empty()){
        auto [e,parentModel]=stack.back();stack.pop_back();
        glm::mat4 model=parentModel*reg.Get<TransformComponent>(e).Matrix();
        if(auto* bone=reg.TryGet<BoneComponent>(e);bone&&bone->root==uuid)pose[bone->nodeId]=model;
        for(EntityID c:scene.GetChildren(e))stack.push_back({c,model});
    }
    return pose;
}
}
