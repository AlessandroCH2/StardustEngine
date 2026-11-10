#pragma once
#include "Scene/Scene.h"
#include <vector>
namespace Stardust {
class VideoPlayerSystem {
public:
    static void Update(Scene& scene,float dt);
    static void Remove(Scene& scene,EntityID entity);
    static void Clear(Scene& scene);
    // The current frame of a playing Video Player (Material Override reads it)
    static Ref<RHI::Texture> Output(Scene& scene,EntityID entity);
    static const std::vector<const char*>& SupportedExtensions(); // depends on the platform
    static bool IsVideoFile(const std::string& path);
    // Opens a clip and decodes its first second: size, length, audio, and how many frames came out (0 = cannot play)
    struct ClipInfo { int width = 0, height = 0; double duration = 0; u32 channels = 0, sampleRate = 0; int framesDecoded = 0; u64 firstPixel = 0; };
    static ClipInfo Probe(const std::string& clip);
};
}
