#pragma once
#include "RHI/RHI.h"
#include <nlohmann/json.hpp>
namespace Stardust {
struct TextureImportSettings {
    int type=0,maxSize=4096,filter=2,wrap=1; // Default/Normal/Sprite; point/bilinear/aniso; clamp/repeat
    bool srgb=true,mipmaps=true,readable=false;
    static TextureImportSettings Load(const std::string& asset);
    bool Save(const std::string& asset)const;
    RHI::SamplerIndex Sampler()const;
};
}
