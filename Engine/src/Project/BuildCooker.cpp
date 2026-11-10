#include "Project/BuildCooker.h"
#include "Assets/Pak.h"
#include "Assets/AssetManager.h"
#include "Scene/Scene.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Core/WindowIcon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <ufbx.h>
#include <cgltf.h>
namespace Stardust {
namespace fs=std::filesystem;using json=nlohmann::json;
namespace {
struct Collector {
 fs::path root;std::set<std::string> files;std::vector<std::string> pending;std::vector<std::string> optionalPrefixes;
 fs::path Absolute(const std::string& path){return root/(path.starts_with("Packages/")?fs::u8path(path):fs::path("Assets")/fs::u8path(path));}
 bool Add(std::string value,const std::string& owner="",bool relativeFirst=false){
  if(value.empty()||value.starts_with("builtin:")||value.find('\0')!=std::string::npos)return false;if(value.starts_with("asset:"))value=value.substr(6);
  std::replace(value.begin(),value.end(),'\\','/');fs::path candidate=fs::u8path(value);std::vector<fs::path> candidates{candidate};if(!owner.empty()){auto sibling=fs::u8path(owner).parent_path()/candidate;if(relativeFirst)candidates.insert(candidates.begin(),sibling);else candidates.push_back(sibling);}
  for(auto relative:candidates){relative=relative.lexically_normal();if(relative.is_absolute()){std::error_code ec;auto path=fs::relative(relative,root/"Assets",ec);if(ec||path.empty()||*path.begin()=="..")continue;relative=path;}if(relative.empty()||*relative.begin()=="..")continue;
   auto ext=relative.extension().string();if(ext==".cs"||ext==".pak")continue;bool editor=false;for(auto& part:relative)if(part=="Editor")editor=true;if(editor)continue;
   auto name=FileSystem::Utf8(relative);std::error_code ec;if(!fs::is_regular_file(Absolute(name),ec))continue;if(files.insert(name).second)pending.push_back(name);return true;
  }return false;
 }
 void Strings(const json& j,const std::string& owner){if(j.is_object()&&j.size()==1&&j.contains("asset")&&j["asset"].is_string()){auto path=j["asset"].get<std::string>();for(auto& prefix:optionalPrefixes)if(path==prefix||path.starts_with(prefix+"/"))return;}if(j.is_string()){auto value=j.get<std::string>();if(!Add(value,owner)&&!value.empty()&&(value[0]=='{'||value[0]=='[')){auto nested=json::parse(value,nullptr,false);if(!nested.is_discarded())Strings(nested,owner);}}else if(j.is_object())for(auto& [key,v]:j.items()){if(key=="uri"&&v.is_string())Add(v.get<std::string>(),owner,true);else Strings(v,owner);}else if(j.is_array())for(auto& v:j)Strings(v,owner);}
 void Resolve(){while(!pending.empty()){auto path=pending.back();pending.pop_back();auto absolute=Absolute(path);auto ext=absolute.extension().string();if(ext==".gltf"||ext==".glb"){cgltf_options options{};cgltf_data* data=nullptr;if(cgltf_parse_file(&options,absolute.string().c_str(),&data)==cgltf_result_success){for(size_t i=0;i<data->buffers_count;++i)if(data->buffers[i].uri)Add(data->buffers[i].uri,path,true);for(size_t i=0;i<data->images_count;++i)if(data->images[i].uri)Add(data->images[i].uri,path,true);cgltf_free(data);}continue;}if(ext==".fbx"){ufbx_load_opts opts{};opts.ignore_geometry=true;opts.ignore_animation=true;opts.ignore_missing_external_files=true;auto* scene=ufbx_load_file(absolute.string().c_str(),&opts,nullptr);if(scene){for(auto* texture:scene->textures){if(texture->relative_filename.length)Add(texture->relative_filename.data,path);if(texture->filename.length)Add(texture->filename.data,path);}ufbx_free_scene(scene);}continue;}
  if(ext==".spv")Add(path+".properties.json");
  if(ext!=".meta")Add(path+".meta");if(ext==".png"||ext==".jpg"||ext==".ttf"||ext==".otf"||ext==".wav"||ext==".mp3"||ext==".ogg"||ext==".terrain"||ext==".bin"||ext==".spv")continue;
  auto bytes=FileSystem::ReadText(absolute);auto data=json::parse(bytes,nullptr,false);if(!data.is_discarded())Strings(data,path);
  if(ext==".obj"||ext==".mtl"){std::istringstream lines(bytes);std::string line;while(std::getline(lines,line)){std::istringstream words(line);std::string key,value;words>>key;std::getline(words,value);auto first=value.find_first_not_of(" \t\r");if(first==std::string::npos)continue;value=value.substr(first);if(key=="mtllib"||key.starts_with("map_")||key=="bump"){if(!Add(value,path,true)){std::istringstream tokens(value);while(tokens>>value)Add(value,path);}}}}
  if(ext==".hlsl"||ext==".hlsli"){std::regex include(R"inc(#include\s*"([^"]+)")inc");for(auto i=std::sregex_iterator(bytes.begin(),bytes.end(),include);i!=std::sregex_iterator();++i)Add((*i)[1],path,true);}
 }}
};
}
bool BuildCooker::Cook(const fs::path& project,const fs::path& output,const ProjectConfig& config,CookReport* report){
 fs::path stage=output/(".cook-"+std::to_string(GenerateUUID()));
 try{
  const bool encrypt=!config.pakKey.empty();if(encrypt)Pak::SetPassphrase(config.pakKey);
  Collector main{project};for(auto& pack:config.contentPacks)main.optionalPrefixes.push_back(pack.folder);for(size_t i=0;i<config.scenes.size();++i)if(i>=config.scenesEnabled.size()||config.scenesEnabled[i])if(!main.Add(config.scenes[i]))throw std::runtime_error("Missing build scene: "+config.scenes[i]);if(!config.icon.empty()&&!main.Add(config.icon))throw std::runtime_error("Missing game icon: "+config.icon);main.Add("Settings/Input.json");main.Resolve();
  std::set<std::string> packages;auto stageFiles=[&](const std::set<std::string>& files,const fs::path& destination){fs::create_directories(destination);for(auto& path:files){if(path.starts_with("Packages/")){packages.insert(path);continue;}auto target=destination/fs::u8path(path);fs::create_directories(target.parent_path());fs::copy_file(project/"Assets"/fs::u8path(path),target,fs::copy_options::overwrite_existing);}};
  stageFiles(main.files,stage/"Main");if(!Project::WriteRuntime(stage/"Main/__runtime.sdruntime",config)||!Pak::Build(stage/"Main",output/"Data/game.pak",encrypt))throw std::runtime_error("Cannot create main pak");
  if(report){report->mainAssets=main.files;report->archives.push_back("game.pak");}
  std::unordered_set<std::string> names{"game","packages"};
  for(auto& pack:config.contentPacks){if(pack.name.empty()||pack.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos||!names.insert(pack.name).second)throw std::runtime_error("Invalid or duplicate pak name: "+pack.name);auto folder=fs::u8path(pack.folder).lexically_normal();if(folder.empty()||folder.is_absolute()||*folder.begin()==".."||!fs::is_directory(project/"Assets"/folder))throw std::runtime_error("Invalid pak folder: "+pack.folder);
   Collector optional{project};for(auto& file:fs::recursive_directory_iterator(project/"Assets"/folder))if(file.is_regular_file())optional.Add(FileSystem::Utf8(fs::relative(file.path(),project/"Assets")));optional.Resolve();for(auto& path:main.files)optional.files.erase(path);stageFiles(optional.files,stage/pack.name);if(!Pak::Build(stage/pack.name,output/"Data/Optional"/(pack.name+".pak"),encrypt))throw std::runtime_error("Cannot create optional pak");if(report)report->archives.push_back(pack.name+".pak");
  }
  fs::create_directories(stage/"Packages");for(auto& path:packages){auto relative=fs::u8path(path).lexically_relative("Packages");auto target=stage/"Packages"/relative;fs::create_directories(target.parent_path());fs::copy_file(project/fs::u8path(path),target,fs::copy_options::overwrite_existing);}if(!Pak::Build(stage/"Packages",output/"Data/packages.pak",encrypt))throw std::runtime_error("Cannot create packages pak");
  if(!config.icon.empty()&&config.buildTarget!="Linux"){
   std::string name=config.name;std::erase_if(name,[](char c){return std::string("\\/:*?\"<>|").find(c)!=std::string::npos;});if(name.empty())name="Game";
   auto executable=output/(name+".exe");
   if(fs::is_regular_file(executable)&&!WindowIcon::Embed(executable,project/"Assets"/fs::u8path(config.icon)))throw std::runtime_error("Cannot embed game icon");
  }
  if(encrypt){
   std::string name=config.name;std::erase_if(name,[](char c){return std::string("\\/:*?\"<>|").find(c)!=std::string::npos;});if(name.empty())name="Game";
   auto executable=config.buildTarget=="Linux"?output/"Game.x86_64":output/(name+".exe");
   if(fs::is_regular_file(executable)&&!Pak::EmbedKey(executable,config.pakKey))throw std::runtime_error("Cannot write the pak key into the player");
  }
  fs::remove_all(stage);SD_INFO("Cooked {} main dependencies and {} optional packs",main.files.size(),config.contentPacks.size());return true;
 }catch(const std::exception& e){SD_ERROR("Build cook failed: {}",e.what());fs::remove_all(stage);return false;}
}
}
