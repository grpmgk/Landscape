//***************************************************************************************
// Meshlet.h - Meshlet data structures for Nanite-like rendering
//***************************************************************************************

#pragma once

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <vector>
#include <string>
#include <cstdint>

// Maximum vertices and primitives per meshlet (hardware limits)
constexpr uint32_t MAX_MESHLET_VERTICES = 64;
constexpr uint32_t MAX_MESHLET_PRIMITIVES = 124;

// Our meshlet structure (renamed to avoid conflict with DirectX::Meshlet)
struct MeshletData
{
    uint32_t VertexOffset;
    uint32_t VertexCount;
    uint32_t PrimitiveOffset;
    uint32_t PrimitiveCount;
};

// Meshlet bounds for culling
struct MeshletBounds
{
    DirectX::XMFLOAT3 Center;
    float Radius;
    DirectX::XMFLOAT3 ConeAxis;
    float ConeCutoff;
    DirectX::XMFLOAT3 ConeApex;
    float Padding;
};

// LOD cluster node
struct ClusterNode
{
    uint32_t MeshletStart;
    uint32_t MeshletCount;
    uint32_t ParentIndex;
    uint32_t ChildStart;
    uint32_t ChildCount;
    float LODError;
    DirectX::XMFLOAT3 BoundCenter;
    float BoundRadius;
};

// Per-instance data
struct MeshInstance
{
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 InvTransposeWorld;
    uint32_t MeshIndex;
    uint32_t MaterialIndex;
    uint32_t Padding[2];
};


// Mesh data containing all meshlets
struct MeshletMesh
{
    std::string Name;
    
    // Vertex data
    std::vector<DirectX::XMFLOAT3> Positions;
    std::vector<DirectX::XMFLOAT3> Normals;
    std::vector<DirectX::XMFLOAT2> TexCoords;
    std::vector<DirectX::XMFLOAT3> Tangents;
    
    // Original indices (for fallback rendering)
    std::vector<uint32_t> Indices;
    
    // Meshlet data
    std::vector<MeshletData> Meshlets;
    std::vector<MeshletBounds> MeshletBoundsData;
    std::vector<uint32_t> UniqueVertexIndices;
    std::vector<uint8_t> PrimitiveIndices;
    
    // LOD hierarchy
    std::vector<ClusterNode> ClusterNodes;
    uint32_t LODCount = 1;
    
    // Bounding info
    DirectX::BoundingBox BBox;
    DirectX::BoundingSphere BSphere;
};

// Vertex format for mesh shader
struct MeshletVertex
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexCoord;
    DirectX::XMFLOAT3 Tangent;
    uint32_t MeshletID;
};

// GPU-visible meshlet info
struct GPUMeshletInfo
{
    uint32_t VertexOffset;
    uint32_t VertexCount;
    uint32_t PrimitiveOffset;
    uint32_t PrimitiveCount;
};

// Culling results
struct CullingStats
{
    uint32_t VisibleMeshlets;
    uint32_t CulledMeshlets;
    uint32_t TotalTriangles;
    uint32_t Padding;
};
