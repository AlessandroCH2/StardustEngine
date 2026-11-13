#include "Core/Base64.h"
#include "Scene/SceneSerializer.h"
#include "Scene/ComponentState.h"
#include "Scene/PrefabSystem.h"
#include "Scene/ParticleSystem.h"
#include "UI/UIControls.h"
#include "Scripting/ScriptEngine.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Assets/AssetManager.h"
#include "UI/UIAssets.h"
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace Stardust {

using json = nlohmann::ordered_json;
static constexpr int kSceneVersion = 3;

static json Vec(const glm::vec2& v) { return { v.x, v.y }; }
static glm::vec2 Vec2(const json& j, glm::vec2 fallback = glm::vec2(0)) { return j.is_array() && j.size() == 2 ? glm::vec2(j[0].get<float>(), j[1].get<float>()) : fallback; }
static json Vec(const glm::vec3& v) { return { v.x, v.y, v.z }; }
static json Vec(const glm::vec4& v) { return { v.x, v.y, v.z, v.w }; }
static glm::vec3 Vec3(const json& j, glm::vec3 def = glm::vec3(0)) {
    return j.is_array() && j.size() == 3 ? glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()) : def;
}
static glm::vec4 Vec4(const json& j, glm::vec4 def = glm::vec4(0)) {
    return j.is_array() && j.size() == 4 ? glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()) : def;
}
// 64-bit UUIDs are stored as hex strings (JSON numbers lose precision in many tools)
static std::string UUIDStr(u64 v) { return std::format("{:016x}", v); }
static u64 ParseUUID(const json& j) { return j.is_string() ? std::stoull(j.get<std::string>(), nullptr, 16) : 0; }

