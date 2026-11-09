#pragma once
#include "Scene/Components.h"
namespace Stardust::RectTransformUtils {
inline glm::vec2 Size(const RectTransformComponent& r,glm::vec2 parent){return parent*(r.anchorMax-r.anchorMin)+r.sizeDelta;}
inline void SetAnchors(RectTransformComponent& r,glm::vec2 parent,glm::vec2 minimum,glm::vec2 maximum){
    auto size=Size(r,parent);auto point=parent*glm::mix(r.anchorMin,r.anchorMax,r.pivot)+r.anchoredPosition;
    r.anchorMin=minimum;r.anchorMax=maximum;r.sizeDelta=size-parent*(maximum-minimum);r.anchoredPosition=point-parent*glm::mix(minimum,maximum,r.pivot);
}
inline void SetPivot(RectTransformComponent& r,glm::vec2 parent,glm::vec2 pivot){
    auto size=Size(r,parent);auto shift=(pivot-r.pivot)*size*r.scale;
    auto rotated=glm::vec2(glm::rotate(glm::mat4(1),glm::radians(r.rotation),glm::vec3(0,0,1))*glm::vec4(shift,0,0));
    auto oldReference=parent*glm::mix(r.anchorMin,r.anchorMax,r.pivot);
    r.anchoredPosition+=oldReference-parent*glm::mix(r.anchorMin,r.anchorMax,pivot)+rotated;r.pivot=pivot;
}
inline glm::vec2 OffsetMin(const RectTransformComponent& r){return r.anchoredPosition-r.sizeDelta*r.pivot;}
inline glm::vec2 OffsetMax(const RectTransformComponent& r){return r.anchoredPosition+r.sizeDelta*(glm::vec2(1)-r.pivot);}
inline void SetOffsets(RectTransformComponent& r,glm::vec2 minimum,glm::vec2 maximum){r.sizeDelta=maximum-minimum;r.anchoredPosition=minimum+r.sizeDelta*r.pivot;}
}
