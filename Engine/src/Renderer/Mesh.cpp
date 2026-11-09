#include "Renderer/Mesh.h"
#include <glm/gtc/constants.hpp>

namespace Stardust {

Mesh::Mesh(RHI::Device& device, const std::vector<Vertex>& vertices, const std::vector<u32>& indices, std::string name)
    : m_IndexCount((u32)indices.size()), m_VertexCount((u32)vertices.size()), m_Name(std::move(name)) {
    m_Vertices = device.CreateBuffer({ vertices.size() * sizeof(Vertex), RHI::BufferUsage::Storage, RHI::MemoryUsage::GPUOnly, m_Name + ".VB" }, vertices.data());
    m_Indices = device.CreateBuffer({ indices.size() * sizeof(u32), RHI::BufferUsage::Index, RHI::MemoryUsage::GPUOnly, m_Name + ".IB" }, indices.data());
    m_Positions.reserve(vertices.size());
    for (auto& v : vertices) m_Positions.push_back(v.position);
    m_CpuIndices = indices;
    if (!vertices.empty()) {
        m_Bounds.min = m_Bounds.max = vertices[0].position;
        for (auto& v : vertices) { m_Bounds.min = glm::min(m_Bounds.min, v.position); m_Bounds.max = glm::max(m_Bounds.max, v.position); }
    }
}

const char* PrimitiveName(PrimitiveType t) {
    static const char* names[] = { "Cube", "Sphere", "Plane", "Cylinder" };
    return names[(int)t];
}

static void MakeCube(std::vector<Vertex>& v, std::vector<u32>& i) {
    const glm::vec3 n[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    for (int f = 0; f < 6; ++f) {
        glm::vec3 normal = n[f];
        glm::vec3 up = std::abs(normal.y) > 0.5f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::vec3 right = glm::cross(up, normal);
        u32 base = (u32)v.size();
        const glm::vec2 c[4] = { {-1,-1},{1,-1},{1,1},{-1,1} };
        for (int k = 0; k < 4; ++k) {
            glm::vec3 p = (normal + right * c[k].x + up * c[k].y) * 0.5f;
            v.push_back({ p, normal, (c[k] + 1.0f) * 0.5f, glm::vec4(right, 1) });
        }
        i.insert(i.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

static void MakeSphere(std::vector<Vertex>& v, std::vector<u32>& i, u32 seg = 48, u32 rings = 24) {
    for (u32 r = 0; r <= rings; ++r) {
        float phi = glm::pi<float>() * r / rings;
        for (u32 s = 0; s <= seg; ++s) {
            float theta = glm::two_pi<float>() * s / seg;
            glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            glm::vec3 t(-std::sin(theta), 0, std::cos(theta));
            v.push_back({ n * 0.5f, n, { (float)s / seg, (float)r / rings }, glm::vec4(t, 1) });
        }
    }
    for (u32 r = 0; r < rings; ++r)
        for (u32 s = 0; s < seg; ++s) {
            u32 a = r * (seg + 1) + s, b = a + seg + 1;
            i.insert(i.end(), { a, a + 1, b, b, a + 1, b + 1 });
        }
}

static void MakePlane(std::vector<Vertex>& v, std::vector<u32>& i, u32 div = 10) {
    for (u32 z = 0; z <= div; ++z)
        for (u32 x = 0; x <= div; ++x) {
            float fx = (float)x / div - 0.5f, fz = (float)z / div - 0.5f;
            v.push_back({ { fx * 10.0f, 0, fz * 10.0f }, { 0, 1, 0 }, { (float)x, (float)z }, { 1, 0, 0, 1 } });
        }
    for (u32 z = 0; z < div; ++z)
        for (u32 x = 0; x < div; ++x) {
            u32 a = z * (div + 1) + x, b = a + div + 1;
            i.insert(i.end(), { a, b, a + 1, a + 1, b, b + 1 });
        }
}

static void MakeCylinder(std::vector<Vertex>& v, std::vector<u32>& i, u32 seg = 40) {
    for (u32 s = 0; s <= seg; ++s) {
        float t = glm::two_pi<float>() * s / seg;
        glm::vec3 n(std::cos(t), 0, std::sin(t));
        v.push_back({ n * 0.5f + glm::vec3(0, -0.5f, 0), n, { (float)s / seg, 0 }, { -n.z, 0, n.x, 1 } });
        v.push_back({ n * 0.5f + glm::vec3(0, 0.5f, 0), n, { (float)s / seg, 1 }, { -n.z, 0, n.x, 1 } });
    }
    for (u32 s = 0; s < seg; ++s) {
        u32 a = s * 2;
        i.insert(i.end(), { a, a + 1, a + 2, a + 2, a + 1, a + 3 });
    }
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap ? 0.5f : -0.5f;
        glm::vec3 n(0, cap ? 1.0f : -1.0f, 0);
        u32 center = (u32)v.size();
        v.push_back({ { 0, y, 0 }, n, { 0.5f, 0.5f }, { 1, 0, 0, 1 } });
        for (u32 s = 0; s <= seg; ++s) {
            float t = glm::two_pi<float>() * s / seg;
            v.push_back({ { std::cos(t) * 0.5f, y, std::sin(t) * 0.5f }, n, { std::cos(t) * 0.5f + 0.5f, std::sin(t) * 0.5f + 0.5f }, { 1, 0, 0, 1 } });
        }
        for (u32 s = 0; s < seg; ++s) {
            if (cap) i.insert(i.end(), { center, center + 2 + s, center + 1 + s });
            else     i.insert(i.end(), { center, center + 1 + s, center + 2 + s });
        }
    }
}

void MeshLibrary::Init(RHI::Device& device) {
    std::vector<Vertex> waterVertices;std::vector<u32> waterIndices;MakePlane(waterVertices,waterIndices,256);s_Water=MakeRef<Mesh>(device,waterVertices,waterIndices,"Water Grid");
    s_Meshes.resize((size_t)PrimitiveType::Count);
    for (u32 t = 0; t < (u32)PrimitiveType::Count; ++t) {
        std::vector<Vertex> v; std::vector<u32> i;
        switch ((PrimitiveType)t) {
        case PrimitiveType::Cube: MakeCube(v, i); break;
        case PrimitiveType::Sphere: MakeSphere(v, i); break;
        case PrimitiveType::Plane: MakePlane(v, i); break;
        case PrimitiveType::Cylinder: MakeCylinder(v, i); break;
        default: break;
        }
        s_Meshes[t] = MakeRef<Mesh>(device, v, i, PrimitiveName((PrimitiveType)t));
    }
}

void MeshLibrary::Shutdown() { s_Water.reset();s_Meshes.clear(); }
Ref<Mesh> MeshLibrary::WaterGrid(){return s_Water;}
Ref<Mesh> MeshLibrary::Get(PrimitiveType type) { return s_Meshes[(size_t)type]; }

} // namespace Stardust