static void WriteEntity(Scene& scene, EntityID e, json& out) {
    Registry& reg = scene.Reg();
    json j;
    j["uuid"] = UUIDStr(reg.Get<IDComponent>(e).uuid);
    auto& n = reg.Get<NameComponent>(e);
    j["name"] = n.name;
    j["layer"]=n.layer;
    j["active"] = n.active;
    if(!n.prefab.empty())j["prefabSource"]=n.prefab;
    if(auto* link=reg.TryGet<PrefabInstanceComponent>(e))j["prefabInstance"]={{"asset",link->asset},{"baseline",json::parse(link->baseline)},{"mapping",json::parse(link->mapping)},{"revert",link->revert}};
    if (n.tag != "Untagged") j["tag"] = n.tag;
    EntityID parent = scene.GetParent(e);
    if (parent) j["parent"] = UUIDStr(reg.Get<IDComponent>(parent).uuid);

    auto& t = reg.Get<TransformComponent>(e);
    j["transform"] = {
        { "position", Vec(t.position) },
        { "rotation", { t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z } },
        { "scale", Vec(t.scale) },
        { "euler", Vec(t.eulerHint) },
    };
    if (auto* c = reg.TryGet<MeshRendererComponent>(e)) {
        nlohmann::json mat;
        c->material.ToJson(mat);
        j["meshRenderer"] = { {"materials",c->materials}, { "mesh", c->mesh }, { "material", c->materialPath }, { "castShadows", c->castShadows },{"receiveShadows",c->receiveShadows},
                              { "embeddedMaterial", mat } };
    }
    if(auto* c=reg.TryGet<VideoPlayerComponent>(e))j["videoPlayer"]={{"clip",c->clip},{"renderMode",c->renderMode},{"targetTexture",c->targetTexture},{"playOnStart",c->playOnStart},{"loop",c->loop},{"speed",c->speed},{"audio",c->audio},{"volume",c->volume}};
    if(auto* c=reg.TryGet<SpriteBatchComponent>(e)){j["spriteBatch"]={{"textures",c->textures},{"quads",Base64::Encode(c->quads.data(),c->quads.size()*sizeof(SpriteQuad))},{"material",c->material},{"color",Vec(c->color)},{"sortingOrder",c->sortingOrder}};
        if(!c->animations.empty()){json anims=json::array();for(auto& a:c->animations){json frames=json::array();for(auto& f:a.frames)frames.push_back({f.x,f.y,f.z,f.w});anims.push_back({{"frames",frames},{"fps",a.fps}});}j["spriteBatch"]["animations"]=anims;}}
    if(auto* c=reg.TryGet<ParticleSystemComponent>(e)){nlohmann::json settings;ParticleSystems::ToJson(*c,settings);j["particleSystem"]=json::parse(settings.dump());}
    if(auto* c=reg.TryGet<SpriteRendererComponent>(e))j["spriteRenderer"]={{"sprite",c->sprite},{"material",c->material},{"color",Vec(c->color)},{"pixelsPerUnit",c->pixelsPerUnit},{"flipX",c->flipX},{"flipY",c->flipY},{"sortingOrder",c->sortingOrder}};
    if (auto* c = reg.TryGet<CameraComponent>(e))
        j["camera"] = { {"cullingMask",c->cullingMask},{"clearFlags",c->clearFlags},{"background",{c->background.r,c->background.g,c->background.b,c->background.a}},{"renderShadows",c->renderShadows},{"postProcessing",c->postProcessing},{"targetTexture",c->targetTexture},{"targetWidth",c->targetWidth},{"targetHeight",c->targetHeight}, {"orthographic",c->orthographic},{"orthographicSize",c->orthographicSize},{"far",c->farPlane}, { "fov", c->fovDegrees }, { "near", c->nearPlane }, { "primary", c->primary } };
    if (auto* c = reg.TryGet<DirectionalLightComponent>(e))
        j["directionalLight"] = { { "color", Vec(c->color) }, { "intensity", c->intensity },
                                  { "castShadows", c->castShadows }, { "shadowDistance", c->shadowDistance } };
    if (auto* c = reg.TryGet<EnvironmentComponent>(e))
        j["environment"] = { {"shadowStrength",c->shadowStrength},{"minimumAmbient",c->minimumAmbient}, {"skyMode",c->skyMode},{"skyMaterial",c->skyMaterial},{"skyFaces",c->skyFaces},{"skyColor",Vec(c->skyColor)},{"skyRotation",c->skyRotation},{"skyExposure",c->skyExposure},{ "exposure", c->exposure }, { "ambient", c->ambientIntensity }, {"ambientSky",Vec(c->ambientSky)},{"ambientHorizon",Vec(c->ambientHorizon)},{"ambientGround",Vec(c->ambientGround)},
                             { "cloudCoverage", c->cloudCoverage }, { "cloudDensity", c->cloudDensity },
                             { "fogDensity", c->fogDensity }, { "fogHeightFalloff", c->fogHeightFalloff },
                             { "windDirection", c->windDirectionDeg }, { "windStrength", c->windStrength },
                             { "bloom", c->bloomIntensity }, { "contrast", c->contrast }, { "saturation", c->saturation },
                             { "vignette", c->vignette }, { "fxaa", c->fxaa } };
    auto matJson = [](const PhysicsMaterialProps& m) { return json{ { "friction", m.friction }, { "restitution", m.restitution }, { "isTrigger", m.isTrigger } }; };
    if (auto* c = reg.TryGet<PointLightComponent>(e))
        j["pointLight"] = { { "color", Vec(c->color) }, { "intensity", c->intensity }, { "range", c->range } };
    if (auto* c = reg.TryGet<SpotLightComponent>(e))
        j["spotLight"] = { { "color", Vec(c->color) }, { "intensity", c->intensity }, { "range", c->range },
                           { "innerAngle", c->innerAngle }, { "outerAngle", c->outerAngle } };
    if (auto* c = reg.TryGet<AudioSourceComponent>(e))
        j["audioSource"] = { { "clip", c->clip }, { "volume", c->volume }, { "pitch", c->pitch }, { "loop", c->loop },
                             { "playOnStart", c->playOnStart }, { "spatial", c->spatial }, { "minDistance", c->minDistance }, { "maxDistance", c->maxDistance } };
    if (reg.Has<AudioListenerComponent>(e)) j["audioListener"] = json::object();
    if (auto* c = reg.TryGet<CanvasComponent>(e)) j["canvas"] = {{"resolution",Vec(c->referenceResolution)},{"scaleWithScreen",c->scaleWithScreen},{"sortingOrder",c->sortingOrder},{"renderMode",c->renderMode},{"pixelsPerUnit",c->pixelsPerUnit},{"planeDistance",c->planeDistance}};
    if (auto* c = reg.TryGet<RectTransformComponent>(e)) j["rectTransform"] = {{"anchorMin",Vec(c->anchorMin)},{"anchorMax",Vec(c->anchorMax)},{"pivot",Vec(c->pivot)},{"position",Vec(c->anchoredPosition)},{"size",Vec(c->sizeDelta)},{"rotation",c->rotation},{"scale",Vec(c->scale)}};
    if (auto* c = reg.TryGet<UIImageComponent>(e)) j["uiImage"] = {{"texture",c->texture},{"sprite",c->sprite},{"material",c->material},{"color",Vec(c->color)},{"type",c->type},{"preserveAspect",c->preserveAspect},{"raycastTarget",c->raycastTarget},{"fillAmount",c->fillAmount},{"fillMethod",c->fillMethod},{"pixelsPerUnitMultiplier",c->pixelsPerUnitMultiplier}};
    if (auto* c = reg.TryGet<UITextComponent>(e)) j["uiText"] = {{"text",c->text},{"color",Vec(c->color)},{"fontSize",c->fontSize},{"font",c->font},{"material",c->material},{"richText",c->richText},{"wordWrap",c->wordWrap},{"autoSize",c->autoSize},{"minFontSize",c->minFontSize},{"maxFontSize",c->maxFontSize},{"characterSpacing",c->characterSpacing},{"lineSpacing",c->lineSpacing},{"alignment",c->alignment},{"verticalAlignment",c->verticalAlignment},{"overflow",c->overflow},{"raycastTarget",c->raycastTarget},{"outlineColor",Vec(c->outlineColor)},{"outlineWidth",c->outlineWidth},{"shadowColor",Vec(c->shadowColor)},{"shadowOffset",Vec(c->shadowOffset)}};
    if (auto* c = reg.TryGet<UIButtonComponent>(e)) {j["uiButton"] = {{"transition",c->transition},{"fadeDuration",c->fadeDuration},{"normalState",c->normalState},{"highlightedState",c->highlightedState},{"pressedState",c->pressedState},{"disabledState",c->disabledState},{"interactable",c->interactable},{"highlightedColor",Vec(c->highlightedColor)},{"pressedColor",Vec(c->pressedColor)},{"disabledColor",Vec(c->disabledColor)},{"target",UUIDStr(c->target)},{"method",c->method}};for(auto& a:c->actions)j["uiButton"]["actions"].push_back({{"enabled",a.enabled},{"target",UUIDStr(a.target)},{"component",a.component},{"slot",a.scriptSlot},{"method",a.method},{"arguments",json::parse(a.argumentsJson,nullptr,false)},{"argumentTypes",a.argumentTypes}}); }
    if(auto* c=reg.TryGet<UIScrollViewComponent>(e)){nlohmann::json v;UIControls::ToJson(*c,v);j["uiScrollView"]=json::parse(v.dump());}
    if(auto* c=reg.TryGet<UIDropdownComponent>(e)){nlohmann::json v;UIControls::ToJson(*c,v);j["uiDropdown"]=json::parse(v.dump());}
    if(auto* c=reg.TryGet<UIInputFieldComponent>(e))j["uiInputField"]={{"text",c->text},{"placeholder",c->placeholder},{"font",c->font},{"fontSize",c->fontSize},{"color",Vec(c->color)},{"placeholderColor",Vec(c->placeholderColor)},{"interactable",c->interactable},{"readOnly",c->readOnly},{"multiline",c->multiline},{"password",c->password},{"characterLimit",c->characterLimit}};
    if(auto* c=reg.TryGet<CanvasGroupComponent>(e))j["canvasGroup"]={{"alpha",c->alpha},{"interactable",c->interactable},{"blocksRaycasts",c->blocksRaycasts}};
    if(auto* c=reg.TryGet<UILayoutGroupComponent>(e))j["uiLayout"]={{"mode",c->mode},{"columns",c->columns},{"spacing",Vec(c->spacing)},{"cellSize",Vec(c->cellSize)},{"padding",Vec(c->padding)},{"fitContent",c->fitContent},{"startCorner",c->startCorner},{"startAxis",c->startAxis},{"childAlignment",c->childAlignment}};
    if(reg.Has<RectMaskComponent>(e))j["rectMask"]=true;
    if(auto* c=reg.TryGet<UIMaskComponent>(e))j["uiMask"]={{"showGraphic",c->showGraphic}};
    if(auto* c=reg.TryGet<FbxRigComponent>(e))j["fbxRig"]={{"source",c->source}};if(auto* c=reg.TryGet<BoneComponent>(e))j["bone"]={{"root",UUIDStr(c->root)},{"node",c->nodeId}};
    if (auto* c = reg.TryGet<AnimatorComponent>(e)) j["animator"] = {{"controller",c->controller},{"avatar",c->avatar},{"state",c->state},{"speed",c->speed},{"playing",c->playing}};

    if (auto* c = reg.TryGet<RigidbodyComponent>(e))
        j["rigidbody"] = { { "type", (int)c->type }, { "mass", c->mass }, { "linearDamping", c->linearDamping },
                           { "angularDamping", c->angularDamping }, { "useGravity", c->useGravity }, { "freezeRotation", c->freezeRotation } };
    if (auto* c = reg.TryGet<Rigidbody2DComponent>(e))
        j["rigidbody2D"] = { { "type", (int)c->type }, { "mass", c->mass }, { "linearDamping", c->linearDamping }, { "angularDamping", c->angularDamping },
                             { "gravityScale", c->gravityScale }, { "freezeRotation", c->freezeRotation } };
    if (auto* c = reg.TryGet<BoxCollider2DComponent>(e)) j["boxCollider2D"] = { { "size", Vec(c->size) }, { "offset", Vec(c->offset) }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<CircleCollider2DComponent>(e)) j["circleCollider2D"] = { { "radius", c->radius }, { "offset", Vec(c->offset) }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<CapsuleCollider2DComponent>(e)) j["capsuleCollider2D"] = { { "size", Vec(c->size) }, { "offset", Vec(c->offset) }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<BoxColliderComponent>(e))
        j["boxCollider"] = { { "size", Vec(c->size) }, { "center", Vec(c->center) }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<SphereColliderComponent>(e))
        j["sphereCollider"] = { { "radius", c->radius }, { "center", Vec(c->center) }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<CapsuleColliderComponent>(e))
        j["capsuleCollider"] = { { "radius", c->radius }, { "height", c->height }, { "center", Vec(c->center) }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<MeshColliderComponent>(e))
        j["meshCollider"] = { { "convex", c->convex }, { "material", matJson(c->material) } };
    if (auto* c = reg.TryGet<WheelColliderComponent>(e))
        j["wheelCollider"] = { { "radius", c->radius }, { "width", c->width }, { "suspensionDistance", c->suspensionDistance }, { "springFrequency", c->springFrequency },
                               { "springDamping", c->springDamping }, { "maxSteerAngle", c->maxSteerAngle }, { "motor", c->motor }, { "handbrake", c->handbrake },
                               { "brakeTorque", c->brakeTorque }, { "handbrakeTorque", c->handbrakeTorque }, { "forwardFriction", c->forwardFriction }, { "sidewaysFriction", c->sidewaysFriction } };
    if (auto* c = reg.TryGet<VehicleControllerComponent>(e))
        j["vehicleController"] = { { "engineTorque", c->engineTorque }, { "minRPM", c->minRPM }, { "maxRPM", c->maxRPM }, { "shiftUpRPM", c->shiftUpRPM }, { "shiftDownRPM", c->shiftDownRPM },
                                   { "centerOfMassOffset", c->centerOfMassOffset }, { "maxPitchRoll", c->maxPitchRoll }, { "antiRollBars", c->antiRollBars }, { "limitedSlipRatio", c->limitedSlipRatio } };
    if (auto* c = reg.TryGet<ScriptComponent>(e)) {
        j["scripts"] = json::array();
        for (size_t i = 0; i < c->Count(); ++i) {
            auto& slot = c->Slot(i);
            auto fields = json::parse(slot.fieldsJson, nullptr, false);
            j["scripts"].push_back({{"class", slot.className}, {"enabled",slot.enabled}, {"fields", fields.is_object() ? fields : json::object()}});
        }
    }

    if(auto* c=reg.TryGet<ProceduralTerrainComponent>(e))j["proceduralTerrain"]={{"mountainHeight",c->mountainHeight},{"hillHeight",c->hillHeight},{"meadowRadius",c->meadowRadius},{"seed",c->seed},{"generateOnMissing",c->generateOnMissing},{"proceduralSurface",c->proceduralSurface},{"layers",c->layers}};
    if (auto* c = reg.TryGet<TerrainComponent>(e)) {
        const TerrainSettings& s = c->settings;
        j["terrain"] = { { "size", s.size }, { "resolution", s.resolution },
                         { "drawTrees", s.trees },
                         { "dataPath", c->dataPath }, { "collider", c->collider } };
        json layers = json::array();
        for (const TerrainLayer& l : s.layers)
            layers.push_back({ { "name", l.name }, {"asset",l.assetPath}, { "albedoMap", l.albedoMap }, { "tint", Vec(l.tint) }, { "tiling", l.tiling }, { "roughness", l.roughness } });
        j["terrain"]["layers"] = layers;j["terrain"]["neighbors"]={UUIDStr(c->neighbors[0]),UUIDStr(c->neighbors[1]),UUIDStr(c->neighbors[2]),UUIDStr(c->neighbors[3])};
    }
    if (auto* c = reg.TryGet<TerrainVegetationComponent>(e))
        j["terrainVegetation"] = { { "grass", c->grass }, { "grassDensity", c->grassDensity }, { "grassHeight", c->grassHeight }, { "grassDistance", c->grassDistance },
                                   { "flowerAmount", c->flowerAmount }, { "grassColor", Vec(c->grassColor) }, { "dryGrassColor", Vec(c->dryGrassColor) },
                                   { "trees", c->trees }, { "treeDensity", c->treeDensity }, { "maxTrees", c->maxTrees }, { "treeSeed", c->treeSeed } };
    if (auto* c = reg.TryGet<LODGroupComponent>(e)) {
        json levels = json::array();
        for (auto& level : c->levels) { json ids = json::array(); for (u64 id : level.renderers) ids.push_back(UUIDStr(id)); levels.push_back({ { "screenHeight", level.screenHeight }, { "renderers", ids } }); }
        j["lodGroup"] = { { "size", c->size }, { "center", Vec(c->localCenter) }, { "levels", levels } };
    }
    VisitToggleComponents([&]<typename T>(int,const char* key){if(reg.Has<T>(e)&&!reg.IsEnabled<T>(e))j["disabledComponents"].push_back(key);});
    out.push_back(std::move(j));

    for (EntityID c : scene.GetChildren(e)) WriteEntity(scene, c, out);
}

