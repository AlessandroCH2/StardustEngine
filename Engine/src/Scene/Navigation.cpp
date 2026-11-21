#include "Scene/Navigation.h"
#include "Scene/Scene.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include "Project/Project.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_map>
#include <memory>
#include <cstring>
#include <chrono>
#include <format>
#include <fstream>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#pragma warning(push, 0)
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <DetourCommon.h>
#pragma warning(pop)

namespace Stardust::Navigation {
using json = nlohmann::json;

namespace {
constexpr u32 kMagic = 0x4D4E4453; // "SDNM"
constexpr u32 kVersion = 1;
constexpr int kMaxAgents = 1024;

struct NavMeshDeleter { void operator()(dtNavMesh* m) const { dtFreeNavMesh(m); } };
struct QueryDeleter { void operator()(dtNavMeshQuery* q) const { dtFreeNavMeshQuery(q); } };
struct CrowdDeleter { void operator()(dtCrowd* c) const { dtFreeCrowd(c); } };

struct AgentSlot { int index = -1; glm::vec3 written{ 0.0f }; };

struct State {
    std::unique_ptr<dtNavMesh, NavMeshDeleter> mesh;
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query;
    std::unique_ptr<dtCrowd, CrowdDeleter> crowd;
    float crowdRadius = 0.0f;
    std::string source;          // asset path it was loaded from ("" = built in memory)
    std::unordered_map<EntityID, AgentSlot> agents;
    std::vector<glm::vec3> debugTriangles;
    bool debugDirty = true;
    std::string info;
};
std::unordered_map<Scene*, State> s_States;
bool s_DebugDraw = true;

State& Get(Scene& scene) { return s_States[&scene]; }

// ---------------------------------------------------------------------------------------------- geometry
struct Geometry {
    std::vector<float> verts;   // x y z
    std::vector<int> tris;
    glm::vec3 min{ FLT_MAX }, max{ -FLT_MAX };
    void Add(const glm::vec3& p) { verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z); min = glm::min(min, p); max = glm::max(max, p); }
    int Count() const { return (int)verts.size() / 3; }
    glm::vec3 Vert(int i) const { return { verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2] }; }
};

void AddBox(Geometry& g, const glm::mat4& m) {
    const int base = g.Count();
    for (int i = 0; i < 8; ++i) g.Add(glm::vec3(m * glm::vec4((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f)));
    const int faces[12][3] = { {0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5} };
    for (auto& f : faces) { g.tris.push_back(base + f[0]); g.tris.push_back(base + f[1]); g.tris.push_back(base + f[2]); }
}

void Collect(Scene& scene, const NavMeshSurfaceComponent& surface, Geometry& g) {
    Registry& reg = scene.Reg();
    for (EntityID e : reg.AllEntities()) {
        if (!scene.IsActiveInHierarchy(e)) continue;
        const int layer = reg.Get<NameComponent>(e).layer;
        if (!(surface.layerMask & (1u << std::clamp(layer, 0, 31)))) continue;
        // Things that move are not part of the walkable world
        if (reg.TryGetEnabled<NavMeshAgentComponent>(e) || reg.TryGetEnabled<CharacterControllerComponent>(e)) continue;
        if (auto* rb = reg.TryGetEnabled<RigidbodyComponent>(e); rb && rb->type == BodyType::Dynamic) continue;
        const glm::mat4 world = scene.GetWorldMatrix(e);
        if (auto* terrain = reg.TryGetEnabled<TerrainComponent>(e)) {
            auto data = TerrainData::Find(reg.Get<IDComponent>(e).uuid);
            if (data && data->Resolution() > 1) {
                const u32 res = data->Resolution();
                const float size = data->Settings().size, step = size / (res - 1);
                const glm::vec3 origin = glm::vec3(world[3]) + glm::vec3(-size * 0.5f, 0, -size * 0.5f);
                // Coarser than the height map when it is very dense: Recast voxelizes anyway
                const u32 stride = std::max(1u, (u32)std::floor(surface.cellSize / std::max(step, 1e-4f)));
                const int base = g.Count();
                u32 n = 0;
                for (u32 z = 0; z < res; z += stride, ++n) for (u32 x = 0; x < res; x += stride)
                    g.Add(origin + glm::vec3(x * step, data->Heights()[z * res + x], z * step));
                const u32 w = (res - 1) / stride + 1;
                for (u32 z = 0; z + 1 < n; ++z) for (u32 x = 0; x + 1 < w; ++x) {
                    int i = base + (int)(z * w + x);
                    g.tris.insert(g.tris.end(), { i, i + (int)w, i + 1, i + 1, i + (int)w, i + (int)w + 1 });
                }
            }
            (void)terrain;
            continue;
        }
        if (surface.collectFrom == 1) {
            if (auto* box = reg.TryGetEnabled<BoxColliderComponent>(e); box && !box->material.isTrigger) {
                AddBox(g, world * glm::translate(glm::mat4(1), box->center) * glm::scale(glm::mat4(1), box->size)); continue;
            }
            if (auto* sphere = reg.TryGetEnabled<SphereColliderComponent>(e); sphere && !sphere->material.isTrigger) {
                AddBox(g, world * glm::translate(glm::mat4(1), sphere->center) * glm::scale(glm::mat4(1), glm::vec3(sphere->radius * 1.6f))); continue;
            }
            if (auto* capsule = reg.TryGetEnabled<CapsuleColliderComponent>(e); capsule && !capsule->material.isTrigger) {
                AddBox(g, world * glm::translate(glm::mat4(1), capsule->center) * glm::scale(glm::mat4(1), glm::vec3(capsule->radius * 1.6f, capsule->height, capsule->radius * 1.6f))); continue;
            }
            if (!reg.TryGetEnabled<MeshColliderComponent>(e)) continue;
        }
        auto* mr = reg.TryGetEnabled<MeshRendererComponent>(e);
        if (!mr) continue;
        auto mesh = AssetManager::GetMesh(mr->mesh);
        if (!mesh || mesh->Positions().empty()) continue;
        const int base = g.Count();
        for (auto& p : mesh->Positions()) g.Add(glm::vec3(world * glm::vec4(p, 1.0f)));
        const bool flip = glm::determinant(glm::mat3(world)) < 0.0f;
        auto& idx = mesh->Indices();
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            g.tris.push_back(base + (int)idx[i]);
            g.tris.push_back(base + (int)idx[i + (flip ? 2 : 1)]);
            g.tris.push_back(base + (int)idx[i + (flip ? 1 : 2)]);
        }
    }
}

