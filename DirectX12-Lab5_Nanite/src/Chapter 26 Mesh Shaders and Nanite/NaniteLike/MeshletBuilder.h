//***************************************************************************************
// MeshletBuilder.h - Builds meshlets from traditional mesh data
//***************************************************************************************

#pragma once

// Include our d3dx12.h first to avoid conflicts
#include "../../Common/d3dx12.h"
#include "Meshlet.h"
#include "../../Common/GeometryGenerator.h"

// DirectXMesh library for optimized meshlet generation
#include <DirectXMesh.h>

class MeshletBuilder
{
public:
    static bool BuildMeshlets(
        const std::vector<DirectX::XMFLOAT3>& positions,
        const std::vector<DirectX::XMFLOAT3>& normals,
        const std::vector<DirectX::XMFLOAT2>& texCoords,
        const std::vector<DirectX::XMFLOAT3>& tangents,
        const std::vector<uint32_t>& indices,
        MeshletMesh& outMesh);
    
    static bool BuildFromGeometry(
        const GeometryGenerator::MeshData& meshData,
        MeshletMesh& outMesh);
    
    static bool LoadOBJ(
        const std::wstring& filename,
        MeshletMesh& outMesh);
    
    // Load OBJ using DirectStorage for fast I/O
    static bool LoadOBJWithDirectStorage(
        const std::wstring& filename,
        MeshletMesh& outMesh,
        class DirectStorageLoader* storageLoader);
    
    static void ComputeMeshletBounds(
        const std::vector<DirectX::XMFLOAT3>& positions,
        const std::vector<uint32_t>& uniqueVertexIndices,
        const std::vector<uint8_t>& primitiveIndices,
        const MeshletData& meshlet,
        MeshletBounds& outBounds);
    
    static void BuildLODHierarchy(MeshletMesh& mesh, uint32_t maxLODLevels = 8);
    
private:
    static void GenerateMeshletsSimple(
        const std::vector<uint32_t>& indices,
        uint32_t vertexCount,
        std::vector<MeshletData>& meshlets,
        std::vector<uint32_t>& uniqueVertexIndices,
        std::vector<uint8_t>& primitiveIndices);
};