std::string SceneSerializer::ToJson(Scene& scene, bool compact) {
    json root;
    root["version"] = kSceneVersion;
    root["name"] = scene.Name();
    json entities = json::array();
    for (EntityID e : scene.GetRoots()) WriteEntity(scene, e, entities); // parents always precede children
    root["entities"] = std::move(entities);
    return compact ? root.dump() : root.dump(2);
}

// Creates the entities of a JSON array. freshIds: give new UUIDs (prefab instances / duplicates) and remap parents.
static std::vector<EntityID> ReadEntities(Scene& sceneRef, const json& entities, bool freshIds, EntityID rootParent) {
    Scene* scene = &sceneRef;
    Registry& reg = scene->Reg();
    std::unordered_map<u64, EntityID> byUUID;
    std::vector<EntityID> roots;
    for (const json& j : entities) {
        u64 fileUuid = ParseUUID(j.value("uuid", json()));
        EntityID e = scene->CreateEntity(j.value("name", "GameObject"), freshIds ? 0 : fileUuid);
        byUUID[fileUuid ? fileUuid : reg.Get<IDComponent>(e).uuid] = e;
        reg.Get<NameComponent>(e).active = j.value("active", true);
        reg.Get<NameComponent>(e).tag = j.value("tag", std::string("Untagged"));
        reg.Get<NameComponent>(e).layer=std::clamp(j.value("layer",0),0,31);
        reg.Get<NameComponent>(e).prefab=j.value("prefabSource", "");
        if(j.contains("prefabInstance")){auto& link=reg.Add<PrefabInstanceComponent>(e);auto& m=j["prefabInstance"];link.asset=m.value("asset", "");link.baseline=m.value("baseline",json::object()).dump();link.mapping=m.value("mapping",json::object()).dump();link.revert=m.value("revert",false);}

        if (j.contains("pointLight")) {
            const json& m = j["pointLight"];
            auto& c = reg.Add<PointLightComponent>(e);
            c.color = Vec3(m.value("color", json()), c.color);
            c.intensity = m.value("intensity", c.intensity);
            c.range = m.value("range", c.range);
        }
        if (j.contains("spotLight")) {
            const json& m = j["spotLight"];
            auto& c = reg.Add<SpotLightComponent>(e);
            c.color = Vec3(m.value("color", json()), c.color);
            c.intensity = m.value("intensity", c.intensity);
            c.range = m.value("range", c.range);
            c.innerAngle = m.value("innerAngle", c.innerAngle);
            c.outerAngle = m.value("outerAngle", c.outerAngle);
        }
        if (j.contains("audioSource")) {
            const json& m = j["audioSource"];
            auto& c = reg.Add<AudioSourceComponent>(e);
            c.clip = m.value("clip", c.clip);
            c.volume = m.value("volume", c.volume);
            c.pitch = m.value("pitch", c.pitch);
            c.loop = m.value("loop", c.loop);
            c.playOnStart = m.value("playOnStart", c.playOnStart);
            c.spatial = m.value("spatial", c.spatial);
            c.minDistance = m.value("minDistance", c.minDistance);
            c.maxDistance = m.value("maxDistance", c.maxDistance);
        }
        if (j.contains("audioListener")) reg.Add<AudioListenerComponent>(e);
        if (j.contains("canvas")) {
            auto& c = reg.Add<CanvasComponent>(e); auto& m = j["canvas"];
            c.referenceResolution = Vec2(m.value("resolution",json()),c.referenceResolution);
            c.scaleWithScreen = m.value("scaleWithScreen",true); c.sortingOrder = m.value("sortingOrder",0); c.renderMode=m.value("renderMode",0);c.pixelsPerUnit=m.value("pixelsPerUnit",100.f);c.planeDistance=m.value("planeDistance",10.f);
        }
        if (j.contains("rectTransform")) {
            auto& c = reg.Add<RectTransformComponent>(e); auto& m = j["rectTransform"];
            c.anchorMin = Vec2(m.value("anchorMin",json()),c.anchorMin); c.anchorMax = Vec2(m.value("anchorMax",json()),c.anchorMax);
            c.pivot = Vec2(m.value("pivot",json()),c.pivot); c.anchoredPosition = Vec2(m.value("position",json())); c.sizeDelta = Vec2(m.value("size",json()),c.sizeDelta);c.rotation=m.value("rotation",0.f);c.scale=Vec2(m.value("scale",json()),glm::vec2(1));
        }
        if (j.contains("uiImage")) { auto& c = reg.Add<UIImageComponent>(e); auto& m = j["uiImage"]; c.texture = m.value("texture", ""); c.color = Vec4(m.value("color",json()),c.color);c.sprite=m.value("sprite", "");c.material=m.value("material", "");c.type=m.value("type",0);c.preserveAspect=m.value("preserveAspect",false);c.raycastTarget=m.value("raycastTarget",true);c.fillAmount=m.value("fillAmount",1.f);c.fillMethod=m.value("fillMethod",0);c.pixelsPerUnitMultiplier=std::max(.01f,m.value("pixelsPerUnitMultiplier",c.sprite.empty()?1.f:100.f/std::max(1.f,SpriteAsset::Load(c.sprite).pixelsPerUnit))); }
        if (j.contains("uiText")) { auto& c = reg.Add<UITextComponent>(e); auto& m = j["uiText"]; c.text = m.value("text", "Text"); c.color = Vec4(m.value("color",json()),c.color); c.fontSize = m.value("fontSize",24.0f);c.font=m.value("font", "");c.material=m.value("material", "");c.richText=m.value("richText",true);c.wordWrap=m.value("wordWrap",true);c.autoSize=m.value("autoSize",false);c.minFontSize=m.value("minFontSize",12.f);c.maxFontSize=m.value("maxFontSize",48.f);c.characterSpacing=m.value("characterSpacing",0.f);c.lineSpacing=m.value("lineSpacing",1.f);c.alignment=m.value("alignment",1);c.verticalAlignment=m.value("verticalAlignment",1);c.overflow=m.value("overflow",0);c.raycastTarget=m.value("raycastTarget",true);c.outlineColor=Vec4(m.value("outlineColor",json()),c.outlineColor);c.outlineWidth=m.value("outlineWidth",0.f);c.shadowColor=Vec4(m.value("shadowColor",json()),c.shadowColor);c.shadowOffset=Vec2(m.value("shadowOffset",json()),c.shadowOffset); }
        if(j.contains("uiScrollView")&&j["uiScrollView"].is_object())UIControls::FromJson(nlohmann::json::parse(j["uiScrollView"].dump()),reg.Add<UIScrollViewComponent>(e));
        if(j.contains("uiDropdown")&&j["uiDropdown"].is_object())UIControls::FromJson(nlohmann::json::parse(j["uiDropdown"].dump()),reg.Add<UIDropdownComponent>(e));
        if(j.contains("uiInputField")){auto& c=reg.Add<UIInputFieldComponent>(e);auto& m=j["uiInputField"];c.text=m.value("text","");c.placeholder=m.value("placeholder","Enter text...");c.font=m.value("font","");c.fontSize=m.value("fontSize",24.f);c.color=Vec4(m.value("color",json()),c.color);c.placeholderColor=Vec4(m.value("placeholderColor",json()),c.placeholderColor);c.interactable=m.value("interactable",true);c.readOnly=m.value("readOnly",false);c.multiline=m.value("multiline",false);c.password=m.value("password",false);c.characterLimit=m.value("characterLimit",0);}
        if (j.contains("uiButton")) { auto& c = reg.Add<UIButtonComponent>(e); auto& m = j["uiButton"]; c.transition=m.value("transition",1);c.fadeDuration=m.value("fadeDuration",.1f);c.normalState=m.value("normalState","Normal");c.highlightedState=m.value("highlightedState","Highlighted");c.pressedState=m.value("pressedState","Pressed");c.disabledState=m.value("disabledState","Disabled");c.interactable = m.value("interactable",true); c.highlightedColor = Vec4(m.value("highlightedColor",json()),c.highlightedColor);c.pressedColor=Vec4(m.value("pressedColor",json()),c.pressedColor);c.disabledColor=Vec4(m.value("disabledColor",json()),c.disabledColor);c.target=ParseUUID(m.value("target",json()));c.method=m.value("method", "OnClick");for(auto& a:m.value("actions",json::array())){UIButtonAction action;action.enabled=a.value("enabled",true);action.target=ParseUUID(a.value("target",json()));action.component=a.value("component","Script");action.scriptSlot=a.value("slot",0);action.method=a.value("method","");action.argumentsJson=a.value("arguments",json::array()).dump();action.argumentTypes=a.value("argumentTypes",std::vector<std::string>{});c.actions.push_back(std::move(action));} }
        if(j.contains("canvasGroup")){auto& c=reg.Add<CanvasGroupComponent>(e);auto& m=j["canvasGroup"];c.alpha=m.value("alpha",1.f);c.interactable=m.value("interactable",true);c.blocksRaycasts=m.value("blocksRaycasts",true);}
        if(j.contains("uiLayout")){auto& c=reg.Add<UILayoutGroupComponent>(e);auto& m=j["uiLayout"];c.mode=m.value("mode",0);c.columns=m.value("columns",2);c.spacing=Vec2(m.value("spacing",json()),c.spacing);c.cellSize=Vec2(m.value("cellSize",json()),c.cellSize);c.padding=Vec4(m.value("padding",json()),c.padding);c.fitContent=m.value("fitContent",false);c.startCorner=m.value("startCorner",0);c.startAxis=m.value("startAxis",0);c.childAlignment=m.value("childAlignment",0);}
        if(j.value("rectMask",false))reg.Add<RectMaskComponent>(e);
        if(j.contains("uiMask"))reg.Add<UIMaskComponent>(e).showGraphic=j["uiMask"].value("showGraphic",true);
        if (j.contains("animator")) { auto& c = reg.Add<AnimatorComponent>(e); auto& m = j["animator"]; c.controller = m.value("controller", ""); c.avatar = m.value("avatar", ""); c.state = m.value("state", ""); c.speed = m.value("speed",1.0f); c.playing = m.value("playing",true); }


        if (j.contains("transform")) {
            const json& t = j["transform"];
            auto& tc = reg.Get<TransformComponent>(e);
            tc.position = Vec3(t.value("position", json()));
            glm::vec4 q = Vec4(t.value("rotation", json()), glm::vec4(1, 0, 0, 0));
            tc.rotation = glm::normalize(glm::quat(q.x, q.y, q.z, q.w));
            tc.scale = Vec3(t.value("scale", json()), glm::vec3(1));
            tc.eulerHint = t.contains("euler") ? Vec3(t["euler"]) : glm::degrees(glm::eulerAngles(tc.rotation));
        }
        if(j.contains("fbxRig"))reg.Add<FbxRigComponent>(e).source=j["fbxRig"].value("source","");if(j.contains("bone")){auto& c=reg.Add<BoneComponent>(e);c.root=ParseUUID(j["bone"].value("root",json()));c.nodeId=j["bone"].value("node",0u);}
        if (j.contains("meshRenderer")) {
            const json& m = j["meshRenderer"];
            auto& c = reg.Add<MeshRendererComponent>(e);
            if (m.contains("mesh")) c.mesh = m["mesh"].get<std::string>();
            else c.mesh = "builtin:" + m.value("primitive", std::string("Cube")); // scenes from before materials
            c.materials=m.value("materials",std::vector<std::string>{});
            c.materialPath = m.value("material", std::string());
            c.castShadows = m.value("castShadows", true);c.receiveShadows=m.value("receiveShadows",true);
            if (m.contains("embeddedMaterial")) c.material.FromJson(nlohmann::json(m["embeddedMaterial"]));
            else {
                c.material.color = Vec4(m.value("albedo", json()), c.material.color);
                c.material.roughness = m.value("roughness", c.material.roughness);
                c.material.metallic = m.value("metallic", c.material.metallic);
                c.material.emissive = Vec3(m.value("emissive", json()));
            }
        }
        if(j.contains("videoPlayer")){auto& c=reg.Add<VideoPlayerComponent>(e);auto& m=j["videoPlayer"];c.clip=m.value("clip","");c.targetTexture=m.value("targetTexture","");c.playing=c.playOnStart=m.value("playOnStart",true);c.loop=m.value("loop",true);c.speed=m.value("speed",1.f);c.renderMode=std::clamp(m.value("renderMode",0),0,1);c.audio=m.value("audio",true);c.volume=m.value("volume",1.f);}
        if(j.contains("spriteBatch")&&j["spriteBatch"].is_object()){auto& c=reg.Add<SpriteBatchComponent>(e);auto& m=j["spriteBatch"];if(m.contains("textures")&&m["textures"].is_array())for(auto& t:m["textures"])if(t.is_string())c.textures.push_back(t.get<std::string>());auto bytes=Base64::Decode(m.value("quads",std::string()));c.quads.resize(bytes.size()/sizeof(SpriteQuad));if(!c.quads.empty())std::memcpy(c.quads.data(),bytes.data(),c.quads.size()*sizeof(SpriteQuad));c.material=m.value("material","");c.color=Vec4(m.value("color",json()),glm::vec4(1));c.sortingOrder=m.value("sortingOrder",0);for(auto& a:m.value("animations",json::array())){SpriteAnimation anim;anim.fps=a.value("fps",8.f);for(auto& f:a.value("frames",json::array()))if(f.is_array()&&f.size()==4)anim.frames.push_back({f[0].get<float>(),f[1].get<float>(),f[2].get<float>(),f[3].get<float>()});c.animations.push_back(anim);}c.Touch();}
        if(j.contains("particleSystem")&&j["particleSystem"].is_object())ParticleSystems::FromJson(nlohmann::json::parse(j["particleSystem"].dump()),reg.Add<ParticleSystemComponent>(e));
        if(j.contains("spriteRenderer")){auto& c=reg.Add<SpriteRendererComponent>(e);auto& m=j["spriteRenderer"];c.sprite=m.value("sprite","");c.material=m.value("material","");c.color=Vec4(m.value("color",json()),glm::vec4(1));c.pixelsPerUnit=m.value("pixelsPerUnit",0.f);c.flipX=m.value("flipX",false);c.flipY=m.value("flipY",false);c.sortingOrder=m.value("sortingOrder",0);}
        if (j.contains("camera")) {
            const json& m = j["camera"];
            auto& c = reg.Add<CameraComponent>(e);
            c.cullingMask=m.value("cullingMask",~0u);c.clearFlags=std::clamp(m.value("clearFlags",0),0,1);c.renderShadows=m.value("renderShadows",true);c.postProcessing=m.value("postProcessing",true);
            if(m.contains("background")&&m["background"].is_array()&&m["background"].size()==4)for(int k=0;k<4;++k)c.background[k]=m["background"][k].get<float>();
            c.targetTexture=m.value("targetTexture","");c.targetWidth=std::clamp(m.value("targetWidth",512),16,8192);c.targetHeight=std::clamp(m.value("targetHeight",512),16,8192);c.orthographic=m.value("orthographic",false);c.orthographicSize=m.value("orthographicSize",5.f);c.farPlane=m.value("far",1000.f);c.fovDegrees = m.value("fov", c.fovDegrees);
            c.nearPlane = m.value("near", c.nearPlane);
            c.primary = m.value("primary", c.primary);
        }
        if (j.contains("directionalLight")) {
            const json& m = j["directionalLight"];
            auto& c = reg.Add<DirectionalLightComponent>(e);
            c.color = Vec3(m.value("color", json()), c.color);
            c.intensity = m.value("intensity", c.intensity);
            c.castShadows = m.value("castShadows", c.castShadows);
            c.shadowDistance = m.value("shadowDistance", c.shadowDistance);
        }
        if (j.contains("environment")) {
            const json& m = j["environment"];
            auto& c = reg.Add<EnvironmentComponent>(e);
            c.exposure = m.value("exposure", c.exposure);
            c.ambientIntensity = m.value("ambient", c.ambientIntensity);c.ambientSky=Vec3(m.value("ambientSky",json()),c.ambientSky);c.ambientHorizon=Vec3(m.value("ambientHorizon",json()),c.ambientHorizon);c.ambientGround=Vec3(m.value("ambientGround",json()),c.ambientGround);
            c.shadowStrength=m.value("shadowStrength",.85f);c.minimumAmbient=m.value("minimumAmbient",.10f);c.skyMode=m.value("skyMode",0);c.skyMaterial=m.value("skyMaterial","");if(m.contains("skyFaces")&&m["skyFaces"].is_array()&&m["skyFaces"].size()==6)c.skyFaces=m["skyFaces"].get<std::array<std::string,6>>();c.skyColor=Vec4(m.value("skyColor",json()),c.skyColor);c.skyRotation=m.value("skyRotation",0.f);c.skyExposure=m.value("skyExposure",0.f);
            c.cloudCoverage = m.value("cloudCoverage", c.cloudCoverage);
            c.cloudDensity = m.value("cloudDensity", c.cloudDensity);
            c.fogDensity = m.value("fogDensity", c.fogDensity);
            c.fogHeightFalloff = m.value("fogHeightFalloff", c.fogHeightFalloff);
            c.windDirectionDeg = m.value("windDirection", c.windDirectionDeg);
            c.windStrength = m.value("windStrength", c.windStrength);
            c.bloomIntensity = m.value("bloom", c.bloomIntensity);
            c.contrast = m.value("contrast", c.contrast);
            c.saturation = m.value("saturation", c.saturation);
            c.vignette = m.value("vignette", c.vignette);
            c.fxaa = m.value("fxaa", c.fxaa);
        }
        auto readMat = [&](const json& m, PhysicsMaterialProps& out) {
            if (!m.contains("material")) return;
            out.friction = m["material"].value("friction", out.friction);
            out.restitution = m["material"].value("restitution", out.restitution);
            out.isTrigger = m["material"].value("isTrigger", out.isTrigger);
        };
        if (j.contains("rigidbody")) {
            const json& m = j["rigidbody"];
            auto& c = reg.Add<RigidbodyComponent>(e);
            c.type = (BodyType)m.value("type", (int)c.type);
            c.mass = m.value("mass", c.mass);
            c.linearDamping = m.value("linearDamping", c.linearDamping);
            c.angularDamping = m.value("angularDamping", c.angularDamping);
            c.useGravity = m.value("useGravity", c.useGravity);
            c.freezeRotation = m.value("freezeRotation", c.freezeRotation);
        }
        auto vec2 = [](const json& v, glm::vec2 fallback) { return v.is_array() && v.size() == 2 ? glm::vec2(v[0].get<float>(), v[1].get<float>()) : fallback; };
        if (j.contains("rigidbody2D")) {
            const json& m = j["rigidbody2D"];
            auto& c = reg.Add<Rigidbody2DComponent>(e);
            c.type = (BodyType)m.value("type", (int)c.type); c.mass = m.value("mass", c.mass);
            c.linearDamping = m.value("linearDamping", c.linearDamping); c.angularDamping = m.value("angularDamping", c.angularDamping);
            c.gravityScale = m.value("gravityScale", c.gravityScale); c.freezeRotation = m.value("freezeRotation", c.freezeRotation);
        }
        if (j.contains("boxCollider2D")) { const json& m = j["boxCollider2D"]; auto& c = reg.Add<BoxCollider2DComponent>(e); c.size = vec2(m.value("size", json()), c.size); c.offset = vec2(m.value("offset", json()), c.offset); readMat(m, c.material); }
        if (j.contains("circleCollider2D")) { const json& m = j["circleCollider2D"]; auto& c = reg.Add<CircleCollider2DComponent>(e); c.radius = m.value("radius", c.radius); c.offset = vec2(m.value("offset", json()), c.offset); readMat(m, c.material); }
        if (j.contains("capsuleCollider2D")) { const json& m = j["capsuleCollider2D"]; auto& c = reg.Add<CapsuleCollider2DComponent>(e); c.size = vec2(m.value("size", json()), c.size); c.offset = vec2(m.value("offset", json()), c.offset); readMat(m, c.material); }
        if (j.contains("boxCollider")) {
            const json& m = j["boxCollider"];
            auto& c = reg.Add<BoxColliderComponent>(e);
            c.size = Vec3(m.value("size", json()), c.size);
            c.center = Vec3(m.value("center", json()));
            readMat(m, c.material);
        }
        if (j.contains("sphereCollider")) {
            const json& m = j["sphereCollider"];
            auto& c = reg.Add<SphereColliderComponent>(e);
            c.radius = m.value("radius", c.radius);
            c.center = Vec3(m.value("center", json()));
            readMat(m, c.material);
        }
        if (j.contains("capsuleCollider")) {
            const json& m = j["capsuleCollider"];
            auto& c = reg.Add<CapsuleColliderComponent>(e);
            c.radius = m.value("radius", c.radius);
            c.height = m.value("height", c.height);
            c.center = Vec3(m.value("center", json()));
            readMat(m, c.material);
        }
        if (j.contains("meshCollider")) {
            const json& m = j["meshCollider"];
            auto& c = reg.Add<MeshColliderComponent>(e);
            c.convex = m.value("convex", c.convex);
            readMat(m, c.material);
        }
        if (j.contains("wheelCollider")) {
            const json& m = j["wheelCollider"];
            auto& c = reg.Add<WheelColliderComponent>(e);
            c.radius = m.value("radius", c.radius); c.width = m.value("width", c.width); c.suspensionDistance = m.value("suspensionDistance", c.suspensionDistance);
            c.springFrequency = m.value("springFrequency", c.springFrequency); c.springDamping = m.value("springDamping", c.springDamping);
            c.maxSteerAngle = m.value("maxSteerAngle", c.maxSteerAngle); c.motor = m.value("motor", c.motor); c.handbrake = m.value("handbrake", c.handbrake);
            c.brakeTorque = m.value("brakeTorque", c.brakeTorque); c.handbrakeTorque = m.value("handbrakeTorque", c.handbrakeTorque);
            c.forwardFriction = m.value("forwardFriction", c.forwardFriction); c.sidewaysFriction = m.value("sidewaysFriction", c.sidewaysFriction);
        }
        if (j.contains("vehicleController")) {
            const json& m = j["vehicleController"];
            auto& c = reg.Add<VehicleControllerComponent>(e);
            c.engineTorque = m.value("engineTorque", c.engineTorque); c.minRPM = m.value("minRPM", c.minRPM); c.maxRPM = m.value("maxRPM", c.maxRPM);
            c.shiftUpRPM = m.value("shiftUpRPM", c.shiftUpRPM); c.shiftDownRPM = m.value("shiftDownRPM", c.shiftDownRPM);
            c.centerOfMassOffset = m.value("centerOfMassOffset", c.centerOfMassOffset); c.maxPitchRoll = m.value("maxPitchRoll", c.maxPitchRoll);
            c.antiRollBars = m.value("antiRollBars", c.antiRollBars); c.limitedSlipRatio = m.value("limitedSlipRatio", c.limitedSlipRatio);
        }
        if (j.contains("scripts") || j.contains("script")) {
            auto slots = j.contains("scripts") ? j["scripts"] : json::array({j["script"]});
            if (!slots.empty()) {
                auto& c = reg.Add<ScriptComponent>(e);
                for (size_t i = 0; i < slots.size(); ++i) {
                    if (i) c.additional.push_back({});
                    c.Slot(i).className = slots[i].value("class", std::string());
                    c.Slot(i).enabled = slots[i].value("enabled",true);
                    c.Slot(i).fieldsJson = slots[i].contains("fields") ? slots[i]["fields"].dump() : "{}";
                }
            }
        }
        if (j.contains("terrain")) {
            const json& m = j["terrain"];
            auto& tc = reg.Add<TerrainComponent>(e);
            TerrainSettings& s = tc.settings;
            tc.dataPath = m.value("dataPath", std::string());
            tc.collider = m.value("collider", true);if(m.contains("neighbors")&&m["neighbors"].is_array()&&m["neighbors"].size()==4)for(int n=0;n<4;++n)tc.neighbors[n]=ParseUUID(m["neighbors"][n]);
            if(m.contains("layers")&&m["layers"].is_array())s.layers.resize(std::max<size_t>(1,std::min<size_t>(kTerrainLayers,m["layers"].size())));
            if (m.contains("layers") && m["layers"].is_array())
                for (size_t li = 0; li < std::min<size_t>(kTerrainLayers, m["layers"].size()); ++li) {
                    const json& lj = m["layers"][li];
                    TerrainLayer& l = s.layers[li];
                    l.name = lj.value("name", l.name);
                    l.albedoMap = lj.value("albedoMap", l.albedoMap);
                    l.assetPath=lj.value("asset",std::string());
                    l.tint = Vec3(lj.value("tint", json()), l.tint);
                    l.tiling = lj.value("tiling", l.tiling);
                    l.roughness = lj.value("roughness", l.roughness);
                }
            s.size = m.value("size", s.size);
            s.mountainHeight = m.value("mountainHeight", s.mountainHeight);
            s.hillHeight = m.value("hillHeight", s.hillHeight);
            s.meadowRadius = m.value("meadowRadius", s.meadowRadius);
            s.seed = m.value("seed", s.seed);
            s.resolution = m.value("resolution", s.resolution);
            s.trees = m.value("drawTrees", true);
            // Scenes saved before the Terrain Vegetation component kept these values on the terrain
            const json& v = j.contains("terrainVegetation") ? j["terrainVegetation"] : m;
            const bool legacy = !j.contains("terrainVegetation") && m.contains("grass");
            if (j.contains("terrainVegetation") || (legacy && (m.value("grass", false) || m.value("trees", false)))) {
                auto& c = reg.Add<TerrainVegetationComponent>(e);
                c.grass = v.value("grass", c.grass);
                c.grassDensity = v.value("grassDensity", c.grassDensity);
                c.grassHeight = v.value("grassHeight", c.grassHeight);
                c.grassDistance = v.value("grassDistance", c.grassDistance);
                c.flowerAmount = v.value("flowerAmount", c.flowerAmount);
                c.grassColor = Vec3(v.value("grassColor", json()), c.grassColor);
                c.dryGrassColor = Vec3(v.value("dryGrassColor", json()), c.dryGrassColor);
                c.trees = v.value("trees", c.trees);
                c.treeDensity = v.value("treeDensity", c.treeDensity);
                c.maxTrees = v.value("maxTrees", c.maxTrees);
                c.treeSeed = v.value("treeSeed", c.treeSeed);
            }
        }
        if(j.contains("proceduralTerrain")||(j.contains("terrain")&&j["terrain"].contains("mountainHeight"))){
            bool legacy=!j.contains("proceduralTerrain");auto& m=legacy?j["terrain"]:j["proceduralTerrain"];auto& c=reg.Add<ProceduralTerrainComponent>(e);
            c.mountainHeight=m.value("mountainHeight",220.f);c.hillHeight=m.value("hillHeight",14.f);c.meadowRadius=m.value("meadowRadius",260.f);c.seed=m.value("seed",7u);c.generateOnMissing=m.value("generateOnMissing",true);c.proceduralSurface=m.value("proceduralSurface",legacy);
            if(m.contains("layers")&&m["layers"].is_array()&&m["layers"].size()==4&&m["layers"][0].is_number())c.layers=m["layers"].get<std::array<u32,4>>();
        }
        if (j.contains("lodGroup")) {
            auto& c = reg.Add<LODGroupComponent>(e); const json& m = j["lodGroup"];
            c.size = m.value("size", 0.f);
            if (m.contains("center") && m["center"].is_array() && m["center"].size() == 3) for (int k = 0; k < 3; ++k) c.localCenter[k] = m["center"][k].get<float>();
            c.levels.clear();
            for (auto& level : m.value("levels", json::array())) {
                LODLevel l; l.screenHeight = std::clamp(level.value("screenHeight", 0.5f), 0.f, 1.f);
                for (auto& id : level.value("renderers", json::array())) if (id.is_string()) try { l.renderers.push_back(std::stoull(id.get<std::string>(), nullptr, 16)); } catch (...) {}
                c.levels.push_back(std::move(l));
            }
        }
        if(j.contains("disabledComponents")&&j["disabledComponents"].is_array())VisitToggleComponents([&]<typename T>(int,const char* key){for(auto& disabled:j["disabledComponents"])if(disabled==key)reg.SetEnabled<T>(e,false);});
        bool linked = false;
        if (j.contains("parent")) {
            auto it = byUUID.find(ParseUUID(j["parent"]));
            if (it != byUUID.end()) { scene->SetParent(e, it->second, false); linked = true; }
            else if (!freshIds) SD_WARN("Entity '{}' references a missing parent", j.value("name", "?"));
        }
        if (!linked) {
            roots.push_back(e);
            if (rootParent) scene->SetParent(e, rootParent, false);
        }
    }
    if (freshIds) {
        std::function<void(json&)> remap = [&](json& value) {
            if (value.is_object() || value.is_array()) { for (auto& child : value) remap(child); }
            else if (value.is_string()) {
                auto hex = value.get<std::string>();
                if (hex.size() != 16 || !std::all_of(hex.begin(),hex.end(),[](unsigned char c){return std::isxdigit(c)!=0;})) return;
                auto it = byUUID.find(std::stoull(hex,nullptr,16));
                if (it != byUUID.end()) value = UUIDStr(reg.Get<IDComponent>(it->second).uuid);
            }
        };
        for(auto& [uuid,entity]:byUUID)if(auto* terrain=reg.TryGet<TerrainComponent>(entity))for(auto& neighbor:terrain->neighbors){auto found=byUUID.find(neighbor);if(found!=byUUID.end())neighbor=reg.Get<IDComponent>(found->second).uuid;}
        for(auto& [uuid,entity]:byUUID)if(auto* lod=reg.TryGet<LODGroupComponent>(entity))for(auto& level:lod->levels)for(auto& id:level.renderers){auto found=byUUID.find(id);if(found!=byUUID.end())id=reg.Get<IDComponent>(found->second).uuid;}
        for(auto& [uuid,entity]:byUUID)if(auto* bone=reg.TryGet<BoneComponent>(entity)){auto found=byUUID.find(bone->root);if(found!=byUUID.end())bone->root=reg.Get<IDComponent>(found->second).uuid;}
        for(auto& [uuid,entity]:byUUID)if(auto* button=reg.TryGet<UIButtonComponent>(entity)){auto found=byUUID.find(button->target);if(found!=byUUID.end())button->target=reg.Get<IDComponent>(found->second).uuid;for(auto& action:button->actions){auto target=byUUID.find(action.target);if(target!=byUUID.end())action.target=reg.Get<IDComponent>(target->second).uuid;auto args=json::parse(action.argumentsJson,nullptr,false);if(args.is_array())for(size_t i=0;i<args.size()&&i<action.argumentTypes.size();++i){auto& type=action.argumentTypes[i];auto& arg=args[i];if(arg.is_string()&&(type=="Entity"||type.starts_with("Component:")||type.starts_with("Script:"))){auto ref=byUUID.find(ParseUUID(arg));if(ref!=byUUID.end())arg=UUIDStr(reg.Get<IDComponent>(ref->second).uuid);}}action.argumentsJson=args.is_array()?args.dump():"[]";}}
        for(auto& [uuid,entity]:byUUID)if(auto* link=reg.TryGet<PrefabInstanceComponent>(entity)){auto mapping=json::parse(link->mapping);remap(mapping);link->mapping=mapping.dump();}
        for (auto& [uuid,entity] : byUUID) if (auto* scripts = reg.TryGet<ScriptComponent>(entity))
            for (size_t i=0;i<scripts->Count();++i) {
                auto fields=json::parse(scripts->Slot(i).fieldsJson,nullptr,false);
                if (fields.is_object()) {json mapping=json::object();for(auto& [oldId,newEntity]:byUUID)mapping[UUIDStr(oldId)]=UUIDStr(reg.Get<IDComponent>(newEntity).uuid);scripts->Slot(i).fieldsJson=ScriptEngine::RemapFieldReferences(scripts->Slot(i).className,fields.dump(),mapping.dump()); }
            }
    }
    return roots;
}

