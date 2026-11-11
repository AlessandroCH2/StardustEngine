#include "Renderer/Material.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Assets/TextureImport.h"
#include "Assets/Pak.h"
#include <unordered_map>
#include <set>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Stardust {

static const char* kShaderNames[] = { "Stardust/Standard", "Stardust/Unlit", "Stardust/Toon", "Stardust/Transparent", "Stardust/UI", "Stardust/Water" };

const char* ShaderName(ShaderType t) { return kShaderNames[(int)t]; }

ShaderType ShaderFromName(const std::string& name) {
    for (int i = 0; i < (int)ShaderType::Count; ++i)
        if (name == kShaderNames[i]) return (ShaderType)i;
    return ShaderType::Standard;
}

void Material::ToJson(nlohmann::json& j) const {
    j["properties"]=nlohmann::json::parse(propertiesJson,nullptr,false);if(!j["properties"].is_object())j["properties"]=nlohmann::json::object();
    j["name"] = name;
    j["shader"] = ShaderName(shader);
    j["customPixelShader"] = customPixelShader;
    j["color"] = { color.r, color.g, color.b, color.a };
    j["albedoMap"] = albedoMap;
    j["normalMap"] = normalMap;
    j["roughness"] = roughness;
    j["metallic"] = metallic;
    j["normalStrength"] = normalStrength;
    j["alphaCutoff"] = alphaCutoff;
    j["emissive"] = { emissive.r, emissive.g, emissive.b };
    j["emissiveIntensity"] = emissiveIntensity;
    j["tiling"] = { tiling.x, tiling.y };
    j["offset"] = { offset.x, offset.y };
    j["doubleSided"] = doubleSided;
    j["toonSteps"] = toonSteps;
    j["rimStrength"] = rimStrength;
}

void Material::FromJson(const nlohmann::json& j) {
    auto vec = [&](const char* key, float* dst, int n) {
        if (!j.contains(key) || !j[key].is_array() || (int)j[key].size() != n) return;
        for (int i = 0; i < n; ++i) dst[i] = j[key][i].get<float>();
    };
    if(j.contains("properties")&&j["properties"].is_object())propertiesJson=j["properties"].dump();
    name = j.value("name", name);
    shader = ShaderFromName(j.value("shader", std::string(ShaderName(shader))));
    customPixelShader = j.value("customPixelShader", std::string());
    vec("color", &color.r, 4);
    albedoMap = j.value("albedoMap", albedoMap);
    normalMap = j.value("normalMap", normalMap);
    roughness = j.value("roughness", roughness);
    metallic = j.value("metallic", metallic);
    normalStrength = j.value("normalStrength", normalStrength);
    alphaCutoff = j.value("alphaCutoff", alphaCutoff);
    vec("emissive", &emissive.r, 3);
    emissiveIntensity = j.value("emissiveIntensity", emissiveIntensity);
    vec("tiling", &tiling.x, 2);
    vec("offset", &offset.x, 2);
    doubleSided = j.value("doubleSided", doubleSided);
    toonSteps = j.value("toonSteps", toonSteps);
    rimStrength = j.value("rimStrength", rimStrength);
}


