#pragma once
#include "Scene/Scene.h"
namespace Stardust {
struct UIRect {
    EntityID entity=0,canvas=0;
    glm::vec2 size{0},canvasSize{0};
    glm::mat4 localToCanvas{1};
    float alpha=1;bool interactable=true,raycast=true;
    std::vector<std::array<glm::vec2,4>> masks;
    Ref<RHI::Texture> maskTexture; // nearest Mask with an image
    glm::mat4 maskFromCanvas{1};
    glm::vec2 maskSize{1};
    glm::vec4 maskUv{0,0,1,1};
};
class CanvasRenderer {
public:
    static std::vector<UIRect> Layout(Scene& scene,glm::vec2 viewport,bool sceneView);
    static glm::mat4 CanvasWorld(Scene& scene,EntityID canvas,glm::vec2 logicalSize=glm::vec2(0));
    static bool MouseOnCanvas(Scene& scene,EntityID canvas,const RenderCamera& camera,glm::vec2 origin,glm::vec2 size,glm::vec2 mouse,glm::vec2& point,glm::vec2 logicalSize=glm::vec2(0));
    static void Submit(Scene& scene,SceneRenderer& renderer,RHI::Device& device,glm::vec2 viewport,bool sceneView,bool alwaysOnTop=false,bool includeOverlay=true);
    static EntityID HandleInput(Scene& scene,glm::vec2 origin,glm::vec2 size,const RenderCamera& camera,bool sceneView,bool interactive);
    static void Clear();
    static void InvalidateAssets();
};
}
