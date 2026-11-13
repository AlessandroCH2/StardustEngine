#include "Assets/AssetManager.h"
#include <fstream>
#include <set>
#include <unordered_set>
#include <glm/gtx/component_wise.hpp>
#include <span>
#include "Assets/TextureImport.h"
#include "Assets/AssetMeta.h"
#include "Core/Log.h"
#include "Core/FileSystem.h"
#include "Assets/Pak.h"
#include "UI/CanvasRenderer.h"
#include "Project/Project.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <unordered_set>
#include <span>
#include <sstream>
#include <future>
#include <thread>
#include <chrono>

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <ufbx.h>
#pragma warning(pop)

namespace Stardust {

namespace fs = std::filesystem;

static std::unordered_map<std::string, Ref<RHI::Texture>> s_Textures;
static std::unordered_map<std::string, Ref<Mesh>> s_Meshes;
static std::unordered_map<std::string, Ref<Material>> s_Materials;
static Ref<RHI::Texture> s_Defaults[3];
static std::unordered_map<std::string, std::shared_ptr<ufbx_scene>> s_FbxScenes;
struct AnimatedMesh { int frame = -1; std::string animation; Ref<Mesh> mesh; };
static std::unordered_map<std::string, AnimatedMesh> s_AnimatedMeshes;
static void ClearSkinning();


static std::string Lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }

void AssetManager::Init(RHI::Device& device, const fs::path& assetsRoot) {
    s_Device = &device;
    s_Root = assetsRoot;
    const u8 colors[3][4] = { { 255, 255, 255, 255 }, { 128, 128, 255, 255 }, { 0, 0, 0, 255 } };
    const char* names[3] = { "DefaultWhite", "DefaultNormal", "DefaultBlack" };
    for (int i = 0; i < 3; ++i) {
        RHI::TextureDesc d;
        d.name = names[i];
        d.width = d.height = 4;
        d.format = RHI::Format::RGBA8_UNORM;
        d.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::CopyDst;
        s_Defaults[i] = device.CreateTexture(d);
        std::vector<u8> px(4 * 4 * 4);
        for (int p = 0; p < 16; ++p) memcpy(&px[p * 4], colors[i], 4);
        device.UploadTexture(s_Defaults[i].get(), px.data(), px.size());
    }
}

void AssetManager::Shutdown() {
    SaveDirtyMaterials();
    s_Textures.clear();
    s_Meshes.clear();
    s_Materials.clear();
    s_FbxScenes.clear(); s_AnimatedMeshes.clear();ClearSkinning();
    for (auto& d : s_Defaults) d.reset();
    s_Device = nullptr;
}
void AssetManager::RefreshMounts(){static u64 previous=0;auto current=Pak::Generation();if(previous==current||!s_Device)return;previous=current;s_Device->WaitIdle();s_Textures.clear();s_Meshes.clear();s_Materials.clear();s_FbxScenes.clear();s_AnimatedMeshes.clear();ClearSkinning();CanvasRenderer::InvalidateAssets();}
// Textures decoding on worker threads, by cache key
struct DecodedTexture { std::vector<u8> pixels; int width = 0, height = 0; double readMs = 0, decodeMs = 0; };
struct PendingTexture { std::future<DecodedTexture> job; TextureImportSettings settings; bool srgb = false; bool started = false; };
static std::unordered_map<std::string, PendingTexture> s_PendingTextures;

void AssetManager::Invalidate(const std::string& path) {
    auto erase=[&](auto& entries){std::erase_if(entries,[&](const auto& item){return item.first==path||item.first.starts_with(path+"/")||item.first.starts_with(path+"|");});};
    erase(s_Textures);erase(s_PendingTextures);erase(s_Meshes);erase(s_Materials);s_FbxScenes.clear();s_AnimatedMeshes.clear();ClearSkinning();
}

std::string AssetManager::ToRelative(const fs::path& absolute) {
    // Paths built from the assets folder are made relative by text alone: std::filesystem::relative
    // resolves both paths on disk, which is slow enough to show up when the editor draws file lists
    if (absolute.is_absolute() && !s_Root.empty()) {
        fs::path lexical = absolute.lexically_normal().lexically_relative(s_Root.lexically_normal());
        if (!lexical.empty() && !lexical.generic_string().starts_with("..")) return FileSystem::Utf8(lexical);
    }
    std::error_code ec;
    fs::path rel = fs::relative(absolute, s_Root, ec);
    if (ec || rel.empty() || rel.generic_string().starts_with("..")) return FileSystem::Utf8(absolute);
    return FileSystem::Utf8(rel);
}

fs::path AssetManager::ToAbsolute(const std::string& relative) {
    fs::path p=fs::u8path(relative);
    if (relative.starts_with("Packages/")) return Pak::Resolve(Project::Dir() / p);
    return Pak::Resolve(p.is_absolute() ? p : s_Root / p);
}

bool AssetManager::IsMeshFile(const fs::path& p) {
    std::string e = Lower(p.extension().string());
    return e == ".obj" || e == ".gltf" || e == ".glb" || e == ".fbx";
}

bool AssetManager::IsTextureFile(const fs::path& p) {
    std::string e = Lower(p.extension().string());
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp";
}

