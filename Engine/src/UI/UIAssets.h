#pragma once
#include "Assets/AssetManager.h"
#include <unordered_map>
namespace Stardust {
struct SpriteAsset {
    std::string texture;
    glm::vec4 rect{0}; // x,y,width,height from top-left in texture pixels
    glm::vec4 border{0}; // left,bottom,right,top
    glm::vec2 pivot{0.5f};
    float pixelsPerUnit=100;
    static SpriteAsset Load(const std::string& path);
    bool Save(const std::string& path) const;
};
struct FontPage { Ref<RHI::Texture> texture; std::vector<u8> pixels; int x=1,y=1,row=0; bool dirty=true; };
struct FontGlyph { std::shared_ptr<FontPage> page; glm::vec4 uv{0}; glm::vec2 offset{0},size{0}; float advance=0; };
class UIFont {
public:
    static std::shared_ptr<UIFont> Get(RHI::Device& device,const std::string& asset);
    static void Flush(RHI::Device& device);
    static void Clear();
    const FontGlyph& Glyph(RHI::Device& device,unsigned codepoint);
    float Ascent() const {return m_Ascent;}
    float Kerning(unsigned previous,unsigned current) const;
private:
    struct Impl; std::shared_ptr<Impl> m_Impl;
    float m_Ascent=38;
    std::unordered_map<unsigned,FontGlyph> m_Glyphs;
    std::vector<std::shared_ptr<FontPage>> m_Pages;
};
struct TextStyle { glm::vec4 color{1}; float size=24; bool bold=false,italic=false,underline=false,strike=false; std::string font; };
struct TextToken { unsigned codepoint=0; TextStyle style; std::string sprite; };
std::vector<TextToken> ParseRichText(const std::string& text,const TextStyle& base,bool rich);
}
