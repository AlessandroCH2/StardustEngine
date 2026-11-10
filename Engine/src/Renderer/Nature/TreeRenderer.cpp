#include "Renderer/Nature/TreeRenderer.h"
#include "Renderer/SceneRenderer.h"
#include "Renderer/Quality.h"
#include "Core/Log.h"
#include "Core/Noise.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <random>
#include <cstring>

namespace Stardust {

using namespace RHI;

static constexpr u32 kMaxInstances = 40000;
static constexpr float kLodDistance = 75.0f;
static constexpr float kMaxDistance = 2200.0f;

struct TreePushC {
    u32 frameBuffer, vertexBuffer, instanceBuffer, instanceBase;
    u32 leafTexture, entityId, cascade, isLeaf;
    float treeHeight; float pad[3];
};
static_assert(sizeof(TreePushC) == 48);

struct TreeInstanceGPU {
    glm::vec4 posRot;
    glm::vec4 scaleHash;
};

// ------------------------------------------------------------------------------------------------ generation

namespace {

struct TreeParams {
    float height, trunkRadius, crownRadius, crownVScale, leafSize;
    int branches, clusters;
    u32 seed;
};

struct MeshBuilder {
    std::vector<Vertex> v;
    std::vector<u32> i;
};

struct Gen {
    TreeParams p;
    std::mt19937 rng;
    std::uniform_real_distribution<float> U{ 0.0f, 1.0f };
    float R(float a, float b) { return a + (b - a) * U(rng); }