std::vector<std::string> AssetManager::Find(const std::vector<std::string>& extensions) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(s_Root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file()) continue;
        std::string e = Lower(it->path().extension().string());
        if (std::find(extensions.begin(), extensions.end(), e) != extensions.end() && !AssetMeta::IsSideFile(it->path())) out.push_back(ToRelative(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------------------------------------------ textures

void AssetManager::SetRuntimeTexture(const std::string& path,Ref<RHI::Texture> texture){if(texture){s_Textures[path+"|s"]=texture;s_Textures[path+"|l"]=texture;}else{s_Textures.erase(path+"|s");s_Textures.erase(path+"|l");}}
void AssetManager::SetRuntimeMesh(const std::string& path,Ref<Mesh> mesh){if(mesh)s_Meshes[path]=std::move(mesh);else s_Meshes.erase(path);}
// Large textures decode on worker threads: a scene's textures load side by side instead of one after another,
// and the frame keeps going with the fallback texture meanwhile. Small ones (UI icons) stay immediate.
namespace {
constexpr u64 kAsyncTextureBytes = 512 * 1024;
int RunningDecodes() { int n = 0; for (auto& [key, p] : s_PendingTextures) if (p.started) ++n; return n; }

DecodedTexture DecodeTexture(const fs::path& file, int maxSize) {
    DecodedTexture out;
    auto t0 = std::chrono::steady_clock::now();
    auto bytes = FileSystem::ReadBinary(file);
    auto t1 = std::chrono::steady_clock::now();
    int w, h, n;
    stbi_uc* data = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (!data) return out;
    if (std::max(w, h) > maxSize) {
        float scale = (float)maxSize / std::max(w, h);
        int nw = std::max(1, (int)(w * scale)), nh = std::max(1, (int)(h * scale));
        out.pixels.resize((size_t)nw * nh * 4);
        for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) for (int c = 0; c < 4; ++c)
            out.pixels[((size_t)y * nw + x) * 4 + c] = data[((size_t)(y * h / nh) * w + x * w / nw) * 4 + c];
        w = nw; h = nh;
    } else out.pixels.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    out.width = w; out.height = h;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    out.readMs = ms(t0, t1); out.decodeMs = ms(t1, std::chrono::steady_clock::now());
    return out;
}
}

static Ref<RHI::Texture> UploadDecoded(RHI::Device& device, const std::string& path, const DecodedTexture& image, const TextureImportSettings& settings, bool srgb) {
    RHI::TextureDesc d;
    d.name = path;
    d.width = (u32)image.width; d.height = (u32)image.height;
    d.mipLevels = settings.mipmaps ? 1 + (u32)std::floor(std::log2((float)std::max(image.width, image.height))) : 1;
    d.format = srgb ? RHI::Format::RGBA8_SRGB : RHI::Format::RGBA8_UNORM;
    d.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::CopyDst | RHI::TextureUsage::CopySrc;
    auto tex = device.CreateTexture(d);
    device.UploadTexture(tex.get(), image.pixels.data(), (u64)image.width * image.height * 4);
    SD_INFO("Texture loaded: {} ({}x{}, read {:.0f} ms, decode {:.0f} ms)", path, image.width, image.height, image.readMs, image.decodeMs);
    return tex;
}

Ref<RHI::Texture> AssetManager::GetTexture(const std::string& path, bool srgb) {
    if (path.empty() || !s_Device) return nullptr;
    std::string key = path + (srgb ? "|s" : "|l");
    if (auto it = s_Textures.find(key); it != s_Textures.end()) return it->second;

    const fs::path file = ToAbsolute(path);
    auto pending = s_PendingTextures.find(key);
    if (pending == s_PendingTextures.end()) {
        auto settings = TextureImportSettings::Load(path);
        const bool linear = srgb && settings.srgb;
        std::error_code ec;
        const u64 size = Pak::Contains(file) ? Pak::Size(file) : (u64)fs::file_size(file, ec);
        if (size < kAsyncTextureBytes || ec) {
            DecodedTexture image = DecodeTexture(file, settings.maxSize);
            if (image.pixels.empty()) { SD_WARN("Texture not found or unsupported: {}", path); s_Textures[key] = nullptr; return nullptr; } // do not retry every frame
            return s_Textures[key] = UploadDecoded(*s_Device, path, image, settings, linear);
        }
        pending = s_PendingTextures.emplace(key, PendingTexture{ {}, settings, linear, false }).first;
    }
    PendingTexture& job = pending->second;
    // Keep one core for the game itself
    if (!job.started && RunningDecodes() < std::max(1, (int)std::thread::hardware_concurrency() - 1)) {
        job.job = std::async(std::launch::async, DecodeTexture, file, job.settings.maxSize);
        job.started = true;
    }
    if (!job.started || job.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
    DecodedTexture image = job.job.get();
    auto settings = job.settings; const bool linear = job.srgb;
    s_PendingTextures.erase(pending);
    if (image.pixels.empty()) { SD_WARN("Texture not found or unsupported: {}", path); s_Textures[key] = nullptr; return nullptr; }
    return s_Textures[key] = UploadDecoded(*s_Device, path, image, settings, linear);
}

int AssetManager::PendingTextures() { return (int)s_PendingTextures.size(); }

void AssetManager::WaitForTextures() {
    // Decodes the queue to the end: for tests and loading screens that must not show fallbacks
    while (!s_PendingTextures.empty()) {
        std::vector<std::pair<std::string, bool>> keys;
        for (auto& [key, job] : s_PendingTextures) keys.push_back({ key.substr(0, key.size() - 2), key.ends_with("|s") });
        for (auto& [path, srgb] : keys) {
            auto found = s_PendingTextures.find(path + (srgb ? "|s" : "|l"));
            if (found != s_PendingTextures.end() && found->second.started) found->second.job.wait();
            GetTexture(path, srgb);
        }
    }
}

u32 AssetManager::DefaultSRV(DefaultTexture t) { return s_Defaults[(int)t] ? s_Defaults[(int)t]->SRV() : 0; }

u32 AssetManager::TextureSRV(const std::string& path, bool srgb, DefaultTexture fallback) {
    auto t = GetTexture(path, srgb);
    return t ? t->SRV() : DefaultSRV(fallback);
}

// ------------------------------------------------------------------------------------------------ meshes

void ComputeNormalsAndTangents(std::vector<Vertex>& v, const std::vector<u32>& idx, bool computeNormals) {
    if (computeNormals) for (auto& x : v) x.normal = glm::vec3(0);
    std::vector<glm::vec3> tan(v.size(), glm::vec3(0)), bit(v.size(), glm::vec3(0));
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        Vertex &a = v[idx[i]], &b = v[idx[i + 1]], &c = v[idx[i + 2]];
        glm::vec3 e1 = b.position - a.position, e2 = c.position - a.position;
        if (computeNormals) {
            glm::vec3 n = glm::cross(e1, e2);
            a.normal += n; b.normal += n; c.normal += n;
        }
        glm::vec2 d1 = b.uv - a.uv, d2 = c.uv - a.uv;
        float det = d1.x * d2.y - d2.x * d1.y;
        if (std::abs(det) < 1e-12f) continue;
        float r = 1.0f / det;
        glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
        glm::vec3 bt = (e2 * d1.x - e1 * d2.x) * r;
        for (int k = 0; k < 3; ++k) { tan[idx[i + k]] += t; bit[idx[i + k]] += bt; }
    }
    for (size_t i = 0; i < v.size(); ++i) {
        glm::vec3 n = glm::length(v[i].normal) > 0 ? glm::normalize(v[i].normal) : glm::vec3(0, 1, 0);
        v[i].normal = n;
        glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
        if (glm::length(t) < 1e-6f) t = glm::normalize(glm::cross(n, std::abs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
        t = glm::normalize(t);
        float w = glm::dot(glm::cross(n, t), bit[i]) < 0.0f ? -1.0f : 1.0f;
        v[i].tangent = glm::vec4(t, w);
    }
}

static glm::mat4 FbxMatrix(const ufbx_matrix& m){return glm::mat4(glm::vec4(m.cols[0].x,m.cols[0].y,m.cols[0].z,0),glm::vec4(m.cols[1].x,m.cols[1].y,m.cols[1].z,0),glm::vec4(m.cols[2].x,m.cols[2].y,m.cols[2].z,0),glm::vec4(m.cols[3].x,m.cols[3].y,m.cols[3].z,1));}
static std::shared_ptr<ufbx_scene> CachedFbx(const fs::path& path){
    std::string key = FileSystem::Utf8(path);
    auto& cached = s_FbxScenes[key];
    if (!cached) {
        ufbx_load_opts opts{};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0;
        opts.evaluate_skinning = true;
        opts.ignore_missing_external_files = true;
        ufbx_error error{};
        auto bytes=FileSystem::ReadBinary(path);
        auto* loaded = ufbx_load_memory(bytes.data(),bytes.size(), &opts, &error);
        if (!loaded) { SD_ERROR("FBX import failed: {}", error.description.data); return {}; }
        cached = std::shared_ptr<ufbx_scene>(loaded, ufbx_free_scene);
    }
    return cached;
}
Ref<Mesh> AssetManager::ImportFbx(const fs::path& path,float time,const std::string& animation){auto cached=CachedFbx(path);if(!cached)return nullptr;
    ufbx_scene* scene = cached.get();
    std::shared_ptr<ufbx_scene> evaluated;
    if (time >= 0 && scene->anim_stacks.count) {
        const ufbx_anim* anim = scene->anim_stacks.data[0]->anim;
        for (auto* stack : scene->anim_stacks) if (animation == stack->name.data) anim = stack->anim;
        ufbx_evaluate_opts opts{}; opts.evaluate_skinning = true;
        double duration = anim->time_end - anim->time_begin;
        double t = anim->time_begin + (duration > 0 ? std::fmod(std::max(0.0f,time),duration) : 0);
        evaluated = std::shared_ptr<ufbx_scene>(ufbx_evaluate_scene(scene, anim, t, &opts, nullptr), ufbx_free_scene);
        if (evaluated) scene = evaluated.get();
    }
    std::vector<Vertex> vertices; std::vector<u32> indices;std::vector<SubMesh> parts;
    for (auto* node : scene->nodes) {
        auto* mesh = node->mesh; if (!mesh) continue;
        std::vector<uint32_t> triangles(mesh->max_face_triangles * 3);
        auto normalMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);
        size_t faceId=0;for (auto face : mesh->faces) {
            u32 slot=0;std::string name="Default";if(faceId<mesh->face_material.count){auto local=mesh->face_material.data[faceId];if(local<node->materials.count){auto* material=node->materials.data[local];slot=material->typed_id;name=material->name.data;}}++faceId;
            if(parts.empty()||parts.back().materialSlot!=slot)parts.push_back({(u32)indices.size(),0,slot,name});
            size_t count = ufbx_triangulate_face(triangles.data(), triangles.size(), mesh, face) * 3;
            parts.back().indexCount+=(u32)count;
            for (size_t i = 0; i < count; ++i) {
                auto ix = triangles[i];
                auto p = ufbx_get_vertex_vec3(&mesh->skinned_position,ix);
                if (mesh->skinned_is_local) p = ufbx_transform_position(&node->geometry_to_world,p);
                Vertex v{}; v.position = {(float)p.x,(float)p.y,(float)p.z};
                if (mesh->skinned_normal.exists) {
                    auto n = ufbx_get_vertex_vec3(&mesh->skinned_normal,ix);
                    if (mesh->skinned_is_local) n = ufbx_transform_direction(&normalMatrix,n);
                    v.normal = glm::normalize(glm::vec3((float)n.x,(float)n.y,(float)n.z));
                }
                if (mesh->vertex_uv.exists) { auto uv = ufbx_get_vertex_vec2(&mesh->vertex_uv,ix); v.uv = {(float)uv.x,1.0f-(float)uv.y}; }
                indices.push_back((u32)vertices.size()); vertices.push_back(v);
            }
        }
    }
    if (vertices.empty()) return nullptr;
    bool normals = glm::length(vertices.front().normal) < 0.001f;
    ComputeNormalsAndTangents(vertices,indices,normals);
    auto result=MakeRef<Mesh>(*s_Device,vertices,indices,FileSystem::Utf8(path.filename()));result->SetSubMeshes(std::move(parts));return result;
}
static std::string SafeFileName(std::string name) {
    for (char& c : name) if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || (unsigned char)c < 32) c = '_';
    return name.empty() ? std::string("Unnamed") : name;
}
static std::string LastPathPart(std::string_view path) {
    auto cut = path.find_last_of("/\\");
    return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
}
static ufbx_texture* FileTextureOf(ufbx_texture* texture) {
    if (texture && texture->type != UFBX_TEXTURE_FILE && texture->file_textures.count) return texture->file_textures.data[0];
    return texture;
}

std::vector<FbxContentItem> AssetManager::GetFbxContents(const std::string& asset) {
    using json = nlohmann::json;
    const fs::path source = ToAbsolute(asset);
    const fs::path folder = source.parent_path() / fs::u8path(FileSystem::Utf8(source.stem()) + ".fbm");
    const fs::path manifest = folder / "contents.json";
    std::vector<FbxContentItem> items;
    auto readManifest = [&] {
        json data = json::parse(FileSystem::ReadText(manifest), nullptr, false);
        if (!data.is_array()) return;
        for (const json& entry : data) {
            FbxContentItem item{ entry.value("path", std::string()), entry.value("name", std::string()), entry.value("slot", 0u), entry.value("material", false) };
            if (fs::exists(ToAbsolute(item.path))) items.push_back(std::move(item));
        }
    };
    std::error_code ec;
    if (fs::exists(manifest, ec)) { readManifest(); return items; }

    auto scene = CachedFbx(source);
    if (!scene) return items;
    fs::create_directories(folder, ec);

    // Textures: embedded data is written out; external files are used where they already are
    std::unordered_map<const ufbx_texture*, std::string> texturePaths;
    std::unordered_set<std::string> usedNames;
    auto exportTexture = [&](ufbx_texture* raw) -> std::string {
        ufbx_texture* texture = FileTextureOf(raw);
        if (!texture) return {};
        if (auto found = texturePaths.find(texture); found != texturePaths.end()) return found->second;
        std::string fileName = LastPathPart(texture->relative_filename.length ? std::string_view(texture->relative_filename.data, texture->relative_filename.length) : std::string_view(texture->filename.data, texture->filename.length));
        if (fileName.empty()) fileName = std::string(texture->name.data, texture->name.length) + ".png";
        fileName = SafeFileName(fileName);
        std::string result;
        if (texture->content.size) {
            fs::path stemPath = fs::u8path(fileName);
            std::string unique = fileName;
            for (int n = 1; !usedNames.insert(unique).second; ++n) unique = FileSystem::Utf8(stemPath.stem()) + "_" + std::to_string(n) + stemPath.extension().string();
            const fs::path out = folder / fs::u8path(unique);
            if (std::ofstream file(out, std::ios::binary); file) file.write((const char*)texture->content.data, (std::streamsize)texture->content.size);
            result = ToRelative(out);
        } else {
            for (const fs::path& candidate : { source.parent_path() / fs::u8path(std::string(texture->relative_filename.data, texture->relative_filename.length)), source.parent_path() / fs::u8path(fileName), source.parent_path() / "textures" / fs::u8path(fileName), fs::u8path(std::string(texture->absolute_filename.data, texture->absolute_filename.length)) }) {
                if (candidate.empty() || !fs::is_regular_file(candidate, ec)) continue;
                std::string relative = ToRelative(candidate);
                if (!relative.empty() && !fs::u8path(relative).is_absolute() && !relative.starts_with("..")) { result = relative; break; }
                const fs::path out = folder / fs::u8path(fileName);
                fs::copy_file(candidate, out, fs::copy_options::overwrite_existing, ec);
                if (!ec) { result = ToRelative(out); break; }
            }
        }
        texturePaths[texture] = result;
        return result;
    };

    json data = json::array();
    std::unordered_set<std::string> materialNames;
    std::set<std::string> listedTextures;
    auto list = [&](const std::string& path, const std::string& name, u32 slot, bool material) {
        data.push_back({ { "path", path }, { "name", name }, { "slot", slot }, { "material", material } });
    };
    for (ufbx_material* material : scene->materials) {
        ufbx_texture* base = material->pbr.base_color.texture ? material->pbr.base_color.texture : material->fbx.diffuse_color.texture;
        ufbx_texture* normal = material->pbr.normal_map.texture ? material->pbr.normal_map.texture : material->fbx.normal_map.texture ? material->fbx.normal_map.texture : material->fbx.bump.texture;
        const std::string albedoPath = exportTexture(base), normalPath = exportTexture(normal);

        std::string stem = SafeFileName(std::string(material->name.data, material->name.length)), unique = stem;
        for (int n = 1; !materialNames.insert(unique).second; ++n) unique = stem + "_" + std::to_string(n);
        const std::string path = ToRelative(folder / fs::u8path(unique + ".mat"));
        auto created = CreateMaterial(path);
        created->albedoMap = albedoPath;
        created->normalMap = normalPath;
        if (albedoPath.empty() && material->pbr.base_color.has_value) {
            const auto c = material->pbr.base_color.value_vec4;
            created->color = glm::clamp(glm::vec4((float)c.x, (float)c.y, (float)c.z, 1.0f), glm::vec4(0), glm::vec4(1));
        }
        if (material->pbr.roughness.has_value) created->roughness = std::clamp((float)material->pbr.roughness.value_real, 0.05f, 1.0f);
        if (material->pbr.metalness.has_value) created->metallic = std::clamp((float)material->pbr.metalness.value_real, 0.0f, 1.0f);
        SaveMaterial(*created);
        list(path, unique, material->typed_id, true);
    }
    for (auto& [texture, path] : texturePaths)
        if (!path.empty() && listedTextures.insert(path).second) list(path, FileSystem::Utf8(fs::u8path(path).filename()), 0, false);
    // Textures the materials don't use are still part of the file
    for (ufbx_texture* texture : scene->textures) {
        const std::string path = exportTexture(texture);
        if (!path.empty() && listedTextures.insert(path).second) list(path, FileSystem::Utf8(fs::u8path(path).filename()), 0, false);
    }
    FileSystem::WriteText(manifest, data.dump(2));
    readManifest();
    return items;
}

std::vector<FbxTakeDesc> AssetManager::GetFbxTakes(const std::string& asset){std::vector<FbxTakeDesc> takes;auto cached=CachedFbx(ToAbsolute(asset));if(!cached)return takes;for(auto* stack:cached->anim_stacks){float duration=(float)(stack->anim->time_end-stack->anim->time_begin);if(duration>0)takes.push_back({stack->name.data,duration});}return takes;}
std::vector<FbxBoneDesc> AssetManager::GetFbxSkeleton(const std::string& asset,float time,const std::string& animation){auto cached=CachedFbx(ToAbsolute(asset));if(!cached)return {};auto* scene=cached.get();std::shared_ptr<ufbx_scene> evaluated;if(time>=0&&scene->anim_stacks.count){auto* anim=scene->anim_stacks.data[0]->anim;for(auto* stack:scene->anim_stacks)if(animation==stack->name.data)anim=stack->anim;double duration=anim->time_end-anim->time_begin;double t=anim->time_begin+(duration>0?std::fmod(std::max(0.f,time),duration):0);evaluated=std::shared_ptr<ufbx_scene>(ufbx_evaluate_scene(scene,anim,t,nullptr,nullptr),ufbx_free_scene);if(evaluated)scene=evaluated.get();}
 std::unordered_set<ufbx_node*> needed;auto include=[&](ufbx_node* node){while(node&&node!=scene->root_node){needed.insert(node);node=node->parent;}};for(auto* node:scene->nodes)if(node->bone)include(node);for(auto* skin:scene->skin_deformers)for(auto* cluster:skin->clusters)include(cluster->bone_node);
 std::vector<FbxBoneDesc> result;std::function<void(ufbx_node*,int)> walk=[&](ufbx_node* node,int parent){int next=parent;if(needed.contains(node)){FbxBoneDesc bone;bone.name=node->name.data;bone.nodeId=node->typed_id;bone.parent=parent;bone.world=FbxMatrix(node->node_to_world);bone.local=parent>=0?glm::inverse(result[parent].world)*bone.world:bone.world;next=(int)result.size();result.push_back(bone);}for(auto* child:node->children)walk(child,next);};walk(scene->root_node,-1);return result;}
// ---------------------------------------------------------------- GPU skinning
namespace {
struct SkinJoint { u32 nodeId = 0; glm::mat4 offset{ 1 }, fallback{ 1 }; };
struct SkinInfluence { u32 joints[2] = { 0, 0 }; glm::vec4 weights{ 1, 0, 0, 0 }; };
static_assert(sizeof(SkinInfluence) == 24);
struct SkinnedSource { Ref<Mesh> rest; Ref<RHI::Buffer> influences; std::vector<SkinJoint> joints; float padding = 0; };
struct SkinnedInstance {
    Ref<Mesh> mesh;
    std::array<Ref<RHI::Buffer>, 2> vertices, palettes;
    u64 frame = ~0ull, hash = 0;
    int written = -1;
};
struct SkinDispatch { u32 source, influences, palette, output, count; };

std::unordered_map<std::string, std::shared_ptr<SkinnedSource>> s_SkinSources;
std::unordered_map<std::string, SkinnedInstance> s_SkinInstances;
std::vector<SkinDispatch> s_SkinQueue;
Ref<RHI::Pipeline> s_SkinPipeline;

// Rest pose vertices in model space plus up to four weighted joints per vertex
std::shared_ptr<SkinnedSource> BuildSkinnedSource(RHI::Device& device, const fs::path& path) {
    auto cached = CachedFbx(path);
    if (!cached) return nullptr;
    auto source = std::make_shared<SkinnedSource>();
    source->joints.push_back({}); // joint 0: vertices that follow no bone
    std::unordered_map<const ufbx_skin_cluster*, u32> jointOf;
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    std::vector<SkinInfluence> influences;
    std::vector<SubMesh> parts;
    for (auto* node : cached->nodes) {
        auto* mesh = node->mesh;
        if (!mesh) continue;
        const glm::mat4 fromModel = glm::inverse(FbxMatrix(node->geometry_to_world));
        const auto normalMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);
        ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        std::vector<uint32_t> triangles(mesh->max_face_triangles * 3);
        size_t faceId = 0;
        for (auto face : mesh->faces) {
            u32 slot = 0;
            std::string name = "Default";
            if (faceId < mesh->face_material.count) {
                auto local = mesh->face_material.data[faceId];
                if (local < node->materials.count) { auto* material = node->materials.data[local]; slot = material->typed_id; name = material->name.data; }
            }
            ++faceId;
            if (parts.empty() || parts.back().materialSlot != slot) parts.push_back({ (u32)indices.size(), 0, slot, name });
            size_t count = ufbx_triangulate_face(triangles.data(), triangles.size(), mesh, face) * 3;
            parts.back().indexCount += (u32)count;
            for (size_t i = 0; i < count; ++i) {
                const auto ix = triangles[i];
                auto p = ufbx_transform_position(&node->geometry_to_world, ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                Vertex v{};
                v.position = { (float)p.x, (float)p.y, (float)p.z };
                if (mesh->vertex_normal.exists) {
                    auto n = ufbx_transform_direction(&normalMatrix, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix));
                    v.normal = glm::normalize(glm::vec3((float)n.x, (float)n.y, (float)n.z));
                }
                if (mesh->vertex_uv.exists) { auto uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix); v.uv = { (float)uv.x, 1.0f - (float)uv.y }; }

                u16 joints[4] = {};
                float weights[4] = {}, total = 0;
                int used = 0;
                const auto vertexIndex = mesh->vertex_indices.data[ix];
                if (skin && vertexIndex < skin->vertices.count) {
                    const auto info = skin->vertices.data[vertexIndex];
                    for (u32 k = 0; k < info.num_weights && used < 4; ++k) { // ufbx sorts them, largest first
                        const auto weight = skin->weights.data[info.weight_begin + k];
                        auto* cluster = skin->clusters.data[weight.cluster_index];
                        if (!cluster->bone_node) continue;
                        auto [entry, added] = jointOf.try_emplace(cluster, (u32)source->joints.size());
                        if (added) source->joints.push_back({ cluster->bone_node->typed_id, FbxMatrix(cluster->geometry_to_bone) * fromModel, FbxMatrix(cluster->bone_node->node_to_world) });
                        joints[used] = (u16)entry->second;
                        weights[used] = (float)weight.weight;
                        total += weights[used];
                        ++used;
                    }
                }
                SkinInfluence influence;
                if (total > 0) {
                    influence.joints[0] = joints[0] | ((u32)joints[1] << 16);
                    influence.joints[1] = joints[2] | ((u32)joints[3] << 16);
                    influence.weights = glm::vec4(weights[0], weights[1], weights[2], weights[3]) / total;
                }
                influences.push_back(influence);
                indices.push_back((u32)vertices.size());
                vertices.push_back(v);
            }
        }
    }
    if (vertices.empty()) return nullptr;
    ComputeNormalsAndTangents(vertices, indices, glm::length(vertices.front().normal) < 0.001f);
    source->rest = MakeRef<Mesh>(device, vertices, indices, FileSystem::Utf8(path.filename()) + ".Rest");
    source->rest->SetSubMeshes(std::move(parts));
    source->influences = device.CreateBuffer({ influences.size() * sizeof(SkinInfluence), RHI::BufferUsage::Storage, RHI::MemoryUsage::GPUOnly, "Skin.Influences" }, influences.data());
    source->padding = glm::compMax(source->rest->Bounds().max - source->rest->Bounds().min) * 0.35f;
    return source;
}
}

