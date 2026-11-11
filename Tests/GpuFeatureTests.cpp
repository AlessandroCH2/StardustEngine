#include "Core/Window.h"
#include "Core/FileSystem.h"
#include "Project/Project.h"
#include "Project/PackageManager.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptEngine.h"
#include "Physics/PhysicsWorld.h"
#include "UI/CanvasRenderer.h"
#include <iostream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include "EditorIcons.h"
#include "SceneViewOverlay.h"
#include "AssetUI.h"
namespace Stardust {void ApplyEditorTheme();}
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
using namespace Stardust;
int main(){
    // The fixtures live in the source tree: the first parent folder of the executable that has Tests/Fixtures
    for (auto dir = Stardust::FileSystem::ExecutableDir(); !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::is_directory(dir / "Tests" / "Fixtures")) { Stardust::FileSystem::SetEngineRoot(dir); break; }
        if (dir == dir.parent_path()) break;
    }
 try{
    ImGui::CreateContext();ImGui::GetIO().DeltaTime=1.f/30;
    auto root=FileSystem::EngineRoot()/"Examples/FeatureLab";
    if(!Project::Load(root/"FeatureLab.sdproject"))throw std::runtime_error("Feature Lab missing");
    PackageManager::EnsureDefault();
    WindowDesc desc;desc.width=960;desc.height=540;desc.visible=false;Window window(desc);
    RHI::DeviceDesc options;options.validation=true;options.vsync=false;auto device=RHI::Device::Create(options,window);
    MeshLibrary::Init(*device);AssetManager::Init(*device,root/"Assets");PhysicsWorld::InitJolt();
    if(!ScriptEngine::Init())throw std::runtime_error("C# host failed");
    auto scene=SceneSerializer::Load(root/"Assets/Scenes/FeatureLab.scene");if(!scene)throw std::runtime_error("Feature Lab scene failed");
    scene->PrepareTerrains(*device);scene->OnRuntimeStart();
    auto renderer=MakeScope<SceneRenderer>(*device);renderer->Resize(960,540);
    RHI::BufferDesc readback;readback.name="Feature Lab capture";readback.size=960*540*4;readback.memory=RHI::MemoryUsage::GPUToCPU;readback.usage=RHI::BufferUsage::CopyDst;auto capture=device->CreateBuffer(readback);
    RHI::BufferDesc mapReadback=readback;mapReadback.size=256*192*4;auto mapCapture=device->CreateBuffer(mapReadback);
    for(int i=0;i<20;++i){
        window.PollEvents();if(!device->BeginFrame())continue;scene->OnUpdate(1.f/30);auto& cmd=device->Cmd();scene->RenderCameraTargets(*device,cmd,i/30.f);
        RenderCamera camera;if(!scene->GetPrimaryCamera(camera,960.f/540))throw std::runtime_error("Main camera missing");
        renderer->BeginScene(camera,scene->GatherEnvironment(),i/30.f);scene->SubmitToRenderer(*renderer,*device);CanvasRenderer::Submit(*scene,*renderer,*device,{960,540},false);renderer->Render(cmd);
        auto map=AssetManager::GetTexture("Textures/Minimap.rendertexture",false);if(!map||map->Desc().width!=256||map->Desc().height!=192)throw std::runtime_error("Render texture dimensions incorrect");
        cmd.CopyTextureToBuffer(renderer->Output(),capture.get(),0,0,960,540);cmd.CopyTextureToBuffer(map.get(),mapCapture.get(),0,0,256,192);device->EndFrame();
    }
    device->WaitIdle();auto* pixels=(const unsigned char*)capture->Mapped();auto* mapPixels=(const unsigned char*)mapCapture->Mapped();
    if(!pixels||!mapPixels)throw std::runtime_error("GPU capture unavailable");
    int red=0,mapRed=0;for(int i=0;i<960*540;++i)if(pixels[i*4]>180&&pixels[i*4+1]<100&&pixels[i*4+2]<120)red++;
    for(int i=0;i<256*192;++i)if(mapPixels[i*4]>180&&mapPixels[i*4+1]<100&&mapPixels[i*4+2]<120)mapRed++;
    auto output=FileSystem::EngineRoot()/"build/FeatureLab.png";stbi_write_png(output.string().c_str(),960,540,4,pixels,960*4);
    stbi_write_png((FileSystem::EngineRoot()/"build/FeatureLabMinimap.png").string().c_str(),256,192,4,mapPixels,256*4);
    if(red<5||mapRed>red/4)throw std::runtime_error("Layer-filtered minimap pixel check failed");
    std::cout<<"GPU render target, layer filtering, sprite, water and UI video passed. Red pixels: "<<red<<"; map: "<<mapRed<<"\n";

    {
        RenderCamera camera;scene->GetPrimaryCamera(camera,960.f/540);auto render=[&](Scene& probe,float time){window.PollEvents();if(!device->BeginFrame())throw std::runtime_error("Probe frame unavailable");auto& cmd=device->Cmd();auto env=probe.GatherEnvironment();env.cloudCoverage=0;env.fxaa=false;renderer->BeginScene(camera,env,time);probe.SubmitToRenderer(*renderer,*device);renderer->Render(cmd);cmd.CopyTextureToBuffer(renderer->Output(),capture.get(),0,0,960,540);device->EndFrame();device->WaitIdle();auto* data=(const u8*)capture->Mapped();return std::vector<u8>(data,data+960*540*4);};
        auto save=[&](const char* name,const std::vector<u8>& data){stbi_write_png((FileSystem::EngineRoot()/"build"/name).string().c_str(),960,540,4,data.data(),960*4);};
        Scene probe;auto ocean=probe.CreatePrimitive(PrimitiveType::Plane,"Water motion probe");probe.Reg().Get<TransformComponent>(ocean).scale={15,1,15};auto& surface=probe.Reg().Get<MeshRendererComponent>(ocean);surface.material.shader=ShaderType::Water;surface.material.doubleSided=true;probe.Reg().Get<MeshRendererComponent>(ocean).material.propertiesJson=R"({"WaveSpeed":0,"WaveHeight":0.5,"WindDirection":[1,0]})";
        auto bottom=probe.CreatePrimitive(PrimitiveType::Plane,"Bottom");probe.Reg().Get<TransformComponent>(bottom).position={0,-1.5f,0};probe.Reg().Get<TransformComponent>(bottom).scale={3,1,3};probe.Reg().Get<MeshRendererComponent>(bottom).material.color={.65f,.5f,.25f,1};
        auto stillA=render(probe,0),stillB=render(probe,17);if(stillA!=stillB)throw std::runtime_error("Water speed zero did not freeze the surface");probe.Reg().Get<MeshRendererComponent>(ocean).material.propertiesJson=R"({"WaveSpeed":1,"WaveHeight":0.5,"WindDirection":[1,0]})";auto moving=render(probe,4);if(moving==stillB)throw std::runtime_error("Water speed did not reach shader");probe.Reg().Get<MeshRendererComponent>(ocean).material.propertiesJson=R"({"WaveSpeed":1,"WaveHeight":0.5,"WindDirection":[0,1]})";auto changedWind=render(probe,4);if(changedWind==moving)throw std::runtime_error("Water direction did not reach shader");save("WaterMotion.png",moving);save("WaterDirection.png",changedWind);
        Scene materialScene;auto object=materialScene.CreatePrimitive(PrimitiveType::Cube,"Custom properties probe");materialScene.Reg().Get<TransformComponent>(object).scale={5,5,5};auto& material=materialScene.Reg().Get<MeshRendererComponent>(object).material;material.customPixelShader=(FileSystem::EngineRoot()/"build/TestShaders/CustomProperties.ps.spv").generic_string();FileSystem::WriteText(std::filesystem::path(material.customPixelShader+".properties.json"),R"({"properties":[{"name":"Tint","type":"color","default":[1,0,0,1]},{"name":"Brightness","type":"float","default":1}]})");material.propertiesJson=R"({"Tint":[0,1,0,1],"Brightness":1})";auto green=render(materialScene,0);material.propertiesJson=R"({"Tint":[1,0,1,1],"Brightness":1})";auto magenta=render(materialScene,0);int greenPixels=0,magentaPixels=0;for(size_t i=0;i<green.size();i+=4){if(green[i]<100&&green[i+1]>150&&green[i+2]<100)greenPixels++;if(magenta[i]>150&&magenta[i+1]<100&&magenta[i+2]>150)magentaPixels++;}if(greenPixels<500||magentaPixels<500)throw std::runtime_error("Custom shader property GPU colors incorrect");save("CustomShaderProperties.png",magenta);std::cout<<"Water freeze/speed/direction and custom shader GPU properties passed.\n";
    }

    {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={960,540};if(std::filesystem::exists("C:/Windows/Fonts/segoeui.ttf"))io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",17);else io.Fonts->AddFontDefault();ApplyEditorTheme();if(ImGui::GetStyle().FramePadding.y<3)throw std::runtime_error("Global button/tab padding is too small");device->ImGuiInit();RHI::TextureDesc desc;desc.width=960;desc.height=540;desc.format=device->BackBuffer()->Desc().format;desc.usage=RHI::TextureUsage::RenderTarget|RHI::TextureUsage::CopySrc;desc.name="Editor controls preview";auto preview=device->CreateTexture(desc);
        float headerY=-1;Material material;material.shader=ShaderType::Water;
        for(int frame=0;frame<3;++frame){if(!device->BeginFrame())throw std::runtime_error("UI preview frame unavailable");device->ImGuiNewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({960,540});ImGui::Begin("Editor controls",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar);
        EditorIcons::ToolButton("##Move",EditorIcons::Tool::Move,true,"Move");ImGui::SameLine();EditorIcons::ToolButton("##Rotate",EditorIcons::Tool::Rotate,false,"Rotate");ImGui::SameLine();EditorIcons::ToolButton("##Scale",EditorIcons::Tool::Scale,false,"Scale");ImGui::SameLine();ImGui::TextUnformatted("Scene");ImGui::Separator();
        ImGui::BeginChild("Settings",{430,0},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar);int selected=2;const char* tabs[]={"General","Packages","Input","Layers","Tags"};float y=ImGui::GetCursorScreenPos().y;if(headerY>=0&&headerY!=y)throw std::runtime_error("Settings header scrolled");headerY=y;EditorIcons::SettingsTabs("PreviewTabs",selected,tabs,5);ImGui::BeginChild("Scrolling body",{0,0});if(frame>0)ImGui::SetScrollY(150);for(int i=0;i<60;++i)ImGui::Text("Input action %d",i);ImGui::EndChild();ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Inspector",{0,0},ImGuiChildFlags_Borders);bool remove=false;bool componentEnabled=true;ImGui::PushID("ScriptHeader");if(EditorIcons::ComponentHeader("Button Extend (Script)",&componentEnabled,true,remove)){ImGui::TextDisabled("Script");ImGui::SameLine(180);ImGui::Text("ButtonExtend");ImGui::TextUnformatted("Navigation");ImGui::SameLine(180);ImGui::SetNextItemWidth(-1);int navigation=0;ImGui::Combo("##nav",&navigation,"None\0Automatic\0");ImGui::TextUnformatted("Transition");ImGui::SameLine(180);ImGui::SetNextItemWidth(-1);int transition=0;ImGui::Combo("##transition",&transition,"Color Tint\0Animation\0");for(const char* label:{"Normal Color","Highlighted Color","Pressed Color"}){ImGui::TextUnformatted(label);ImGui::SameLine(180);ImGui::SetNextItemWidth(-1);float color[4]={1,1,1,1};EditorIcons::ColorField(label,color);}}ImGui::PopID();ImGui::Spacing();for(const char* type:{"Transform","Camera","Mesh Renderer","Environment","Audio Source","Animator"}){ImGui::PushID(type);EditorIcons::ComponentHeader(type,std::string_view(type)=="Transform"?nullptr:&componentEnabled,true,remove);ImGui::PopID();}ImGui::Separator();AssetUI::MaterialEditor(material);ImGui::EndChild();ImGui::End();ImGui::Render();device->ImGuiRender(preview.get());device->Cmd().CopyTextureToBuffer(preview.get(),capture.get(),0,0,960,540);device->EndFrame();device->WaitIdle();}
        auto* data=(const u8*)capture->Mapped();std::vector<u8> rgba(data,data+960*540*4);if(desc.format==RHI::Format::BGRA8_UNORM||desc.format==RHI::Format::BGRA8_SRGB)for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);stbi_write_png((FileSystem::EngineRoot()/"build/EditorControls.png").string().c_str(),960,540,4,rgba.data(),960*4);
        if(!device->BeginFrame())throw std::runtime_error("Clip test frame unavailable");device->ImGuiNewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({960,540});ImGui::Begin("Scene overlay clipping",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);auto* draw=ImGui::GetWindowDrawList();ImVec2 clipMin{120,140},clipMax{840,450};draw->AddRectFilled(clipMin,clipMax,IM_COL32(25,35,45,255));{SceneViewOverlayClip clip(clipMin,clipMax);draw->AddLine({0,70},{960,470},IM_COL32(255,0,255,255),5);draw->AddRect({40,80},{920,400},IM_COL32(255,0,255,255),0,0,3);}ImGui::End();ImGui::Render();device->ImGuiRender(preview.get());device->Cmd().CopyTextureToBuffer(preview.get(),capture.get(),0,0,960,540);device->EndFrame();device->WaitIdle();data=(const u8*)capture->Mapped();rgba.assign(data,data+960*540*4);if(desc.format==RHI::Format::BGRA8_UNORM||desc.format==RHI::Format::BGRA8_SRGB)for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);int inside=0,outside=0;for(int y=0;y<540;++y)for(int x=0;x<960;++x){auto offset=(y*960+x)*4;if(rgba[offset]>220&&rgba[offset+1]<40&&rgba[offset+2]>220){if(x<120||x>=840||y<140||y>=450)outside++;else inside++;}}if(outside||inside<100)throw std::runtime_error("Scene gizmos escaped the viewport image clip");stbi_write_png((FileSystem::EngineRoot()/"build/SceneOverlayClip.png").string().c_str(),960,540,4,rgba.data(),960*4);std::cout<<"Scene overlay clipping and global control padding passed.\n";
        device->ImGuiShutdown();preview.reset();
    }
    scene->OnRuntimeStop();CanvasRenderer::Clear();scene.reset();renderer.reset();capture.reset();mapCapture.reset();ScriptEngine::Shutdown();MeshLibrary::Shutdown();AssetManager::Shutdown();PhysicsWorld::ShutdownJolt();device.reset();ImGui::DestroyContext();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