std::vector<EntityID> SceneSerializer::AppendScene(Scene& scene,const std::string& text){auto data=json::parse(text);return ReadEntities(scene,data.at("entities"),true,0);}

Ref<Scene> SceneSerializer::FromJson(const std::string& text) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.contains("entities")) { SD_ERROR("Invalid scene file"); return nullptr; }
    auto scene = MakeRef<Scene>();
    scene->SetName(root.value("name", "Untitled"));
    Registry& reg = scene->Reg();
    std::unordered_map<u64, EntityID> byUUID;

    root=PrefabSystem::Resolve(nlohmann::json(root));
    ReadEntities(*scene, root["entities"], false, kNullEntity);
    return scene;
}

bool SceneSerializer::Save(Scene& scene, const std::filesystem::path& path) {
    bool ok = FileSystem::WriteText(path, ToJson(scene));
    if (ok) SD_INFO("Scene saved: {}", FileSystem::Utf8(path));
    else SD_ERROR("Could not write scene: {}", FileSystem::Utf8(path));
    return ok;
}

Ref<Scene> SceneSerializer::Load(const std::filesystem::path& path) {
    std::string text = FileSystem::ReadText(path);
    if (text.empty()) { SD_ERROR("Could not read scene: {}", FileSystem::Utf8(path)); return nullptr; }
    auto scene = FromJson(text);
    if (scene) SD_INFO("Scene loaded: {}", FileSystem::Utf8(path));
    return scene;
}

} // namespace Stardust