static void ClearSkinning() { s_SkinInstances.clear(); s_SkinSources.clear(); s_SkinQueue.clear(); s_SkinPipeline.reset(); }

void AssetManager::FlushSkinning(RHI::CommandList& cmd) {
    if (s_SkinQueue.empty() || !s_Device) return;
    if (!s_SkinPipeline) s_SkinPipeline = s_Device->CreateComputePipeline({ "Skinning.cs", "Skinning" });
    cmd.SetPipeline(s_SkinPipeline.get());
    for (const SkinDispatch& job : s_SkinQueue) {
        cmd.Push(job);
        cmd.Dispatch((job.count + 63) / 64);
    }
    s_SkinQueue.clear();
    cmd.MemoryBarrier();
    // Instances of scenes that are gone (an animation preview copy, a closed scene) free their buffers after a while
    const u64 now = s_Device->FrameNumber();
    if (now % 600 == 0) std::erase_if(s_SkinInstances, [&](const auto& item) { return item.second.frame + 600 < now; });
}

Ref<Mesh> AssetManager::GetPosedFbx(const std::string& asset, u64 instance, const std::unordered_map<u32, glm::mat4>& pose) {
    if (!s_Device) return nullptr;
    auto& source = s_SkinSources[asset];
    if (!source) source = BuildSkinnedSource(*s_Device, ToAbsolute(asset));
    if (!source) return nullptr;

    auto& state = s_SkinInstances[asset + "|" + std::to_string(instance)];
    const u32 vertexCount = source->rest->VertexCount();
    if (!state.mesh) {
        state.mesh = source->rest->CloneForSkinning();
        for (int i = 0; i < 2; ++i) {
            state.vertices[i] = s_Device->CreateBuffer({ vertexCount * sizeof(Vertex), RHI::BufferUsage::Storage, RHI::MemoryUsage::GPUOnly, "Skin.Vertices" });
            state.palettes[i] = s_Device->CreateBuffer({ source->joints.size() * sizeof(glm::mat4), RHI::BufferUsage::Storage, RHI::MemoryUsage::CPUToGPU, "Skin.Palette" });
        }
    }
    // The Scene and Game views both submit the object: one deformation per frame is enough
    if (state.frame == s_Device->FrameNumber()) return state.mesh;
    state.frame = s_Device->FrameNumber();

    u64 hash = 14695981039346656037ull;
    std::vector<u32> order;
    order.reserve(pose.size());
    for (auto& [id, matrix] : pose) order.push_back(id);
    std::sort(order.begin(), order.end());
    for (u32 id : order) {
        hash ^= id; hash *= 1099511628211ull;
        for (u8 byte : std::span((const u8*)&pose.at(id), sizeof(glm::mat4))) { hash ^= byte; hash *= 1099511628211ull; }
    }
    if (state.written >= 0 && state.hash == hash) return state.mesh;
    state.hash = hash;

    // Alternating buffers means the GPU never reads a buffer while a newer frame rewrites it
    const int slot = state.written == 0 ? 1 : 0;
    state.written = slot;
    glm::mat4* palette = state.palettes[slot]->As<glm::mat4>();
    AABB bounds{ glm::vec3(FLT_MAX), glm::vec3(-FLT_MAX) };
    for (size_t j = 0; j < source->joints.size(); ++j) {
        const SkinJoint& joint = source->joints[j];
        auto found = pose.find(joint.nodeId);
        const glm::mat4& world = found != pose.end() ? found->second : joint.fallback;
        palette[j] = j == 0 ? glm::mat4(1) : world * joint.offset;
        if (j) { bounds.min = glm::min(bounds.min, glm::vec3(world[3])); bounds.max = glm::max(bounds.max, glm::vec3(world[3])); }
    }
    if (source->joints.size() > 1) {
        bounds.min -= glm::vec3(source->padding);
        bounds.max += glm::vec3(source->padding);
        state.mesh->SetBounds(bounds);
    }
    state.mesh->SetVertexBuffer(state.vertices[slot]);
    s_SkinQueue.push_back({ source->rest->VertexBuffer()->Index(), source->influences->Index(), state.palettes[slot]->Index(), state.vertices[slot]->Index(), vertexCount });
    return state.mesh;
}
Ref<Mesh> AssetManager::GetAnimatedFbx(const std::string& ref, float time, const std::string& animation) {
    if (!s_Device || Lower(fs::path(ref).extension().string()) != ".fbx") return GetMesh(ref);
    int frame = (int)(std::max(0.0f,time) * 30);
    auto key = ref + "|" + animation;
    auto& cached = s_AnimatedMeshes[key];
    if (cached.frame != frame || !cached.mesh) {
        cached.mesh = ImportFbx(ToAbsolute(ref),frame / 30.0f,animation); cached.frame = frame;
    }
    return cached.mesh;
}