// ---------------------------------------------------------------------------------------------- build
struct BuildResult { std::vector<std::pair<std::pair<int, int>, std::vector<u8>>> tiles; dtNavMeshParams params{}; int polys = 0; };

bool Build(Scene& scene, const NavMeshSurfaceComponent& s, BuildResult& out, std::string& error) {
    Geometry g;
    Collect(scene, s, g);
    if (g.tris.empty()) { error = "no geometry to bake (Mesh Renderers, colliders or terrains)"; return false; }
    glm::vec3 bmin = g.min, bmax = g.max;
    if (s.useBounds) { bmin = glm::max(bmin, s.boundsCenter - s.boundsSize * 0.5f); bmax = glm::min(bmax, s.boundsCenter + s.boundsSize * 0.5f); }
    if (glm::any(glm::lessThanEqual(bmax, bmin))) { error = "the bounds contain no geometry"; return false; }

    const float cs = s.cellSize, ch = s.cellHeight;
    const int walkableHeight = (int)std::ceil(s.agentHeight / ch);
    const int walkableClimb = (int)std::floor(s.stepHeight / ch);
    const int walkableRadius = (int)std::ceil(s.agentRadius / cs);
    const int tileSize = s.tileSize;
    const int borderSize = walkableRadius + 3;
    int gw = 0, gh = 0;
    rcCalcGridSize(&bmin.x, &bmax.x, cs, &gw, &gh);
    const int tw = (gw + tileSize - 1) / tileSize, th = (gh + tileSize - 1) / tileSize;
    const float tileWorld = tileSize * cs;
    if ((i64)tw * th > (1 << 20)) { error = "too many tiles: raise the cell size or the tile size"; return false; }

    int tileBits = 1; while ((1 << tileBits) < tw * th) ++tileBits;
    tileBits = std::min(tileBits, 14);
    const int polyBits = 22 - tileBits;
    out.params = {};
    std::memcpy(out.params.orig, &bmin.x, sizeof(float) * 3);
    out.params.tileWidth = out.params.tileHeight = tileWorld;
    out.params.maxTiles = 1 << tileBits;
    out.params.maxPolys = 1 << polyBits;

    // Triangles per tile column (by their XZ bounds), so each tile rasterizes only its own
    const int triCount = (int)g.tris.size() / 3;
    std::vector<std::vector<int>> buckets((size_t)tw * th);
    for (int t = 0; t < triCount; ++t) {
        glm::vec3 a = g.Vert(g.tris[t * 3]), b = g.Vert(g.tris[t * 3 + 1]), c = g.Vert(g.tris[t * 3 + 2]);
        glm::vec3 lo = glm::min(a, glm::min(b, c)), hi = glm::max(a, glm::max(b, c));
        const float pad = borderSize * cs;
        int x0 = std::max(0, (int)std::floor((lo.x - pad - bmin.x) / tileWorld)), x1 = std::min(tw - 1, (int)std::floor((hi.x + pad - bmin.x) / tileWorld));
        int z0 = std::max(0, (int)std::floor((lo.z - pad - bmin.z) / tileWorld)), z1 = std::min(th - 1, (int)std::floor((hi.z + pad - bmin.z) / tileWorld));
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) buckets[(size_t)z * tw + x].push_back(t);
    }

    rcContext ctx(false);
    std::vector<int> tileTris;
    std::vector<unsigned char> areas;
    for (int ty = 0; ty < th; ++ty) for (int tx = 0; tx < tw; ++tx) {
        auto& bucket = buckets[(size_t)ty * tw + tx];
        if (bucket.empty()) continue;
        rcConfig cfg{};
        cfg.cs = cs; cfg.ch = ch;
        cfg.walkableSlopeAngle = std::clamp(s.maxSlope, 0.0f, 89.0f);
        cfg.walkableHeight = walkableHeight; cfg.walkableClimb = walkableClimb; cfg.walkableRadius = walkableRadius;
        cfg.maxEdgeLen = (int)(12.0f / cs);
        cfg.maxSimplificationError = 1.3f;
        cfg.minRegionArea = (int)rcSqr(std::sqrt(std::max(0.0f, s.minRegionArea)) / cs);
        cfg.mergeRegionArea = (int)rcSqr(20.0f / cs);
        cfg.maxVertsPerPoly = 6;
        cfg.tileSize = tileSize; cfg.borderSize = borderSize;
        cfg.width = cfg.height = tileSize + borderSize * 2;
        cfg.detailSampleDist = cs * 6.0f;
        cfg.detailSampleMaxError = ch * 1.0f;
        cfg.bmin[0] = bmin.x + tx * tileWorld - borderSize * cs; cfg.bmin[1] = bmin.y; cfg.bmin[2] = bmin.z + ty * tileWorld - borderSize * cs;
        cfg.bmax[0] = bmin.x + (tx + 1) * tileWorld + borderSize * cs; cfg.bmax[1] = bmax.y; cfg.bmax[2] = bmin.z + (ty + 1) * tileWorld + borderSize * cs;

        std::unique_ptr<rcHeightfield, void(*)(rcHeightfield*)> hf(rcAllocHeightfield(), rcFreeHeightField);
        if (!hf || !rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) continue;
        tileTris.clear();
        for (int t : bucket) { tileTris.push_back(g.tris[t * 3]); tileTris.push_back(g.tris[t * 3 + 1]); tileTris.push_back(g.tris[t * 3 + 2]); }
        const int nt = (int)tileTris.size() / 3;
        areas.assign(nt, 0);
        rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, g.verts.data(), g.Count(), tileTris.data(), nt, areas.data());
        if (!rcRasterizeTriangles(&ctx, g.verts.data(), g.Count(), tileTris.data(), areas.data(), nt, *hf, cfg.walkableClimb)) continue;
        rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf);
        rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
        rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf);
        std::unique_ptr<rcCompactHeightfield, void(*)(rcCompactHeightfield*)> chf(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        if (!rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf, *chf)) continue;
        hf.reset();
        if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) continue;
        if (!rcBuildDistanceField(&ctx, *chf)) continue;
        if (!rcBuildRegions(&ctx, *chf, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea)) continue;
        std::unique_ptr<rcContourSet, void(*)(rcContourSet*)> cset(rcAllocContourSet(), rcFreeContourSet);
        if (!rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset) || cset->nconts == 0) continue;
        std::unique_ptr<rcPolyMesh, void(*)(rcPolyMesh*)> pmesh(rcAllocPolyMesh(), rcFreePolyMesh);
        if (!rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) continue;
        std::unique_ptr<rcPolyMeshDetail, void(*)(rcPolyMeshDetail*)> dmesh(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
        if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) continue;
        if (pmesh->nverts >= 0xffff || pmesh->npolys == 0) continue;
        for (int i = 0; i < pmesh->npolys; ++i) { if (pmesh->areas[i] == RC_WALKABLE_AREA) pmesh->areas[i] = 0; pmesh->flags[i] = 1; }
        dtNavMeshCreateParams p{};
        p.verts = pmesh->verts; p.vertCount = pmesh->nverts; p.polys = pmesh->polys; p.polyAreas = pmesh->areas; p.polyFlags = pmesh->flags;
        p.polyCount = pmesh->npolys; p.nvp = pmesh->nvp;
        p.detailMeshes = dmesh->meshes; p.detailVerts = dmesh->verts; p.detailVertsCount = dmesh->nverts; p.detailTris = dmesh->tris; p.detailTriCount = dmesh->ntris;
        p.walkableHeight = s.agentHeight; p.walkableRadius = s.agentRadius; p.walkableClimb = s.stepHeight;
        p.tileX = tx; p.tileY = ty; p.tileLayer = 0;
        rcVcopy(p.bmin, pmesh->bmin); rcVcopy(p.bmax, pmesh->bmax);
        p.cs = cfg.cs; p.ch = cfg.ch; p.buildBvTree = true;
        unsigned char* data = nullptr; int size = 0;
        if (!dtCreateNavMeshData(&p, &data, &size)) continue;
        out.polys += pmesh->npolys;
        out.tiles.push_back({ { tx, ty }, std::vector<u8>(data, data + size) });
        dtFree(data);
    }
    if (out.tiles.empty()) { error = "nothing walkable found (check Max Slope, Agent Height and the geometry)"; return false; }
    return true;
}

