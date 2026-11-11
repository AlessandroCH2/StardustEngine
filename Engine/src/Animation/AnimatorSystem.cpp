#include "Animation/AnimatorSystem.h"
#include "Animation/ModelSkeleton.h"
#include "Animation/AvatarSystem.h"
#include "Animation/AnimatedProperties.h"
#include "Scripting/ScriptEngine.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <unordered_set>
namespace Stardust {
using json = nlohmann::json;
// Bumped whenever an asset is (re)parsed, so compiled clips know when to rebuild
static u64 s_AssetGeneration = 1;
static const json& ReadAsset(const std::string& path) {
    static const json empty = json::object();
    struct Entry { std::filesystem::file_time_type stamp; json data; std::chrono::steady_clock::time_point checked; bool exists = false; };
    static std::unordered_map<std::string, Entry> cache;
    if (path.empty()) return empty;
    // The file's time stamp is checked at most twice a second (editing an asset still reloads it),
    // not on every one of the several lookups per animated object and frame
    auto now = std::chrono::steady_clock::now();
    auto found = cache.find(path);
    if (found != cache.end() && now - found->second.checked < std::chrono::milliseconds(500))
        return found->second.exists && found->second.data.is_object() ? found->second.data : empty;
    auto& entry = cache[path];
    entry.checked = now;
    auto resolved = AssetManager::ToAbsolute(path);
    std::error_code ec; auto stamp = std::filesystem::last_write_time(resolved,ec);
    entry.exists = !(ec && !FileSystem::Exists(resolved));
    if (!entry.exists) return empty;
    if (entry.data.is_null() || (!ec && entry.stamp != stamp)) { entry.stamp = stamp; entry.data = json::parse(FileSystem::ReadText(resolved),nullptr,false); ++s_AssetGeneration; }
    return entry.data.is_object() ? entry.data : empty;
}

// ---------------------------------------------------------------- compiled clips
// Generic (non-humanoid) clips are converted once into flat key arrays, and each animated object
// keeps the entities its tracks resolve to, so sampling a frame does no JSON or name lookups.
namespace {
struct CompiledChannel { std::vector<float> times; std::vector<glm::vec3> values; };
struct CompiledTrack { std::vector<std::string> path; bool constant = false; CompiledChannel position, rotation, scale; };
struct CompiledClip { u64 generation = 0; bool fast = false; std::vector<CompiledTrack> tracks; };
struct Binding { u64 rootUuid = 0; std::vector<EntityID> targets; std::vector<u64> uuids; };

CompiledChannel CompileChannel(const json& keys, bool& ok) {
    CompiledChannel c;
    if (!keys.is_array()) { ok = false; return c; }
    for (auto& k : keys) {
        if (!k.is_array() || k.size() < 4) continue;
        c.times.push_back(k[0].get<float>());
        c.values.push_back({ k[1].get<float>(), k[2].get<float>(), k[3].get<float>() });
    }
    for (size_t i = 1; i < c.times.size(); ++i) if (c.times[i] < c.times[i - 1]) ok = false; // unsorted: keep the generic path
    return c;
}

const CompiledClip& Compile(const json& clip) {
    static std::unordered_map<const json*, CompiledClip> cache;
    auto& c = cache[&clip];
    if (c.generation == s_AssetGeneration) return c;
    c = CompiledClip{}; c.generation = s_AssetGeneration;
    if (clip.value("humanoid", false)) return c;
    bool ok = true;
    for (auto& track : clip.value("tracks", json::array())) {
        CompiledTrack t;
        for (auto& [key, value] : track.items())
            if (key != "path" && key != "position" && key != "rotation" && key != "scale" && key != "interpolation") ok = false; // animated properties
        auto interpolation = track.value("interpolation", std::string("Linear"));
        if (interpolation == "Cubic") ok = false;
        t.constant = interpolation == "Constant";
        std::stringstream segments(track.value("path", std::string())); std::string name;
        while (std::getline(segments, name, '/')) if (!name.empty()) t.path.push_back(name);
        if (track.contains("position")) t.position = CompileChannel(track["position"], ok);
        if (track.contains("rotation")) t.rotation = CompileChannel(track["rotation"], ok);
        if (track.contains("scale")) t.scale = CompileChannel(track["scale"], ok);
        c.tracks.push_back(std::move(t));
        if (!ok) break;
    }
    c.fast = ok;
    return c;
}

glm::vec3 SampleChannel(const CompiledChannel& c, float time, bool constant) {
    auto it = std::upper_bound(c.times.begin(), c.times.end(), time);
    if (it == c.times.begin()) return c.values.front();
    if (it == c.times.end()) return c.values.back();
    size_t b = (size_t)(it - c.times.begin()), a = b - 1;
    if (constant) return c.values[a];
    float span = c.times[b] - c.times[a];
    float t = span > 0 ? std::clamp((time - c.times[a]) / span, 0.f, 1.f) : 0.f;
    return glm::mix(c.values[a], c.values[b], t);
}

const Binding& Bind(Scene& scene, EntityID root, const CompiledClip& clip) {
    static std::unordered_map<const void*, std::unordered_map<u64, Binding>> bindings;  // per scene and clip
    auto& perClip = bindings[&clip];
    auto& reg = scene.Reg();
    const u64 rootUuid = reg.Get<IDComponent>(root).uuid;
    u64 key = rootUuid ^ ((u64)(uintptr_t)&scene * 0x9E3779B97F4A7C15ull);
    auto& b = perClip[key];
    bool valid = b.rootUuid == rootUuid && b.targets.size() == clip.tracks.size();
    for (size_t i = 0; valid && i < b.targets.size(); ++i)
        if (b.targets[i] && (!reg.Valid(b.targets[i]) || reg.Get<IDComponent>(b.targets[i]).uuid != b.uuids[i])) valid = false;
    if (valid) return b;
    if (perClip.size() > 4096) { perClip.clear(); return Bind(scene, root, clip); }
    b.rootUuid = rootUuid; b.targets.assign(clip.tracks.size(), kNullEntity); b.uuids.assign(clip.tracks.size(), 0);
    for (size_t i = 0; i < clip.tracks.size(); ++i) {
        EntityID target = root;
        for (auto& name : clip.tracks[i].path) {
            EntityID child = kNullEntity;
            for (auto candidate : scene.GetChildren(target)) if (reg.Get<NameComponent>(candidate).name == name) { child = candidate; break; }
            target = child; if (!target) break;
        }
        b.targets[i] = target;
        b.uuids[i] = target ? reg.Get<IDComponent>(target).uuid : 0;
    }
    return b;
}
}

// Samples a clip asset: the compiled path when the clip only moves transforms with linear/constant keys
static void SampleClipAsset(Scene& scene, EntityID root, const json& clip, float time, bool loop) {
    const CompiledClip& compiled = Compile(clip);
    if (!compiled.fast) { AnimatorSystem::SampleClip(scene, root, clip, time, loop); return; }
    float duration = std::max(.0001f, clip.value("duration", 1.f));
    float t = loop ? std::fmod(std::max(0.f, time), duration) : std::clamp(time, 0.f, duration);
    const Binding& binding = Bind(scene, root, compiled);
    auto& reg = scene.Reg();
    for (size_t i = 0; i < compiled.tracks.size(); ++i) {
        EntityID target = binding.targets[i];
        if (!target) continue;
        const CompiledTrack& track = compiled.tracks[i];
        auto& transform = reg.Get<TransformComponent>(target);
        if (!track.position.times.empty()) transform.position = SampleChannel(track.position, t, track.constant);
        if (!track.rotation.times.empty()) transform.SetEuler(SampleChannel(track.rotation, t, track.constant));
        if (!track.scale.times.empty()) transform.scale = SampleChannel(track.scale, t, track.constant);
    }
}
static glm::vec3 Sample(const json& keys, float time, glm::vec3 fallback,const std::string& interpolation="Linear") {
    if (!keys.is_array() || keys.empty()) return fallback;
    auto value = [](const json& k) { return glm::vec3(k.at(1).get<float>(),k.at(2).get<float>(),k.at(3).get<float>()); };
    const json* before=nullptr;const json* after=nullptr;
    for(auto& k:keys){if(!k.is_array()||k.size()<4)continue;float t=k[0];if(t<=time&&(!before||t>(*before)[0].get<float>()))before=&k;if(t>=time&&(!after||t<(*after)[0].get<float>()))after=&k;}
    if(!before)return after?value(*after):fallback;if(!after)return value(*before);float a=(*before)[0],b=(*after)[0];float t=b>a?std::clamp((time-a)/(b-a),0.f,1.f):0.f;
    if(interpolation=="Constant")return value(*before);
    if(interpolation=="Cubic"){
        glm::vec3 outgoing(0),incoming(0);if(before->size()>=10)outgoing={(*before)[7],(*before)[8],(*before)[9]};if(after->size()>=10)incoming={(*after)[4],(*after)[5],(*after)[6]};
        float t2=t*t,t3=t2*t;return (2*t3-3*t2+1)*value(*before)+(t3-2*t2+t)*(b-a)*outgoing+(-2*t3+3*t2)*value(*after)+(t3-t2)*(b-a)*incoming;
    }
    return glm::mix(value(*before),value(*after),t);
}
// Keys are [time, v0..vN-1] with optional in and out tangents after the values
glm::vec4 AnimatorSystem::EvaluateN(const json& keys,float time,glm::vec4 fallback,const std::string& interpolation,int dimension){
    if(!keys.is_array()||keys.empty())return fallback;
    const size_t minimum=(size_t)dimension+1;
    auto value=[&](const json& k){glm::vec4 v(0);for(int i=0;i<dimension;++i)v[i]=k.at(i+1).get<float>();return v;};
    const json* before=nullptr;const json* after=nullptr;
    for(auto& k:keys){if(!k.is_array()||k.size()<minimum)continue;float t=k[0];if(t<=time&&(!before||t>(*before)[0].get<float>()))before=&k;if(t>=time&&(!after||t<(*after)[0].get<float>()))after=&k;}
    if(!before)return after?value(*after):fallback;
    if(!after)return value(*before);
    float a=(*before)[0],b=(*after)[0];float t=b>a?std::clamp((time-a)/(b-a),0.f,1.f):0.f;
    if(interpolation=="Constant")return value(*before);
    if(interpolation=="Cubic"){
        glm::vec4 outgoing(0),incoming(0);
        if(before->size()>=(size_t)1+3*dimension)for(int i=0;i<dimension;++i)outgoing[i]=(*before)[1+2*dimension+i].get<float>();
        if(after->size()>=(size_t)1+3*dimension)for(int i=0;i<dimension;++i)incoming[i]=(*after)[1+dimension+i].get<float>();
        float t2=t*t,t3=t2*t;return (2*t3-3*t2+1)*value(*before)+(t3-2*t2+t)*(b-a)*outgoing+(-2*t3+3*t2)*value(*after)+(t3-t2)*(b-a)*incoming;
    }
    return glm::mix(value(*before),value(*after),t);
}
glm::vec3 AnimatorSystem::Evaluate(const json& keys,float time,glm::vec3 fallback,const std::string& interpolation){return Sample(keys,time,fallback,interpolation);}
void AnimatorSystem::SampleClip(Scene& scene,EntityID root,const json& clip,float time,bool loop){
 if(clip.value("humanoid",false)){AvatarSystem::Apply(scene,root,clip,time,loop);return;}
 float duration=std::max(.0001f,clip.value("duration",1.f));float t=loop?std::fmod(std::max(0.f,time),duration):std::clamp(time,0.f,duration);
            for (auto& track : clip.value("tracks",json::array())) {
                EntityID target = root;
                std::stringstream segments(track.value("path",std::string())); std::string name;
                while (std::getline(segments,name,'/')) {
                    if (name.empty()) continue;
                    EntityID child = kNullEntity;
                    for (auto candidate : scene.GetChildren(target)) if (scene.Reg().Get<NameComponent>(candidate).name == name) { child=candidate; break; }
                    target=child; if (!target) break;
                }
                if (!target) continue;
                auto& transform = scene.Reg().Get<TransformComponent>(target);
                if (track.contains("position")) transform.position=Sample(track["position"],t,transform.position,track.value("interpolation","Linear"));
                if (track.contains("rotation")) transform.SetEuler(Sample(track["rotation"],t,transform.eulerHint,track.value("interpolation","Linear")));
                if (track.contains("scale")) transform.scale=Sample(track["scale"],t,transform.scale,track.value("interpolation","Linear"));
                AnimatedProperties::RegisterReferences(track);
                for (auto& [key, keys] : track.items()) {
                    int dimension; char kind; std::string id;
                    if (!AnimatedProperties::ParseChannel(key, dimension, kind, id) || keys.empty()) continue;
                    const bool stepped = kind == 'b' || kind == 'i' || kind == 'r';
                    AnimatedProperties::Set(scene, target, id, EvaluateN(keys, t, glm::vec4(0), stepped ? std::string("Constant") : track.value("interpolation", std::string("Linear")), dimension));
                }
            }
}
bool AnimatorSystem::AdvanceController(AnimatorComponent& animator,const json& controller,float duration,const json* parameterOwner){
 const json& owner=parameterOwner?*parameterOwner:controller;
 if(auto ps=owner.find("parameters");ps!=owner.end()&&ps->is_array())for(auto& p:*ps){auto name=p.value("name",std::string());auto d=p.find("default");
  if(p.value("type","")=="Vector2"){bool pair=d!=p.end()&&d->is_array()&&d->size()==2;animator.parameters.try_emplace(name+".x",pair?(*d)[0].get<float>():0.f);animator.parameters.try_emplace(name+".y",pair?(*d)[1].get<float>():0.f);}
  else animator.parameters.try_emplace(name,d!=p.end()&&d->is_number()?d->get<float>():0.f);}
 if(animator.state.empty())animator.state=controller.value("defaultState","");
 static const json noArray=json::array(),noObject=json::object();
 auto transitions=controller.find("transitions");
 auto statesIt=controller.find("states");
 const json& states=statesIt!=controller.end()&&statesIt->is_object()?*statesIt:noObject;
 const json& parameters=[&]()->const json&{auto f=owner.find("parameters");return f!=owner.end()&&f->is_array()?*f:noArray;}();
 if(transitions==controller.end()||!transitions->is_array())return false;
 for(const json& t:*transitions){
  if(t.value("mute",false))continue;
  auto fromIt=t.find("from"),toIt=t.find("to");if(fromIt==t.end()||toIt==t.end()||!fromIt->is_string()||!toIt->is_string())continue;
  const std::string& from=fromIt->get_ref<const std::string&>();const std::string& to=toIt->get_ref<const std::string&>();
  if(from!=animator.state&&from!="Any State")continue;if(to==animator.state&&!t.value("canTransitionToSelf",false))continue;
  if(t.value("hasExitTime",false)&&animator.time<std::max(.0001f,duration)*t.value("exitTime",1.f))continue;
  bool pass=true;
  json legacy;const json* conditions=&noArray;
  if(auto c=t.find("conditions");c!=t.end()&&c->is_array())conditions=&*c;
  if(conditions->empty()&&!t.value("parameter",std::string()).empty()){legacy=json::array({{{"parameter",t["parameter"]},{"mode",">="},{"threshold",t.value("threshold",.5f)}}});conditions=&legacy;}
  for(auto& c:*conditions){auto parameter=c.value("parameter","");auto found=animator.parameters.find(parameter);if(found==animator.parameters.end()){pass=false;break;}float value=found->second,threshold=c.value("threshold",0.f);auto op=c.value("mode",">");bool match=op=="If"?value!=0:op=="IfNot"?value==0:op==">"?value>threshold:op=="<"?value<threshold:op=="=="?value==threshold:op=="!="?value!=threshold:op=="<="?value<=threshold:value>=threshold;if(!match){pass=false;break;}}
  if(!pass)continue;if(to!="Exit"&&!states.contains(to))continue;
  for(auto& c:*conditions)for(auto& p:parameters)if(p.value("name","")==c.value("parameter","")&&p.value("type","")=="Trigger")animator.parameters[p.value("name","")]=0;
  animator.previousState=animator.state;animator.previousTime=animator.time;animator.transitionTime=0;animator.transitionDuration=std::max(0.f,t.value("duration",0.f))*(t.value("fixedDuration",true)?1.f:duration);animator.state=to;animator.time=std::max(0.f,t.value("offset",0.f));animator.blendDuration=0;if(to=="Exit")animator.playing=false;return true;
 }return false;
}
// ---------------------------------------------------------------- blend trees
// A state with "blendTree" mixes its child clips by one parameter (1D) or two (2D), like Unity's Blend Trees
glm::vec2 AnimatorSystem::BlendPoint(const json& tree,const std::unordered_map<std::string,float>& parameters){
    auto get=[&](const std::string& name){auto f=parameters.find(name);return f==parameters.end()?0.f:f->second;};
    return {get(tree.value("parameter",std::string())),get(tree.value("parameterY",std::string()))};
}
std::vector<float> AnimatorSystem::BlendWeights(const json& tree,glm::vec2 p){
    auto children=tree.find("children");
    if(children==tree.end()||!children->is_array()||children->empty())return {};
    const json& list=*children;const int count=(int)list.size();
    std::vector<float> weights(count,0.f);
    const std::string type=tree.value("type",std::string("1D"));
    if(type=="1D"){
        std::vector<int> order(count);for(int i=0;i<count;++i)order[i]=i;
        auto threshold=[&](int i){return list[i].value("threshold",0.f);};
        std::stable_sort(order.begin(),order.end(),[&](int a,int b){return threshold(a)<threshold(b);});
        if(p.x<=threshold(order.front())){weights[order.front()]=1;return weights;}
        if(p.x>=threshold(order.back())){weights[order.back()]=1;return weights;}
        for(int k=0;k+1<count;++k){float a=threshold(order[k]),b=threshold(order[k+1]);if(p.x>=a&&p.x<=b){float f=b>a?(p.x-a)/(b-a):0.f;weights[order[k]]=1-f;weights[order[k+1]]+=f;break;}}
        return weights;
    }
    std::vector<glm::vec2> points(count);
    for(int i=0;i<count;++i){auto pos=list[i].value("position",json::array({0.f,0.f}));points[i]=pos.is_array()&&pos.size()==2?glm::vec2(pos[0].get<float>(),pos[1].get<float>()):glm::vec2(0);}
    // Gradient band interpolation: cartesian for Freeform Cartesian, polar (direction + speed) for the directional types
    const bool polar=type!="2D Freeform Cartesian";
    auto angle=[](glm::vec2 u,glm::vec2 v){return std::atan2(u.x*v.y-u.y*v.x,glm::dot(u,v));};
    float total=0;
    for(int i=0;i<count;++i){
        float h=1;
        for(int j=0;j<count;++j){
            if(i==j)continue;
            glm::vec2 a=points[i],b=points[j],ij=b-a,ip=p-a;
            if(polar){
                float la=glm::length(a),lb=glm::length(b),lp=glm::length(p),average=(la+lb)*.5f;
                if(average>1e-5f){
                    float angleAB,angleAP;
                    if(la<1e-5f){angleAB=lp>1e-5f&&lb>1e-5f?angle(p,b):0.f;angleAP=0;}
                    else if(lb<1e-5f){angleAB=lp>1e-5f?angle(a,p):0.f;angleAP=angleAB;}
                    else{angleAB=angle(a,b);angleAP=lp>1e-5f?angle(a,p):0.f;}
                    ij={(lb-la)/average,angleAB*2};ip={(lp-la)/average,angleAP*2};
                }
            }
            float lengthSq=glm::dot(ij,ij);if(lengthSq<1e-8f)continue;
            h=std::min(h,1-glm::dot(ip,ij)/lengthSq);
        }
        weights[i]=std::max(0.f,h);total+=weights[i];
    }
    if(total<=1e-6f){int nearest=0;for(int i=1;i<count;++i)if(glm::distance(points[i],p)<glm::distance(points[nearest],p))nearest=i;std::fill(weights.begin(),weights.end(),0.f);weights[nearest]=1;return weights;}
    for(float& w:weights)w/=total;
    return weights;
}
static float ClipSeconds(const json& child){return std::max(.0001f,ReadAsset(child.value("clip",std::string())).value("duration",1.f));}
static float ChildSpeed(const json& child){return std::max(.01f,std::abs(child.value("speed",1.f)));}
float AnimatorSystem::StateDuration(const json& state,const std::unordered_map<std::string,float>& parameters){
    auto tree=state.find("blendTree");
    if(tree==state.end()||!tree->is_object())return ReadAsset(state.value("clip",std::string())).value("duration",1.f);
    auto weights=BlendWeights(*tree,BlendPoint(*tree,parameters));
    float duration=0;for(size_t i=0;i<weights.size();++i)if(weights[i]>0)duration+=weights[i]*ClipSeconds((*tree)["children"][i])/ChildSpeed((*tree)["children"][i]);
    return std::max(.0001f,weights.empty()?1.f:duration);
}
using PoseSnapshot = std::vector<std::pair<EntityID, TransformComponent>>;
static PoseSnapshot CapturePose(Scene& scene, EntityID root);
static void BlendTransform(TransformComponent& dest, const TransformComponent& from, float weight);
void AnimatorSystem::SampleState(Scene& scene,EntityID root,const json& state,float time,const std::unordered_map<std::string,float>& parameters){
    const bool loop=state.value("loop",true);
    auto tree=state.find("blendTree");
    if(tree==state.end()||!tree->is_object()){auto clip=state.find("clip");if(clip!=state.end()&&clip->is_string()&&!clip->get_ref<const std::string&>().empty())SampleClipAsset(scene,root,ReadAsset(clip->get_ref<const std::string&>()),time,loop);return;}
    auto weights=BlendWeights(*tree,BlendPoint(*tree,parameters));
    if(weights.empty())return;
    const json& children=(*tree)["children"];
    // Every child runs at the same normalized time, so a walk and a run keep their feet in step
    const float phase=time/StateDuration(state,parameters);
    std::vector<int> active;for(int i=0;i<(int)weights.size();++i)if(weights[i]>.001f&&!children[i].value("clip",std::string()).empty())active.push_back(i);
    auto sampleChild=[&](int i){const json& child=children[i];const float length=ClipSeconds(child);float t=phase*length;if(child.value("speed",1.f)<0)t=length-std::fmod(t,length);SampleClipAsset(scene,root,ReadAsset(child.value("clip",std::string())),t,loop);};
    if(active.empty())return;
    if(active.size()==1){sampleChild(active[0]);return;}
    PoseSnapshot original=CapturePose(scene,root),result;float total=0;
    for(int i:active){
        for(auto& [entity,pose]:original)scene.Reg().Get<TransformComponent>(entity)=pose;
        sampleChild(i);
        if(result.empty()){result=original;for(auto& [entity,pose]:result)pose=scene.Reg().Get<TransformComponent>(entity);total=weights[i];continue;}
        total+=weights[i];
        for(auto& [entity,pose]:result){TransformComponent sampled=scene.Reg().Get<TransformComponent>(entity);BlendTransform(sampled,pose,weights[i]/total);pose=sampled;}
    }
    for(auto& [entity,pose]:result)scene.Reg().Get<TransformComponent>(entity)=pose;
}
// Animation Events are collected while animating and delivered once the animators are done:
// a script may add or remove components, which is not safe in the middle of the loop
struct PendingEvent { EntityID entity; json event; };
static std::vector<PendingEvent> s_PendingEvents;

static void CollectEvents(EntityID e, AnimatorComponent& a, const json& states, float dt) {
    if (!states.contains(a.state)) return;
    const json& state = states[a.state];
    const float current = a.time;
    float previous = a.eventTime;
    if (a.eventState != a.state) { a.eventState = a.state; previous = std::max(0.f, current - dt * a.speed); }
    a.eventTime = current;
    if (current <= previous) return;
    const json& clip = ReadAsset(state.value("clip", ""));
    auto events = clip.find("events");
    if (events == clip.end() || !events->is_array()) return;
    const float duration = std::max(.0001f, clip.value("duration", 1.f));
    const bool loop = state.value("loop", true);
    const int firstCycle = loop ? (int)std::floor(previous / duration) : 0, lastCycle = loop ? std::min(firstCycle + 32, (int)std::floor(current / duration)) : 0;
    for (const json& event : *events) {
        const float t = event.value("time", 0.f);
        for (int cycle = firstCycle; cycle <= lastCycle; ++cycle) {
            const float at = t + cycle * duration;
            if (at >= previous && at < current) s_PendingEvents.push_back({ e, event });
        }
    }
}

static PoseSnapshot CapturePose(Scene& scene, EntityID root) {
    // The animated object and its descendants, walked through the hierarchy (not the whole scene)
    PoseSnapshot pose;
    std::vector<EntityID> stack{ root };
    while (!stack.empty()) {
        EntityID entity = stack.back(); stack.pop_back();
        pose.push_back({ entity, scene.Reg().Get<TransformComponent>(entity) });
        for (EntityID child : scene.GetChildren(entity)) stack.push_back(child);
    }
    return pose;
}
static void BlendTransform(TransformComponent& dest, const TransformComponent& from, float weight) {
    dest.position = glm::mix(from.position, dest.position, weight);
    dest.scale = glm::mix(from.scale, dest.scale, weight);
    dest.rotation = glm::slerp(from.rotation, dest.rotation, weight);
    dest.eulerHint = glm::degrees(glm::eulerAngles(dest.rotation));
}

// Samples the animator's current state, cross-fading from the previous one during a transition
static void SampleStatePose(Scene& scene, EntityID e, AnimatorComponent& a, const json& states, float dt, const std::unordered_map<std::string, float>& parameters) {
    if (!states.contains(a.state)) return;
    const json& state = states[a.state];
    if (!state.contains("blendTree") && state.value("clip", std::string()).empty()) return;
    if (a.previousState.empty() || a.transitionDuration <= 0 || a.transitionTime >= a.transitionDuration) {
        a.previousState.clear();
        AnimatorSystem::SampleState(scene, e, state, a.time, parameters);
        return;
    }
    PoseSnapshot original = CapturePose(scene, e);
    a.previousTime += dt * a.speed;
    if (states.contains(a.previousState)) AnimatorSystem::SampleState(scene, e, states[a.previousState], a.previousTime, parameters);
    PoseSnapshot source = original;
    for (auto& [entity, pose] : source) pose = scene.Reg().Get<TransformComponent>(entity);
    for (auto& [entity, pose] : original) scene.Reg().Get<TransformComponent>(entity) = pose;
    AnimatorSystem::SampleState(scene, e, state, a.time, parameters);
    a.transitionTime += dt;
    float blend = std::clamp(a.transitionTime / a.transitionDuration, 0.f, 1.f);
    for (auto& [entity, pose] : source) BlendTransform(scene.Reg().Get<TransformComponent>(entity), pose, blend);
}

// Blend trees change length with their weights: rescale the time so the motion keeps its phase
static float KeepBlendPhase(AnimatorComponent& a, const json& states, const std::unordered_map<std::string, float>& parameters) {
    if (!states.contains(a.state)) return 1.0f;
    const json& state = states[a.state];
    const float duration = AnimatorSystem::StateDuration(state, parameters);
    if (!state.contains("blendTree")) { a.blendDuration = 0; return duration; }
    if (a.blendDuration > 0 && duration != a.blendDuration) a.time *= duration / a.blendDuration;
    a.blendDuration = duration;
    return duration;
}

// Layers above the base one: Override blends towards the layer's pose, Additive adds its change from frame 0
static void UpdateLayers(Scene& scene, EntityID e, AnimatorComponent& animator, const json& controller, float dt) {
    auto found = controller.find("layers");
    if (found == controller.end() || !found->is_array() || found->empty()) { animator.layers.clear(); return; }
    const json& layers = *found;
    animator.layers.resize(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        const json& layer = layers[i];
        AnimatorLayerRuntime& runtime = animator.layers[i];
        const float weight = std::clamp(layer.value("weight", 1.0f), 0.0f, 1.0f);

        AnimatorComponent view;
        view.state = runtime.state; view.previousState = runtime.previousState;
        view.time = runtime.time + dt * animator.speed; view.previousTime = runtime.previousTime;
        view.transitionTime = runtime.transitionTime; view.transitionDuration = runtime.transitionDuration;
        view.eventState = runtime.eventState; view.eventTime = runtime.eventTime;
        view.blendDuration = runtime.blendDuration;
        view.speed = animator.speed;
        view.playing = true;
        std::swap(view.parameters, animator.parameters);
        static const json noStates = json::object();
        const json& states = layer.contains("states") ? layer["states"] : noStates;
        if (view.state.empty()) view.state = layer.value("defaultState", std::string());
        const float duration = KeepBlendPhase(view, states, view.parameters);
        AnimatorSystem::AdvanceController(view, layer, duration, &controller);
        std::swap(view.parameters, animator.parameters);
        if (weight > 0 && ScriptEngine::RuntimeScene() == &scene) CollectEvents(e, view, states, dt);

        if (weight > 0 && states.contains(view.state)) {
            PoseSnapshot before = CapturePose(scene, e);
            const bool additive = layer.value("blending", std::string("Override")) == "Additive";
            std::unordered_map<EntityID, TransformComponent> reference;
            if (additive) {
                const json& state = states[view.state];
                AnimatorSystem::SampleState(scene, e, state, 0.0f, animator.parameters);
                for (auto& [entity, pose] : before) { reference[entity] = scene.Reg().Get<TransformComponent>(entity); scene.Reg().Get<TransformComponent>(entity) = pose; }
            }
            SampleStatePose(scene, e, view, states, dt, animator.parameters);

            std::unordered_set<EntityID> maskRoots;
            for (const json& path : layer.value("mask", json::array())) {
                EntityID target = e;
                std::stringstream parts(path.get<std::string>());
                std::string segment;
                while (target && std::getline(parts, segment, '/')) {
                    if (segment.empty()) continue;
                    EntityID child = kNullEntity;
                    for (EntityID c : scene.GetChildren(target)) if (scene.Reg().Get<NameComponent>(c).name == segment) { child = c; break; }
                    target = child;
                }
                if (target) maskRoots.insert(target);
            }
            auto masked = [&](EntityID entity) {
                if (maskRoots.empty()) return true;
                for (EntityID node = entity; node; node = scene.GetParent(node)) { if (maskRoots.count(node)) return true; if (node == e) break; }
                return false;
            };
            for (auto& [entity, pose] : before) {
                auto& dest = scene.Reg().Get<TransformComponent>(entity);
                if (!masked(entity)) { dest = pose; continue; }
                if (additive) {
                    const TransformComponent& ref = reference[entity];
                    TransformComponent sum = pose;
                    sum.position += dest.position - ref.position;
                    sum.scale *= dest.scale / glm::max(ref.scale, glm::vec3(1e-5f));
                    sum.rotation = glm::normalize(pose.rotation * glm::inverse(ref.rotation) * dest.rotation);
                    dest = sum;
                }
                BlendTransform(dest, pose, weight);
            }
        }
        runtime.state = view.state; runtime.previousState = view.previousState;
        runtime.time = view.time; runtime.previousTime = view.previousTime;
        runtime.transitionTime = view.transitionTime; runtime.transitionDuration = view.transitionDuration;
        runtime.eventState = view.eventState; runtime.eventTime = view.eventTime;
        runtime.blendDuration = view.blendDuration;
    }
}

void AnimatorSystem::Update(Scene& scene, float dt) {
    scene.Reg().Each<AnimatorComponent>([&](EntityID e, AnimatorComponent& animator) {
        if (!animator.playing || !scene.IsActiveInHierarchy(e)) return;
        animator.time += dt * animator.speed;
        const bool rig = scene.Reg().Has<FbxRigComponent>(e);
        if (animator.controller.empty()) { if (rig) ModelSkeleton::ApplyPose(scene, e, animator.time, animator.state); return; }
        try {
            const json& controller = ReadAsset(animator.controller);
            static const json noStates = json::object();
            const json& states = controller.contains("states") ? controller["states"] : noStates;
            AdvanceController(animator, controller, KeepBlendPhase(animator, states, animator.parameters));
            // A state without a clip can still name a take embedded in the FBX
            bool hasClip = states.contains(animator.state) && (states[animator.state].contains("blendTree") || !states[animator.state].value("clip", std::string()).empty());
            if (rig && !hasClip) ModelSkeleton::ApplyPose(scene, e, animator.time, animator.state);
            if (ScriptEngine::RuntimeScene() == &scene) CollectEvents(e, animator, states, dt);
            SampleStatePose(scene, e, animator, states, dt, animator.parameters);
            UpdateLayers(scene, e, animator, controller, dt);
        } catch (const std::exception& ex) { SD_ERROR("Animator '{}': {}", animator.controller, ex.what()); animator.playing = false; }
    });
    auto pending = std::move(s_PendingEvents);
    s_PendingEvents.clear();
    for (const PendingEvent& p : pending)
        if (scene.Reg().Valid(p.entity))
            ScriptEngine::FireAnimationEvent(p.entity, p.event.value("script", std::string()), p.event.value("function", std::string()), p.event.value("signature", std::string()), p.event.value("arguments", json::array()).dump());
}
}
