#include "GizmoMath.h"
#include "EditorApp.h"
#include "UI/CanvasRenderer.h"
#include "UI/RectTransformUtils.h"
#include <imgui.h>
#include <ImGuizmo.h>
namespace Stardust {
bool EditorApp::DrawCanvasTools() {
    auto& scene=ActiveScene();auto& reg=scene.Reg();
    auto rectangles=CanvasRenderer::Layout(scene,m_SceneView.size,true);
    if(m_SceneView.size.x<=0||m_SceneView.size.y<=0)return false;
    auto camera=m_Camera.GetRenderCamera(m_SceneView.size.x/m_SceneView.size.y);
    auto vp=camera.projection*camera.view;auto* draw=ImGui::GetWindowDrawList();
    const ImVec2 viewMin(m_SceneView.min.x,m_SceneView.min.y),viewSize(m_SceneView.size.x,m_SceneView.size.y);
    // Points behind the camera go off screen; outlines are cut where they pass behind it (see GizmoMath.h)
    auto screen=[&](const glm::mat4& transform,glm::vec2 point){ImVec2 out;if(!Gizmo::ProjectPoint(vp,glm::vec3(transform*glm::vec4(point,0,1)),viewMin,viewSize,out))return ImVec2(-10000,-10000);return out;};
    auto outline=[&](const glm::mat4& transform,glm::vec2 size,ImU32 color){
        const glm::vec3 p[4]={glm::vec3(transform*glm::vec4(0,0,0,1)),glm::vec3(transform*glm::vec4(size.x,0,0,1)),glm::vec3(transform*glm::vec4(size,0,1)),glm::vec3(transform*glm::vec4(0,size.y,0,1))};
        for(int k=0;k<4;++k){ImVec2 a,b;if(Gizmo::ProjectSegment(vp,p[k],p[(k+1)%4],viewMin,viewSize,a,b))draw->AddLine(a,b,color,1.5f);}
    };
    for(auto& canvas:rectangles)if(canvas.entity==canvas.canvas)outline(CanvasRenderer::CanvasWorld(scene,canvas.canvas)*canvas.localToCanvas,canvas.size,IM_COL32(255,255,255,230));
    auto selected=std::find_if(rectangles.begin(),rectangles.end(),[&](const auto& r){return r.entity==m_Selected;});
    if(selected==rectangles.end()) {m_UIRectEntity=0;return false;}
    auto r=*selected;
    auto model=CanvasRenderer::CanvasWorld(scene,r.canvas)*r.localToCanvas;
    auto project=[&](glm::vec2 p){return screen(model,p);};
    ImVec2 corners[]={project({0,0}),project({r.size.x,0}),project(r.size),project({0,r.size.y})};
    outline(model,r.size,IM_COL32(90,210,255,230));
    auto* rect=reg.TryGet<RectTransformComponent>(m_Selected);if(!rect)return false;
    auto mouse=ImGui::GetMousePos();int hovered=-1;
    for(int i=0;i<4;++i){draw->AddRectFilled({corners[i].x-4,corners[i].y-4},{corners[i].x+4,corners[i].y+4},IM_COL32(100,220,255,255));if(std::hypot(mouse.x-corners[i].x,mouse.y-corners[i].y)<9)hovered=i;}
    glm::mat4 parent(1);glm::vec2 parentSize=r.canvasSize;auto parentId=scene.GetParent(m_Selected);
    for(auto& entry:rectangles)if(entry.entity==parentId&&entry.canvas==r.canvas){parent=entry.localToCanvas;parentSize=entry.size;break;}
    auto anchorModel=CanvasRenderer::CanvasWorld(scene,r.canvas)*parent;
    for(int i=0;i<4;++i){bool right=i==1||i==2,top=i>=2;auto n=glm::vec2(right?rect->anchorMax.x:rect->anchorMin.x,top?rect->anchorMax.y:rect->anchorMin.y);auto a=screen(anchorModel,parentSize*n);float sx=right?1.f:-1.f,sy=top?-1.f:1.f;ImVec2 tip(a.x+sx*3,a.y+sy*3),b(a.x+sx*13,a.y+sy*3),c(a.x+sx*3,a.y+sy*13);bool hot=std::hypot(mouse.x-(a.x+sx*7),mouse.y-(a.y+sy*7))<9;if(hot)hovered=100+i;draw->AddTriangleFilled(tip,b,c,hot?IM_COL32(255,230,120,255):IM_COL32(225,225,225,255));draw->AddTriangle(tip,b,c,IM_COL32(70,70,70,255));}
    auto pivot=project(r.size*rect->pivot);draw->AddCircle(pivot,5,IM_COL32(255,210,80,255),12,1.5f);if(std::hypot(mouse.x-pivot.x,mouse.y-pivot.y)<6)hovered=200;
    if(!m_SceneView.hovered&&!m_UIRectEntity)hovered=-1;
    if(hovered>=100)ImGui::SetTooltip(hovered==200?"Drag pivot (preserves rectangle)":"Drag anchor (Ctrl: snap to edges / center)");
    glm::vec2 canvasMouse;if(!CanvasRenderer::MouseOnCanvas(scene,r.canvas,camera,m_SceneView.min,m_SceneView.size,{mouse.x,mouse.y},canvasMouse))return false;
    auto local=glm::vec2(glm::inverse(r.localToCanvas)*glm::vec4(canvasMouse,0,1));
    bool inside=local.x>=0&&local.y>=0&&local.x<=r.size.x&&local.y<=r.size.y;
    if(inside&&hovered<0&&!m_Camera.Is2D()&&!m_UIRectEntity){auto inverse=glm::inverse(vp);glm::vec2 ndc{(mouse.x-m_SceneView.min.x)/m_SceneView.size.x*2-1,1-(mouse.y-m_SceneView.min.y)/m_SceneView.size.y*2};auto p=inverse*glm::vec4(ndc,.5f,1);auto ray=glm::normalize(glm::vec3(p)/p.w-camera.position);float distance;auto hit=PickEntityCPU(camera.position,ray,&distance);auto surface=glm::vec3(CanvasRenderer::CanvasWorld(scene,r.canvas,r.canvasSize)*glm::vec4(canvasMouse,0,1));if(hit&&distance<glm::length(surface-camera.position)-.01f)inside=false;}
    if(m_SceneView.hovered&&ImGui::IsMouseClicked(0)&&(inside||hovered>=0)){
        m_UIAnchorOriginal=*rect;m_UIRectEntity=m_Selected;m_UIRectHandle=hovered;m_UIStartMouse=canvasMouse;m_UIStartPosition=rect->anchoredPosition;m_UIStartSize=r.size;m_UIStartDelta=rect->sizeDelta;m_UIStartRotation=rect->rotation;
        {glm::vec2 lo(FLT_MAX),hi(-FLT_MAX);for(glm::vec2 c:{glm::vec2(0),glm::vec2(r.size.x,0),r.size,glm::vec2(0,r.size.y)}){auto p=glm::vec2(r.localToCanvas*glm::vec4(c,0,1));lo=glm::min(lo,p);hi=glm::max(hi,p);}m_UIStartMin=lo;m_UIStartMax=hi;}
        auto center=glm::vec2(r.localToCanvas*glm::vec4(r.size*rect->pivot,0,1));m_UIStartAngle=std::atan2(canvasMouse.y-center.y,canvasMouse.x-center.x);
        m_ScenePickPending=false;
    }
    if(m_UIRectEntity&&(!ImGui::IsMouseDown(0)||m_UIRectEntity!=m_Selected)){m_UIRectEntity=0;return inside||hovered>=0;}
    if(m_UIRectEntity==m_Selected){
        // Snap the dragged edges to the canvas, the parent and the siblings; Alt turns it off for this drag
        glm::vec2 snappedMouse=canvasMouse;
        if(m_UISnapEnabled&&!ImGui::GetIO().KeyAlt&&m_GizmoOp!=ImGuizmo::ROTATE&&m_UIRectHandle<4){
            auto canvasWorld=CanvasRenderer::CanvasWorld(scene,r.canvas);
            auto a=screen(canvasWorld,{0,0}),b=screen(canvasWorld,{1,0});float pixelsPerUnit=std::max(1e-3f,std::hypot(b.x-a.x,b.y-a.y));const float threshold=7.0f/pixelsPerUnit;
            std::vector<float> targets[2];
            for(auto& entry:rectangles){if(entry.canvas!=r.canvas||entry.entity==m_Selected||scene.IsDescendantOf(entry.entity,m_Selected))continue;if(entry.entity!=r.canvas&&entry.entity!=parentId&&scene.GetParent(entry.entity)!=parentId)continue;glm::vec2 lo(FLT_MAX),hi(-FLT_MAX);for(glm::vec2 c:{glm::vec2(0),glm::vec2(entry.size.x,0),entry.size,glm::vec2(0,entry.size.y)}){auto p=glm::vec2(entry.localToCanvas*glm::vec4(c,0,1));lo=glm::min(lo,p);hi=glm::max(hi,p);}for(int axis=0;axis<2;++axis){targets[axis].push_back(lo[axis]);targets[axis].push_back((lo[axis]+hi[axis])*.5f);targets[axis].push_back(hi[axis]);}}
            const glm::vec2 drag=canvasMouse-m_UIStartMouse;
            for(int axis=0;axis<2;++axis){std::vector<float> moving;if(m_UIRectHandle<0)moving={m_UIStartMin[axis]+drag[axis],(m_UIStartMin[axis]+m_UIStartMax[axis])*.5f+drag[axis],m_UIStartMax[axis]+drag[axis]};else{bool high=axis==0?(m_UIRectHandle==1||m_UIRectHandle==2):m_UIRectHandle>=2;moving={(high?m_UIStartMax[axis]:m_UIStartMin[axis])+drag[axis]};}
                float best=threshold,correction=0,guide=0;bool found=false;for(float point:moving)for(float target:targets[axis])if(std::abs(target-point)<best){best=std::abs(target-point);correction=target-point;guide=target;found=true;}
                if(found){snappedMouse[axis]+=correction;glm::vec2 p0=axis==0?glm::vec2(guide,0):glm::vec2(0,guide),p1=axis==0?glm::vec2(guide,r.canvasSize.y):glm::vec2(r.canvasSize.x,guide);draw->AddLine(screen(canvasWorld,p0),screen(canvasWorld,p1),IM_COL32(255,90,190,230),1.2f);}}
        }
        auto delta=glm::vec2(glm::inverse(parent)*glm::vec4(snappedMouse-m_UIStartMouse,0,0));
        if(m_UIRectHandle>=100&&m_UIRectHandle<104){auto minimum=m_UIAnchorOriginal.anchorMin,maximum=m_UIAnchorOriginal.anchorMax;int corner=m_UIRectHandle-100;bool right=corner==1||corner==2,top=corner>=2;auto change=delta/glm::max(parentSize,glm::vec2(1));float& x=right?maximum.x:minimum.x;float& y=top?maximum.y:minimum.y;x+=change.x;y+=change.y;if(ImGui::GetIO().KeyCtrl){x=std::round(x*2)*.5f;y=std::round(y*2)*.5f;}x=std::clamp(x,right?minimum.x:0.f,right?1.f:maximum.x);y=std::clamp(y,top?minimum.y:0.f,top?1.f:maximum.y);*rect=m_UIAnchorOriginal;RectTransformUtils::SetAnchors(*rect,parentSize,minimum,maximum);}
        else if(m_UIRectHandle==200){auto rotation=glm::rotate(glm::mat4(1),glm::radians(m_UIAnchorOriginal.rotation),glm::vec3(0,0,1))*glm::scale(glm::mat4(1),glm::vec3(m_UIAnchorOriginal.scale,1));auto localDelta=glm::vec2(glm::inverse(rotation)*glm::vec4(delta,0,0));*rect=m_UIAnchorOriginal;RectTransformUtils::SetPivot(*rect,parentSize,glm::clamp(rect->pivot+localDelta/glm::max(m_UIStartSize,glm::vec2(1)),glm::vec2(0),glm::vec2(1)));}
        else if(m_GizmoOp==ImGuizmo::ROTATE){auto center=glm::vec2(r.localToCanvas*glm::vec4(r.size*rect->pivot,0,1));float angle=std::atan2(canvasMouse.y-center.y,canvasMouse.x-center.x);rect->rotation=m_UIStartRotation+glm::degrees(angle-m_UIStartAngle);}
        else if(m_UIRectHandle<0)rect->anchoredPosition=m_UIStartPosition+delta;
        else {auto rotation=glm::rotate(glm::mat4(1),glm::radians(m_UIStartRotation),glm::vec3(0,0,1))*glm::scale(glm::mat4(1),glm::vec3(rect->scale,1));auto d=glm::vec2(glm::inverse(rotation)*glm::vec4(delta,0,0));glm::vec2 sign((m_UIRectHandle==0||m_UIRectHandle==3)?-1:1,m_UIRectHandle<2?-1:1);auto extent=glm::max(m_UIStartSize+d*sign,glm::vec2(1));auto change=extent-m_UIStartSize;rect->sizeDelta=m_UIStartDelta+change;rect->anchoredPosition=m_UIStartPosition+glm::vec2(rotation*glm::vec4(change*(rect->pivot+(sign-glm::vec2(1))*.5f),0,0));}
        m_SceneDirty=true;return true;
    }
    return hovered>=0;
}
}