    // Tube along a polyline. Wind data in tangent: x = height ratio, y = flexibility, z = phase
    void Tube(MeshBuilder& m, const std::vector<glm::vec3>& pts, const std::vector<float>& radii, int sides, float flex) {
        u32 base = (u32)m.v.size();
        float len = 0;
        glm::vec3 prevAxis(0, 1, 0);
        glm::vec3 b1(1, 0, 0);
        for (size_t k = 0; k < pts.size(); ++k) {
            glm::vec3 axis = k + 1 < pts.size() ? glm::normalize(pts[k + 1] - pts[k]) : prevAxis;
            if (k > 0) len += glm::length(pts[k] - pts[k - 1]);
            // parallel transport of the ring frame
            b1 = glm::normalize(b1 - axis * glm::dot(b1, axis));
            if (!std::isfinite(b1.x) || glm::length(b1) < 0.5f) b1 = glm::normalize(glm::cross(axis, std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
            glm::vec3 b2 = glm::cross(axis, b1);
            for (int j = 0; j <= sides; ++j) {
                float a = glm::two_pi<float>() * j / sides;
                glm::vec3 n = b1 * std::cos(a) + b2 * std::sin(a);
                glm::vec3 pos = pts[k] + n * radii[k];
                m.v.push_back({ pos, n, { (float)j / sides, len * 0.5f }, { pos.y / p.height, flex * (float)k / (pts.size() - 1), 0.0f, 1.0f } });
            }
            prevAxis = axis;
        }
        u32 S = sides + 1;
        for (u32 k = 0; k + 1 < pts.size(); ++k)
            for (u32 j = 0; j < (u32)sides; ++j) {
                u32 a = base + k * S + j, b = base + (k + 1) * S + j;
                m.i.insert(m.i.end(), { a, a + 1, b + 1, a, b + 1, b });
            }
    }

    // Curved branch from start along dir, bending upwards
    std::vector<glm::vec3> Path(glm::vec3 start, glm::vec3 dir, float length, int segs, float upBend) {
        std::vector<glm::vec3> pts{ start };
        glm::vec3 d = glm::normalize(dir);
        float step = length / segs;
        for (int s = 0; s < segs; ++s) {
            d = glm::normalize(d + glm::vec3(R(-0.15f, 0.15f), upBend, R(-0.15f, 0.15f)));
            pts.push_back(pts.back() + d * step);
        }
        return pts;
    }

    // Leaf cluster: three intersecting cards, normals blended towards the crown sphere normal
    void Cluster(MeshBuilder& m, glm::vec3 c, float size, glm::vec3 crownCenter) {
        glm::vec3 sphereN = glm::normalize(c - crownCenter + glm::vec3(0, 0.3f, 0));
        float phase = U(rng);
        for (int q = 0; q < 3; ++q) {
            glm::vec3 axis = glm::normalize(glm::vec3(R(-1, 1), R(-1, 1), R(-1, 1)));
            glm::vec3 t1 = glm::normalize(glm::cross(axis, glm::vec3(0.3f, 1.0f, 0.2f)));
            glm::vec3 t2 = glm::cross(axis, t1);
            float s = size * R(0.8f, 1.2f) * 0.5f;
            glm::vec3 n = glm::normalize(glm::mix(axis, sphereN, 0.8f));
            u32 base = (u32)m.v.size();
            const glm::vec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            for (int k = 0; k < 4; ++k) {
                glm::vec2 o = uv[k] * 2.0f - 1.0f;
                glm::vec3 pos = c + (t1 * o.x + t2 * o.y) * s;
                m.v.push_back({ pos, n, uv[k], { pos.y / p.height, 1.0f, phase, 1.0f } });
            }
            m.i.insert(m.i.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
    }
};

TreeParams kTreeParams[TreeRenderer::kTypes] = {
    { 12.5f, 0.34f, 4.8f, 0.85f, 2.4f, 9, 230, 11 },  // large broadleaf
    { 9.5f, 0.27f, 3.7f, 1.1f, 2.1f, 7, 170, 23 },    // slimmer broadleaf
    { 4.2f, 0.13f, 2.2f, 0.9f, 1.6f, 5, 70, 37 },     // young tree / bush
};

} // namespace

void TreeRenderer::BuildModels() {
    for (u32 t = 0; t < kTypes; ++t) {
        Gen g{ kTreeParams[t], std::mt19937(kTreeParams[t].seed) };
        const TreeParams& p = g.p;
        MeshBuilder bark[kLods], leaves[kLods];
        glm::vec3 crownCenter(0, p.height * 0.62f, 0);
        std::vector<glm::vec3> clusterPos;

        // Trunk with a gentle wobble, continuing as a thinner leader into the crown
        std::vector<glm::vec3> trunk;
        std::vector<float> trunkR;
        const int trunkSegs = 7;
        for (int s = 0; s <= trunkSegs; ++s) {
            float f = (float)s / trunkSegs;
            trunk.push_back({ g.R(-0.08f, 0.08f) * f * p.height * 0.1f, f * p.height * 0.85f, g.R(-0.08f, 0.08f) * f * p.height * 0.1f });
            trunkR.push_back(p.trunkRadius * (1.0f - f * 0.85f) * (s == 0 ? 1.35f : 1.0f));
        }
        g.Tube(bark[0], trunk, trunkR, 9, 0.2f);
        g.Tube(bark[1], trunk, trunkR, 6, 0.2f);

        // Main branches radiating out and up into the crown
        for (int b = 0; b < p.branches; ++b) {
            float hf = g.R(0.42f, 0.8f);
            glm::vec3 start(0, p.height * hf, 0);
            float az = b * 2.39996f + g.R(-0.3f, 0.3f);
            float el = g.R(0.35f, 0.85f);
            glm::vec3 dir(std::cos(az) * std::cos(el), std::sin(el), std::sin(az) * std::cos(el));
            float len = p.crownRadius * g.R(0.75f, 1.05f);
            auto pts = g.Path(start, dir, len, 4, 0.08f);
            std::vector<float> rr;
            float r0 = p.trunkRadius * g.R(0.35f, 0.5f) * (1.0f - hf * 0.5f);
            for (size_t k = 0; k < pts.size(); ++k) rr.push_back(r0 * (1.0f - 0.85f * k / (pts.size() - 1)));
            g.Tube(bark[0], pts, rr, 6, 0.6f);
            g.Tube(bark[1], pts, rr, 4, 0.6f);
            clusterPos.push_back(pts.back());

            // Twigs (LOD0 only)
            for (int c = 0; c < 3; ++c) {
                size_t k = 1 + (size_t)(g.U(g.rng) * (pts.size() - 2));
                glm::vec3 td = glm::rotate(dir, g.R(-0.9f, 0.9f), glm::vec3(0, 1, 0)) + glm::vec3(0, g.R(0.0f, 0.6f), 0);
                auto tp = g.Path(pts[k], td, len * g.R(0.3f, 0.5f), 2, 0.1f);
                std::vector<float> tr;
                for (size_t q = 0; q < tp.size(); ++q) tr.push_back(rr[k] * 0.5f * (1.0f - 0.8f * q / (tp.size() - 1)));
                g.Tube(bark[0], tp, tr, 4, 0.9f);
                clusterPos.push_back(tp.back());
            }
        }

        // Fill the crown shell with clusters
        while ((int)clusterPos.size() < p.clusters) {
            glm::vec3 d = glm::normalize(glm::vec3(g.R(-1, 1), g.R(-0.6f, 1), g.R(-1, 1)));
            float r = g.R(0.55f, 1.0f);
            glm::vec3 c = crownCenter + glm::vec3(d.x * p.crownRadius, d.y * p.crownRadius * p.crownVScale, d.z * p.crownRadius) * r;
            clusterPos.push_back(c);
        }
        for (size_t k = 0; k < clusterPos.size(); ++k) {
            g.Cluster(leaves[0], clusterPos[k], p.leafSize, crownCenter);
            if (k % 3 == 0) g.Cluster(leaves[1], clusterPos[k], p.leafSize * 1.55f, crownCenter);
        }

        for (u32 l = 0; l < kLods; ++l) {
            std::string name = std::format("Tree{}_LOD{}", t, l);
            m_Models[t].bark[l] = MakeRef<Mesh>(m_Device, bark[l].v, bark[l].i, name + "_Bark");
            m_Models[t].leaves[l] = MakeRef<Mesh>(m_Device, leaves[l].v, leaves[l].i, name + "_Leaves");
        }
        m_Models[t].height = p.height;
        m_Models[t].radius = std::max(p.crownRadius * 1.1f, p.height * 0.55f);
    }
}

// Leaf atlas: a round cluster of many small pointed leaves with alpha
void TreeRenderer::CreateLeafTexture() {
    const int S = 512;
    std::vector<u8> px((size_t)S * S * 4);
    // background colour = average leaf green so mip-mapped edges do not go dark
    for (int k = 0; k < S * S; ++k) { px[k * 4 + 0] = 62; px[k * 4 + 1] = 92; px[k * 4 + 2] = 30; px[k * 4 + 3] = 0; }
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    for (int leaf = 0; leaf < 110; ++leaf) {
        float ang = U(rng) * glm::two_pi<float>();
        float rad = std::sqrt(U(rng)) * 0.36f;
        glm::vec2 c(0.5f + std::cos(ang) * rad, 0.5f + std::sin(ang) * rad);
        float rot = ang + (U(rng) - 0.5f) * 1.6f;
        float len = (0.06f + U(rng) * 0.05f) * S, wid = len * (0.32f + U(rng) * 0.12f);
        glm::vec2 dir(std::cos(rot), std::sin(rot)), perp(-dir.y, dir.x);
        float shade = 0.75f + U(rng) * 0.45f;
        float yellow = U(rng) * 0.25f;
        int x0 = (int)(c.x * S - len), x1 = (int)(c.x * S + len), y0 = (int)(c.y * S - len), y1 = (int)(c.y * S + len);
        for (int y = std::max(0, y0); y < std::min(S, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(S, x1); ++x) {
                glm::vec2 d = glm::vec2((float)x, (float)y) - c * (float)S;
                float u = glm::dot(d, dir) / len, v = glm::dot(d, perp) / wid;
                if (std::abs(u) > 1.0f) continue;
                float halfW = std::pow(1.0f - u * u, 0.8f);
                if (std::abs(v) > halfW) continue;
                float side = v > 0 ? 1.0f : 0.85f;           // two leaf halves lit differently
                float rib = std::abs(v) < 0.07f ? 0.8f : 1.0f; // midrib
                float r = (40.0f + yellow * 80.0f) * shade * side * rib;
                float gC = (95.0f + yellow * 30.0f) * shade * side * rib;
                float b = 28.0f * shade * side * rib;
                u8* o = &px[((size_t)y * S + x) * 4];
                o[0] = (u8)std::min(r, 255.0f); o[1] = (u8)std::min(gC, 255.0f); o[2] = (u8)std::min(b, 255.0f); o[3] = 255;
            }
    }
    TextureDesc d;
    d.name = "LeafAtlas";
    d.width = d.height = S;
    d.mipLevels = 10;
    d.format = Format::RGBA8_SRGB;
    d.usage = TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    m_LeafTexture = m_Device.CreateTexture(d);
    m_Device.UploadTexture(m_LeafTexture.get(), px.data(), px.size());
}

// ------------------------------------------------------------------------------------------------ renderer

TreeRenderer::TreeRenderer(Device& device, Format hdr, Format id, Format depth) : m_Device(device) {
    GraphicsPipelineDesc d;
    d.colorFormats = { hdr, id };
    d.depthFormat = depth;
    d.name = "TreeBark"; d.vs = "Tree.vs"; d.ps = "TreeBark.ps";
    m_Bark = device.CreateGraphicsPipeline(d);
    d.name = "TreeLeaves"; d.ps = "TreeLeaves.ps"; d.cull = CullMode::None;
    m_Leaves = device.CreateGraphicsPipeline(d);

    GraphicsPipelineDesc s;
    s.depthFormat = depth;
    s.cull = CullMode::None;
    s.depthClamp = true;
    s.depthBiasConstant = -1.5f;
    s.depthBiasSlope = -2.0f;
    s.name = "TreeBarkShadow"; s.vs = "TreeShadow.vs";
    m_BarkShadow = device.CreateGraphicsPipeline(s);
    s.name = "TreeLeavesShadow"; s.ps = "TreeLeavesShadow.ps";
    m_LeavesShadow = device.CreateGraphicsPipeline(s);

    for (auto& b : m_Instances)
        b = device.CreateBuffer({ kMaxInstances * sizeof(TreeInstanceGPU), BufferUsage::Storage, MemoryUsage::CPUToGPU, "TreeInstances" });

    BuildModels();
    CreateLeafTexture();
}

void TreeRenderer::Prepare(const TerrainData& terrain, const glm::vec3& origin, u32 entityId,
                           const glm::vec3& cameraPos, const Frustum& frustum, const FrameConstants& fc, bool shadows) {
    m_EntityId = entityId;
    m_HasData = terrain.Settings().trees && !terrain.Trees().empty();
    m_Visible = 0;
    m_Triangles = 0;
    std::memset(m_Batches, 0, sizeof(m_Batches));
    if (!m_HasData) return;

    static thread_local std::vector<TreeInstanceGPU> lists[kLists][kTypes][kLods];
    for (auto& a : lists) for (auto& b : a) for (auto& c : b) c.clear();

    for (const TreeInstance& t : terrain.Trees()) {
        const TreeModel& model = m_Models[t.type];
        glm::vec3 pos = origin + t.position;
        glm::vec3 center = pos + glm::vec3(0, model.height * 0.55f * t.scale, 0);
        float radius = model.radius * t.scale;
        float dist = glm::length(center - cameraPos);
        float hash = Hash01((u32)(t.position.x * 13.0f) ^ ((u32)(t.position.z * 7.0f) << 12));
        TreeInstanceGPU gi{ glm::vec4(pos, t.rotation), glm::vec4(t.scale, hash, 0, 0) };

        if (dist < kMaxDistance && frustum.Sphere(center, radius)) {
            u32 lod = dist < kLodDistance ? 0 : 1;
            lists[0][t.type][lod].push_back(gi);
            m_Visible++;
        }
        if (!shadows) continue;
        for (u32 c = 0; c < kShadowCascades; ++c) {
            float halfExtent = fc.cascadeTexelSize[c] * GraphicsQuality::Current().shadowMapSize * 0.5f;
            if (dist > fc.cascadeSplits[c] + halfExtent + radius) continue;
            glm::vec4 clip = fc.cascadeViewProj[c] * glm::vec4(center, 1.0f);
            float r = radius / halfExtent;
            if (std::abs(clip.x) > 1.0f + r || std::abs(clip.y) > 1.0f + r) continue;
            u32 lod = (c == 0 && dist < kLodDistance) ? 0 : 1;
            lists[1 + c][t.type][lod].push_back(gi);
        }
    }

    auto* dst = m_Instances[m_Device.FrameIndex()]->As<TreeInstanceGPU>();
    u32 offset = 0;
    for (u32 l = 0; l < kLists; ++l)
        for (u32 t = 0; t < kTypes; ++t)
            for (u32 lod = 0; lod < kLods; ++lod) {
                auto& src = lists[l][t][lod];
                u32 n = std::min((u32)src.size(), kMaxInstances - offset);
                std::memcpy(dst + offset, src.data(), n * sizeof(TreeInstanceGPU));
                m_Batches[l][t][lod] = { offset, n };
                offset += n;
                if (l == 0) m_Triangles += (u64)n * (m_Models[t].bark[lod]->IndexCount() + m_Models[t].leaves[lod]->IndexCount()) / 3;
            }
}

void TreeRenderer::DrawList(CommandList& cmd, Buffer* frameBuffer, u32 list, u32 cascade, bool shadow) {
    for (int pass = 0; pass < 2; ++pass) { // bark first, then leaves (fewer pipeline switches)
        bool leaf = pass == 1;
        cmd.SetPipeline(shadow ? (leaf ? m_LeavesShadow : m_BarkShadow).get() : (leaf ? m_Leaves : m_Bark).get());
        for (u32 t = 0; t < kTypes; ++t)
            for (u32 lod = 0; lod < kLods; ++lod) {
                const Batch& b = m_Batches[list][t][lod];
                if (!b.count) continue;
                const Ref<Mesh>& mesh = leaf ? m_Models[t].leaves[lod] : m_Models[t].bark[lod];
                TreePushC p{};
                p.frameBuffer = frameBuffer->Index();
                p.vertexBuffer = mesh->VertexBuffer()->Index();
                p.instanceBuffer = m_Instances[m_Device.FrameIndex()]->Index();
                p.instanceBase = b.base;
                p.leafTexture = m_LeafTexture->SRV();
                p.entityId = m_EntityId;
                p.cascade = cascade;
                p.isLeaf = leaf;
                p.treeHeight = m_Models[t].height;
                cmd.Push(p);
                cmd.SetIndexBuffer(mesh->IndexBuffer());
                cmd.DrawIndexed(mesh->IndexCount(), b.count);
            }
    }
}

void TreeRenderer::Draw(CommandList& cmd, Buffer* frameBuffer) {
    if (m_HasData) DrawList(cmd, frameBuffer, 0, 0, false);
}

void TreeRenderer::DrawShadow(CommandList& cmd, Buffer* frameBuffer, u32 cascade) {
    if (m_HasData) DrawList(cmd, frameBuffer, 1 + cascade, cascade, true);
}

} // namespace Stardust