std::vector<u8> Serialize(const BuildResult& r) {
    std::vector<u8> bytes;
    auto put = [&](const void* data, size_t size) { auto* p = (const u8*)data; bytes.insert(bytes.end(), p, p + size); };
    u32 header[3] = { kMagic, kVersion, (u32)r.tiles.size() };
    put(header, sizeof(header));
    put(&r.params, sizeof(r.params));
    for (auto& [xy, data] : r.tiles) { i32 t[3] = { xy.first, xy.second, (i32)data.size() }; put(t, sizeof(t)); put(data.data(), data.size()); }
    return bytes;
}

bool Load(State& st, const std::vector<u8>& bytes, std::string& error) {
    st.mesh.reset(); st.query.reset(); st.crowd.reset(); st.agents.clear(); st.debugDirty = true;
    if (bytes.size() < 12 + sizeof(dtNavMeshParams)) { error = "file too small"; return false; }
    u32 header[3]; std::memcpy(header, bytes.data(), sizeof(header));
    if (header[0] != kMagic || header[1] != kVersion) { error = "not a Stardust nav mesh"; return false; }
    dtNavMeshParams params; std::memcpy(&params, bytes.data() + 12, sizeof(params));
    std::unique_ptr<dtNavMesh, NavMeshDeleter> mesh(dtAllocNavMesh());
    if (!mesh || dtStatusFailed(mesh->init(&params))) { error = "nav mesh init failed"; return false; }
    size_t at = 12 + sizeof(params);
    for (u32 i = 0; i < header[2]; ++i) {
        if (at + 12 > bytes.size()) { error = "truncated"; return false; }
        i32 t[3]; std::memcpy(t, bytes.data() + at, 12); at += 12;
        if (t[2] <= 0 || at + (size_t)t[2] > bytes.size()) { error = "truncated tile"; return false; }
        auto* data = (unsigned char*)dtAlloc(t[2], DT_ALLOC_PERM);
        std::memcpy(data, bytes.data() + at, t[2]); at += t[2];
        if (dtStatusFailed(mesh->addTile(data, t[2], DT_TILE_FREE_DATA, 0, nullptr))) dtFree(data);
    }
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(mesh.get(), 4096))) { error = "query init failed"; return false; }
    st.mesh = std::move(mesh); st.query = std::move(query);
    int tiles = 0, polys = 0;
    const dtNavMesh* m = st.mesh.get();
    for (int i = 0; i < m->getMaxTiles(); ++i) if (const dtMeshTile* tile = m->getTile(i); tile && tile->header) { ++tiles; polys += tile->header->polyCount; }
    st.info = std::format("{} tiles, {} polygons", tiles, polys);
    return true;
}

