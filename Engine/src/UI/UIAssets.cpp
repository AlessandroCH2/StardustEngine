#include "UI/UIAssets.h"
#include "Core/FileSystem.h"
#include "Assets/AssetMeta.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace Stardust {
SpriteAsset SpriteAsset::Load(const std::string& path) {
    SpriteAsset s;
    const bool embedded=!path.ends_with(".sprite");
    // A texture imported as Sprite keeps its sprite in its own .meta: no extra file
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

// The signed distance field of a glyph, laid out like stbtt_GetCodepointSDF (same size, offsets and value
// scale) but measured on the glyph as stb's rasterizer fills it (non-zero winding, 8x supersampled).
// stbtt_GetCodepointSDF decides inside/outside by counting crossings, which punches holes wherever a font
// draws overlapping contours (common in display fonts); the filled bitmap has no such problem.
namespace {
void Edt1D(const double* f,double* d,int n,std::vector<int>& v,std::vector<double>& z){
    // Felzenszwalb & Huttenlocher: the lower envelope of the parabolas rooted at each sample
    int k=0;v[0]=0;z[0]=-1e30;z[1]=1e30;
    auto cross=[&](int q,int p){return ((f[q]+double(q)*q)-(f[p]+double(p)*p))/(2.0*q-2.0*p);};
    for(int q=1;q<n;++q){
        double s=cross(q,v[k]);
        while(s<=z[k]){--k;s=cross(q,v[k]);}
        ++k;v[k]=q;z[k]=s;z[k+1]=1e30;
    }
    k=0;for(int q=0;q<n;++q){while(z[k+1]<q)++k;double dq=q-v[k];d[q]=dq*dq+f[v[k]];}
}
// squared distance of every pixel to the nearest pixel where `seed` is true
std::vector<float> Edt(const std::vector<u8>& seed,int W,int H){
    const double inf=1e20;std::vector<float> g(size_t(W)*H);
    std::vector<double> col(size_t(W)*H);
    for(size_t i=0;i<g.size();++i)col[i]=seed[i]?0.0:inf;
    int n=std::max(W,H);std::vector<double> f(n),d(n),zf(n+1);std::vector<int> v(n);
    for(int x=0;x<W;++x){for(int y=0;y<H;++y)f[y]=col[size_t(y)*W+x];Edt1D(f.data(),d.data(),H,v,zf);for(int y=0;y<H;++y)col[size_t(y)*W+x]=d[y];}
    for(int y=0;y<H;++y){for(int x=0;x<W;++x)f[x]=col[size_t(y)*W+x];Edt1D(f.data(),d.data(),W,v,zf);for(int x=0;x<W;++x)g[size_t(y)*W+x]=(float)d[x];}
    return g;
}
unsigned char* GlyphSdf(const stbtt_fontinfo* info,float scale,int codepoint,int padding,unsigned char onedge,float distanceScale,int* width,int* height,int* xoff,int* yoff){
    int x0,y0,x1,y1;stbtt_GetCodepointBitmapBox(info,codepoint,scale,scale,&x0,&y0,&x1,&y1);
    if(x0==x1||y0==y1)return nullptr;
    x0-=padding;y0-=padding;x1+=padding;y1+=padding;
    const int w=x1-x0,h=y1-y0,K=8,W=w*K,H=h*K;
    // the filled glyph, K times larger, placed where the field's pixels are
    int bw=0,bh=0,bx=0,by=0;unsigned char* bitmap=stbtt_GetCodepointBitmap(info,scale*K,scale*K,codepoint,&bw,&bh,&bx,&by);
    std::vector<u8> inside(size_t(W)*H,0);
    if(bitmap){for(int y=0;y<bh;++y)for(int x=0;x<bw;++x){int X=bx-x0*K+x,Y=by-y0*K+y;if(X>=0&&Y>=0&&X<W&&Y<H)inside[size_t(Y)*W+X]=bitmap[y*bw+x]>=128;}stbtt_FreeBitmap(bitmap,nullptr);}
    std::vector<u8> outside(inside.size());for(size_t i=0;i<inside.size();++i)outside[i]=!inside[i];
    auto toInside=Edt(inside,W,H),toOutside=Edt(outside,W,H);
    auto* out=(unsigned char*)malloc(size_t(w)*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        size_t i=size_t(y*K+K/2)*W+(x*K+K/2);
        float dist=inside[i]?(std::sqrt(toOutside[i])-.5f)/K:-(std::sqrt(toInside[i])-.5f)/K;   // pixels of the field, + inside
        out[size_t(y)*w+x]=(unsigned char)std::clamp(onedge+dist*distanceScale,0.f,255.f);
    }
    *width=w;*height=h;*xoff=x0;*yoff=y0;
    return out;
}
}
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
    auto* sdf=GlyphSdf(&impl.info,impl.scale,actual,6,128,SdfDistanceScale,&w,&h,&xoff,&yoff);
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
    if(sdf)free(sdf);
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
