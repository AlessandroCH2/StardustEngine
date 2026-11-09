#pragma once
#include "Scene/Components.h"
#include <type_traits>
namespace Stardust {
// Stable scripting IDs and serialized keys. Structural transforms/rig data are always present.
template<typename Fn> void VisitToggleComponents(Fn&& fn){
#define SD_TOGGLE(T,ID,KEY) fn.template operator()<T>(ID,KEY)
 SD_TOGGLE(MeshRendererComponent,1,"meshRenderer");SD_TOGGLE(RigidbodyComponent,2,"rigidbody");SD_TOGGLE(BoxColliderComponent,3,"boxCollider");SD_TOGGLE(SphereColliderComponent,4,"sphereCollider");SD_TOGGLE(CapsuleColliderComponent,5,"capsuleCollider");SD_TOGGLE(CameraComponent,6,"camera");SD_TOGGLE(DirectionalLightComponent,7,"directionalLight");SD_TOGGLE(MeshColliderComponent,9,"meshCollider");SD_TOGGLE(PointLightComponent,10,"pointLight");SD_TOGGLE(AudioSourceComponent,11,"audioSource");SD_TOGGLE(AnimatorComponent,12,"animator");SD_TOGGLE(CanvasComponent,13,"canvas");SD_TOGGLE(UIImageComponent,15,"uiImage");SD_TOGGLE(UITextComponent,16,"uiText");SD_TOGGLE(UIButtonComponent,17,"uiButton");SD_TOGGLE(CanvasGroupComponent,18,"canvasGroup");SD_TOGGLE(RectMaskComponent,19,"rectMask");SD_TOGGLE(UIMaskComponent,29,"uiMask");SD_TOGGLE(UILayoutGroupComponent,20,"layoutGroup");SD_TOGGLE(UIInputFieldComponent,21,"uiInputField");SD_TOGGLE(SpriteRendererComponent,22,"spriteRenderer");SD_TOGGLE(EnvironmentComponent,23,"environment");SD_TOGGLE(TerrainComponent,24,"terrain");SD_TOGGLE(SpotLightComponent,25,"spotLight");SD_TOGGLE(VideoPlayerComponent,26,"videoPlayer");SD_TOGGLE(AudioListenerComponent,27,"audioListener");SD_TOGGLE(ProceduralTerrainComponent,28,"proceduralTerrain");
#undef SD_TOGGLE
}
template<typename T> constexpr bool ToggleableComponent = !std::is_same_v<T,TransformComponent>&&!std::is_same_v<T,RectTransformComponent>&&!std::is_same_v<T,PrefabInstanceComponent>&&!std::is_same_v<T,FbxRigComponent>&&!std::is_same_v<T,BoneComponent>;
}
