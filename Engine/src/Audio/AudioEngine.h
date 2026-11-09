#pragma once
#include "Core/Base.h"
#include "Scene/Registry.h"
#include <glm/glm.hpp>

namespace Stardust {

class Scene;

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
    static void PlayOneShot(const std::string& clip, const glm::vec3& position, float volume, bool spatial);
    static void SetMasterVolume(float v);

    // Editor preview (no scene needed)
    static void PreviewClip(const std::string& clip);
    static void StopPreview();

private:
    static void EnsureSound(Scene& scene, EntityID e);
    static inline bool s_Ready = false;
    static inline Scene* s_Scene = nullptr;
};

} // namespace Stardust
