#include "UI/CanvasRenderer.h"
#include "UI/UIAssets.h"
#include "Scripting/ScriptEngine.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <unordered_map>
#include <format>
namespace Stardust {
namespace {
EntityID hovered=0,pressed=0,focused=0;
bool hoveredInteractable=true;
struct Batch {Material material;std::vector<Vertex> vertices;std::vector<u32> indices;std::string key;};
struct Cached {u64 hash=0;Ref<Mesh> mesh;};
// One cache per renderer: a render texture camera sharing the Game view's entries would evict them every frame
struct CacheSlot {std::vector<Cached> entries;u64 used=0;};
std::unordered_map<std::string,CacheSlot> cache;
u64 s_SubmitCount=0;
// Screen overlays are drawn after tone mapping into a UNORM target, so their images must not be decoded from sRGB
bool s_GammaSpace=false;
glm::vec2 Point(const glm::mat4& m,glm::vec2 p){return glm::vec2(m*glm::vec4(p,0,1));}
std::array<glm::vec2,4> Corners(const glm::mat4& m,glm::vec2 s){return {Point(m,{0,0}),Point(m,{s.x,0}),Point(m,s),Point(m,{0,s.y})};}
std::string MaterialKey(const Material& m){nlohmann::json j;m.ToJson(j);return j.dump()+std::to_string(m.runtimeAlbedo?m.runtimeAlbedo->SRV():0);}
Material BaseMaterial(const std::string& path,glm::vec4 color){Material m;if(!path.empty())if(auto loaded=AssetManager::GetMaterial(path))m=*loaded;m.shader=ShaderType::UI;m.doubleSided=true;m.color*=color;m.normalStrength=0;m.roughness=0;m.alphaCutoff=0;return m;}
struct V {glm::vec2 position,uv;};
// A convex polygon in the rect's local space, clipped to the rect (clip) and to the masks
void Poly(std::vector<Batch>& batches,const Material& material,const UIRect& rect,std::vector<V> polygon,bool clip=false) {
    auto trim=[&](const std::array<glm::vec2,4>& mask){
        float area=0;for(int i=0;i<4;++i){auto a=mask[i],b=mask[(i+1)%4];area+=a.x*b.y-a.y*b.x;}
        float sign=area>=0?1.f:-1.f;
        for(int edge=0;edge<4&&!polygon.empty();++edge){std::vector<V> next;auto a=mask[edge],b=mask[(edge+1)%4];auto side=[&](glm::vec2 p){return sign*((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x));};
            V previous=polygon.back();float ps=side(previous.position);
            for(auto current:polygon){float cs=side(current.position);if((ps>=0)!=(cs>=0)){float t=ps/(ps-cs);next.push_back({glm::mix(previous.position,current.position,t),glm::mix(previous.uv,current.uv,t)});}if(cs>=0)next.push_back(current);previous=current;ps=cs;}polygon=std::move(next);
        }
    };
    if(clip)trim({glm::vec2(0),glm::vec2(rect.size.x,0),rect.size,glm::vec2(0,rect.size.y)});
    for(auto& v:polygon)v.position=Point(rect.localToCanvas,v.position);
    for(const auto& mask:rect.masks)trim(mask);
    if(polygon.size()<3)return;
    auto key=MaterialKey(material)+"|"+std::to_string(rect.maskTexture?rect.maskTexture->SRV():0);if(batches.empty()||batches.back().key!=key){batches.push_back({material,{},{},key});batches.back().material.runtimeMask=rect.maskTexture;}auto& batch=batches.back();u32 base=(u32)batch.vertices.size();
    for(auto& v:polygon){glm::vec3 maskCoord(0);if(rect.maskTexture){auto t=glm::vec2(rect.maskFromCanvas*glm::vec4(v.position,0,1))/glm::max(rect.maskSize,glm::vec2(1e-3f));maskCoord={glm::mix(rect.maskUv.x,rect.maskUv.z,t.x),glm::mix(rect.maskUv.w,rect.maskUv.y,t.y),1};}batch.vertices.push_back({{v.position.x,v.position.y,0},maskCoord,v.uv,{1,0,0,1}});}
    for(u32 i=1;i+1<polygon.size();++i)batch.indices.insert(batch.indices.end(),{base,base+i,base+i+1});
}
void Quad(std::vector<Batch>& batches,const Material& material,const UIRect& rect,glm::vec2 min,glm::vec2 size,glm::vec4 uv,bool clip=false,float italic=0) {
    if(size.x<=0||size.y<=0)return;
    Poly(batches,material,rect,{{{min.x,min.y},{uv.x,uv.w}},{{min.x+size.x,min.y},{uv.z,uv.w}},{{min.x+size.x+italic,min.y+size.y},{uv.z,uv.y}},{{min.x+italic,min.y+size.y},{uv.x,uv.y}}},clip);
}
void Image(std::vector<Batch>& batches,const UIRect& r,const UIImageComponent& image,RHI::Device& device,glm::vec2 position=glm::vec2(0),glm::vec2 overrideSize=glm::vec2(-1),glm::vec4 multiply=glm::vec4(1)) {
    auto sprite=SpriteAsset::Load(image.sprite.empty()?image.texture:image.sprite);
    auto texture=AssetManager::GetTexture(sprite.texture,!s_GammaSpace);
    glm::vec2 textureSize=texture?glm::vec2(texture->Width(),texture->Height()):glm::vec2(1);
    if(sprite.rect.z<=0||sprite.rect.w<=0)sprite.rect={0,0,textureSize.x,textureSize.y};
    glm::vec2 size=overrideSize.x<0?r.size:overrideSize;
    if(image.preserveAspect){float ratio=sprite.rect.z/std::max(sprite.rect.w,1.f);glm::vec2 fitted=size;if(size.x/std::max(size.y,1.f)>ratio)fitted.x=size.y*ratio;else fitted.y=size.x/ratio;position+=(size-fitted)*0.5f;size=fitted;}
    auto material=BaseMaterial(image.material,image.color*multiply);material.runtimeAlbedo=texture;
    glm::vec4 uv={sprite.rect.x/textureSize.x,sprite.rect.y/textureSize.y,(sprite.rect.x+sprite.rect.z)/textureSize.x,(sprite.rect.y+sprite.rect.w)/textureSize.y};
    if(image.type==3){float amount=std::clamp(image.fillAmount,0.f,1.f);if(image.fillMethod==1){size.y*=amount;uv.y=glm::mix(uv.w,uv.y,amount);}else{size.x*=amount;uv.z=glm::mix(uv.x,uv.z,amount);}Quad(batches,material,r,position,size,uv);return;}
    float units=100.f/(std::max(1.f,sprite.pixelsPerUnit)*std::max(.01f,image.pixelsPerUnitMultiplier));
    if(image.type==1){
        auto border=sprite.border*units;float x[4]={0,std::min(border.x,size.x*.5f),size.x-std::min(border.z,size.x*.5f),size.x};float y[4]={0,std::min(border.y,size.y*.5f),size.y-std::min(border.w,size.y*.5f),size.y};
        float u[4]={uv.x,uv.x+sprite.border.x/textureSize.x,uv.z-sprite.border.z/textureSize.x,uv.z};float v[4]={uv.w,uv.w-sprite.border.y/textureSize.y,uv.y+sprite.border.w/textureSize.y,uv.y};
        for(int iy=0;iy<3;++iy)for(int ix=0;ix<3;++ix)Quad(batches,material,r,position+glm::vec2(x[ix],y[iy]),{x[ix+1]-x[ix],y[iy+1]-y[iy]},{u[ix],v[iy+1],u[ix+1],v[iy]});return;
    }
    if(image.type==2){glm::vec2 tile=glm::max(glm::vec2(sprite.rect.z,sprite.rect.w)*units,glm::vec2(1));int count=0;for(float y=0;y<size.y&&count<4096;y+=tile.y)for(float x=0;x<size.x&&count++<4096;x+=tile.x){auto extent=glm::min(tile,size-glm::vec2(x,y));auto partial=uv;partial.z=glm::mix(uv.x,uv.z,extent.x/tile.x);partial.y=glm::mix(uv.w,uv.y,extent.y/tile.y);Quad(batches,material,r,position+glm::vec2(x,y),extent,partial);}return;}
    Quad(batches,material,r,position,size,uv);
}
void Text(std::vector<Batch>& batches,const UIRect& r,const UITextComponent& text,RHI::Device& device) {
    TextStyle base;base.color=text.color*glm::vec4(1,1,1,r.alpha);base.size=text.fontSize;base.font=text.font;
    auto tokens=ParseRichText(text.text,base,text.richText);
    struct Letter{TextToken token;std::shared_ptr<UIFont> font;FontGlyph glyph;float advance;};
    std::vector<Letter> letters;
    for(auto& token:tokens){auto font=UIFont::Get(device,token.style.font);if(!font)continue;FontGlyph glyph;if(token.codepoint!='\n'&&token.sprite.empty())glyph=font->Glyph(device,token.codepoint);float advance=token.sprite.empty()?glyph.advance*(token.style.size/48.f):token.style.size;letters.push_back({token,font,glyph,advance+text.characterSpacing});}
    struct Line{std::vector<size_t> chars;float width=0,height=0;};
    auto layout=[&](float scale){std::vector<Line> lines(1);for(size_t i=0;i<letters.size();++i){auto& l=letters[i];if(l.token.codepoint=='\n'){lines.back().height=std::max(lines.back().height,text.fontSize*scale*1.2f);lines.emplace_back();continue;}
        float advance=l.advance*scale;
        if(text.wordWrap&&l.token.codepoint!=' '&&(i==0||letters[i-1].token.codepoint==' '||letters[i-1].token.codepoint=='\n')){float word=0;for(size_t k=i;k<letters.size()&&letters[k].token.codepoint!=' '&&letters[k].token.codepoint!='\n';++k)word+=letters[k].advance*scale;if(lines.back().width+word>r.size.x&&!lines.back().chars.empty())lines.emplace_back();}
        if(text.wordWrap&&lines.back().width+advance>r.size.x&&!lines.back().chars.empty())lines.emplace_back();
        auto& line=lines.back();line.chars.push_back(i);line.width+=advance;line.height=std::max(line.height,l.token.style.size*scale*1.2f*text.lineSpacing);
    }return lines;};
    float scale=1;
    if(text.autoSize){float lo=std::max(1.f,text.minFontSize),hi=std::max(lo,text.maxFontSize);for(int i=0;i<9;++i){float mid=(lo+hi)*.5f;auto lines=layout(mid/std::max(1.f,text.fontSize));float h=0,w=0;for(auto& line:lines){h+=line.height;w=std::max(w,line.width);}if(h<=r.size.y&&w<=r.size.x)lo=mid;else hi=mid;}scale=lo/std::max(1.f,text.fontSize);}
    auto lines=layout(scale);
    if(text.overflow==2&&!lines.empty()){
        size_t count=0;float total=0;while(count<lines.size()&&total+lines[count].height<=r.size.y){total+=lines[count].height;++count;}count=std::max(size_t(1),count);
        bool truncated=count<lines.size()||lines[count-1].width>r.size.x;
        if(truncated){lines.resize(count);auto& line=lines.back();auto style=line.chars.empty()?base:letters[line.chars.back()].token.style;auto font=UIFont::Get(device,style.font);if(font){auto glyph=font->Glyph(device,'.');float advance=(glyph.advance*style.size/48.f+text.characterSpacing)*scale;
            while(!line.chars.empty()&&line.width+3*advance>r.size.x){line.width-=letters[line.chars.back()].advance*scale;line.chars.pop_back();}
            for(int i=0;i<3;++i){line.chars.push_back(letters.size());letters.push_back({{'.',style,{}},font,glyph,advance/scale});line.width+=advance;}
        }}
    }
    float height=0;for(auto& line:lines)height+=line.height;
    float top=r.size.y;if(text.verticalAlignment==1)top+=(height-r.size.y)*.5f;else if(text.verticalAlignment==2)top=height;
    for(auto& line:lines){float x=text.alignment==1?(r.size.x-line.width)*.5f:text.alignment==2?r.size.x-line.width:0;
        for(auto index:line.chars){auto& l=letters[index];auto& style=l.token.style;float factor=style.size*scale/48.f;float baseline=top-l.font->Ascent()*factor;
            if(!l.token.sprite.empty()){UIImageComponent image;image.sprite=l.token.sprite;image.color=style.color;Image(batches,r,image,device,{x,baseline},{style.size*scale,style.size*scale});}
            else if(l.glyph.page){
                auto material=BaseMaterial(text.material,style.color);material.runtimeAlbedo=l.glyph.page->texture;material.normalStrength=1;material.roughness=text.outlineWidth;material.alphaCutoff=style.bold?.43f:.5f;material.emissive=glm::vec3(text.outlineColor);material.emissiveIntensity=1;
                glm::vec2 p={x+l.glyph.offset.x*factor,baseline-(l.glyph.offset.y+l.glyph.size.y)*factor};auto size=l.glyph.size*factor;
                if(text.shadowColor.a>0){auto shadow=material;shadow.color=text.shadowColor;shadow.color.a*=r.alpha;shadow.roughness=0;Quad(batches,shadow,r,p+text.shadowOffset,size,l.glyph.uv,text.overflow!=1,style.italic?size.y*.18f:0);}
                Quad(batches,material,r,p,size,l.glyph.uv,text.overflow!=1,style.italic?size.y*.18f:0);
                if(style.underline||style.strike){auto decoration=BaseMaterial("",style.color);if(style.underline)Quad(batches,decoration,r,{x,baseline-style.size*scale*.1f},{l.advance*scale,std::max(1.f,style.size*scale*.055f)},{0,0,1,1},text.overflow!=1);if(style.strike)Quad(batches,decoration,r,{x,baseline+style.size*scale*.3f},{l.advance*scale,std::max(1.f,style.size*scale*.055f)},{0,0,1,1},text.overflow!=1);}
            }
            x+=l.advance*scale;
        }top-=line.height;
    }
}
}
namespace {
// ------------------------------------------------------------------------------------------------ Scroll View / Dropdown

const UIRect* FindRect(const std::vector<UIRect>& rects,EntityID e){for(auto& r:rects)if(r.entity==e)return &r;return nullptr;}
EntityID ScrollContent(Scene& scene,EntityID e,const UIScrollViewComponent& s){if(s.content)if(auto c=scene.FindByUUID(s.content))return c;for(auto child:scene.GetChildren(e))if(scene.Reg().Has<RectTransformComponent>(child))return child;return 0;}
// The content rectangle in the view's local space (bottom-left origin)
struct ScrollState{bool valid=false;EntityID content=0;glm::vec2 view{0},min{0},size{0};};
ScrollState ScrollOf(Scene& scene,const std::vector<UIRect>& rects,const UIRect& view,const UIScrollViewComponent& s){
    ScrollState st;st.content=ScrollContent(scene,view.entity,s);auto* c=st.content?FindRect(rects,st.content):nullptr;if(!c)return st;
    st.valid=true;st.view=view.size;st.size=c->size;st.min=Point(glm::inverse(view.localToCanvas)*c->localToCanvas,{0,0});return st;}
// 0..1 per axis: x 0 = left, y 1 = top
glm::vec2 Normalized(const ScrollState& st){glm::vec2 n(0,1);
    if(st.size.x>st.view.x)n.x=std::clamp(-st.min.x/(st.size.x-st.view.x),0.f,1.f);
    if(st.size.y>st.view.y)n.y=std::clamp(1-((st.min.y+st.size.y)-st.view.y)/(st.size.y-st.view.y),0.f,1.f);return n;}
// How far the content is outside its allowed range (what Elastic and Clamped pull back)
glm::vec2 OutOfBounds(const ScrollState& st,const UIScrollViewComponent& s){glm::vec2 o(0);
    if(s.horizontal){float lo=std::min(0.f,st.view.x-st.size.x),hi=0;if(st.min.x<lo)o.x=st.min.x-lo;else if(st.min.x>hi)o.x=st.min.x-hi;}
    if(s.vertical){float top=st.min.y+st.size.y;float lo=st.view.y,hi=std::max(st.view.y,st.size.y);if(top<lo)o.y=top-lo;else if(top>hi)o.y=top-hi;}
    return o;}
void MoveContent(Scene& scene,EntityID content,glm::vec2 delta){if(auto* t=scene.Reg().TryGet<RectTransformComponent>(content))t->anchoredPosition+=delta;}
// Scroll bars in view space: track and handle, per axis (0 horizontal, 1 vertical)
struct Bar{bool shown=false;glm::vec2 trackMin{0},trackSize{0},handleMin{0},handleSize{0};};
Bar ScrollBar(const ScrollState& st,const UIScrollViewComponent& s,int axis){Bar b;if(!st.valid||s.scrollbars==0)return b;
    const bool enabled=axis==0?s.horizontal:s.vertical;const float view=axis==0?st.view.x:st.view.y,content=axis==0?st.size.x:st.size.y;
    if(!enabled||(s.scrollbars==1&&content<=view+0.5f))return b;
    const bool other=(axis==0?s.vertical&&(s.scrollbars==2||st.size.y>st.view.y+0.5f):s.horizontal&&(s.scrollbars==2||st.size.x>st.view.x+0.5f));
    const float w=s.scrollbarWidth,pad=s.scrollbarPadding,corner=other?w+pad:0;
    if(axis==1){b.trackMin={st.view.x-w-pad,pad+corner};b.trackSize={w,st.view.y-2*pad-corner};}else{b.trackMin={pad,pad};b.trackSize={st.view.x-2*pad-corner,w};}
    const float track=axis==1?b.trackSize.y:b.trackSize.x;if(track<=4)return b;
    const float length=std::clamp(track*view/std::max(content,1.f),std::min(s.minHandleSize,track),track);
    const glm::vec2 n=Normalized(st);const float t=axis==1?n.y:n.x;
    if(axis==1){b.handleSize={w,length};b.handleMin={b.trackMin.x,b.trackMin.y+(track-length)*t};}else{b.handleSize={length,w};b.handleMin={b.trackMin.x+(track-length)*t,b.trackMin.y};}
    b.shown=true;return b;}
std::unordered_map<EntityID,int> s_BarHover; // scroll view -> axis under the pointer (-1 none)

// Dropdown list: below the dropdown, or above it when there is no room (Auto) or when asked
struct ListGeometry{glm::vec2 pos{0},size{0};float content=0;};
ListGeometry DropdownList(const UIRect& r,const UIDropdownComponent& d){ListGeometry g;
    g.content=d.options.size()*d.itemHeight+d.listPadding*2;const float h=std::min(g.content,std::max(d.itemHeight+d.listPadding*2,d.maxListHeight));
    bool up=d.direction==2;if(d.direction==0){float bottom=Point(r.localToCanvas,{0,0}).y;up=bottom-d.listOffset-h<0&&Point(r.localToCanvas,{0,r.size.y}).y+d.listOffset+h<=r.canvasSize.y;}
    g.pos=up?glm::vec2(0,r.size.y+d.listOffset):glm::vec2(0,-d.listOffset-h);g.size={r.size.x,h};return g;}
UIRect ListRect(const UIRect& r,const ListGeometry& g){UIRect l=r;l.masks.clear();l.maskTexture=nullptr;l.localToCanvas=r.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(g.pos,0));l.size=g.size;return l;}
// Item i in list space: bottom-left corner
glm::vec2 ItemMin(const UIDropdownComponent& d,const ListGeometry& g,int i){return {d.listPadding,g.size.y-d.listPadding-(i+1)*d.itemHeight+d.scroll};}
EntityID s_OpenDropdown=0;
}

