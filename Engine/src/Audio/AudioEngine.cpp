#include "Audio/AudioEngine.h"
#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include "Core/Log.h"
#include "Core/FileSystem.h"
#include "Assets/Pak.h"
#include <fstream>
#include <unordered_map>
#include <cstring>
#include <memory>
#include <mutex>

#pragma warning(push, 0)
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include <miniaudio.h>
#pragma warning(pop)

namespace Stardust {

// The decoder seeks within a pak entry just as it would within a loose file.
struct AudioFile { std::filesystem::path path;std::ifstream loose;u64 cursor=0,size=0,base=0,cipher=0;bool packed=false; };
static ma_vfs_callbacks s_Vfs{};
static void InitVfs(){
 s_Vfs.onOpen=[](ma_vfs*,const char* path,ma_uint32 mode,ma_vfs_file* output)->ma_result{if(mode&MA_OPEN_MODE_WRITE)return MA_ACCESS_DENIED;auto f=std::make_unique<AudioFile>();f->path=std::filesystem::u8path(path);std::filesystem::path archive;f->packed=Pak::Locate(f->path,archive,f->base,f->size,&f->cipher);f->loose.open(f->packed?archive:f->path,std::ios::binary|std::ios::ate);if(!f->loose)return MA_DOES_NOT_EXIST;if(!f->packed)f->size=(u64)f->loose.tellg();f->loose.seekg(f->base);*output=f.release();return MA_SUCCESS;};
 s_Vfs.onClose=[](ma_vfs*,ma_vfs_file file)->ma_result{delete (AudioFile*)file;return MA_SUCCESS;};
 s_Vfs.onRead=[](ma_vfs*,ma_vfs_file file,void* data,size_t count,size_t* read)->ma_result{auto& f=*(AudioFile*)file;count=(size_t)std::min<u64>(count,f.size-f.cursor);u64 n=0;f.loose.read((char*)data,count);n=f.loose.gcount();if(f.cipher)Pak::Decrypt(f.cipher,f.cursor,data,n);f.cursor+=n;if(read)*read=(size_t)n;return n?MA_SUCCESS:MA_AT_END;};
 s_Vfs.onSeek=[](ma_vfs*,ma_vfs_file file,ma_int64 offset,ma_seek_origin origin)->ma_result{auto& f=*(AudioFile*)file;auto at=(ma_int64)(origin==ma_seek_origin_start?0:origin==ma_seek_origin_end?f.size:f.cursor)+offset;if(at<0||(u64)at>f.size)return MA_BAD_SEEK;f.cursor=at;f.loose.clear();f.loose.seekg(f.base+at);return MA_SUCCESS;};
 s_Vfs.onTell=[](ma_vfs*,ma_vfs_file file,ma_int64* at)->ma_result{*at=((AudioFile*)file)->cursor;return MA_SUCCESS;};
 s_Vfs.onInfo=[](ma_vfs*,ma_vfs_file file,ma_file_info* info)->ma_result{info->sizeInBytes=((AudioFile*)file)->size;return MA_SUCCESS;};
}
static ma_engine s_Engine;
static bool s_Paused = false;
static std::unordered_map<EntityID, ma_sound*> s_Sounds;
static std::vector<ma_sound*> s_OneShots;
static ma_sound* s_Preview = nullptr;

static void FreeSound(ma_sound* s) { if (!s) return; ma_sound_uninit(s); delete s; }

static ma_sound* LoadSound(const std::string& clip, bool spatial) {
    if (clip.empty()) return nullptr;
    auto absolute=AssetManager::ToAbsolute(clip);std::string path=FileSystem::Utf8(absolute);
    auto* s = new ma_sound;
    // Long files (music) stream from disk, short ones are decoded once
    std::error_code ec;
    ma_uint32 flags = (Pak::Contains(absolute)?Pak::Size(absolute):std::filesystem::file_size(absolute,ec)) > 2 * 1024 * 1024 ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&s_Engine, path.c_str(), flags, nullptr, nullptr, s) != MA_SUCCESS) {
        SD_ERROR("Audio clip could not be loaded: {}", clip);
        delete s;
        return nullptr;
    }
    return s;
}

