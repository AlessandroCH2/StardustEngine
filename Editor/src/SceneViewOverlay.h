#pragma once
#include <imgui.h>
namespace Stardust {
// Scene overlays share the image bounds, excluding the tab bar and toolbar.
class SceneViewOverlayClip {
public:
    SceneViewOverlayClip(ImVec2 min,ImVec2 max){ImGui::PushClipRect(min,max,true);}
    ~SceneViewOverlayClip(){ImGui::PopClipRect();}
    SceneViewOverlayClip(const SceneViewOverlayClip&)=delete;
    SceneViewOverlayClip& operator=(const SceneViewOverlayClip&)=delete;
};
}