NavMeshSurfaceComponent* FindSurface(Scene& scene, EntityID* entity = nullptr) {
    NavMeshSurfaceComponent* found = nullptr;
    scene.Reg().Each<NavMeshSurfaceComponent>([&](EntityID e, NavMeshSurfaceComponent& c) {
        if (!found && scene.Reg().IsEnabled<NavMeshSurfaceComponent>(e) && scene.IsActiveInHierarchy(e)) { found = &c; if (entity) *entity = e; }
    });
    return found;
}

// Loads the surface's baked data when it changed
State* Ensure(Scene& scene) {
    State& st = Get(scene);
    auto* surface = FindSurface(scene);
    if (!surface) return st.mesh ? &st : nullptr;
    if (!surface->data.empty() && st.source != surface->data) {
        st.source = surface->data;
        auto bytes = FileSystem::ReadBinary(AssetManager::ToAbsolute(surface->data));
        std::string error;
        if (bytes.empty() || !Load(st, bytes, error)) SD_ERROR("Nav mesh '{}': {}", surface->data, bytes.empty() ? "missing" : error);
    }
    return st.mesh ? &st : nullptr;
}

const dtQueryFilter& Filter() { static dtQueryFilter f; return f; }
const float kExtents[3] = { 2.0f, 4.0f, 2.0f };

