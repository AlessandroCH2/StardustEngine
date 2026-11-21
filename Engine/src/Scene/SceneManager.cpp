#include "Scene/SceneManager.h"
#include "Scene/SceneSerializer.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <future>
#include <atomic>
#include <chrono>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_map>

// Background scene loading, without stalling the frame:
//  1. a worker thread reads and parses the scene, finds every mesh, texture, material and prefab it uses
//     (prefabs too: the cars and people scripts spawn) and decodes the meshes (CPU work);
//  2. the main thread, a few milliseconds per frame, turns the decoded meshes into GPU meshes and starts
//     the textures (which decode on their own worker threads);
//  3. the worker also builds the scene's entities (deserialisation touches nothing shared);
//  4. once all of that is ready (and activation is allowed) the scene replaces the current one: the main
//     thread only adds the prepared entities, their physics bodies and their scripts.
// Progress: 0-0.6 reading and decoding, 0.6-0.95 uploading, 1 done.
namespace Stardust {namespace {
struct LoadOperation {
    std::future<std::string> reader; std::atomic<float> progress{0}; bool allow=true; int status=0; std::string error;
    Ref<Scene> scene; bool readerDone=false;
    std::vector<std::pair<std::string,bool>> textures; size_t texIndex=0; int uploadWork=0;
};
std::mutex operationMutex;std::unordered_map<u64,std::shared_ptr<LoadOperation>> operations;u64 nextOperation=1;

std::string Lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),::tolower);return s;}
std::string Extension(const std::string& s){auto dot=s.find_last_of('.');return dot==std::string::npos?std::string():Lower(s.substr(dot));}

// Every asset a scene (or a prefab, a material) names, found by its file extension
struct AssetScan {
    std::set<std::string> meshes, materials, prefabs;
    std::set<std::pair<std::string,bool>> textures;
    void Scan(const nlohmann::json& j, bool srgb = true) {
        if (j.is_string()) {
            const auto& s = j.get_ref<const std::string&>();
            if (s.size() < 5 || s.size() > 400) return;
            auto ext = Extension(s);
            if (ext == ".obj" || ext == ".glb" || ext == ".gltf") meshes.insert(s);
            else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") textures.insert({ s, srgb });
            else if (ext == ".mat") materials.insert(s);
            else if (ext == ".prefab") prefabs.insert(s);
        } else if (j.is_object()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                auto key = Lower(it.key());
                // in materials only colour maps are sRGB
                bool linear = key.find("normal") != std::string::npos || key.find("rough") != std::string::npos || key.find("metal") != std::string::npos
                    || key.find("occlusion") != std::string::npos || key.find("height") != std::string::npos || key.find("mask") != std::string::npos;
                Scan(it.value(), srgb && !linear);
            }
        } else if (j.is_array()) for (auto& v : j) Scan(v, srgb);
    }
    void ScanFile(const std::string& asset) {
        try { auto text = FileSystem::ReadText(AssetManager::ToAbsolute(asset)); if (text.empty()) return; Scan(nlohmann::json::parse(text)); } catch (...) {}
    }
};
}