Ref<Mesh> AssetManager::ImportObj(const fs::path& path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;
    struct Materials : tinyobj::MaterialReader { fs::path root;explicit Materials(fs::path p):root(p){} bool operator()(const std::string& name,std::vector<tinyobj::material_t>* mats,std::map<std::string,int>* ids,std::string* warn,std::string* err) override {std::istringstream in(FileSystem::ReadText(root/fs::u8path(name)));tinyobj::LoadMtl(ids,mats,&in,warn,err);return true;} } reader(path.parent_path());
    std::istringstream obj(FileSystem::ReadText(path));
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &obj, &reader, true)) {
        SD_ERROR("OBJ import failed: {} {}", path.string(), err);
        return nullptr;
    }
    std::vector<Vertex> verts;
    std::vector<u32> idx;std::vector<SubMesh> parts;
    std::unordered_map<u64, u32> dedup;
    bool hasNormals = !attrib.normals.empty();
    for (auto& s : shapes){size_t face=0,inFace=0;
        for (auto& i : s.mesh.indices) {
            u32 slot=face<s.mesh.material_ids.size()?std::max(0,s.mesh.material_ids[face]):0;
            if(parts.empty()||parts.back().materialSlot!=slot)parts.push_back({(u32)idx.size(),0,slot,slot<materials.size()?materials[slot].name:"Default"});parts.back().indexCount++;if(++inFace==3){++face;inFace=0;}
            u64 key = ((u64)(u32)i.vertex_index << 42) ^ ((u64)(u32)(i.normal_index + 1) << 21) ^ (u64)(u32)(i.texcoord_index + 1);
            auto it = dedup.find(key);
            if (it != dedup.end()) { idx.push_back(it->second); continue; }
            Vertex v{};
            v.position = { attrib.vertices[3 * i.vertex_index], attrib.vertices[3 * i.vertex_index + 1], attrib.vertices[3 * i.vertex_index + 2] };
            if (i.normal_index >= 0) v.normal = { attrib.normals[3 * i.normal_index], attrib.normals[3 * i.normal_index + 1], attrib.normals[3 * i.normal_index + 2] };
            if (i.texcoord_index >= 0) v.uv = { attrib.texcoords[2 * i.texcoord_index], 1.0f - attrib.texcoords[2 * i.texcoord_index + 1] };
            dedup[key] = (u32)verts.size();
            idx.push_back((u32)verts.size());
            verts.push_back(v);
        }
    }
    if (verts.empty()) return nullptr;
    ComputeNormalsAndTangents(verts, idx, !hasNormals);
    auto result=MakeRef<Mesh>(*s_Device, verts, idx, FileSystem::Utf8(path.filename()));result->SetSubMeshes(std::move(parts));return result;
}