bool Nearest(State& st, const glm::vec3& p, dtPolyRef& ref, glm::vec3& out, const float* extents = kExtents) {
    ref = 0;
    if (dtStatusFailed(st.query->findNearestPoly(&p.x, extents, &Filter(), &ref, &out.x)) || !ref) return false;
    return true;
}

void EnsureCrowd(State& st, float radius) {
    if (st.crowd && radius <= st.crowdRadius) return;
    st.crowdRadius = std::max({ 2.0f, radius, st.crowdRadius });
    st.crowd.reset(dtAllocCrowd());
    st.crowd->init(kMaxAgents, st.crowdRadius, st.mesh.get());
    dtObstacleAvoidanceParams params = *st.crowd->getObstacleAvoidanceParams(0);
    params.velBias = 0.5f; params.adaptiveDivs = 7; params.adaptiveRings = 3; params.adaptiveDepth = 3;
    st.crowd->setObstacleAvoidanceParams(3, &params);
    st.agents.clear(); // their crowd indexes are gone: re-added on the next update
}

dtCrowdAgentParams Params(const NavMeshAgentComponent& c) {
    dtCrowdAgentParams p{};
    p.radius = std::max(0.05f, c.radius); p.height = std::max(0.1f, c.height);
    p.maxAcceleration = std::max(0.01f, c.acceleration); p.maxSpeed = std::max(0.0f, c.speed);
    // Auto braking: slow down in time to stop at the destination (the crowd only slows within 2 radii)
    if (c.autoBraking && c.hasPath && c.remainingDistance > 0.0f)
        p.maxSpeed = std::min(p.maxSpeed, std::sqrt(2.0f * p.maxAcceleration * std::max(0.0f, c.remainingDistance - c.stoppingDistance)) + 0.05f);
    p.collisionQueryRange = p.radius * 12.0f; p.pathOptimizationRange = p.radius * 30.0f;
    p.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    p.obstacleAvoidanceType = 3;
    // Lower priority numbers push others aside more
    p.separationWeight = 0.5f + 3.0f * (c.avoidancePriority / 99.0f);
    p.queryFilterType = 0;
    return p;
}

float Remaining(const dtCrowdAgent* a) {
    if (!a || a->ncorners == 0) return a ? dtVdist(a->npos, a->targetPos) : 0.0f;
    float d = dtVdist(a->npos, &a->cornerVerts[0]);
    for (int i = 1; i < a->ncorners; ++i) d += dtVdist(&a->cornerVerts[(i - 1) * 3], &a->cornerVerts[i * 3]);
    if (!(a->cornerFlags[a->ncorners - 1] & DT_STRAIGHTPATH_END)) d += dtVdist(&a->cornerVerts[(a->ncorners - 1) * 3], a->targetPos);
    return d;
}
}

bool& DebugDraw() { return s_DebugDraw; }

