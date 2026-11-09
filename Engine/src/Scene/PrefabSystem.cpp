#include "Scene/PrefabSystem.h"
#include "Scripting/ScriptEngine.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <unordered_set>
namespace Stardust {
using json=nlohmann::json;
static std::string Hex(u64 value){return std::format("{:016x}",value);}
void PrefabSystem::Link(Scene& scene,EntityID root,const std::string& asset,const std::string& source){
    auto original=json::parse(source,nullptr,false);if(!original.contains("entities"))return;
    std::vector<EntityID> objects;std::function<void(EntityID)> collect=[&](auto e){objects.push_back(e);for(auto c:scene.GetChildren(e))collect(c);};collect(root);
    json mapping=json::object();size_t i=0;for(auto& node:original["entities"]){if(i>=objects.size())break;mapping[node["uuid"].get<std::string>()]=Hex(scene.Reg().Get<IDComponent>(objects[i++]).uuid);}
    auto& link=scene.Reg().Has<PrefabInstanceComponent>(root)?scene.Reg().Get<PrefabInstanceComponent>(root):scene.Reg().Add<PrefabInstanceComponent>(root);
    link.asset=asset;link.baseline=original.dump();link.mapping=mapping.dump();scene.Reg().Get<NameComponent>(root).prefab=asset;
}
static void Remap(json& node,const json& mapping){if(node.is_object()){for(auto& [key,v]:node.items())if(key!="arguments"&&!(key=="fields"&&node.contains("class")))Remap(v,mapping);if(node.contains("class")&&node.contains("fields"))node["fields"]=json::parse(ScriptEngine::RemapFieldReferences(node.value("class",""),node["fields"].dump(),mapping.dump()));if(node.contains("arguments")&&node["arguments"].is_array()){auto types=node.value("argumentTypes",std::vector<std::string>{});for(size_t i=0;i<types.size()&&i<node["arguments"].size();++i)if(types[i]=="Entity"||types[i].starts_with("Component:")||types[i].starts_with("Script:"))Remap(node["arguments"][i],mapping);}}else if(node.is_array())for(auto& v:node)Remap(v,mapping);else if(node.is_string()){auto value=node.get<std::string>();if(mapping.contains(value))node=mapping[value];}}
static void Overrides(json& next,const json& baseline,const json& current){
    if(baseline==current)return;
    if(baseline.is_object()&&current.is_object()&&next.is_object()){
        for(auto& [key,value]:baseline.items())if(!current.contains(key))next.erase(key);
        for(auto& [key,value]:current.items())if(!baseline.contains(key))next[key]=value;else if(next.contains(key))Overrides(next[key],baseline[key],value);else if(value!=baseline[key])next[key]=value;
    }else next=current;
}
json PrefabSystem::Resolve(json scene){
    if(!scene.contains("entities")||!scene["entities"].is_array())return scene;
    auto entities=scene["entities"];std::vector<std::string> roots;for(auto& e:entities)if(e.contains("prefabInstance"))roots.push_back(e.value("uuid",""));
    for(auto& rootId:roots)try{
        auto found=std::find_if(entities.begin(),entities.end(),[&](auto& e){return e.value("uuid","")==rootId;});if(found==entities.end())continue;
        auto currentRoot=*found;auto link=currentRoot["prefabInstance"];std::string asset=link.value("asset","");
        auto source=json::parse(FileSystem::ReadText(AssetManager::ToAbsolute(asset)),nullptr,false);if(!source.is_object()||!source.contains("entities")||source["entities"].empty())continue;
        auto baseline=link.value("baseline",json::object());bool revert=link.value("revert",false);if(source==baseline&&!revert)continue;
        auto mapping=link.value("mapping",json::object());json oldMapping=mapping;
        std::unordered_map<std::string,json> current,old;for(auto& e:entities)current[e.value("uuid","")]=e;
        if(baseline.contains("entities"))for(auto e:baseline["entities"]){auto id=e.value("uuid","");Remap(e,oldMapping);old[id]=e;}
        for(auto& node:source["entities"]){auto id=node.value("uuid","");if(!mapping.contains(id))mapping[id]=Hex(GenerateUUID());}
        mapping[source["entities"][0]["uuid"].get<std::string>()]=rootId;
        std::unordered_set<std::string> removed;json replacement=json::array(),newMapping=json::object();
        for(size_t i=0;i<source["entities"].size();++i){auto node=source["entities"][i];std::string id=node.value("uuid","");std::string target=mapping[id];
            if(!revert&&i&&((oldMapping.contains(id)&&!current.contains(target))||removed.contains(node.value("parent","")))){removed.insert(id);continue;}
            Remap(node,mapping);
            if(!revert&&current.contains(target)&&old.contains(id)){
                auto previous=old[id],authored=current[target];previous.erase("uuid");previous.erase("parent");previous.erase("prefabInstance");previous.erase("prefabSource");authored.erase("uuid");authored.erase("parent");authored.erase("prefabInstance");authored.erase("prefabSource");auto savedId=node["uuid"];auto savedParent=node.value("parent",json());Overrides(node,previous,authored);node["uuid"]=savedId;if(!savedParent.is_null())node["parent"]=savedParent;
            }
            if(i==0){node["transform"]=currentRoot["transform"];if(currentRoot.contains("parent"))node["parent"]=currentRoot["parent"];else node.erase("parent");}
            newMapping[id]=target;replacement.push_back(node);
        }
        if(replacement.empty())continue;
        replacement[0]["prefabSource"]=asset;replacement[0]["prefabInstance"]={{"asset",asset},{"baseline",source},{"mapping",newMapping}};
        std::unordered_set<std::string> oldIds;for(auto& [key,value]:oldMapping.items())oldIds.insert(value.get<std::string>());oldIds.insert(rootId);
        json combined=json::array();for(auto& e:entities){auto id=e.value("uuid","");if(id==rootId)for(auto& node:replacement)combined.push_back(node);else if(!oldIds.contains(id))combined.push_back(e);}entities=std::move(combined);
    }catch(const std::exception& e){SD_WARN("Cannot refresh prefab: {}",e.what());}
    scene["entities"]=entities;return scene;
}
bool PrefabSystem::Refresh(Ref<Scene>& scene){auto current=json::parse(SceneSerializer::ToJson(*scene));auto resolved=Resolve(current);if(resolved==current)return false;auto next=SceneSerializer::FromJson(resolved.dump());if(!next)return false;scene=next;return true;}
void PrefabSystem::Revert(Scene& scene,EntityID root){if(auto* link=scene.Reg().TryGet<PrefabInstanceComponent>(root))link->revert=true;}
}