Ref<Mesh> AssetManager::ImportGltf(const fs::path& path) {
    cgltf_options opt{};
    opt.file.read=[](const cgltf_memory_options*,const cgltf_file_options*,const char* path,cgltf_size* size,void** data){try{auto bytes=FileSystem::ReadBinary(fs::u8path(path));if(bytes.empty())return cgltf_result_file_not_found;*size=bytes.size();*data=malloc(bytes.size());if(!*data)return cgltf_result_out_of_memory;memcpy(*data,bytes.data(),bytes.size());return cgltf_result_success;}catch(...){return cgltf_result_io_error;}};
    opt.file.release=[](const cgltf_memory_options*,const cgltf_file_options*,void* data){free(data);};
    cgltf_data* data = nullptr;
    std::string p = FileSystem::Utf8(path);
    if (cgltf_parse_file(&opt, p.c_str(), &data) != cgltf_result_success) { SD_ERROR("glTF parse failed: {}", p); return nullptr; }
    if (cgltf_load_buffers(&opt, data, p.c_str()) != cgltf_result_success) { SD_ERROR("glTF buffers failed: {}", p); cgltf_free(data); return nullptr; }

    std::vector<Vertex> verts;
    std::vector<u32> idx;std::vector<SubMesh> parts;
    bool needNormals = false, needTangents = false;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        cgltf_node* node = &data->nodes[n];
        if (!node->mesh) continue;
        float m[16];
        cgltf_node_transform_world(node, m);
        glm::mat4 world = glm::make_mat4(m);
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(world)));
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *tng = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                auto& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
                else if (at.type == cgltf_attribute_type_tangent) tng = at.data;
            }
            if (!pos) continue;
            u32 first=(u32)idx.size();
            u32 base = (u32)verts.size();
            for (cgltf_size v = 0; v < pos->count; ++v) {
                Vertex vx{};
                float f[4] = { 0, 0, 0, 1 };
                cgltf_accessor_read_float(pos, v, f, 3);
                vx.position = glm::vec3(world * glm::vec4(f[0], f[1], f[2], 1.0f));
                if (nrm) { cgltf_accessor_read_float(nrm, v, f, 3); vx.normal = glm::normalize(normalMat * glm::vec3(f[0], f[1], f[2])); }
                if (uv) { cgltf_accessor_read_float(uv, v, f, 2); vx.uv = { f[0], f[1] }; }
                if (tng) { cgltf_accessor_read_float(tng, v, f, 4); vx.tangent = glm::vec4(glm::normalize(glm::mat3(world) * glm::vec3(f[0], f[1], f[2])), f[3]); }
                verts.push_back(vx);
            }
            if (prim.indices)
                for (cgltf_size k = 0; k < prim.indices->count; ++k) idx.push_back(base + (u32)cgltf_accessor_read_index(prim.indices, k));
            else
                for (cgltf_size k = 0; k < pos->count; ++k) idx.push_back(base + (u32)k);
            u32 slot=prim.material?(u32)(prim.material-data->materials):0;parts.push_back({first,(u32)idx.size()-first,slot,prim.material&&prim.material->name?prim.material->name:"Default"});
            needNormals |= !nrm;
            needTangents |= !tng;
        }
    }
    cgltf_free(data);
    if (verts.empty()) { SD_ERROR("glTF has no triangle meshes: {}", p); return nullptr; }
    if (needNormals || needTangents) ComputeNormalsAndTangents(verts, idx, needNormals);
    auto result=MakeRef<Mesh>(*s_Device, verts, idx, FileSystem::Utf8(path.filename()));result->SetSubMeshes(std::move(parts));return result;
}