std::vector<UIRect> CanvasRenderer::Layout(Scene& scene,glm::vec2 viewport,bool sceneView) {
    auto& reg=scene.Reg();std::vector<EntityID> canvases;std::vector<UIRect> result;
    reg.Each<CanvasComponent>([&](EntityID e,CanvasComponent&){if(scene.IsActiveInHierarchy(e))canvases.push_back(e);});
    std::stable_sort(canvases.begin(),canvases.end(),[&](auto a,auto b){return reg.Get<CanvasComponent>(a).sortingOrder<reg.Get<CanvasComponent>(b).sortingOrder;});
    for(auto canvas:canvases){auto& c=reg.Get<CanvasComponent>(canvas);glm::vec2 dimensions=glm::max(c.referenceResolution,glm::vec2(1));if(!sceneView&&c.renderMode!=1){float scale=c.scaleWithScreen?std::min(viewport.x/dimensions.x,viewport.y/dimensions.y):1;dimensions=viewport/std::max(.0001f,scale);}
        std::function<void(EntityID,UIRect,glm::vec4)> visit;visit=[&](EntityID e,UIRect parent,glm::vec4 imposed){if(!scene.IsActiveInHierarchy(e)||(e!=canvas&&reg.Has<CanvasComponent>(e)))return;
            UIRect rect=parent;rect.entity=e;rect.canvas=canvas;rect.canvasSize=dimensions;
            if(e!=canvas)if(auto* r=reg.TryGetEnabled<RectTransformComponent>(e)){rect.size=glm::max(parent.size*(r->anchorMax-r->anchorMin)+r->sizeDelta,glm::vec2(0));auto position=parent.size*glm::mix(r->anchorMin,r->anchorMax,r->pivot)+r->anchoredPosition;rect.localToCanvas=parent.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(position,0))*glm::rotate(glm::mat4(1),glm::radians(r->rotation),glm::vec3(0,0,1))*glm::scale(glm::mat4(1),glm::vec3(r->scale,1))*glm::translate(glm::mat4(1),glm::vec3(-rect.size*r->pivot,0));}
            if(imposed.z>=0){rect.size=glm::vec2(imposed.z,imposed.w);rect.localToCanvas=parent.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(imposed.x,imposed.y,0));}
            if(auto* group=reg.TryGetEnabled<UILayoutGroupComponent>(e);group&&group->fitContent&&e!=canvas){glm::vec2 total(0);size_t count=0;for(auto child:scene.GetChildren(e))if(scene.IsActiveInHierarchy(child)){auto* r=reg.TryGetEnabled<RectTransformComponent>(child);glm::vec2 size=r?glm::max(r->sizeDelta,glm::vec2(0)):group->cellSize;if(group->mode==0){total.x+=size.x;total.y=std::max(total.y,size.y);}else if(group->mode==1){total.y+=size.y;total.x=std::max(total.x,size.x);}++count;}if(group->mode==0)total.x+=group->spacing.x*std::max(0,(int)count-1);else if(group->mode==1)total.y+=group->spacing.y*std::max(0,(int)count-1);else{int columns=std::max(1,group->columns);int rows=((int)count+columns-1)/columns;if(group->startAxis==1)std::swap(columns,rows);total={columns*group->cellSize.x+std::max(0,columns-1)*group->spacing.x,rows*group->cellSize.y+std::max(0,rows-1)*group->spacing.y};}rect.size=total+glm::vec2(group->padding.x+group->padding.z,group->padding.y+group->padding.w);}
            if(auto* g=reg.TryGetEnabled<CanvasGroupComponent>(e)){rect.alpha*=g->alpha;rect.interactable&=g->interactable;rect.raycast&=g->blocksRaycasts;}
            result.push_back(rect);
            if(reg.IsEnabled<RectMaskComponent>(e))rect.masks.push_back(Corners(rect.localToCanvas,rect.size));
            if(auto* scroll=reg.TryGetEnabled<UIScrollViewComponent>(e);scroll&&scroll->clipContent)rect.masks.push_back(Corners(rect.localToCanvas,rect.size));
            if(reg.IsEnabled<UIMaskComponent>(e)){rect.masks.push_back(Corners(rect.localToCanvas,rect.size));if(auto* image=reg.TryGetEnabled<UIImageComponent>(e);image){auto sprite=SpriteAsset::Load(image->sprite.empty()?image->texture:image->sprite);if(auto texture=AssetManager::GetTexture(sprite.texture,true)){glm::vec2 textureSize(texture->Width(),texture->Height());if(sprite.rect.z<=0||sprite.rect.w<=0)sprite.rect={0,0,textureSize.x,textureSize.y};rect.maskTexture=texture;rect.maskFromCanvas=glm::inverse(rect.localToCanvas);rect.maskSize=rect.size;rect.maskUv={sprite.rect.x/textureSize.x,sprite.rect.y/textureSize.y,(sprite.rect.x+sprite.rect.z)/textureSize.x,(sprite.rect.y+sprite.rect.w)/textureSize.y};}}}
            auto* group=reg.TryGetEnabled<UILayoutGroupComponent>(e);
            struct Slot{EntityID entity;glm::vec2 size;};std::vector<Slot> slots;
            if(group)for(auto child:scene.GetChildren(e))if(scene.IsActiveInHierarchy(child)){auto* r=reg.TryGetEnabled<RectTransformComponent>(child);slots.push_back({child,group->mode==2?group->cellSize:r?glm::max(r->sizeDelta,glm::vec2(1)):group->cellSize});}
            std::unordered_map<EntityID,glm::vec4> placed;
            if(group&&!slots.empty()){
                const bool fromRight=group->startCorner==1||group->startCorner==3,fromBottom=group->startCorner>=2;
                const float alignX=(group->childAlignment%3)*.5f,alignY=(group->childAlignment/3)*.5f;
                const int count=(int)slots.size(),perLine=std::max(1,group->columns),lines=(count+perLine-1)/perLine;
                const int gridColumns=group->startAxis==0?std::min(perLine,count):lines,gridRows=group->startAxis==0?lines:std::min(perLine,count);
                glm::vec2 content(0);
                if(group->mode==0){for(auto& s:slots){content.x+=s.size.x;content.y=std::max(content.y,s.size.y);}content.x+=group->spacing.x*(count-1);}
                else if(group->mode==1){for(auto& s:slots){content.y+=s.size.y;content.x=std::max(content.x,s.size.x);}content.y+=group->spacing.y*(count-1);}
                else content={gridColumns*group->cellSize.x+(gridColumns-1)*group->spacing.x,gridRows*group->cellSize.y+(gridRows-1)*group->spacing.y};
                const glm::vec2 inner(rect.size.x-group->padding.x-group->padding.z,rect.size.y-group->padding.y-group->padding.w);
                const float left=group->padding.x+(inner.x-content.x)*alignX,top=rect.size.y-group->padding.w-(inner.y-content.y)*alignY;
                float along=0;
                for(int i=0;i<count;++i){auto& s=slots[i];glm::vec2 offset(0); // from the content's top-left, y down
                    if(group->mode==0){offset.x=fromRight?content.x-along-s.size.x:along;offset.y=(content.y-s.size.y)*alignY;along+=s.size.x+group->spacing.x;}
                    else if(group->mode==1){offset.y=fromBottom?content.y-along-s.size.y:along;offset.x=(content.x-s.size.x)*alignX;along+=s.size.y+group->spacing.y;}
                    else{int column=group->startAxis==0?i%perLine:i/perLine,row=group->startAxis==0?i/perLine:i%perLine;if(fromRight)column=gridColumns-1-column;if(fromBottom)row=gridRows-1-row;offset={column*(group->cellSize.x+group->spacing.x),row*(group->cellSize.y+group->spacing.y)};}
                    placed[s.entity]={left+offset.x,top-offset.y-s.size.y,s.size.x,s.size.y};}
            }
            for(auto child:scene.GetChildren(e)){auto layout=placed.find(child);visit(child,rect,layout!=placed.end()?layout->second:glm::vec4(-1));}
        };UIRect root;root.size=dimensions;visit(canvas,root,glm::vec4(-1));
    }return result;
}
glm::mat4 CanvasRenderer::CanvasWorld(Scene& scene,EntityID canvas,glm::vec2 logicalSize){auto& c=scene.Reg().Get<CanvasComponent>(canvas);
    if(c.renderMode==2){auto dimensions=logicalSize.x>0?logicalSize:c.referenceResolution;RenderCamera camera;if(scene.GetPrimaryCamera(camera,dimensions.x/std::max(1.f,dimensions.y))){float distance=std::max(camera.nearPlane+.01f,c.planeDistance);float unit=(camera.orthographic?2*camera.orthographicSize:2*std::tan(camera.fovY*.5f)*distance)/std::max(1.f,dimensions.y);return glm::inverse(camera.view)*glm::translate(glm::mat4(1),glm::vec3(0,0,-distance))*glm::scale(glm::mat4(1),glm::vec3(unit))*glm::translate(glm::mat4(1),glm::vec3(-dimensions*.5f,0));}}
    return scene.GetWorldMatrix(canvas)*glm::scale(glm::mat4(1),glm::vec3(1/std::max(1.f,c.pixelsPerUnit)))*glm::translate(glm::mat4(1),glm::vec3(-c.referenceResolution*.5f,0));}
