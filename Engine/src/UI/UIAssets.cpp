#include "UI/UIAssets.h"
#include "Core/FileSystem.h"
#include "Assets/AssetMeta.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <sstream>
namespace Stardust {
SpriteAsset SpriteAsset::Load(const std::string& path) {
    SpriteAsset s;
    const bool embedded=!path.ends_with(".sprite");
    // A texture imported as Sprite keeps its sprite in its own .meta, like Unity: no extra file
    auto j=embedded?AssetMeta::Read(path,"sprite"):nlohmann::json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(path)),nullptr,false);
    if(embedded)s.texture=path;
    if(!j.is_object()||j.empty()) return s;
    if(!embedded)s.texture=j.value("texture","");
    s.pixelsPerUnit=j.value("pixelsPerUnit",100.0f);
    auto read=[&](const char* key,float* value,int count){if(j.contains(key)&&j[key].is_array()&&j[key].size()==count) for(int i=0;i<count;++i)value[i]=j[key][i].get<float>();};
    try {read("rect",&s.rect.x,4);read("border",&s.border.x,4);read("pivot",&s.pivot.x,2);}catch(...){}
    return s;
}
bool SpriteAsset::Save(const std::string& path) const {
    nlohmann::json j={{"rect",{rect.x,rect.y,rect.z,rect.w}},{"border",{border.x,border.y,border.z,border.w}},{"pivot",{pivot.x,pivot.y}},{"pixelsPerUnit",pixelsPerUnit}};
    if(!path.ends_with(".sprite"))return AssetMeta::Write(path,"sprite",j);
    j["texture"]=texture;
    return FileSystem::WriteText(AssetManager::ToAbsolute(path),j.dump(2));
}
struct UIFont::Impl {std::vector<u8> bytes; stbtt_fontinfo info{}; float scale=1;};
static std::unordered_map<std::string,std::shared_ptr<UIFont>> fonts;
std::shared_ptr<UIFont> UIFont::Get(RHI::Device& device,const std::string& asset) {
    auto path=asset.empty()?std::filesystem::path("C:/Windows/Fonts/segoeui.ttf"):AssetManager::ToAbsolute(asset);
#ifndef _WIN32
    if(asset.empty())for(const char* candidate:{"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"})if(std::filesystem::exists(candidate)){path=candidate;break;}
#endif
    auto key=FileSystem::Utf8(path);
    auto found=fonts.find(key); if(found!=fonts.end()) return found->second;
    auto font=std::make_shared<UIFont>(); font->m_Impl=std::make_shared<Impl>();
    font->m_Impl->bytes=FileSystem::ReadBinary(path);
    if(font->m_Impl->bytes.empty()||!stbtt_InitFont(&font->m_Impl->info,font->m_Impl->bytes.data(),0)) {
        SD_WARN("Cannot load UI font: {}",path.string());
        if(!asset.empty()) {auto fallback=Get(device,"");fonts[key]=fallback;return fallback;}
        return nullptr;
    }
    font->m_Impl->scale=stbtt_ScaleForPixelHeight(&font->m_Impl->info,48);
    int ascent,descent,gap;stbtt_GetFontVMetrics(&font->m_Impl->info,&ascent,&descent,&gap);
    font->m_Ascent=ascent*font->m_Impl->scale;
    fonts[key]=font; return font;
}
const FontGlyph& UIFont::Glyph(RHI::Device& device,unsigned cp) {
    if(auto it=m_Glyphs.find(cp);it!=m_Glyphs.end())return it->second;
    auto& impl=*m_Impl;unsigned actual=stbtt_FindGlyphIndex(&impl.info,cp)?cp:'?';
    int advance,bearing;stbtt_GetCodepointHMetrics(&impl.info,actual,&advance,&bearing);
    FontGlyph g;g.advance=advance*impl.scale;
    int w=0,h=0,xoff=0,yoff=0;
    auto* sdf=stbtt_GetCodepointSDF(&impl.info,impl.scale,actual,6,128,24,&w,&h,&xoff,&yoff);
    if(sdf&&w>0&&h>0&&w<1022&&h<1022) {
        if(m_Pages.empty())m_Pages.push_back(std::make_shared<FontPage>());
        auto page=m_Pages.back();
        if(page->x+w+1>=1024){page->x=1;page->y+=page->row+1;page->row=0;}
        if(page->y+h+1>=1024){page=std::make_shared<FontPage>();m_Pages.push_back(page);}
        if(!page->texture) {
            page->pixels.resize(1024*1024);
            RHI::TextureDesc d;d.name="SDF Font Atlas";d.width=d.height=1024;d.format=RHI::Format::RGBA8_UNORM;d.usage=RHI::TextureUsage::Sampled|RHI::TextureUsage::CopyDst;
            page->texture=device.CreateTexture(d);
        }
        for(int y=0;y<h;++y)memcpy(page->pixels.data()+(page->y+y)*1024+page->x,sdf+y*w,w);
        g.page=page;g.offset={(float)xoff,(float)yoff};g.size={(float)w,(float)h};
        g.uv={page->x/1024.0f,page->y/1024.0f,(page->x+w)/1024.0f,(page->y+h)/1024.0f};
        page->x+=w+1;page->row=std::max(page->row,h);page->dirty=true;
    }
    if(sdf)stbtt_FreeSDF(sdf,nullptr);
    return m_Glyphs.emplace(cp,std::move(g)).first->second;
}
float UIFont::Kerning(unsigned a,unsigned b) const{return stbtt_GetCodepointKernAdvance(&m_Impl->info,a,b)*m_Impl->scale;}
void UIFont::Flush(RHI::Device& device) {
    for(auto& [key,font]:fonts)for(auto& page:font->m_Pages)if(page->dirty&&page->texture) {
        std::vector<u8> rgba(1024*1024*4,255);for(size_t i=0;i<page->pixels.size();++i)rgba[i*4+3]=page->pixels[i];
        device.UploadTexture(page->texture.get(),rgba.data(),rgba.size());page->dirty=false;
    }
}
void UIFont::Clear(){fonts.clear();}
static glm::vec4 ParseColor(std::string text,glm::vec4 fallback) {
    if(text=="red")return {1,0,0,1};if(text=="green")return {0,1,0,1};if(text=="blue")return {0,0,1,1};if(text=="white")return {1,1,1,1};if(text=="black")return {0,0,0,1};if(text=="yellow")return {1,1,0,1};
    if(!text.empty()&&text[0]=='#')text.erase(0,1);
    try {if(text.size()==6||text.size()==8){auto v=std::stoul(text,nullptr,16);if(text.size()==6)v=(v<<8)|255;return {((v>>24)&255)/255.f,((v>>16)&255)/255.f,((v>>8)&255)/255.f,(v&255)/255.f};}}catch(...){}
    return fallback;
}
std::vector<TextToken> ParseRichText(const std::string& text,const TextStyle& base,bool rich) {
    std::vector<TextToken> result;TextStyle style=base;
    std::vector<std::pair<std::string,TextStyle>> stack;
    for(size_t i=0;i<text.size();) {
        if(rich&&text[i]=='<') {
            auto end=text.find('>',i);
            if(end!=std::string::npos) {
                auto tag=text.substr(i+1,end-i-1);auto equals=tag.find('=');auto name=tag.substr(0,equals);
                auto value=equals==std::string::npos?std::string():tag.substr(equals+1);
                if(value.size()>1&&(value.front()=='"'||value.front()=='\''))value=value.substr(1,value.size()-2);
                if(name=="br"||name=="br/"){result.push_back({'\n',style,{}});i=end+1;continue;}
                if(name=="sprite"){result.push_back({0,style,value});i=end+1;continue;}
                if(!name.empty()&&name[0]=='/') {
                    auto closing=name.substr(1);auto found=std::find_if(stack.rbegin(),stack.rend(),[&](auto& item){return item.first==closing;});
                    if(found!=stack.rend()){style=found->second;stack.erase(found.base()-1,stack.end());i=end+1;continue;}
                }
                if(name=="b"||name=="i"||name=="u"||name=="s"||name=="color"||name=="size"||name=="font"||name=="alpha") {
                    stack.push_back({name,style});
                    if(name=="b")style.bold=true;if(name=="i")style.italic=true;if(name=="u")style.underline=true;if(name=="s")style.strike=true;
                    if(name=="color")style.color=ParseColor(value,style.color);
                    if(name=="font")style.font=value;
                    try {if(name=="size")style.size=std::clamp(std::stof(value),1.f,512.f);if(name=="alpha")style.color.a=std::stoul(value[0]=='#'?value.substr(1):value,nullptr,16)/255.f;}catch(...){}
                    i=end+1;continue;
                }
            }
        }
        unsigned cp=(unsigned char)text[i++];int remaining=0;
        if(cp>=0xf0){cp&=7;remaining=3;}else if(cp>=0xe0){cp&=15;remaining=2;}else if(cp>=0xc0){cp&=31;remaining=1;}
        while(remaining--&&i<text.size())cp=(cp<<6)|((unsigned char)text[i++]&63);
        result.push_back({cp,style,{}});
    }
    return result;
}
}
