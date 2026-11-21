#include "Core/Window.h"
#include "Core/FileSystem.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "UI/CanvasRenderer.h"
#include "UI/UIAssets.h"
#include <imgui.h>
#include <iostream>
#include <cmath>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
using namespace Stardust;
int main(int argc,char** argv){try{
    FileSystem::SetEngineRoot(argc>1?std::filesystem::path(argv[1]):std::filesystem::current_path());
    ImGui::CreateContext();WindowDesc wd;wd.width=640;wd.height=360;wd.visible=false;Window window(wd);
    RHI::DeviceDesc options;options.vsync=false;auto device=RHI::Device::Create(options,window);
    AssetManager::Init(*device,FileSystem::EngineRoot()/"Assets");MeshLibrary::Init(*device);
    auto renderer=MakeScope<SceneRenderer>(*device);renderer->Resize(640,360);
    auto readback=device->CreateBuffer({640*360*4,RHI::BufferUsage::CopyDst,RHI::MemoryUsage::GPUToCPU,"Text outline regression"});
    Scene scene;auto canvas=scene.CreateEntity("Canvas");auto& c=scene.Reg().Add<CanvasComponent>(canvas);c.referenceResolution={640,360};
    auto label=scene.CreateEntity("Outlined label");scene.SetParent(label,canvas);
    auto& rect=scene.Reg().Add<RectTransformComponent>(label);rect.anchorMin=rect.anchorMax=rect.pivot={0,0};rect.anchoredPosition={160,120};rect.sizeDelta={150,100};
    auto& text=scene.Reg().Add<UITextComponent>(label);text.text="HH";text.fontSize=72;text.color={1,1,1,1};text.outlineColor={1,0,0,1};text.outlineWidth=.38f;text.wordWrap=false;text.alignment=0;text.verticalAlignment=1;
    RenderCamera camera;camera.position={0,0,5};camera.view=glm::lookAt(camera.position,glm::vec3(0),glm::vec3(0,1,0));camera.projection=PerspectiveReverseZ(glm::radians(60.f),640.f/360,.1f,100);
    auto render=[&](){window.PollEvents();if(!device->BeginFrame())throw std::runtime_error("No render frame");auto& cmd=device->Cmd();auto env=scene.GatherEnvironment();env.cloudCoverage=0;env.fxaa=false;renderer->BeginScene(camera,env,0);CanvasRenderer::Submit(scene,*renderer,*device,{640,360},false);renderer->Render(cmd);cmd.CopyTextureToBuffer(renderer->Output(),readback.get(),0,0,640,360);device->EndFrame();device->WaitIdle();auto* bytes=(const u8*)readback->Mapped();return std::vector<u8>(bytes,bytes+640*360*4);};
    auto red=[](const std::vector<u8>& image,size_t i){return image[i]>80&&image[i]>image[i+1]+35&&image[i]>image[i+2]+35;};
    int failures=0;
    for(float size:{24.f,48.f,96.f})for(bool bold:{false,true})for(int alignment:{0,1,2}){
        text.fontSize=size;text.text=bold?"<b>HH</b>":"HH";text.alignment=alignment;
        auto font=UIFont::Get(*device,"");rect.sizeDelta={2*font->Glyph(*device,'H').advance*size/48.f,size*1.5f};
        text.overflow=1;auto reference=render();text.overflow=0;auto clipped=render();
        int outline=0,missing=0;for(size_t i=0;i<reference.size();i+=4)if(red(reference,i)){outline++;if(!red(clipped,i))missing++;}
        if(outline<15||missing>4){std::cout<<"size "<<size<<" bold "<<bold<<" alignment "<<alignment<<": missing "<<missing<<" / "<<outline<<" outline pixels\n";failures++;}
        if(size==96&&!bold&&alignment==0)stbi_write_png((FileSystem::EngineRoot()/"build/TextOutlineRegression.png").string().c_str(),640,360,4,clipped.data(),640*4);
    }
    // Adjacent outlines and shadows must never paint over the fill of earlier glyphs.
    for(bool shadow:{false,true})for(bool bold:{false,true}){
        text.fontSize=72;text.text=bold?"<b>MONEY100</b>":"MONEY100";text.alignment=0;text.characterSpacing=-12;
        text.overflow=1;rect.sizeDelta={450,110};text.outlineWidth=0;text.shadowColor={0,0,0,0};
        auto face=render();text.outlineWidth=.38f;if(shadow){text.shadowColor={0,0,0,1};text.shadowOffset={-12,0};}
        auto outlined=render();int core=0,covered=0;
        for(size_t i=0;i<face.size();i+=4)if(face[i]>=254&&face[i+1]>=254&&face[i+2]>=254){core++;if(outlined[i]<235||outlined[i+1]<235||outlined[i+2]<235)covered++;}
        if(core<100||covered>4){std::cout<<"Fill covered by neighbour effect: bold "<<bold<<" shadow "<<shadow<<": "<<covered<<" / "<<core<<" core pixels\n";failures++;}
        if(!bold&&!shadow)stbi_write_png((FileSystem::EngineRoot()/"build/TextOutlineOverlap.png").string().c_str(),640,360,4,outlined.data(),640*4);
    }
    text.shadowColor={0,0,0,0};text.characterSpacing=0;
    // Parent masks still clip the effect, even when the label itself allows overflow.
    text.fontSize=72;text.text="HHHHHHHH";text.overflow=1;rect.sizeDelta={300,110};rect.anchoredPosition={0,0};
    auto mask=scene.CreateEntity("Mask");scene.SetParent(mask,canvas);scene.SetParent(label,mask);
    auto& maskRect=scene.Reg().Add<RectTransformComponent>(mask);maskRect.anchorMin=maskRect.anchorMax=maskRect.pivot={0,0};maskRect.anchoredPosition={160,120};maskRect.sizeDelta={50,110};scene.Reg().Add<UIMaskComponent>(mask).showGraphic=false;
    auto masked=render();int escaped=0;for(int y=0;y<360;y++)for(int x=0;x<640;x++)if(red(masked,(y*640+x)*4)&&(x<160||x>=210||y<130||y>=240))escaped++;
    if(escaped)throw std::runtime_error("Text outline escaped its parent mask");
    CanvasRenderer::Clear();renderer.reset();readback.reset();UIFont::Clear();MeshLibrary::Shutdown();AssetManager::Shutdown();device.reset();ImGui::DestroyContext();
    if(failures)throw std::runtime_error("Outlined text was clipped by its own layout rectangle");
    std::cout<<"Text outline GPU regression passed: 18 size/bold/alignment combinations, 4 overlapping fill/effect cases and parent masking\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