u64 SceneManager::LoadAsync(const std::string& path){
    std::lock_guard lock(operationMutex);
    for(auto& [id,op]:operations)if(op->status==0)return 0;
    auto op=std::make_shared<LoadOperation>();auto absolute=AssetManager::ToAbsolute(path);auto id=nextOperation++;operations[id]=op;
    auto loadedList=AssetManager::LoadedMeshRefs();
    std::unordered_set<std::string> loaded(loadedList.begin(),loadedList.end());
    op->reader=std::async(std::launch::async,[weak=std::weak_ptr<LoadOperation>(op),absolute,loaded=std::move(loaded)]{
        auto op=weak.lock();if(!op)return std::string();
        try{
            op->progress=.02f;
            auto bytes=FileSystem::ReadText(absolute);if(bytes.empty())throw std::runtime_error("Scene file is missing or empty");
            op->progress=.06f;
            auto j=nlohmann::json::parse(bytes);if(!j.is_object()||!j.contains("entities"))throw std::runtime_error("Invalid scene format");
            op->progress=.1f;
            AssetScan scan;scan.Scan(j);
            // prefabs name other prefabs, and their own meshes and materials
            std::set<std::string> done;
            for(int depth=0;depth<3;++depth){auto todo=scan.prefabs;for(auto& p:todo)if(done.insert(p).second)scan.ScanFile(p);}
            for(auto& m:scan.materials)scan.ScanFile(m);
            std::vector<std::string> meshes;for(auto& m:scan.meshes)if(!loaded.contains(m))meshes.push_back(m);
            auto start=std::chrono::steady_clock::now();
            for(size_t i=0;i<meshes.size();++i){
                if(op.use_count()<=1)break;   // cancelled
                AssetManager::StageMesh(meshes[i]);
                op->progress=.1f+.5f*(float)(i+1)/(float)meshes.size();
            }
            op->textures.assign(scan.textures.begin(),scan.textures.end());
            SD_INFO("Scene load: {} meshes decoded in the background ({:.0f} ms), {} textures to start, {} prefabs",meshes.size(),
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),op->textures.size(),done.size());
            // the entities too (it touches nothing shared): the main thread only has to add them
            op->scene=SceneSerializer::FromJson(bytes,true);
            if(!op->scene)throw std::runtime_error("Scene deserialization failed");
            op->progress=.6f;
            return std::string("ok");
        }catch(const std::exception& e){std::lock_guard lock(operationMutex);op->error=e.what();return std::string();}
    });
    return id;
}
float SceneManager::LoadProgress(u64 id){std::lock_guard lock(operationMutex);auto i=operations.find(id);return i==operations.end()?0:i->second->progress.load();}
int SceneManager::LoadStatus(u64 id){std::lock_guard lock(operationMutex);auto i=operations.find(id);return i==operations.end()?-1:i->second->status;}
std::string SceneManager::LoadError(u64 id){std::lock_guard lock(operationMutex);auto i=operations.find(id);return i==operations.end()?"Invalid operation":i->second->error;}
void SceneManager::AllowActivation(u64 id,bool allowed){std::lock_guard lock(operationMutex);if(operations.contains(id))operations[id]->allow=allowed;}

void SceneManager::UpdateAsync(const std::function<bool(Ref<Scene>)>& activate){
    std::shared_ptr<LoadOperation> op;
    {std::lock_guard lock(operationMutex);for(auto& [id,o]:operations)if(!o->status){op=o;break;}}
    if(!op)return;
    try{
        // meshes decoded so far go to the GPU while the worker carries on
        AssetManager::UploadStagedMeshes(6.0);
        if(!op->readerDone){
            if(!op->reader.valid()||op->reader.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
            auto result=op->reader.get();op->readerDone=true;
            if(result.empty()||!op->scene)throw std::runtime_error(op->error.empty()?"Scene read failed":op->error);
            op->uploadWork=AssetManager::StagedMeshes()+(int)op->textures.size();
        }
        // start the textures, a few milliseconds' worth a frame (small ones decode at once)
        auto start=std::chrono::steady_clock::now();
        while(op->texIndex<op->textures.size()&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<6.0){
            auto& [path,srgb]=op->textures[op->texIndex++];AssetManager::GetTexture(path,srgb);
        }
        AssetManager::PumpTextures();
        int remaining=AssetManager::StagedMeshes()+AssetManager::PendingTextures()+(int)(op->textures.size()-op->texIndex);
        float done=op->uploadWork>0?1.f-std::clamp((float)remaining/(float)op->uploadWork,0.f,1.f):1.f;
        op->progress=std::max(op->progress.load(),.6f+.35f*done);
        if(remaining>0||!op->allow)return;
        auto scene=std::move(op->scene);
        op->progress=.97f;
        auto t0=std::chrono::steady_clock::now();
        if(!activate(scene))throw std::runtime_error("Scene activation failed");
        SD_INFO("Scene load: activated in {:.0f} ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
        std::lock_guard lock(operationMutex);op->status=1;op->progress=1;
    }catch(const std::exception& e){std::lock_guard lock(operationMutex);op->error=e.what();op->status=-1;}
}
void SceneManager::CancelLoads(){std::unordered_map<u64,std::shared_ptr<LoadOperation>> old;{std::lock_guard lock(operationMutex);old.swap(operations);}for(auto& [id,op]:old)if(op->reader.valid())op->reader.wait();}
}
