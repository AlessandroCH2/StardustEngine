#pragma once
#include "RHI/RHI.h"
#include <glm/glm.hpp>

namespace Stardust {

// 48 bytes, read by shaders through vertex pulling (Common.hlsli :: LoadVertex)
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent{ 1, 0, 0, 1 };
};
static_assert(sizeof(Vertex) == 48);

struct AABB {
    glm::vec3 min{ 0.0f }, max{ 0.0f };
    glm::vec3 Center() const { return (min + max) * 0.5f; }
    glm::vec3 Extents() const { return (max - min) * 0.5f; }
};

struct SubMesh { u32 firstIndex=0,indexCount=0,materialSlot=0;std::string name; };
class Mesh {
public:
    Mesh(RHI::Device& device, const std::vector<Vertex>& vertices, const std::vector<u32>& indices, std::string name = {});

    RHI::Buffer* VertexBuffer() const { return m_Vertices.get(); }
    // Skinned instances share the indices and geometry data of a source mesh, with vertices of their own
    Ref<Mesh> CloneForSkinning() const { return Ref<Mesh>(new Mesh(*this)); }
    void SetVertexBuffer(Ref<RHI::Buffer> vertices) { m_Vertices = std::move(vertices); }
    void SetBounds(const AABB& bounds) { m_Bounds = bounds; }
    RHI::Buffer* IndexBuffer() const { return m_Indices.get(); }
    const std::vector<SubMesh>& SubMeshes() const {return m_SubMeshes;}
    void SetSubMeshes(std::vector<SubMesh> parts){std::erase_if(parts,[](auto& p){return p.indexCount==0;});m_SubMeshes=std::move(parts);}
    u32 IndexCount() const { return m_IndexCount; }
    u32 VertexCount() const { return m_VertexCount; }
    const AABB& Bounds() const { return m_Bounds; }
    const std::string& Name() const { return m_Name; }
    // CPU copies for physics (mesh colliders) and picking
    const std::vector<glm::vec3>& Positions() const { return m_Positions; }
    const std::vector<u32>& Indices() const { return m_CpuIndices; }

private:
    std::vector<SubMesh> m_SubMeshes;
    std::vector<glm::vec3> m_Positions;
    std::vector<u32> m_CpuIndices;
    Ref<RHI::Buffer> m_Vertices, m_Indices;
    u32 m_IndexCount = 0, m_VertexCount = 0;
    AABB m_Bounds;
    std::string m_Name;
};

enum class PrimitiveType : u8 { Cube, Sphere, Plane, Cylinder, Count };
const char* PrimitiveName(PrimitiveType t);

// Built-in meshes created once per device
class MeshLibrary {
public:
    static void Init(RHI::Device& device);
    static void Shutdown();
    static Ref<Mesh> Get(PrimitiveType type);
    static Ref<Mesh> WaterGrid();
private:
    static inline std::vector<Ref<Mesh>> s_Meshes;
    static inline Ref<Mesh> s_Water;
};

} // namespace Stardust