const nlohmann::json& Material::PropertyLayout()const {
 using json=nlohmann::json;static const json empty=json::array();
 if(!customPixelShader.empty()){
  struct Cached {u64 frame=~0ull,generation=~0ull;std::filesystem::file_time_type stamp{};json fields=json::array();};static std::unordered_map<std::string,Cached> cache;auto path=AssetManager::ToAbsolute(customPixelShader+".properties.json");auto& item=cache[FileSystem::Utf8(path)];u64 frame=AssetManager::Device()?AssetManager::Device()->FrameNumber():0;auto generation=Pak::Generation();if(item.frame==frame&&item.generation==generation)return item.fields;item.frame=frame;std::error_code ec;auto stamp=std::filesystem::last_write_time(path,ec);if(stamp==item.stamp&&generation==item.generation)return item.fields;item.stamp=stamp;item.generation=generation;
  auto schema=json::parse(FileSystem::ReadText(path),nullptr,false);item.fields=json::array();if(!schema.is_object()||!schema.contains("properties")||!schema["properties"].is_array())return item.fields;std::set<std::string> names;
  for(auto field:schema["properties"]){if(!field.is_object()||!field.contains("name")||!field["name"].is_string()||field["name"].get<std::string>().empty())continue;auto name=field["name"].get<std::string>();if(!names.insert(name).second)continue;if(!field.contains("type")||!field["type"].is_string())field["type"]="float";if(!field.contains("label")||!field["label"].is_string())field["label"]=name;for(auto key:{"min","max"})if(field.contains(key)&&!field[key].is_number())field.erase(key);if(field.contains("srgb")&&!field["srgb"].is_boolean())field.erase("srgb");item.fields.push_back(field);if(item.fields.size()>=64)break;}
  return item.fields;
 }
 if(shader!=ShaderType::Water)return empty;
 static const json water=json::parse(R"water({"properties":[
 {"name":"ShallowColor","label":"Shallow Water","type":"color","default":[0.055,0.32,0.28,1]},
 {"name":"DeepColor","label":"Deep Water","type":"color","default":[0.018,0.12,0.18,1]},
 {"name":"WaveSpeed","label":"Wave Speed","type":"float","default":1,"min":-5,"max":5},
 {"name":"WindDirection","label":"Wind Direction","type":"vector2","default":[1,0.3]},
 {"name":"WaveHeight","label":"Wave Height (m)","type":"float","default":0.16,"min":0,"max":3},
 {"name":"WaveScale","label":"Wavelength Scale","type":"float","default":1,"min":0.1,"max":10},
 {"name":"Choppiness","label":"Choppiness","type":"float","default":0.4,"min":0,"max":1},
 {"name":"NormalDetail","label":"Small Ripples","type":"float","default":0.45,"min":0,"max":2},
 {"name":"FoamAmount","label":"Foam Amount","type":"float","default":0.6,"min":0,"max":2},
 {"name":"FoamWidth","label":"Shore Foam Width (m)","type":"float","default":0.7,"min":0.01,"max":5},
 {"name":"Absorption","label":"RGB Absorption","type":"vector3","default":[0.42,0.12,0.065]},
 {"name":"Roughness","label":"Surface Roughness","type":"float","default":0.09,"min":0.025,"max":1},
 {"name":"Refraction","label":"Refraction Strength","type":"float","default":0.018,"min":0,"max":0.1},
 {"name":"DirectionalSpread","label":"Wave Direction Spread","type":"float","default":0.75,"min":0,"max":1}
 ]})water")["properties"];
 return water;
}
std::vector<glm::vec4> Material::PropertyValues()const {
 using json=nlohmann::json;
 // Many draws share a material: its values are worked out once per frame (texture slots can change
 // between frames while textures stream in, so the cache never outlives the frame)
 struct Cached{u64 frame=~0ull;std::vector<glm::vec4> values;};
 static std::unordered_map<std::string,Cached> cache;
 const u64 frame=AssetManager::Device()?AssetManager::Device()->FrameNumber():~1ull;
 std::string key=customPixelShader+''+std::to_string((int)shader)+''+propertiesJson;
 if(auto found=cache.find(key);found!=cache.end()&&found->second.frame==frame&&frame!=~1ull)return found->second.values;
 if(cache.size()>4096)cache.clear();
 auto values=ComputePropertyValues();
 auto& entry=cache[key];entry.frame=frame;entry.values=values;
 return values;
}
std::vector<glm::vec4> Material::ComputePropertyValues()const {
 using json=nlohmann::json;const auto& fields=PropertyLayout();auto values=json::parse(propertiesJson,nullptr,false);if(!values.is_object())values=json::object();std::vector<glm::vec4> output;
 for(auto& field:fields){auto name=field.value("name","");auto value=values.contains(name)?values[name]:field.value("default",json(0));auto type=field.value("type","float");glm::vec4 v(0);
  if(type=="texture"){auto path=value.is_string()?value.get<std::string>():std::string();v.x=(float)AssetManager::TextureSRV(path,field.value("srgb",true),DefaultTexture::White);v.y=(float)TextureImportSettings::Load(path).Sampler();}
  else if(value.is_number())v.x=value.get<float>();else if(value.is_boolean())v.x=value.get<bool>()?1:0;else if(value.is_array())for(size_t i=0;i<std::min<size_t>(4,value.size());++i)if(value[i].is_number())v[i]=value[i].get<float>();
  output.push_back(v);
 }return output;
}
} // namespace Stardust
