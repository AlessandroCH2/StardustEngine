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
#include <unordered_set>
namespace Stardust {
using json = nlohmann::json;
static const json& ReadAsset(const std::string& path) {
    static const json empty = json::object();
    static std::unordered_map<std::string, std::pair<std::filesystem::file_time_type,json>> cache;
    auto resolved = AssetManager::ToAbsolute(path);
    std::error_code ec; auto stamp = std::filesystem::last_write_time(resolved,ec);
    if (ec && !FileSystem::Exists(resolved)) return empty;
    auto& cached = cache[FileSystem::Utf8(resolved)];
    if (cached.second.is_null() || (!ec && cached.first != stamp)) { cached.first = stamp; cached.second = json::parse(FileSystem::ReadText(resolved),nullptr,false); }
    return cached.second.is_object() ? cached.second : empty;
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
                for (auto& [key, keys] : track.items()) {
                    int dimension; char kind; std::string id;
                    if (!AnimatedProperties::ParseChannel(key, dimension, kind, id) || keys.empty()) continue;
                    const bool stepped = kind == 'b' || kind == 'i';
                    AnimatedProperties::Set(scene, target, id, EvaluateN(keys, t, glm::vec4(0), stepped ? std::string("Constant") : track.value("interpolation", std::string("Linear")), dimension));
                }
            }
}
bool AnimatorSystem::AdvanceController(AnimatorComponent& animator,const json& controller,float duration,const json* parameterOwner){
 const json& owner=parameterOwner?*parameterOwner:controller;
 for(auto& p:owner.value("parameters",json::array()))animator.parameters.try_emplace(p.value("name",""),p.value("default",0.f));
 if(animator.state.empty())animator.state=controller.value("defaultState","");
 for(auto& t:controller.value("transitions",json::array())){
  auto from=t.value("from","");if(from!=animator.state&&from!="Any State")continue;auto to=t.value("to","");if(to==animator.state&&!t.value("canTransitionToSelf",false))continue;
  if(t.value("hasExitTime",false)&&animator.time<std::max(.0001f,duration)*t.value("exitTime",1.f))continue;
  bool pass=true;auto conditions=t.value("conditions",json::array());
  if(conditions.empty()&&!t.value("parameter",std::string()).empty())conditions.push_back({{"parameter",t["parameter"]},{"mode",">="},{"threshold",t.value("threshold",.5f)}});
  for(auto& c:conditions){auto parameter=c.value("parameter","");auto found=animator.parameters.find(parameter);if(found==animator.parameters.end()){pass=false;break;}float value=found->second,threshold=c.value("threshold",0.f);auto op=c.value("mode",">");bool match=op=="If"?value!=0:op=="IfNot"?value==0:op==">"?value>threshold:op=="<"?value<threshold:op=="=="?value==threshold:op=="!="?value!=threshold:op=="<="?value<=threshold:value>=threshold;if(!match){pass=false;break;}}
  if(!pass)continue;if(to!="Exit"&&!controller.value("states",json::object()).contains(to))continue;
  for(auto& c:conditions)for(auto& p:owner.value("parameters",json::array()))if(p.value("name","")==c.value("parameter","")&&p.value("type","")=="Trigger")animator.parameters[p.value("name","")]=0;
  animator.previousState=animator.state;animator.previousTime=animator.time;animator.transitionTime=0;animator.transitionDuration=std::max(0.f,t.value("duration",0.f))*(t.value("fixedDuration",true)?1.f:duration);animator.state=to;animator.time=std::max(0.f,t.value("offset",0.f));if(to=="Exit")animator.playing=false;return true;
 }return false;
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

using PoseSnapshot = std::vector<std::pair<EntityID, TransformComponent>>;

static PoseSnapshot CapturePose(Scene& scene, EntityID root) {
    PoseSnapshot pose;
    for (EntityID entity : scene.Reg().AllEntities())
        if (entity == root || scene.IsDescendantOf(entity, root)) pose.push_back({ entity, scene.Reg().Get<TransformComponent>(entity) });
    return pose;
}
static void BlendTransform(TransformComponent& dest, const TransformComponent& from, float weight) {
    dest.position = glm::mix(from.position, dest.position, weight);
    dest.scale = glm::mix(from.scale, dest.scale, weight);
    dest.rotation = glm::slerp(from.rotation, dest.rotation, weight);
    dest.eulerHint = glm::degrees(glm::eulerAngles(dest.rotation));
}

// Samples the animator's current state, cross-fading from the previous one during a transition
static void SampleStatePose(Scene& scene, EntityID e, AnimatorComponent& a, const json& states, float dt) {
    if (!states.contains(a.state)) return;
    const json& state = states[a.state];
    std::string clipPath = state.value("clip", "");
    if (clipPath.empty()) return;
    const json& clip = ReadAsset(clipPath);
    if (a.previousState.empty() || a.transitionDuration <= 0 || a.transitionTime >= a.transitionDuration) {
        a.previousState.clear();
        AnimatorSystem::SampleClip(scene, e, clip, a.time, state.value("loop", true));
        return;
    }
    PoseSnapshot original = CapturePose(scene, e);
    a.previousTime += dt * a.speed;
    if (states.contains(a.previousState)) {
        const json& previous = states[a.previousState];
        AnimatorSystem::SampleClip(scene, e, ReadAsset(previous.value("clip", "")), a.previousTime, previous.value("loop", true));
    }
    PoseSnapshot source = original;
    for (auto& [entity, pose] : source) pose = scene.Reg().Get<TransformComponent>(entity);
    for (auto& [entity, pose] : original) scene.Reg().Get<TransformComponent>(entity) = pose;
    AnimatorSystem::SampleClip(scene, e, clip, a.time, state.value("loop", true));
    a.transitionTime += dt;
    float blend = std::clamp(a.transitionTime / a.transitionDuration, 0.f, 1.f);
    for (auto& [entity, pose] : source) BlendTransform(scene.Reg().Get<TransformComponent>(entity), pose, blend);
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
        view.speed = animator.speed;
        view.playing = true;
        std::swap(view.parameters, animator.parameters);
        static const json noStates = json::object();
        const json& states = layer.contains("states") ? layer["states"] : noStates;
        if (view.state.empty()) view.state = layer.value("defaultState", std::string());
        float duration = 1.0f;
        if (states.contains(view.state)) duration = ReadAsset(states[view.state].value("clip", "")).value("duration", 1.0f);
        AnimatorSystem::AdvanceController(view, layer, duration, &controller);
        std::swap(view.parameters, animator.parameters);
        if (weight > 0 && ScriptEngine::RuntimeScene() == &scene) CollectEvents(e, view, states, dt);

        if (weight > 0 && states.contains(view.state)) {
            PoseSnapshot before = CapturePose(scene, e);
            const bool additive = layer.value("blending", std::string("Override")) == "Additive";
            std::unordered_map<EntityID, TransformComponent> reference;
            if (additive) {
                const json& state = states[view.state];
                AnimatorSystem::SampleClip(scene, e, ReadAsset(state.value("clip", "")), 0.0f, false);
                for (auto& [entity, pose] : before) { reference[entity] = scene.Reg().Get<TransformComponent>(entity); scene.Reg().Get<TransformComponent>(entity) = pose; }
            }
            SampleStatePose(scene, e, view, states, dt);

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
            float duration = 1.0f;
            if (states.contains(animator.state)) duration = ReadAsset(states[animator.state].value("clip", "")).value("duration", 1.0f);
            AdvanceController(animator, controller, duration);
            // A state without a clip can still name a take embedded in the FBX
            bool hasClip = states.contains(animator.state) && !states[animator.state].value("clip", std::string()).empty();
            if (rig && !hasClip) ModelSkeleton::ApplyPose(scene, e, animator.time, animator.state);
            if (ScriptEngine::RuntimeScene() == &scene) CollectEvents(e, animator, states, dt);
            SampleStatePose(scene, e, animator, states, dt);
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
