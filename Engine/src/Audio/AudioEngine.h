#pragma once
#include "Core/Base.h"
#include "Scene/Registry.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Stardust {

class Scene;

// A group of the Audio Mixer (Settings/AudioMixer.json): volume in dB, effects, parent group
struct AudioMixerGroup {
    std::string name, parent;     // parent empty = Master
    float volume = 0.0f;          // dB
    bool mute = false, solo = false;
    float pitch = 1.0f;
    float lowpass = 0.0f;         // cutoff Hz, 0 = off
    float highpass = 0.0f;        // cutoff Hz, 0 = off
    float reverb = 0.0f, reverbRoom = 0.5f;            // wet 0..1, room size 0..1
    float echo = 0.0f, echoDelay = 0.3f, echoFeedback = 0.4f; // wet 0..1, seconds, 0..0.95
};
struct AudioMixerSnapshot { std::string name; std::vector<AudioMixerGroup> groups; };

// 3D audio through miniaudio: one sound per AudioSource, positioned relative to the AudioListener (or the camera)
class AudioEngine {
public:
    static bool Init();
    static void Shutdown();
    static bool IsReady() { return s_Ready; }

    static void OnRuntimeStart(Scene& scene);
    static void OnRuntimeStop();
    static void Update(Scene& scene);           // listener + source positions, cleanup of finished one-shots

    static void Play(EntityID e);
    static void Stop(EntityID e);
    static void RemoveEntity(EntityID e);
    static bool IsPlaying(EntityID e);
    static void SetVolume(EntityID e, float v);
    static void SetPitch(EntityID e, float p);
    static void PlayOneShot(const std::string& clip, const glm::vec3& position, float volume, bool spatial, const std::string& output = {});

    // Audio Mixer
    static void LoadMixer();       // Settings/AudioMixer.json of the open project
    static bool SaveMixer();
    static void ApplyMixer();      // after editing MixerGroups() in place
    static std::vector<AudioMixerGroup>& MixerGroups();
    static std::vector<AudioMixerSnapshot>& MixerSnapshots();
    static bool SetMixerParameter(const std::string& group, const std::string& parameter, float value);
    static float GetMixerParameter(const std::string& group, const std::string& parameter);
    static bool TransitionToSnapshot(const std::string& name, float seconds);
    static void SetMasterVolume(float v);
    static void SetPaused(bool paused); // the whole mix, while the game is in the background
    // Streams fed by the caller (video soundtracks): float samples, interleaved
    static u64 OpenStream(u32 channels, u32 sampleRate);
    static void PushStream(u64 stream, const float* samples, u32 frames);
    static void SetStreamPlaying(u64 stream, bool playing, float volume, float pitch);
    static void ResetStream(u64 stream);
    static u32 StreamBuffered(u64 stream);            // frames waiting to play
    static void SkipStream(u64 stream, u32 frames);   // drops frames that are late
    static void CloseStream(u64 stream);

    // Editor preview (no scene needed)
    static void PreviewClip(const std::string& clip);
    static void StopPreview();

private:
    static void EnsureSound(Scene& scene, EntityID e);
    static void* MixerGroupNode(const std::string& name);
    static void UpdateMixer(float dt);
    static void ShutdownMixer();
    static inline bool s_Ready = false;
    static inline Scene* s_Scene = nullptr;
};

} // namespace Stardust