namespace Stardust {

std::string SceneSerializer::EntityToJson(Scene& scene, EntityID root) {
    json r;
    r["prefab"] = 1;
    json entities = json::array();
    WriteEntity(scene, root, entities);
    entities[0].erase("parent");
    r["entities"] = std::move(entities);
    return r.dump(2);
}

EntityID SceneSerializer::InstantiateJson(Scene& scene, const std::string& text, EntityID parent) {
    json r = json::parse(text, nullptr, false);
    if (r.is_discarded() || !r.contains("entities")) return kNullEntity;
    auto roots = ReadEntities(scene, r["entities"], true, parent);
    return roots.empty() ? kNullEntity : roots[0];
}

std::string SceneSerializer::SelectionToJson(Scene& scene,const std::vector<EntityID>& selection){json entities=json::array();for(auto e:selection){if(!scene.Reg().Valid(e))continue;bool nested=false;for(auto other:selection)if(other!=e&&scene.Reg().Valid(other)&&scene.IsDescendantOf(e,other)){nested=true;break;}if(nested)continue;size_t start=entities.size();WriteEntity(scene,e,entities);entities[start].erase("parent");auto parent=scene.GetParent(e);entities[start]["clipboardParent"]=parent?UUIDStr(scene.Reg().Get<IDComponent>(parent).uuid):UUIDStr(0);auto world=scene.GetWorldMatrix(e);json matrix=json::array();for(int c=0;c<4;++c)for(int r=0;r<4;++r)matrix.push_back(world[c][r]);entities[start]["clipboardWorld"]=matrix;}return json{{"entities",entities}}.dump();}
std::vector<EntityID> SceneSerializer::InstantiateSelection(Scene& scene,const std::string& text,EntityID parent){auto data=json::parse(text,nullptr,false);if(!data.is_object()||!data.contains("entities"))return {};auto roots=ReadEntities(scene,data["entities"],true,parent);size_t index=0;for(auto& entity:data["entities"])if(entity.contains("clipboardWorld")&&index<roots.size()){auto e=roots[index++];auto sourceParent=scene.FindByUUID(ParseUUID(entity.value("clipboardParent",json())));if(!parent&&sourceParent)scene.SetParent(e,sourceParent,false);else if(!parent){auto& matrix=entity["clipboardWorld"];if(matrix.is_array()&&matrix.size()==16){glm::mat4 world(1);for(int c=0;c<4;++c)for(int r=0;r<4;++r)world[c][r]=matrix[c*4+r];scene.SetWorldMatrix(e,world);}}}return roots;}

bool SceneSerializer::SavePrefab(Scene& scene, EntityID root, const std::filesystem::path& path) {
    auto data=json::parse(EntityToJson(scene,root));
    data["entities"][0].erase("prefabInstance");data["entities"][0].erase("prefabSource");
    if(auto* link=scene.Reg().TryGet<PrefabInstanceComponent>(root);link&&link->asset==AssetManager::ToRelative(path)){
        auto mapping=json::parse(link->mapping);json inverse=json::object();for(auto& [source,target]:mapping.items())inverse[target.get<std::string>()]=source;
        std::function<void(json&)> remap=[&](json& value){if(value.is_object()){for(auto& [key,v]:value.items())if(key!="arguments"&&!(key=="fields"&&value.contains("class")))remap(v);if(value.contains("class")&&value.contains("fields"))value["fields"]=json::parse(ScriptEngine::RemapFieldReferences(value.value("class",""),value["fields"].dump(),inverse.dump()));if(value.contains("arguments")&&value["arguments"].is_array()){auto types=value.value("argumentTypes",std::vector<std::string>{});for(size_t i=0;i<types.size()&&i<value["arguments"].size();++i)if(types[i]=="Entity"||types[i].starts_with("Component:")||types[i].starts_with("Script:"))remap(value["arguments"][i]);}}else if(value.is_array())for(auto& v:value)remap(v);else if(value.is_string()){auto text=value.get<std::string>();if(inverse.contains(text))value=inverse[text];}};remap(data);
    }
    auto text=data.dump(2);bool ok=FileSystem::WriteText(path,text);
    if(ok){PrefabSystem::Link(scene,root,AssetManager::ToRelative(path),text);SD_INFO("Prefab saved: {}",FileSystem::Utf8(path));}
    return ok;
}

EntityID SceneSerializer::InstantiatePrefab(Scene& scene, const std::filesystem::path& path, EntityID parent) {
    std::string text = FileSystem::ReadText(path);
    if (text.empty()) { SD_ERROR("Prefab not found: {}", FileSystem::Utf8(path)); return kNullEntity; }
    EntityID e = InstantiateJson(scene, text, parent);
    if(e)PrefabSystem::Link(scene,e,AssetManager::ToRelative(path),text);
    return e;
}

} // namespace Stardust