Ref<Mesh> AssetManager::GetMesh(const std::string& ref) {
    if (ref.starts_with("builtin:")) {
        std::string name = ref.substr(8);
        for (u32 t = 0; t < (u32)PrimitiveType::Count; ++t)
            if (name == PrimitiveName((PrimitiveType)t)) return MeshLibrary::Get((PrimitiveType)t);
        return MeshLibrary::Get(PrimitiveType::Cube);
    }
    if (ref.empty() || !s_Device) return nullptr;
    if (auto it = s_Meshes.find(ref); it != s_Meshes.end()) return it->second;
    fs::path abs = ToAbsolute(ref);
    Ref<Mesh> mesh;
    std::string ext = Lower(abs.extension().string());
    if (!FileSystem::Exists(abs)) SD_WARN("Mesh not found: {}", ref);
    else if (ext == ".obj") mesh = ImportObj(abs);
    else if (ext == ".gltf" || ext == ".glb") mesh = ImportGltf(abs);
    else if (ext == ".fbx") mesh = ImportFbx(abs);
    if (mesh) SD_INFO("Mesh imported: {} ({} vertices, {} triangles)", ref, mesh->VertexCount(), mesh->IndexCount() / 3);
    s_Meshes[ref] = mesh;
    return mesh;
}

// ------------------------------------------------------------------------------------------------ materials