static void CloseAllStreams();
bool AudioEngine::Init() {
    if (s_Ready) return true;
    InitVfs();auto config=ma_engine_config_init();config.pResourceManagerVFS=&s_Vfs;
    if (ma_engine_init(&config, &s_Engine) != MA_SUCCESS) { SD_WARN("Audio device unavailable: audio disabled"); return false; }
    s_Ready = true;
    SD_INFO("Audio ready (miniaudio)");
    return true;
}

void AudioEngine::Shutdown() {
    if (!s_Ready) return;
    OnRuntimeStop();
    StopPreview();
    CloseAllStreams();
    ma_engine_uninit(&s_Engine);
    s_Ready = false;
}

void AudioEngine::EnsureSound(Scene& scene, EntityID e) {
    if (s_Sounds.contains(e)) return;
    auto* src = scene.Reg().TryGetEnabled<AudioSourceComponent>(e);
    if (!src) return;
    ma_sound* s = LoadSound(src->clip, src->spatial);
    if (!s) return;
    ma_sound_set_volume(s, src->volume);
    ma_sound_set_pitch(s, src->pitch);
    ma_sound_set_looping(s, src->loop);
    ma_sound_set_min_distance(s, src->minDistance);
    ma_sound_set_max_distance(s, src->maxDistance);
    ma_sound_set_attenuation_model(s, ma_attenuation_model_inverse);
    s_Sounds[e] = s;
}

void AudioEngine::OnRuntimeStart(Scene& scene) {
    s_Scene = &scene;
    if (!s_Ready) return;
    std::vector<EntityID> autoplay;
    scene.Reg().Each<AudioSourceComponent>([&](EntityID e, AudioSourceComponent& a) {
        if (a.playOnStart && scene.IsActiveInHierarchy(e)) autoplay.push_back(e);
    });
    Update(scene);
    for (EntityID e : autoplay) Play(e);
}

void AudioEngine::OnRuntimeStop() {
    for (auto& [e, s] : s_Sounds) FreeSound(s);
    s_Sounds.clear();
    for (auto* s : s_OneShots) FreeSound(s);
    s_OneShots.clear();
    s_Scene = nullptr;
}

void AudioEngine::RemoveEntity(EntityID e){auto it=s_Sounds.find(e);if(it!=s_Sounds.end()){FreeSound(it->second);s_Sounds.erase(it);}}

void AudioEngine::Update(Scene& scene) {
    if (!s_Ready) return;
    Registry& reg = scene.Reg();
    // Listener: the AudioListener entity, else the primary camera
    EntityID listener = kNullEntity;
    reg.Each<AudioListenerComponent>([&](EntityID e, AudioListenerComponent&) { if (!listener && scene.IsActiveInHierarchy(e)) listener = e; });
    if (!listener) reg.Each<CameraComponent>([&](EntityID e, CameraComponent& c) { if (!listener && c.primary) listener = e; });
    if (listener) {
        glm::mat4 w = scene.GetWorldMatrix(listener);
        glm::vec3 p = glm::vec3(w[3]), f = glm::normalize(-glm::vec3(w[2])), u = glm::normalize(glm::vec3(w[1]));
        ma_engine_listener_set_position(&s_Engine, 0, p.x, p.y, p.z);
        ma_engine_listener_set_direction(&s_Engine, 0, f.x, f.y, f.z);
        ma_engine_listener_set_world_up(&s_Engine, 0, u.x, u.y, u.z);
    }
    for (auto it = s_Sounds.begin(); it != s_Sounds.end();) {
        if (!reg.Valid(it->first)||!reg.IsEnabled<AudioSourceComponent>(it->first)) { FreeSound(it->second); it = s_Sounds.erase(it); continue; }
        glm::vec3 p = glm::vec3(scene.GetWorldMatrix(it->first)[3]);
        ma_sound_set_position(it->second, p.x, p.y, p.z);
        ++it;
    }
    std::erase_if(s_OneShots, [](ma_sound* s) { if (ma_sound_at_end(s)) { FreeSound(s); return true; } return false; });
}

void AudioEngine::Play(EntityID e) {
    if (!s_Ready || !s_Scene||!s_Scene->Reg().IsEnabled<AudioSourceComponent>(e)) return;
    EnsureSound(*s_Scene, e);
    if (auto it = s_Sounds.find(e); it != s_Sounds.end()) {
        ma_sound_seek_to_pcm_frame(it->second, 0);
        ma_sound_start(it->second);
    }
}