bool CanvasRenderer::MouseOnCanvas(Scene& scene,EntityID canvas,const RenderCamera& camera,glm::vec2 origin,glm::vec2 size,glm::vec2 mouse,glm::vec2& point,glm::vec2 logicalSize){auto ndc=(mouse-origin)/size*2.f-1.f;ndc.y=-ndc.y;auto inverse=glm::inverse(camera.projection*camera.view);auto a=inverse*glm::vec4(ndc,1,1),b=inverse*glm::vec4(ndc,.5,1);auto local=glm::inverse(CanvasWorld(scene,canvas,logicalSize));auto start=glm::vec3(local*glm::vec4(glm::vec3(a)/a.w,1));auto direction=glm::vec3(local*glm::vec4(glm::vec3(b)/b.w-glm::vec3(a)/a.w,0));if(std::abs(direction.z)<1e-8)return false;float distance=-start.z/direction.z;if(distance<0)return false;point=glm::vec2(start+direction*distance);return true;}
void CanvasRenderer::Submit(Scene& scene,SceneRenderer& renderer,RHI::Device& device,glm::vec2 viewport,bool sceneView,bool alwaysOnTop,bool includeOverlay){
    auto rectangles=Layout(scene,viewport,sceneView);std::unordered_set<std::string> live;++s_SubmitCount;
    const std::string owner=std::string(sceneView?":scene#":":game#")+std::to_string((uintptr_t)&renderer);
    // Drawn after every rectangle: scroll bars over their content, open dropdown lists over the whole canvas
    struct Late{UIRect rect;int kind;}; // 0 scroll bars, 1 dropdown list
    std::vector<Late> late;
    for(auto& r:rectangles){if(auto* sv=scene.Reg().TryGetEnabled<UIScrollViewComponent>(r.entity);sv&&sv->scrollbars)late.push_back({r,0});
        if(auto* dd=scene.Reg().TryGetEnabled<UIDropdownComponent>(r.entity);dd&&dd->open&&!sceneView)late.push_back({r,1});}
    auto submitBatches=[&](const UIRect& r,std::vector<Batch>& batches,const std::string& suffix){auto& reg=scene.Reg();
        auto key=std::to_string(reg.Get<IDComponent>(r.entity).uuid)+suffix+owner;live.insert(key);auto& slot=cache[key];slot.used=s_SubmitCount;auto& entries=slot.entries;entries.resize(batches.size());
        auto& canvas=reg.Get<CanvasComponent>(r.canvas);bool overlay=alwaysOnTop||(!sceneView&&canvas.renderMode==0);
        auto transform=alwaysOnTop?renderer.Camera().projection*renderer.Camera().view*CanvasWorld(scene,r.canvas,r.canvasSize):overlay?glm::ortho(0.f,r.canvasSize.x,0.f,r.canvasSize.y,-1.f,1.f):CanvasWorld(scene,r.canvas,r.canvasSize);
        for(size_t i=0;i<batches.size();++i){auto& batch=batches[i];u64 hash=14695981039346656037ull;auto add=[&](const void* p,size_t n){auto* b=(const u8*)p;for(size_t k=0;k<n;++k){hash^=b[k];hash*=1099511628211ull;}};add(batch.vertices.data(),batch.vertices.size()*sizeof(Vertex));add(batch.indices.data(),batch.indices.size()*4);if(!entries[i].mesh||entries[i].hash!=hash){entries[i].mesh=MakeRef<Mesh>(device,batch.vertices,batch.indices,"Canvas Geometry");entries[i].hash=hash;}renderer.SubmitUI(entries[i].mesh,transform,batch.material,r.entity,overlay);}
    };
    auto solid=[&](glm::vec4 color){return BaseMaterial("",color);};
    auto spriteOrColor=[&](std::vector<Batch>& batches,const UIRect& r,const std::string& sprite,glm::vec4 color,glm::vec2 min,glm::vec2 size){
        if(!sprite.empty()){UIImageComponent image;image.sprite=sprite;image.color=color;image.type=1;Image(batches,r,image,device,min,size);}
        else Quad(batches,solid(color),r,min,size,{0,0,1,1});};
    for(auto& r:rectangles){auto& reg=scene.Reg();if(!includeOverlay&&reg.Get<CanvasComponent>(r.canvas).renderMode==0)continue;s_GammaSpace=alwaysOnTop||(!sceneView&&reg.Get<CanvasComponent>(r.canvas).renderMode==0);if(!(renderer.Camera().cullingMask&(1u<<std::clamp(reg.Get<NameComponent>(r.entity).layer,0,31))))continue;if(r.alpha<=0)continue;std::vector<Batch> batches;
        auto* maskOwner=reg.TryGetEnabled<UIMaskComponent>(r.entity);
        if(auto* image=reg.TryGetEnabled<UIImageComponent>(r.entity);image&&(!maskOwner||maskOwner->showGraphic)){
            glm::vec4 tint(1,1,1,r.alpha);
            if(auto* button=reg.TryGetEnabled<UIButtonComponent>(r.entity)){
                int state=!button->interactable||!r.interactable?3:pressed==r.entity?2:hovered==r.entity||focused==r.entity?1:0;
                glm::vec4 target=state==3?button->disabledColor:state==2?button->pressedColor:state==1?button->highlightedColor:glm::vec4(1);
                if(button->transition==1){float blend=button->fadeDuration<=0?1:std::clamp(ImGui::GetIO().DeltaTime/button->fadeDuration,0.f,1.f);button->currentTint=glm::mix(button->currentTint,target,blend);tint*=button->currentTint;}
                if(button->transition==2&&state!=button->visualState)if(auto* animator=reg.TryGetEnabled<AnimatorComponent>(r.entity)){animator->state=state==3?button->disabledState:state==2?button->pressedState:state==1?button->highlightedState:button->normalState;animator->time=0;animator->playing=true;}
                button->visualState=state;
            }
            if(auto* dropdown=reg.TryGetEnabled<UIDropdownComponent>(r.entity))
                tint*=!dropdown->interactable||!r.interactable?dropdown->disabledTint:(pressed==r.entity||dropdown->open)?dropdown->pressedTint:hovered==r.entity?dropdown->hoverTint:glm::vec4(1);
            Image(batches,r,*image,device,glm::vec2(0),glm::vec2(-1),tint);
        }
        if(auto* dropdown=reg.TryGetEnabled<UIDropdownComponent>(r.entity)){
            // Caption (icon and text of the chosen option) and the arrow
            const bool has=dropdown->value>=0&&dropdown->value<(int)dropdown->options.size();
            const auto& option=has?dropdown->options[dropdown->value]:UIDropdownOption{dropdown->placeholder,""};
            float left=dropdown->textPadding;const float arrowRoom=dropdown->arrowSize+dropdown->textPadding*1.5f;
            if(!option.sprite.empty()){float s=std::min(r.size.y-8,dropdown->fontSize*1.3f);UIImageComponent icon;icon.sprite=option.sprite;icon.color=glm::vec4(1,1,1,r.alpha);icon.preserveAspect=true;Image(batches,r,icon,device,{left,(r.size.y-s)*.5f},{s,s});left+=s+6;}
            auto caption=r;caption.size={std::max(1.f,r.size.x-left-arrowRoom),r.size.y};caption.localToCanvas=r.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(left,0,0));
            UITextComponent text;text.text=option.text;text.font=dropdown->font;text.fontSize=dropdown->fontSize;text.color=dropdown->textColor*glm::vec4(1,1,1,has?1:.55f);text.alignment=0;text.verticalAlignment=1;text.wordWrap=false;text.overflow=2;text.richText=false;Text(batches,caption,text,device);
            const float a=dropdown->arrowSize;const glm::vec2 c(r.size.x-dropdown->textPadding-a*.5f,r.size.y*.5f);const glm::vec4 arrowColor=dropdown->arrowColor*glm::vec4(1,1,1,r.alpha);
            if(!dropdown->arrowSprite.empty()){UIImageComponent arrow;arrow.sprite=dropdown->arrowSprite;arrow.color=arrowColor;arrow.preserveAspect=true;Image(batches,r,arrow,device,c-glm::vec2(a*.5f),{a,a});}
            else if(dropdown->open)Poly(batches,solid(arrowColor),r,{{{c.x-a*.5f,c.y-a*.25f},{0,0}},{{c.x+a*.5f,c.y-a*.25f},{1,0}},{{c.x,c.y+a*.3f},{.5f,1}}});
            else Poly(batches,solid(arrowColor),r,{{{c.x,c.y-a*.3f},{.5f,0}},{{c.x+a*.5f,c.y+a*.25f},{1,1}},{{c.x-a*.5f,c.y+a*.25f},{0,1}}});
        }
        if(auto* text=reg.TryGetEnabled<UITextComponent>(r.entity))Text(batches,r,*text,device);
        if(auto* input=reg.TryGetEnabled<UIInputFieldComponent>(r.entity)){auto content=r;content.size=glm::max(r.size-glm::vec2(16,4),glm::vec2(1));content.localToCanvas=r.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(8,2,0));content.masks.push_back(Corners(content.localToCanvas,content.size));UITextComponent text;text.text=input->text.empty()?input->placeholder:input->text;if(input->password&&!input->text.empty()){text.text.clear();for(unsigned char c:input->text)if((c&0xC0)!=0x80)text.text+="*";}text.font=input->font;text.fontSize=input->fontSize;text.color=input->text.empty()?input->placeholderColor:input->color;text.alignment=0;text.verticalAlignment=input->multiline?0:1;text.richText=false;text.wordWrap=input->multiline;
            auto font=UIFont::Get(device,input->font);auto width=[&](int end){float x=0;for(auto& token:ParseRichText(input->text.substr(0,std::clamp(end,0,(int)input->text.size())),{},false))if(font&&token.codepoint!=10)x+=font->Glyph(device,input->password?'*':token.codepoint).advance*input->fontSize/48.f;return x;};input->scrollX=!input->multiline&&focused==r.entity?std::max(0.f,width(input->cursor)-content.size.x+4):0;
            if(focused==r.entity&&!input->multiline&&input->selectionStart!=input->selectionEnd){float a=width(std::min(input->selectionStart,input->selectionEnd))-input->scrollX,b=width(std::max(input->selectionStart,input->selectionEnd))-input->scrollX;auto highlight=BaseMaterial("",{.2f,.5f,1,.4f});Quad(batches,highlight,content,{a,(content.size.y-input->fontSize*1.2f)*.5f},{b-a,input->fontSize*1.2f},{0,0,1,1},true);}
            auto textRect=content;textRect.localToCanvas*=glm::translate(glm::mat4(1),glm::vec3(-input->scrollX,0,0));text.overflow=1;Text(batches,textRect,text,device);
            if(focused==r.entity&&input->interactable&&!sceneView&&std::fmod(ImGui::GetTime(),1.)<.5){auto font=UIFont::Get(device,input->font);float x=0,y=input->multiline?content.size.y-input->fontSize*1.2f:(content.size.y-input->fontSize)*.5f;auto prefix=input->text.substr(0,std::clamp(input->cursor,0,(int)input->text.size()));for(auto& token:ParseRichText(prefix,{},false)){if(token.codepoint=='\n'){x=0;y-=input->fontSize*1.2f;}else if(font)x+=font->Glyph(device,input->password?'*':token.codepoint).advance*input->fontSize/48.f;}auto caret=BaseMaterial("",input->color);Quad(batches,caret,content,{std::clamp(x-input->scrollX,0.f,content.size.x-2),y},{1.5f,input->fontSize},{0,0,1,1},true);}
        }
        submitBatches(r,batches,"");
    }
    for(auto& item:late){auto& reg=scene.Reg();const UIRect& r=item.rect;
        if(!includeOverlay&&reg.Get<CanvasComponent>(r.canvas).renderMode==0)continue;
        if(!(renderer.Camera().cullingMask&(1u<<std::clamp(reg.Get<NameComponent>(r.entity).layer,0,31))))continue;
        s_GammaSpace=alwaysOnTop||(!sceneView&&reg.Get<CanvasComponent>(r.canvas).renderMode==0);
        std::vector<Batch> batches;
        if(item.kind==0){auto& sv=reg.Get<UIScrollViewComponent>(r.entity);auto st=ScrollOf(scene,rectangles,r,sv);
            for(int axis=0;axis<2;++axis){auto bar=ScrollBar(st,sv,axis);if(!bar.shown)continue;
                auto it=s_BarHover.find(r.entity);const bool hot=(it!=s_BarHover.end()&&it->second==axis)||(sv.dragging&&sv.barDrag==axis);
                spriteOrColor(batches,r,sv.trackSprite,sv.trackColor*glm::vec4(1,1,1,r.alpha),bar.trackMin,bar.trackSize);
                spriteOrColor(batches,r,sv.handleSprite,(hot?sv.handleHoverColor:sv.handleColor)*glm::vec4(1,1,1,r.alpha),bar.handleMin,bar.handleSize);}
            submitBatches(r,batches,":bars");
        }else{auto& dd=reg.Get<UIDropdownComponent>(r.entity);auto g=DropdownList(r,dd);auto list=ListRect(r,g);list.alpha=1;
            // Shadow, background, then the visible items clipped to the list
            auto shadow=list;shadow.localToCanvas=list.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(3,-4,0));Quad(batches,solid({0,0,0,.22f}),shadow,{0,0},g.size,{0,0,1,1});
            spriteOrColor(batches,list,dd.listSprite,dd.listColor,{0,0},g.size);
            auto inner=list;inner.masks.push_back(Corners(list.localToCanvas,list.size));
            for(int i=0;i<(int)dd.options.size();++i){auto min=ItemMin(dd,g,i);if(min.y+dd.itemHeight<0||min.y>g.size.y)continue;const glm::vec2 size(g.size.x-dd.listPadding*2,dd.itemHeight);
                const glm::vec4 color=i==dd.hoveredItem?dd.itemHoverColor:i==dd.value?dd.itemSelectedColor:dd.itemColor;
                if(color.a>0||!dd.itemSprite.empty())spriteOrColor(batches,inner,dd.itemSprite,color,min,size);
                float x=min.x+10;
                if(dd.showCheckmark){if(i==dd.value){const float k=std::min(14.f,dd.itemHeight*.4f);const glm::vec2 c(min.x+10+k*.5f,min.y+dd.itemHeight*.5f);const float t=std::max(1.5f,k*.16f);auto check=solid(dd.checkmarkColor);
                    auto stroke=[&](glm::vec2 a,glm::vec2 b){glm::vec2 n=glm::normalize(glm::vec2(-(b.y-a.y),b.x-a.x))*t*.5f;Poly(batches,check,inner,{{a-n,{0,0}},{b-n,{1,0}},{b+n,{1,1}},{a+n,{0,1}}});};
                    stroke(c+glm::vec2(-k*.5f,0),c+glm::vec2(-k*.12f,-k*.38f));stroke(c+glm::vec2(-k*.12f,-k*.38f),c+glm::vec2(k*.5f,k*.4f));}
                    x+=std::min(14.f,dd.itemHeight*.4f)+10;}
                const auto& option=dd.options[i];
                if(!option.sprite.empty()){float s=std::min(dd.itemHeight-8,dd.fontSize*1.3f);UIImageComponent icon;icon.sprite=option.sprite;icon.preserveAspect=true;Image(batches,inner,icon,device,{x,min.y+(dd.itemHeight-s)*.5f},{s,s});x+=s+6;}
                auto label=inner;label.size={std::max(1.f,min.x+size.x-x-6),dd.itemHeight};label.localToCanvas=inner.localToCanvas*glm::translate(glm::mat4(1),glm::vec3(x,min.y,0));
                UITextComponent text;text.text=option.text;text.font=dd.font;text.fontSize=dd.fontSize;text.color=dd.itemTextColor;text.alignment=0;text.verticalAlignment=1;text.wordWrap=false;text.overflow=2;text.richText=false;Text(batches,label,text,device);}
            // A thin scroll indicator when the list is longer than its height
            if(g.content>g.size.y+0.5f){const float track=g.size.y-8,length=std::max(18.f,track*g.size.y/g.content),t=dd.scroll/std::max(1.f,g.content-g.size.y);Quad(batches,solid({0,0,0,.25f}),list,{g.size.x-6,4+(track-length)*(1-t)},{3,length},{0,0,1,1});}
            submitBatches(r,batches,":list");
        }
    }
    // Entries of this renderer that were not drawn, and entries of renderers gone for a while
    std::erase_if(cache,[&](const auto& item){return (item.first.ends_with(owner)&&!live.contains(item.first))||s_SubmitCount-item.second.used>2000;});
    UIFont::Flush(device);
}
EntityID CanvasRenderer::HandleInput(Scene& scene,glm::vec2 origin,glm::vec2 size,const RenderCamera& camera,bool sceneView,bool interactive){
    if(focused&&((scene.Reg().IsEnabled<UIInputFieldComponent>(focused)&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))||(scene.Reg().IsEnabled<UIButtonComponent>(focused)&&!scene.Reg().IsEnabled<UIButtonComponent>(focused))))focused=pressed=0;
    if(focused&&((scene.Reg().IsEnabled<UIInputFieldComponent>(focused)&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))||(scene.Reg().IsEnabled<UIButtonComponent>(focused)&&!scene.Reg().IsEnabled<UIButtonComponent>(focused))))focused=pressed=0;
    if(focused&&((scene.Reg().IsEnabled<UIInputFieldComponent>(focused)&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))||(scene.Reg().Has<UIButtonComponent>(focused)&&!scene.Reg().IsEnabled<UIButtonComponent>(focused))))focused=pressed=0;
    if(focused&&((scene.Reg().Has<UIInputFieldComponent>(focused)&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))||(scene.Reg().Has<UIButtonComponent>(focused)&&!scene.Reg().IsEnabled<UIButtonComponent>(focused))))focused=pressed=0;
    if(focused&&((scene.Reg().Has<UIInputFieldComponent>(focused)&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))||(scene.Reg().Has<UIButtonComponent>(focused)&&!scene.Reg().IsEnabled<UIButtonComponent>(focused))))focused=pressed=0;
    if(focused&&((scene.Reg().Has<UIInputFieldComponent>(focused)&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))||(scene.Reg().Has<UIButtonComponent>(focused)&&!scene.Reg().IsEnabled<UIButtonComponent>(focused))))focused=pressed=0;
    auto rectangles=Layout(scene,size,sceneView);auto mouse=ImGui::GetMousePos();glm::vec2 m(mouse.x,mouse.y);
    bool pointerInside=ImGui::IsWindowHovered()&&m.x>=origin.x&&m.y>=origin.y&&m.x<=origin.x+size.x&&m.y<=origin.y+size.y;
    auto& io=ImGui::GetIO();
    const float dt=std::clamp(io.DeltaTime,0.0001f,0.1f);
    auto canvasPoint=[&](const UIRect& r,glm::vec2& out){auto& canvas=scene.Reg().Get<CanvasComponent>(r.canvas);if(!sceneView&&canvas.renderMode==0){out={(m.x-origin.x)/size.x*r.canvasSize.x,(origin.y+size.y-m.y)/size.y*r.canvasSize.y};return true;}return MouseOnCanvas(scene,r.canvas,camera,origin,size,m,out,r.canvasSize);};
    auto localPoint=[&](const UIRect& r,glm::vec2& out){glm::vec2 p;if(!canvasPoint(r,p))return false;out=Point(glm::inverse(r.localToCanvas),p);return true;};
    auto within=[](glm::vec2 p,glm::vec2 min,glm::vec2 s){return p.x>=min.x&&p.y>=min.y&&p.x<=min.x+s.x&&p.y<=min.y+s.y;};
    static bool s_SwallowRelease=false; // the click that closed a dropdown list must not reopen it or press what is under it
    if(interactive&&s_SwallowRelease&&!ImGui::IsMouseDown(0)&&!ImGui::IsMouseReleased(0)){s_SwallowRelease=false;}
    if(interactive&&!s_OpenDropdown)for(auto& r:rectangles)if(auto* dd=scene.Reg().TryGetEnabled<UIDropdownComponent>(r.entity);dd&&dd->open){s_OpenDropdown=r.entity;dd->hoveredItem=dd->value;break;}
    // ---- Open dropdown list: it takes the pointer, the wheel and the arrow keys until it closes
    if(interactive&&s_OpenDropdown){
        auto* dd=scene.Reg().Valid(s_OpenDropdown)?scene.Reg().TryGetEnabled<UIDropdownComponent>(s_OpenDropdown):nullptr;
        const UIRect* r=dd&&dd->open?FindRect(rectangles,s_OpenDropdown):nullptr;
        if(!r){if(dd)dd->open=false;s_OpenDropdown=0;}
        else{
            auto g=DropdownList(*r,*dd);auto list=ListRect(*r,g);const int count=(int)dd->options.size();
            const float maxScroll=std::max(0.f,g.content-g.size.y);
            glm::vec2 lp;const bool inList=pointerInside&&localPoint(list,lp)&&within(lp,{0,0},g.size);
            auto reveal=[&](int i){auto min=ItemMin(*dd,g,i);if(min.y<0)dd->scroll+= -min.y+dd->listPadding;if(min.y+dd->itemHeight>g.size.y)dd->scroll-=min.y+dd->itemHeight-g.size.y+dd->listPadding;dd->scroll=std::clamp(dd->scroll,0.f,maxScroll);};
            auto close=[&](){dd->open=false;dd->hoveredItem=-1;s_OpenDropdown=0;};
            auto select=[&](int i){if(i>=0&&i<count&&i!=dd->value){dd->value=i;ScriptEngine::OnUIInput(r->entity,std::to_string(i),false);}close();};
            if(inList&&io.MouseWheel!=0)dd->scroll=std::clamp(dd->scroll-io.MouseWheel*dd->itemHeight*1.5f,0.f,maxScroll);
            if(inList&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0||io.MouseWheel!=0)){dd->hoveredItem=-1;for(int i=0;i<count;++i){auto min=ItemMin(*dd,g,i);if(lp.y>=min.y&&lp.y<min.y+dd->itemHeight&&lp.x>=dd->listPadding&&lp.x<=g.size.x-dd->listPadding)dd->hoveredItem=i;}}
            if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)){dd->hoveredItem=std::min(count-1,std::max(dd->hoveredItem,-1)+1);reveal(dd->hoveredItem);}
            if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)){dd->hoveredItem=std::max(0,(dd->hoveredItem<0?count:dd->hoveredItem)-1);reveal(dd->hoveredItem);}
            if(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)||ImGui::IsKeyPressed(ImGuiKey_Space))select(dd->hoveredItem);
            else if(ImGui::IsKeyPressed(ImGuiKey_Escape))close();
            else if(ImGui::IsMouseClicked(0)&&!inList){close();s_SwallowRelease=true;}
            else if(ImGui::IsMouseReleased(0)&&inList&&!s_SwallowRelease&&dd->hoveredItem>=0)select(dd->hoveredItem);
            hovered=0;pressed=0;
            return 0;
        }
    }
    // ---- Scroll views: wheel, drag (with inertia), scroll bars, and the elastic or clamped edges
    if(interactive){
        static EntityID dragView=0;static glm::vec2 dragStart{0},lastLocal{0};static bool dragActive=false;
        s_BarHover.clear();
        // The innermost scroll view under the pointer gets the wheel
        EntityID wheelTarget=0;
        if(pointerInside&&(io.MouseWheel!=0||io.MouseWheelH!=0))for(auto it=rectangles.rbegin();it!=rectangles.rend();++it)if(scene.Reg().IsEnabled<UIScrollViewComponent>(it->entity)){glm::vec2 lp;if(localPoint(*it,lp)&&within(lp,{0,0},it->size)){wheelTarget=it->entity;break;}}
        for(auto& r:rectangles){auto* sv=scene.Reg().TryGetEnabled<UIScrollViewComponent>(r.entity);if(!sv)continue;
            auto st=ScrollOf(scene,rectangles,r,*sv);if(!st.valid)continue;
            glm::vec2 moved(0);
            auto move=[&](glm::vec2 delta){if(!sv->horizontal)delta.x=0;if(!sv->vertical)delta.y=0;if(delta.x==0&&delta.y==0)return;MoveContent(scene,st.content,delta);moved+=delta;st.min+=delta;};
            auto toNormalized=[&](int axis,float t){t=std::clamp(t,0.f,1.f);if(axis==1&&st.size.y>st.view.y){float top=st.view.y+(1-t)*(st.size.y-st.view.y);move({0,top-st.size.y-st.min.y});}if(axis==0&&st.size.x>st.view.x)move({-t*(st.size.x-st.view.x)-st.min.x,0});};
            if(sv->requestedNormalized.x>=0){toNormalized(0,sv->requestedNormalized.x);sv->requestedNormalized.x=-1;sv->velocity.x=0;}
            if(sv->requestedNormalized.y>=0){toNormalized(1,sv->requestedNormalized.y);sv->requestedNormalized.y=-1;sv->velocity.y=0;}
            glm::vec2 lp;const bool inside=pointerInside&&localPoint(r,lp)&&within(lp,{0,0},r.size);
            // Scroll bars: hover, click on the handle (drag) or on the track (jump)
            Bar bars[2]={ScrollBar(st,*sv,0),ScrollBar(st,*sv,1)};
            for(int axis=0;axis<2;++axis)if(bars[axis].shown&&inside&&within(lp,bars[axis].trackMin,bars[axis].trackSize))s_BarHover[r.entity]=axis;
            if(inside&&ImGui::IsMouseClicked(0)&&!s_SwallowRelease){
                auto bar=s_BarHover.find(r.entity);
                if(bar!=s_BarHover.end()){sv->barDrag=bar->second;sv->dragging=true;dragView=r.entity;dragActive=true;lastLocal=lp;pressed=0;s_SwallowRelease=true;
                    auto& b=bars[bar->second];if(!within(lp,b.handleMin,b.handleSize)){const int axis=bar->second;const float track=axis==1?b.trackSize.y:b.trackSize.x,length=axis==1?b.handleSize.y:b.handleSize.x;const float along=axis==1?lp.y-b.trackMin.y:lp.x-b.trackMin.x;toNormalized(axis,(along-length*.5f)/std::max(1.f,track-length));}}
                else if(sv->dragToScroll&&dragView==0){dragView=r.entity;dragStart={m.x,m.y};lastLocal=lp;dragActive=false;sv->velocity={0,0};}
            }
            if(dragView==r.entity&&ImGui::IsMouseDown(0)){
                glm::vec2 now;if(localPoint(r,now)){
                    if(sv->barDrag>=0){auto& b=bars[sv->barDrag];const int axis=sv->barDrag;const float track=axis==1?b.trackSize.y:b.trackSize.x,length=axis==1?b.handleSize.y:b.handleSize.x;
                        if(b.shown&&track>length){const float d=(axis==1?now.y-lastLocal.y:now.x-lastLocal.x)/(track-length);const glm::vec2 n=Normalized(st);toNormalized(axis,(axis==1?n.y:n.x)+d);}lastLocal=now;}
                    else{if(!dragActive&&glm::distance(glm::vec2(m.x,m.y),dragStart)>6){dragActive=true;sv->dragging=true;pressed=0;}
                        if(dragActive){glm::vec2 delta=now-lastLocal;const glm::vec2 out=OutOfBounds(st,*sv);if(sv->movementType==1){if(out.x!=0)delta.x*=.5f;if(out.y!=0)delta.y*=.5f;}move(delta);sv->velocity=glm::mix(sv->velocity,delta/dt,.6f);}
                        lastLocal=now;}
                }
            }else if(dragView==r.entity){dragView=0;dragActive=false;if(sv->barDrag>=0)sv->velocity={0,0};sv->dragging=false;sv->barDrag=-1;if(!sv->inertia)sv->velocity={0,0};}
            if(wheelTarget==r.entity){const bool sideways=io.KeyShift||!sv->vertical;const float wheel=io.MouseWheel!=0?io.MouseWheel:io.MouseWheelH;
                move(sideways?glm::vec2(wheel*sv->scrollSensitivity,0):glm::vec2(0,-wheel*sv->scrollSensitivity));sv->velocity={0,0};}
            // Free movement, then the edges
            if(!sv->dragging){
                if(sv->inertia&&glm::length(sv->velocity)>1){move(sv->velocity*dt);sv->velocity*=std::pow(std::clamp(sv->decelerationRate,0.f,.999f),dt);}else sv->velocity={0,0};
                const glm::vec2 out=OutOfBounds(st,*sv);
                if(sv->movementType==2)move(-out);
                else if(sv->movementType==1&&(out.x!=0||out.y!=0)){const float k=1-std::exp(-dt/std::max(.01f,sv->elasticity)*3);glm::vec2 back=-out*k;if(std::abs(out.x)<.5f)back.x=-out.x;if(std::abs(out.y)<.5f)back.y=-out.y;move(back);if(out.x!=0)sv->velocity.x=0;if(out.y!=0)sv->velocity.y=0;}
            }else if(sv->movementType==2)move(-OutOfBounds(st,*sv));
            const glm::vec2 n=Normalized(st);
            if(glm::any(glm::greaterThan(glm::abs(n-sv->lastNormalized),glm::vec2(1e-4f)))){if(sv->lastNormalized.x>=0)ScriptEngine::OnUIInput(r.entity,std::format("{:.4f},{:.4f}",n.x,n.y),false);sv->lastNormalized=n;}
            (void)moved;
        }
    }
    if(!pointerInside){if(interactive)hovered=0;if(!interactive||!focused||!scene.Reg().IsEnabled<UIInputFieldComponent>(focused))return 0;if(ImGui::IsMouseClicked(0)){auto value=scene.Reg().Get<UIInputFieldComponent>(focused).text;ScriptEngine::OnUIInput(focused,value,true);focused=0;return 0;}if(!ImGui::IsWindowFocused())return 0;}
    hovered=0;
    for(auto it=rectangles.rbegin();pointerInside&&it!=rectangles.rend();++it){auto& r=*it;if(!r.raycast||r.alpha<=0)continue;auto& reg=scene.Reg();bool drawable=reg.IsEnabled<UIImageComponent>(r.entity)||reg.IsEnabled<UITextComponent>(r.entity)||reg.IsEnabled<UIInputFieldComponent>(r.entity);if(!drawable&&(sceneView||r.entity!=r.canvas))continue;
        if(auto* image=reg.TryGetEnabled<UIImageComponent>(r.entity);image&&!image->raycastTarget)continue;if(auto* text=reg.TryGetEnabled<UITextComponent>(r.entity);text&&!text->raycastTarget)continue;
        auto& canvas=reg.Get<CanvasComponent>(r.canvas);glm::vec2 canvasPoint;
        if(!sceneView&&canvas.renderMode==0)canvasPoint={(m.x-origin.x)/size.x*r.canvasSize.x,(origin.y+size.y-m.y)/size.y*r.canvasSize.y};else if(!MouseOnCanvas(scene,r.canvas,camera,origin,size,m,canvasPoint,r.canvasSize))continue;
        auto local=glm::vec2(glm::inverse(r.localToCanvas)*glm::vec4(canvasPoint,0,1));if(local.x<0||local.y<0||local.x>r.size.x||local.y>r.size.y)continue;
        bool clipped=false;for(auto& mask:r.masks){float sign=0;for(int i=0;i<4;++i){auto a=mask[i],b=mask[(i+1)%4];float cross=(b.x-a.x)*(canvasPoint.y-a.y)-(b.y-a.y)*(canvasPoint.x-a.x);if(i==0)sign=cross;else if(sign*cross<0)clipped=true;}}if(clipped)continue;
        hovered=r.entity;hoveredInteractable=r.interactable;break;
    }
    EntityID previousFocus=focused;
    if(pointerInside&&ImGui::IsMouseClicked(0)){pressed=hovered;focused=hovered;}
    if(interactive&&previousFocus&&previousFocus!=focused)if(auto* input=scene.Reg().TryGetEnabled<UIInputFieldComponent>(previousFocus))ScriptEngine::OnUIInput(previousFocus,input->text,true);
    if(interactive&&focused)if(auto* input=scene.Reg().TryGetEnabled<UIInputFieldComponent>(focused);input&&input->interactable){
        ImGui::SetNextFrameWantCaptureKeyboard(true);ImGui::GetIO().WantTextInput=true;ImGui::GetCurrentContext()->WantTextInputNextFrame=1;auto& io=ImGui::GetIO();auto original=input->text;
        input->cursor=std::clamp(input->cursor,0,(int)input->text.size());input->selectionStart=std::clamp(input->selectionStart,0,(int)input->text.size());input->selectionEnd=std::clamp(input->selectionEnd,0,(int)input->text.size());
        auto before=[&](int at){if(at>0){--at;while(at>0&&((unsigned char)input->text[at]&0xC0)==0x80)--at;}return at;};auto after=[&](int at){if(at<(int)input->text.size()){++at;while(at<(int)input->text.size()&&((unsigned char)input->text[at]&0xC0)==0x80)++at;}return at;};
        auto move=[&](int position){if(!io.KeyShift)input->selectionStart=position;input->cursor=input->selectionEnd=position;};
        auto eraseSelection=[&](){int a=std::min(input->selectionStart,input->selectionEnd),b=std::max(input->selectionStart,input->selectionEnd);if(a!=b)input->text.erase(a,b-a);input->cursor=input->selectionStart=input->selectionEnd=a;};
        if((ImGui::IsMouseClicked(0)||ImGui::IsMouseDragging(0)&&pressed==focused)&&hovered==focused){auto rect=std::find_if(rectangles.begin(),rectangles.end(),[&](auto& r){return r.entity==focused;});if(rect!=rectangles.end()){glm::vec2 point;auto& canvas=scene.Reg().Get<CanvasComponent>(rect->canvas);if(canvas.renderMode==0)point={(m.x-origin.x)/size.x*rect->canvasSize.x,(origin.y+size.y-m.y)/size.y*rect->canvasSize.y};else MouseOnCanvas(scene,rect->canvas,camera,origin,size,m,point,rect->canvasSize);auto local=glm::vec2(glm::inverse(rect->localToCanvas)*glm::vec4(point,0,1));float target=local.x-8+input->scrollX,x=0;int cursor=0;if(auto* device=AssetManager::Device()){auto font=UIFont::Get(*device,input->font);for(auto& token:ParseRichText(input->text,{},false)){if(token.codepoint==10)break;float advance=font?font->Glyph(*device,input->password?'*':token.codepoint).advance*input->fontSize/48.f:input->fontSize*.5f;if(target<x+advance*.5f)break;x+=advance;cursor=after(cursor);}}if(ImGui::IsMouseDragging(0)){input->cursor=input->selectionEnd=cursor;}else move(cursor);}}
        if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_A)){input->selectionStart=0;input->cursor=input->selectionEnd=(int)input->text.size();}
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))move(before(input->cursor));if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))move(after(input->cursor));if(ImGui::IsKeyPressed(ImGuiKey_Home))move(0);if(ImGui::IsKeyPressed(ImGuiKey_End))move((int)input->text.size());
        if(io.KeyCtrl&&(ImGui::IsKeyPressed(ImGuiKey_C)||ImGui::IsKeyPressed(ImGuiKey_X))&&!input->password){auto selected=input->text.substr(std::min(input->selectionStart,input->selectionEnd),std::abs(input->selectionEnd-input->selectionStart));ImGui::SetClipboardText(selected.c_str());if(!input->readOnly&&ImGui::IsKeyPressed(ImGuiKey_X))eraseSelection();}
        auto insert=[&](std::string value){if(input->readOnly)return;if(!input->multiline){std::erase(value,char(13));std::erase(value,char(10));}if(input->characterLimit>0){auto count=[](const std::string& text){int n=0;for(unsigned char c:text)if((c&0xC0)!=0x80)++n;return n;};int available=std::max(0,input->characterLimit-count(input->text)+count(input->text.substr(std::min(input->selectionStart,input->selectionEnd),std::abs(input->selectionEnd-input->selectionStart))));int n=0;for(size_t i=0;i<value.size();++i)if(((unsigned char)value[i]&0xC0)!=0x80&&++n>available){value.resize(i);break;}}eraseSelection();input->text.insert(input->cursor,value);input->cursor+=(int)value.size();input->selectionStart=input->selectionEnd=input->cursor;};
        if(!input->readOnly){if(ImGui::IsKeyPressed(ImGuiKey_Backspace)){if(input->selectionStart==input->selectionEnd)input->selectionStart=before(input->cursor);eraseSelection();}if(ImGui::IsKeyPressed(ImGuiKey_Delete)){if(input->selectionStart==input->selectionEnd)input->selectionEnd=after(input->cursor);eraseSelection();}if(io.KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_V)){auto clipboard=ImGui::GetClipboardText();if(clipboard)insert(clipboard);}if(!io.KeyCtrl&&!io.KeySuper)for(ImWchar code:io.InputQueueCharacters)if(code>=32){char utf8[5]{};ImTextCharToUtf8(utf8,code);insert(utf8);}}
        bool submit=ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);if(submit&&input->multiline&&!io.KeyCtrl){insert(std::string(1,char(10)));submit=false;}
        if(input->characterLimit>0){int count=0;for(size_t i=0;i<input->text.size();++i)if(((unsigned char)input->text[i]&0xC0)!=0x80&&++count>input->characterLimit){input->text.resize(i);break;}}
        auto value=input->text;if(original!=value)ScriptEngine::OnUIInput(focused,value,false);if(submit||ImGui::IsKeyPressed(ImGuiKey_Escape)){ScriptEngine::OnUIInput(focused,value,true);focused=0;}
    }
    if(interactive&&ImGui::IsMouseReleased(0)){if(hovered&&pressed==hovered&&!s_SwallowRelease){
            if(auto* button=scene.Reg().TryGetEnabled<UIButtonComponent>(hovered);button&&button->interactable&&hoveredInteractable)ScriptEngine::OnUIButton(hovered);
            if(auto* dd=scene.Reg().TryGetEnabled<UIDropdownComponent>(hovered);dd&&dd->interactable&&hoveredInteractable&&!dd->options.empty()){dd->open=true;dd->hoveredItem=dd->value;s_OpenDropdown=hovered;
                if(auto* r=FindRect(rectangles,hovered)){auto g=DropdownList(*r,*dd);dd->scroll=std::clamp(dd->value*dd->itemHeight-(g.size.y-dd->itemHeight)*.5f,0.f,std::max(0.f,g.content-g.size.y));}}
        }pressed=0;}
    if(interactive&&ImGui::IsKeyPressed(ImGuiKey_Tab)){
        std::vector<EntityID> buttons;for(auto& r:rectangles)if(r.interactable&&r.raycast)if((scene.Reg().IsEnabled<UIButtonComponent>(r.entity)&&scene.Reg().Get<UIButtonComponent>(r.entity).interactable)||(scene.Reg().IsEnabled<UIInputFieldComponent>(r.entity)&&scene.Reg().Get<UIInputFieldComponent>(r.entity).interactable)||(scene.Reg().IsEnabled<UIDropdownComponent>(r.entity)&&scene.Reg().Get<UIDropdownComponent>(r.entity).interactable))buttons.push_back(r.entity);
        if(!buttons.empty()){auto found=std::find(buttons.begin(),buttons.end(),focused);int index=found==buttons.end()?-1:(int)(found-buttons.begin());index=(index+(ImGui::GetIO().KeyShift?-1:1)+(int)buttons.size())%(int)buttons.size();focused=buttons[index];}
    }
    if(interactive&&focused&&!scene.Reg().IsEnabled<UIInputFieldComponent>(focused)&&!ImGui::GetIO().WantTextInput&&(ImGui::IsKeyPressed(ImGuiKey_Enter)||ImGui::IsKeyPressed(ImGuiKey_Space))){
        if(auto* dd=scene.Reg().TryGetEnabled<UIDropdownComponent>(focused);dd&&dd->interactable&&!dd->options.empty()){dd->open=true;dd->hoveredItem=dd->value;s_OpenDropdown=focused;}
        else ScriptEngine::OnUIButton(focused);}
    return ImGui::IsMouseClicked(0)?hovered:0;
}
void CanvasRenderer::InvalidateAssets(){cache.clear();UIFont::Clear();}
void CanvasRenderer::Clear(){InvalidateAssets();hovered=pressed=focused=0;s_OpenDropdown=0;s_BarHover.clear();}
}