Ref<Material> AssetManager::GetMaterial(const std::string& path) {
    if (path.empty()) return nullptr;
    if (auto it = s_Materials.find(path); it != s_Materials.end()) return it->second;
    std::string text = FileSystem::ReadText(ToAbsolute(path));
    Ref<Material> m;
    if (!text.empty()) {
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_discarded()) {
            m = MakeRef<Material>();
            m->FromJson(j);
            m->assetPath = path;
        }
    }
    if (!m) SD_WARN("Material not found: {}", path);
    s_Materials[path] = m;
    return m;
}

Ref<Material> AssetManager::CreateMaterial(const std::string& path) {
    auto m = MakeRef<Material>();
    m->name = fs::path(path).stem().string();
    m->assetPath = path;
    SaveMaterial(*m);
    s_Materials[path] = m;
    return m;
}

bool AssetManager::SaveMaterial(Material& m) {
    if (m.assetPath.empty()) return false;
    nlohmann::ordered_json j;
    nlohmann::json tmp;
    m.ToJson(tmp);
    bool ok = FileSystem::WriteText(ToAbsolute(m.assetPath), tmp.dump(2));
    if (ok) m.dirty = false;
    return ok;
}

void AssetManager::SaveDirtyMaterials() {
    for (auto& [p, m] : s_Materials)
        if (m && m->dirty) SaveMaterial(*m);
}

} // namespace Stardust