void AudioEngine::Stop(EntityID e) {
    if (auto it = s_Sounds.find(e); it != s_Sounds.end()) ma_sound_stop(it->second);
}

bool AudioEngine::IsPlaying(EntityID e) {
    auto it = s_Sounds.find(e);
    return it != s_Sounds.end() && ma_sound_is_playing(it->second);
}

void AudioEngine::SetVolume(EntityID e, float v) {
    if (s_Scene) if (auto* a = s_Scene->Reg().TryGet<AudioSourceComponent>(e)) a->volume = v;
    if (auto it = s_Sounds.find(e); it != s_Sounds.end()) ma_sound_set_volume(it->second, v);
}

void AudioEngine::SetPitch(EntityID e, float p) {
    if (s_Scene) if (auto* a = s_Scene->Reg().TryGet<AudioSourceComponent>(e)) a->pitch = p;
    if (auto it = s_Sounds.find(e); it != s_Sounds.end()) ma_sound_set_pitch(it->second, p);
}

void AudioEngine::PlayOneShot(const std::string& clip, const glm::vec3& pos, float volume, bool spatial) {
    if (!s_Ready) return;
    ma_sound* s = LoadSound(clip, spatial);
    if (!s) return;
    ma_sound_set_volume(s, volume);
    ma_sound_set_position(s, pos.x, pos.y, pos.z);
    ma_sound_start(s);
    s_OneShots.push_back(s);
}

void AudioEngine::SetMasterVolume(float v) { if (s_Ready) ma_engine_set_volume(&s_Engine, v); }

void AudioEngine::PreviewClip(const std::string& clip) {
    if (!s_Ready) return;
    StopPreview();
    s_Preview = LoadSound(clip, false);
    if (s_Preview) ma_sound_start(s_Preview);
}

void AudioEngine::StopPreview() {
    FreeSound(s_Preview);
    s_Preview = nullptr;
}

} // namespace Stardust

namespace Stardust {
void AudioEngine::SetPaused(bool paused) {
    if (!s_Ready || paused == s_Paused) return;
    s_Paused = paused;
    if (paused) ma_engine_stop(&s_Engine); else ma_engine_start(&s_Engine);
}
}

