#include "UI/VideoPlayer.h"
#include "Assets/AssetManager.h"
#include "Assets/Pak.h"
#include "Core/Log.h"
#include <fstream>
#define PL_MPEG_IMPLEMENTATION
#include "../../ThirdParty/pl_mpeg.h"
namespace Stardust {
namespace {
struct State {
    std::string clip,target;std::ifstream file;u64 offset=0,size=0,position=0;
    plm_t* decoder=nullptr;Ref<RHI::Texture> texture;std::vector<u8> pixels;float lastTime=0;
    ~State(){if(decoder)plm_destroy(decoder);if(!target.empty())AssetManager::SetRuntimeTexture(target,nullptr);}
};
std::unordered_map<Scene*,std::unordered_map<EntityID,Scope<State>>> players;
void Load(plm_buffer_t* buffer,void* user){auto& state=*(State*)user;u8 data[65536];auto amount=std::min<u64>(sizeof(data),state.size-state.position);if(amount){state.file.read((char*)data,amount);auto read=state.file.gcount();state.position+=read;plm_buffer_write(buffer,data,(size_t)read);}if(state.position>=state.size)plm_buffer_signal_end(buffer);}
void Seek(plm_buffer_t*,size_t position,void* user){auto& s=*(State*)user;s.position=std::min<u64>(position,s.size);s.file.clear();s.file.seekg(s.offset+s.position);}
size_t Tell(plm_buffer_t*,void* user){return(size_t)((State*)user)->position;}
void Frame(plm_t*,plm_frame_t* frame,void* user){auto& s=*(State*)user;auto* device=AssetManager::Device();if(!device)return;
    if(!s.texture||s.texture->Desc().width!=frame->width||s.texture->Desc().height!=frame->height){RHI::TextureDesc d;d.width=frame->width;d.height=frame->height;d.format=RHI::Format::RGBA8_SRGB;d.usage=RHI::TextureUsage::Sampled|RHI::TextureUsage::CopyDst;d.name="Video Frame";s.texture=device->CreateTexture(d);AssetManager::SetRuntimeTexture(s.target,s.texture);}
    s.pixels.resize((size_t)frame->width*frame->height*4);plm_frame_to_rgba(frame,s.pixels.data(),frame->width*4);for(size_t i=3;i<s.pixels.size();i+=4)s.pixels[i]=255;device->UploadTexture(s.texture.get(),s.pixels.data(),s.pixels.size());
}
}
void VideoPlayerSystem::Remove(Scene& scene,EntityID e){if(auto it=players.find(&scene);it!=players.end()){it->second.erase(e);if(it->second.empty())players.erase(it);}}
void VideoPlayerSystem::Clear(Scene& scene){players.erase(&scene);}
void VideoPlayerSystem::Update(Scene& scene,float dt){
    scene.Reg().Each<VideoPlayerComponent>([&](EntityID e,VideoPlayerComponent& c){
        if(!scene.IsActiveInHierarchy(e)||c.clip.empty())return;
        auto& state=players[&scene][e];
        if(!state||state->clip!=c.clip||(!c.targetTexture.empty()&&state->target!=c.targetTexture)){
            state=MakeScope<State>();state->clip=c.clip;state->target=c.targetTexture.empty()?"runtime:video:"+std::to_string(scene.Reg().Get<IDComponent>(e).uuid):c.targetTexture;
            auto path=AssetManager::ToAbsolute(c.clip),archive=path;if(!Pak::Locate(path,archive,state->offset,state->size)){std::error_code ec;state->size=std::filesystem::file_size(path,ec);if(ec){SD_WARN("Video not found: {}",c.clip);c.playing=false;return;}}
            state->file.open(archive,std::ios::binary);state->file.seekg(state->offset);
            auto* buffer=plm_buffer_create_with_callbacks(Load,Seek,Tell,state->size,state.get());state->decoder=plm_create_with_buffer(buffer,TRUE);
            if(!state->decoder||!plm_has_headers(state->decoder)||!plm_get_num_video_streams(state->decoder)){SD_WARN("Unsupported video: {} (expected MPEG-1 .mpg)",c.clip);c.playing=false;return;}
            plm_set_audio_enabled(state->decoder,FALSE);plm_set_video_decode_callback(state->decoder,Frame,state.get());c.duration=(float)plm_get_duration(state->decoder);plm_seek(state->decoder,std::clamp<double>(c.time,0,c.duration),TRUE);state->lastTime=c.time;
        }
        if(!state->decoder)return;
        if(scene.Reg().Has<RectTransformComponent>(e)){
            if(!scene.Reg().Has<UIImageComponent>(e))scene.Reg().Add<UIImageComponent>(e);
            auto& image=scene.Reg().Get<UIImageComponent>(e);image.texture=state->target;image.preserveAspect=true;
        }
        plm_set_loop(state->decoder,c.loop);
        if(std::abs(c.time-state->lastTime)>.05f)plm_seek(state->decoder,std::clamp<double>(c.time,0,c.duration),TRUE);
        if(c.playing)plm_decode(state->decoder,std::clamp(dt,0.f,.1f)*std::clamp(c.speed,0.f,8.f));
        state->lastTime=c.time=(float)plm_get_time(state->decoder);if(plm_has_ended(state->decoder)&&!c.loop)c.playing=false;
    });
}
}
