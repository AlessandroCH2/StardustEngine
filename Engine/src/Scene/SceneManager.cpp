#include "Scene/SceneManager.h"
#include "Scene/SceneSerializer.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include <future>
#include <atomic>
#include <nlohmann/json.hpp>
#include <unordered_map>
namespace Stardust {namespace {
struct LoadOperation {std::future<std::string> reader;std::atomic<float> progress{0};bool allow=true;int status=0;std::string error;};
std::mutex operationMutex;std::unordered_map<u64,std::shared_ptr<LoadOperation>> operations;u64 nextOperation=1;
}
u64 SceneManager::LoadAsync(const std::string& path){std::lock_guard lock(operationMutex);for(auto& [id,op]:operations)if(op->status==0)return 0;auto op=std::make_shared<LoadOperation>();auto absolute=AssetManager::ToAbsolute(path);auto id=nextOperation++;operations[id]=op;op->reader=std::async(std::launch::async,[weak=std::weak_ptr<LoadOperation>(op),absolute]{auto op=weak.lock();try{op->progress=.05f;auto bytes=FileSystem::ReadText(absolute);if(bytes.empty())throw std::runtime_error("Scene file is missing or empty");op->progress=.45f;auto j=nlohmann::json::parse(bytes);if(!j.is_object()||!j.contains("entities"))throw std::runtime_error("Invalid scene format");op->progress=.9f;return bytes;}catch(const std::exception& e){std::lock_guard lock(operationMutex);op->error=e.what();return std::string();}});return id;}
float SceneManager::LoadProgress(u64 id){std::lock_guard lock(operationMutex);auto i=operations.find(id);return i==operations.end()?0:i->second->progress.load();}
int SceneManager::LoadStatus(u64 id){std::lock_guard lock(operationMutex);auto i=operations.find(id);return i==operations.end()?-1:i->second->status;}
std::string SceneManager::LoadError(u64 id){std::lock_guard lock(operationMutex);auto i=operations.find(id);return i==operations.end()?"Invalid operation":i->second->error;}
void SceneManager::AllowActivation(u64 id,bool allowed){std::lock_guard lock(operationMutex);if(operations.contains(id))operations[id]->allow=allowed;}
void SceneManager::UpdateAsync(const std::function<bool(Ref<Scene>)>& activate){std::shared_ptr<LoadOperation> ready;{std::lock_guard lock(operationMutex);for(auto& [id,op]:operations)if(!op->status&&(op->allow||!op->error.empty())&&op->reader.valid()&&op->reader.wait_for(std::chrono::seconds(0))==std::future_status::ready){ready=op;break;}}if(!ready)return;try{auto text=ready->reader.get();if(text.empty())throw std::runtime_error(ready->error.empty()?"Scene read failed":ready->error);auto scene=SceneSerializer::FromJson(text);if(!scene)throw std::runtime_error("Scene deserialization failed");ready->progress=.95f;if(!activate(scene))throw std::runtime_error("Scene activation failed");std::lock_guard lock(operationMutex);ready->status=1;ready->progress=1;}catch(const std::exception& e){std::lock_guard lock(operationMutex);ready->error=e.what();ready->status=-1;}}
void SceneManager::CancelLoads(){std::unordered_map<u64,std::shared_ptr<LoadOperation>> old;{std::lock_guard lock(operationMutex);old.swap(operations);}for(auto& [id,op]:old)if(op->reader.valid())op->reader.wait();}
}