namespace Stardust {
namespace {
// The sound reads through this source: an empty buffer plays silence instead of ending the sound
struct StreamSource { ma_data_source_base base; ma_pcm_rb* buffer = nullptr; u32 channels = 0, sampleRate = 0; };
ma_result StreamRead(ma_data_source* source, void* out, ma_uint64 count, ma_uint64* read) {
    auto* s = (StreamSource*)source;
    ma_uint64 done = 0;
    while (done < count) {
        ma_uint32 chunk = (ma_uint32)std::min<ma_uint64>(count - done, 0xFFFFFFFF); void* data = nullptr;
        if (ma_pcm_rb_acquire_read(s->buffer, &chunk, &data) != MA_SUCCESS || chunk == 0) break;
        std::memcpy((float*)out + done * s->channels, data, (size_t)chunk * s->channels * sizeof(float));
        ma_pcm_rb_commit_read(s->buffer, chunk);
        done += chunk;
    }
    if (done < count) std::memset((float*)out + done * s->channels, 0, (size_t)(count - done) * s->channels * sizeof(float));
    if (read) *read = count;
    return MA_SUCCESS;
}
ma_result StreamFormat(ma_data_source* source, ma_format* format, ma_uint32* channels, ma_uint32* rate, ma_channel* map, size_t mapCap) {
    auto* s = (StreamSource*)source;
    *format = ma_format_f32; *channels = s->channels; *rate = s->sampleRate;
    if (map) ma_channel_map_init_standard(ma_standard_channel_map_default, map, mapCap, s->channels);
    return MA_SUCCESS;
}
ma_data_source_vtable s_StreamVtable = { StreamRead, nullptr, StreamFormat, nullptr, nullptr, nullptr, 0 };
struct AudioStream { ma_pcm_rb buffer; StreamSource source; ma_sound sound; u32 channels = 0, sampleRate = 0; bool playing = false; };
std::unordered_map<u64, std::unique_ptr<AudioStream>> s_Streams;
std::mutex s_StreamMutex; // video decoders push from their own threads
u64 s_NextStream = 1;
}
u64 AudioEngine::OpenStream(u32 channels, u32 sampleRate) {
    std::lock_guard lock(s_StreamMutex);
    if (!s_Ready || !channels || !sampleRate) return 0;
    auto stream = std::make_unique<AudioStream>();
    stream->channels = channels;
    // Two seconds of room: the video pushes a little ahead of what plays
    if (ma_pcm_rb_init(ma_format_f32, channels, sampleRate * 2, nullptr, nullptr, &stream->buffer) != MA_SUCCESS) return 0;
    ma_pcm_rb_set_sample_rate(&stream->buffer, sampleRate);
    stream->sampleRate = sampleRate;
    auto config = ma_data_source_config_init(); config.vtable = &s_StreamVtable;
    stream->source.buffer = &stream->buffer; stream->source.channels = channels; stream->source.sampleRate = sampleRate;
    if (ma_data_source_init(&config, &stream->source.base) != MA_SUCCESS) { ma_pcm_rb_uninit(&stream->buffer); return 0; }
    if (ma_sound_init_from_data_source(&s_Engine, &stream->source, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &stream->sound) != MA_SUCCESS) { ma_data_source_uninit(&stream->source.base); ma_pcm_rb_uninit(&stream->buffer); return 0; }
    const u64 id = s_NextStream++;
    s_Streams[id] = std::move(stream);
    return id;
}
void AudioEngine::PushStream(u64 id, const float* samples, u32 frames) {
    std::lock_guard lock(s_StreamMutex);
    auto found = s_Streams.find(id);
    if (found == s_Streams.end()) return;
    auto& rb = found->second->buffer;
    const u32 channels = found->second->channels;
    while (frames) {
        ma_uint32 room = frames; void* out = nullptr;
        if (ma_pcm_rb_acquire_write(&rb, &room, &out) != MA_SUCCESS || room == 0) break; // full: the rest is dropped
        std::memcpy(out, samples, (size_t)room * channels * sizeof(float));
        ma_pcm_rb_commit_write(&rb, room);
        samples += (size_t)room * channels; frames -= room;
    }
}
void AudioEngine::SetStreamPlaying(u64 id, bool playing, float volume, float pitch) {
    std::lock_guard lock(s_StreamMutex);
    auto found = s_Streams.find(id);
    if (found == s_Streams.end()) return;
    auto& stream = *found->second;
    ma_sound_set_volume(&stream.sound, volume);
    ma_sound_set_pitch(&stream.sound, pitch);
    if (playing != stream.playing) { stream.playing = playing; if (playing) ma_sound_start(&stream.sound); else ma_sound_stop(&stream.sound); }
}
u32 AudioEngine::StreamBuffered(u64 id) { std::lock_guard lock(s_StreamMutex); auto found = s_Streams.find(id); return found != s_Streams.end() ? ma_pcm_rb_available_read(&found->second->buffer) : 0; }
void AudioEngine::SkipStream(u64 id, u32 frames) {
    std::lock_guard lock(s_StreamMutex);
    auto found = s_Streams.find(id);
    if (found == s_Streams.end()) return;
    auto& rb = found->second->buffer;
    frames = std::min(frames, (u32)ma_pcm_rb_available_read(&rb));
    while (frames) { ma_uint32 chunk = frames; void* data = nullptr; if (ma_pcm_rb_acquire_read(&rb, &chunk, &data) != MA_SUCCESS || !chunk) break; ma_pcm_rb_commit_read(&rb, chunk); frames -= chunk; }
}
void AudioEngine::ResetStream(u64 id) { std::lock_guard lock(s_StreamMutex); auto found = s_Streams.find(id); if (found != s_Streams.end()) ma_pcm_rb_reset(&found->second->buffer); }
void AudioEngine::CloseStream(u64 id) {
    std::lock_guard lock(s_StreamMutex);
    auto found = s_Streams.find(id);
    if (found == s_Streams.end()) return;
    ma_sound_uninit(&found->second->sound);
    ma_data_source_uninit(&found->second->source.base);
    ma_pcm_rb_uninit(&found->second->buffer);
    s_Streams.erase(found);
}
static void CloseAllStreams() { while (true) { u64 id; { std::lock_guard lock(s_StreamMutex); if (s_Streams.empty()) break; id = s_Streams.begin()->first; } AudioEngine::CloseStream(id); } }
}