static bool BakeInto(Scene& scene, EntityID e, std::vector<u8>& bytes, std::string& error) {
    auto* s = scene.Reg().TryGet<NavMeshSurfaceComponent>(e);
    if (!s) { error = "no Nav Mesh Surface"; return false; }
    auto start = std::chrono::steady_clock::now();
    BuildResult result;
    if (!Build(scene, *s, result, error)) return false;
    bytes = Serialize(result);
    SD_INFO("Nav mesh: {} tiles, {} polygons in {:.0f} ms", result.tiles.size(), result.polys,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    return true;
}

bool Bake(Scene& scene, EntityID e, std::string* error) {
    std::string why; std::vector<u8> bytes;
    if (!BakeInto(scene, e, bytes, why)) { if (error) *error = why; return false; }
    auto& s = scene.Reg().Get<NavMeshSurfaceComponent>(e);
    std::string name = scene.Name().empty() ? "Scene" : scene.Name();
    std::erase_if(name, [](char c) { return std::string("\\/:*?\"<>|").find(c) != std::string::npos; });
    const std::string relative = std::format("NavMesh/{}_{:08x}.navmesh", name, (u32)(scene.Reg().Get<IDComponent>(e).uuid & 0xffffffffu));
    auto path = AssetManager::ToAbsolute(relative);
    std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    if (!out) { if (error) *error = "cannot write " + path.string(); return false; }
    out.close();
    s.data = relative;
    State& st = Get(scene);
    std::string loadError;
    if (!Load(st, bytes, loadError)) { if (error) *error = loadError; return false; }
    st.source = relative;
    return true;
}

bool BakeRuntime(Scene& scene, EntityID e, std::string* error) {
    std::string why; std::vector<u8> bytes;
    if (!BakeInto(scene, e, bytes, why)) { if (error) *error = why; SD_ERROR("Nav mesh bake: {}", why); return false; }
    State& st = Get(scene);
    if (!Load(st, bytes, why)) { if (error) *error = why; return false; }
    st.source = scene.Reg().Get<NavMeshSurfaceComponent>(e).data; // keeps the in-memory mesh
    return true;
}

void Unload(Scene& scene) { auto& st = Get(scene); st.mesh.reset(); st.query.reset(); st.crowd.reset(); st.agents.clear(); st.source.clear(); st.debugTriangles.clear(); st.info.clear(); }
void Release(Scene& scene) { s_States.erase(&scene); }
bool HasNavMesh(Scene& scene) { return Ensure(scene) != nullptr; }
std::string Info(Scene& scene) { auto* st = Ensure(scene); return st ? st->info : std::string(); }

const std::vector<glm::vec3>& DebugTriangles(Scene& scene) {
    static const std::vector<glm::vec3> empty;
    State* st = Ensure(scene);
    if (!st) return empty;
    if (st->debugDirty) {
        st->debugDirty = false; st->debugTriangles.clear();
        const dtNavMesh* m = st->mesh.get();
        for (int t = 0; t < m->getMaxTiles(); ++t) {
            const dtMeshTile* tile = m->getTile(t);
            if (!tile || !tile->header) continue;
            for (int i = 0; i < tile->header->polyCount; ++i) {
                const dtPoly& poly = tile->polys[i];
                if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
                const dtPolyDetail& pd = tile->detailMeshes[i];
                for (int j = 0; j < pd.triCount; ++j) {
                    const unsigned char* tri = &tile->detailTris[(pd.triBase + j) * 4];
                    for (int k = 0; k < 3; ++k) {
                        const float* v = tri[k] < poly.vertCount ? &tile->verts[poly.verts[tri[k]] * 3] : &tile->detailVerts[(pd.vertBase + tri[k] - poly.vertCount) * 3];
                        st->debugTriangles.push_back({ v[0], v[1] + 0.05f, v[2] });
                    }
                }
            }
        }
    }
    return st->debugTriangles;
}

void RemoveAgent(Scene& scene, EntityID e) {
    auto it = s_States.find(&scene);
    if (it == s_States.end()) return;
    auto& st = it->second;
    auto a = st.agents.find(e);
    if (a == st.agents.end()) return;
    if (st.crowd && a->second.index >= 0) st.crowd->removeAgent(a->second.index);
    st.agents.erase(a);
}

void OnRuntimeStop(Scene& scene) {
    auto it = s_States.find(&scene);
    if (it == s_States.end()) return;
    it->second.crowd.reset(); it->second.agents.clear();
}

void Update(Scene& scene, float dt) {
    Registry& reg = scene.Reg();
    bool anyAgent = false;
    reg.Each<NavMeshAgentComponent>([&](EntityID, NavMeshAgentComponent&) { anyAgent = true; });
    if (!anyAgent) return;
    State* st = Ensure(scene);
    if (!st) return;
    float maxRadius = 0.0f;
    reg.Each<NavMeshAgentComponent>([&](EntityID, NavMeshAgentComponent& c) { maxRadius = std::max(maxRadius, c.radius); });
    EnsureCrowd(*st, maxRadius);
    dtCrowd& crowd = *st->crowd;

    // Add / refresh agents
    std::vector<EntityID> gone;
    for (auto& [e, slot] : st->agents) if (!reg.Valid(e) || !reg.TryGetEnabled<NavMeshAgentComponent>(e) || !scene.IsActiveInHierarchy(e)) gone.push_back(e);
    for (EntityID e : gone) { if (st->agents[e].index >= 0) crowd.removeAgent(st->agents[e].index); st->agents.erase(e); if (reg.Valid(e)) if (auto* c = reg.TryGet<NavMeshAgentComponent>(e)) { c->onNavMesh = false; c->crowdIndex = -1; } }
    reg.Each<NavMeshAgentComponent>([&](EntityID e, NavMeshAgentComponent& c) {
        if (!reg.IsEnabled<NavMeshAgentComponent>(e) || !scene.IsActiveInHierarchy(e)) return;
        glm::vec3 pos = glm::vec3(scene.GetWorldMatrix(e)[3]) - glm::vec3(0, c.baseOffset, 0);
        auto found = st->agents.find(e);
        const bool moved = found != st->agents.end() && glm::length(pos - found->second.written) > 1e-3f;
        if (c.warp) { pos = c.warpTo; c.warp = false; }
        if (found == st->agents.end() || moved || found->second.index < 0) {
            if (found != st->agents.end() && found->second.index >= 0) crowd.removeAgent(found->second.index);
            dtPolyRef ref; glm::vec3 nearest;
            const float ext[3] = { std::max(2.0f, c.radius * 2), std::max(4.0f, c.height), std::max(2.0f, c.radius * 2) };
            if (!Nearest(*st, pos, ref, nearest, ext)) { c.onNavMesh = false; c.crowdIndex = -1; st->agents[e] = { -1, pos }; return; }
            auto params = Params(c);
            int index = crowd.addAgent(&nearest.x, &params);
            st->agents[e] = { index, nearest };
            c.crowdIndex = index; c.onNavMesh = index >= 0;
            if (index >= 0 && c.hasDestination && !c.isStopped) c.pathPending = true;
        } else {
            auto params = Params(c);
            crowd.updateAgentParameters(found->second.index, &params);
        }
        int index = st->agents[e].index;
        if (index < 0) return;
        if (c.pathPending) {
            c.pathPending = false;
            dtPolyRef ref; glm::vec3 target;
            if (Nearest(*st, c.destination, ref, target)) { crowd.requestMoveTarget(index, ref, &target.x); c.hasPath = true; c.pathStatus = 0; }
            else { c.hasPath = false; c.pathStatus = 2; }
        }
    });

    if (dt > 0.0f) crowd.update(std::min(dt, 0.1f), nullptr);

    // Read back and move the objects
    for (auto& [e, slot] : st->agents) {
        if (slot.index < 0 || !reg.Valid(e)) continue;
        auto* c = reg.TryGet<NavMeshAgentComponent>(e);
        const dtCrowdAgent* a = crowd.getAgent(slot.index);
        if (!c || !a || !a->active) continue;
        const glm::vec3 npos(a->npos[0], a->npos[1], a->npos[2]);
        c->velocity = { a->vel[0], a->vel[1], a->vel[2] };
        c->nextPosition = npos;
        c->corners.clear();
        for (int i = 0; i < a->ncorners; ++i) c->corners.push_back({ a->cornerVerts[i * 3], a->cornerVerts[i * 3 + 1], a->cornerVerts[i * 3 + 2] });
        if (c->hasPath) {
            c->remainingDistance = Remaining(a);
            c->pathStatus = a->partial ? 1 : 0;
            if (a->targetState == DT_CROWDAGENT_TARGET_FAILED) { c->pathStatus = 2; c->hasPath = false; }
            const float stop = std::max(c->stoppingDistance, 0.05f);
            if (c->remainingDistance <= stop && a->ncorners <= 1) {
                crowd.resetMoveTarget(slot.index); c->hasPath = false; c->remainingDistance = 0.0f;
            }
        } else c->remainingDistance = 0.0f;
        if (c->updatePosition) {
            glm::mat4 m = scene.GetWorldMatrix(e);
            m[3] = glm::vec4(npos + glm::vec3(0, c->baseOffset, 0), 1.0f);
            glm::vec3 flat(c->velocity.x, 0, c->velocity.z);
            if (c->updateRotation && glm::length(flat) > 0.05f && dt > 0.0f) {
                glm::vec3 scale, t, skew; glm::vec4 persp; glm::quat rot;
                glm::decompose(m, scale, rot, t, skew, persp);
                const float targetYaw = std::atan2(-flat.x, -flat.z); // the engine's forward is -Z
                glm::vec3 fwd = rot * glm::vec3(0, 0, -1);
                float yaw = std::atan2(-fwd.x, -fwd.z);
                float diff = std::remainder(targetYaw - yaw, glm::two_pi<float>());
                const float maxStep = glm::radians(c->angularSpeed) * dt;
                yaw += std::clamp(diff, -maxStep, maxStep);
                glm::quat q = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
                m = glm::translate(glm::mat4(1), glm::vec3(m[3])) * glm::toMat4(q) * glm::scale(glm::mat4(1), scale);
            }
            scene.SetWorldMatrix(e, m);
            slot.written = npos;
        } else slot.written = glm::vec3(scene.GetWorldMatrix(e)[3]) - glm::vec3(0, c->baseOffset, 0);
    }
}

void AgentState(Scene&, EntityID, const NavMeshAgentComponent& c, json& out) {
    out["destination"] = { c.destination.x, c.destination.y, c.destination.z };
    out["hasPath"] = c.hasPath; out["pathPending"] = c.pathPending; out["isStopped"] = c.isStopped; out["isOnNavMesh"] = c.onNavMesh;
    out["pathStatus"] = c.pathStatus; out["remainingDistance"] = c.remainingDistance;
    out["velocity"] = { c.velocity.x, c.velocity.y, c.velocity.z };
    out["nextPosition"] = { c.nextPosition.x, c.nextPosition.y, c.nextPosition.z };
    json corners = json::array(); for (auto& p : c.corners) corners.push_back({ p.x, p.y, p.z });
    out["corners"] = corners;
}

void AgentCommand(Scene& scene, EntityID e, NavMeshAgentComponent& c, const json& in) {
    auto vec = [&](const char* k, glm::vec3& v) { auto it = in.find(k); if (it == in.end() || !it->is_array() || it->size() != 3) return false; v = { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() }; return true; };
    if (vec("setDestination", c.destination)) { c.hasDestination = true; c.pathPending = true; c.isStopped = false; }
    if (auto it = in.find("isStopped"); it != in.end() && it->is_boolean()) {
        const bool stop = it->get<bool>();
        if (stop && !c.isStopped) { auto st = s_States.find(&scene); if (st != s_States.end() && st->second.crowd && c.crowdIndex >= 0) st->second.crowd->resetMoveTarget(c.crowdIndex); }
        if (!stop && c.isStopped && c.hasDestination) c.pathPending = true;
        c.isStopped = stop;
    }
    if (in.value("resetPath", false)) {
        c.hasDestination = false; c.hasPath = false; c.pathPending = false;
        auto st = s_States.find(&scene); if (st != s_States.end() && st->second.crowd && c.crowdIndex >= 0) st->second.crowd->resetMoveTarget(c.crowdIndex);
    }
    if (vec("warp", c.warpTo)) { c.warp = true; RemoveAgent(scene, e); c.crowdIndex = -1; }
    if (vec("velocity", c.velocity)) { auto st = s_States.find(&scene); if (st != s_States.end() && st->second.crowd && c.crowdIndex >= 0) st->second.crowd->requestMoveVelocity(c.crowdIndex, &c.velocity.x); c.hasPath = false; }
}

bool CalculatePath(Scene& scene, const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& corners, int& status) {
    corners.clear(); status = 2;
    State* st = Ensure(scene);
    if (!st) return false;
    dtPolyRef a, b; glm::vec3 pa, pb;
    if (!Nearest(*st, from, a, pa) || !Nearest(*st, to, b, pb)) return false;
    dtPolyRef polys[512]; int count = 0;
    if (dtStatusFailed(st->query->findPath(a, b, &pa.x, &pb.x, &Filter(), polys, &count, 512)) || count == 0) return false;
    glm::vec3 end = pb;
    if (polys[count - 1] != b) { st->query->closestPointOnPoly(polys[count - 1], &pb.x, &end.x, nullptr); status = 1; } else status = 0;
    float straight[512 * 3]; unsigned char flags[512]; dtPolyRef refs[512]; int n = 0;
    st->query->findStraightPath(&pa.x, &end.x, polys, count, straight, flags, refs, &n, 512);
    for (int i = 0; i < n; ++i) corners.push_back({ straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2] });
    return !corners.empty();
}

bool SamplePosition(Scene& scene, const glm::vec3& position, float maxDistance, glm::vec3& result) {
    State* st = Ensure(scene);
    if (!st) return false;
    const float ext[3] = { maxDistance, maxDistance, maxDistance };
    dtPolyRef ref;
    if (!Nearest(*st, position, ref, result, ext)) return false;
    return glm::length(result - position) <= maxDistance;
}

bool Raycast(Scene& scene, const glm::vec3& from, const glm::vec3& to, glm::vec3& hit) {
    hit = to;
    State* st = Ensure(scene);
    if (!st) return true;
    dtPolyRef start; glm::vec3 p;
    if (!Nearest(*st, from, start, p)) { hit = from; return true; }
    float t = 0, normal[3]; dtPolyRef path[256]; int count = 0;
    st->query->raycast(start, &p.x, &to.x, &Filter(), &t, normal, path, &count, 256);
    if (t > 1.0f) return false; // FLT_MAX: reached the end
    hit = p + (to - p) * t;
    return true;
}

} // namespace Stardust::Navigation
