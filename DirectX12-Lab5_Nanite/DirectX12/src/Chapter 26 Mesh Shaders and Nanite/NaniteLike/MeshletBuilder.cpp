//***************************************************************************************
// MeshletBuilder.cpp - Meshlet generation using DirectXMesh library
//***************************************************************************************

#include "MeshletBuilder.h"
#include "DirectStorageLoader.h"
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

// Disable min/max macros from Windows.h
#ifndef NOMINMAX
#define NOMINMAX
#endif

using namespace DirectX;

bool MeshletBuilder::BuildMeshlets(
    const std::vector<XMFLOAT3>& positions,
    const std::vector<XMFLOAT3>& normals,
    const std::vector<XMFLOAT2>& texCoords,
    const std::vector<XMFLOAT3>& tangents,
    const std::vector<uint32_t>& indices,
    MeshletMesh& outMesh)
{
    outMesh.Positions = positions;
    outMesh.Normals = normals;
    outMesh.TexCoords = texCoords;
    outMesh.Tangents = tangents;
    outMesh.Indices = indices;  // Save original indices for fallback rendering

    // Use DirectXMesh for optimized meshlet generation
    std::vector<DirectX::Meshlet> dxMeshlets;
    std::vector<uint8_t> uniqueVertexIB;
    std::vector<DirectX::MeshletTriangle> primitiveIndices;

    HRESULT hr = DirectX::ComputeMeshlets(
        indices.data(),
        indices.size() / 3,
        positions.data(),
        positions.size(),
        nullptr,
        dxMeshlets,
        uniqueVertexIB,
        primitiveIndices,
        MAX_MESHLET_VERTICES,
        MAX_MESHLET_PRIMITIVES);

    if (FAILED(hr))
    {
        OutputDebugStringA("DirectXMesh::ComputeMeshlets failed, using fallback\n");
        GenerateMeshletsSimple(indices, static_cast<uint32_t>(positions.size()),
            outMesh.Meshlets, outMesh.UniqueVertexIndices, outMesh.PrimitiveIndices);
    }
    else
    {
        // Convert DirectXMesh format to our format
        outMesh.Meshlets.resize(dxMeshlets.size());
        for (size_t i = 0; i < dxMeshlets.size(); ++i)
        {
            outMesh.Meshlets[i].VertexOffset = dxMeshlets[i].VertOffset;
            outMesh.Meshlets[i].VertexCount = dxMeshlets[i].VertCount;
            outMesh.Meshlets[i].PrimitiveOffset = dxMeshlets[i].PrimOffset;
            outMesh.Meshlets[i].PrimitiveCount = dxMeshlets[i].PrimCount;
        }

        // Convert unique vertex indices
        size_t vertexIndexSize = (positions.size() <= 256) ? 1 : 
                                 (positions.size() <= 65536) ? 2 : 4;
        size_t numUniqueIndices = uniqueVertexIB.size() / vertexIndexSize;
        outMesh.UniqueVertexIndices.resize(numUniqueIndices);
        
        for (size_t i = 0; i < numUniqueIndices; ++i)
        {
            if (vertexIndexSize == 1)
                outMesh.UniqueVertexIndices[i] = uniqueVertexIB[i];
            else if (vertexIndexSize == 2)
                outMesh.UniqueVertexIndices[i] = reinterpret_cast<const uint16_t*>(uniqueVertexIB.data())[i];
            else
                outMesh.UniqueVertexIndices[i] = reinterpret_cast<const uint32_t*>(uniqueVertexIB.data())[i];
        }

        // Convert primitive indices
        outMesh.PrimitiveIndices.resize(primitiveIndices.size() * 3);
        for (size_t i = 0; i < primitiveIndices.size(); ++i)
        {
            outMesh.PrimitiveIndices[i * 3 + 0] = static_cast<uint8_t>(primitiveIndices[i].i0);
            outMesh.PrimitiveIndices[i * 3 + 1] = static_cast<uint8_t>(primitiveIndices[i].i1);
            outMesh.PrimitiveIndices[i * 3 + 2] = static_cast<uint8_t>(primitiveIndices[i].i2);
        }
    }

    // Compute bounds for each meshlet manually
    outMesh.MeshletBoundsData.resize(outMesh.Meshlets.size());
    for (size_t i = 0; i < outMesh.Meshlets.size(); ++i)
    {
        ComputeMeshletBounds(positions, outMesh.UniqueVertexIndices,
            outMesh.PrimitiveIndices, outMesh.Meshlets[i], outMesh.MeshletBoundsData[i]);
    }

    // Compute overall bounding box/sphere
    BoundingBox::CreateFromPoints(outMesh.BBox, positions.size(),
        positions.data(), sizeof(XMFLOAT3));
    BoundingSphere::CreateFromBoundingBox(outMesh.BSphere, outMesh.BBox);

    return true;
}


bool MeshletBuilder::BuildFromGeometry(
    const GeometryGenerator::MeshData& meshData,
    MeshletMesh& outMesh)
{
    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texCoords;
    std::vector<XMFLOAT3> tangents;

    positions.reserve(meshData.Vertices.size());
    normals.reserve(meshData.Vertices.size());
    texCoords.reserve(meshData.Vertices.size());
    tangents.reserve(meshData.Vertices.size());

    for (const auto& v : meshData.Vertices)
    {
        positions.push_back(v.Position);
        normals.push_back(v.Normal);
        texCoords.push_back(v.TexC);
        tangents.push_back(v.TangentU);
    }

    return BuildMeshlets(positions, normals, texCoords, tangents, meshData.Indices32, outMesh);
}

bool MeshletBuilder::LoadOBJ(const std::wstring& filename, MeshletMesh& outMesh)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        OutputDebugStringA("Failed to open OBJ file\n");
        return false;
    }

    std::vector<XMFLOAT3> tempPositions;
    std::vector<XMFLOAT3> tempNormals;
    std::vector<XMFLOAT2> tempTexCoords;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texCoords;
    std::vector<uint32_t> indices;

    std::unordered_map<std::string, uint32_t> vertexCache;

    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v")
        {
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        }
        else if (prefix == "vn")
        {
            XMFLOAT3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);
        }
        else if (prefix == "vt")
        {
            XMFLOAT2 tex;
            iss >> tex.x >> tex.y;
            tex.y = 1.0f - tex.y;
            tempTexCoords.push_back(tex);
        }
        else if (prefix == "f")
        {
            std::string vertex;
            std::vector<uint32_t> faceIndices;

            while (iss >> vertex)
            {
                auto it = vertexCache.find(vertex);
                if (it != vertexCache.end())
                {
                    faceIndices.push_back(it->second);
                    continue;
                }

                uint32_t posIdx = 0, texIdx = 0, normIdx = 0;
                
                if (sscanf_s(vertex.c_str(), "%u/%u/%u", &posIdx, &texIdx, &normIdx) == 3) {}
                else if (sscanf_s(vertex.c_str(), "%u//%u", &posIdx, &normIdx) == 2) { texIdx = 0; }
                else if (sscanf_s(vertex.c_str(), "%u/%u", &posIdx, &texIdx) == 2) { normIdx = 0; }
                else { sscanf_s(vertex.c_str(), "%u", &posIdx); texIdx = 0; normIdx = 0; }

                posIdx--;
                if (texIdx > 0) texIdx--;
                if (normIdx > 0) normIdx--;

                uint32_t newIndex = static_cast<uint32_t>(positions.size());
                
                positions.push_back(tempPositions[posIdx]);
                normals.push_back(normIdx < tempNormals.size() ? tempNormals[normIdx] : XMFLOAT3(0, 1, 0));
                texCoords.push_back(texIdx < tempTexCoords.size() ? tempTexCoords[texIdx] : XMFLOAT2(0, 0));

                vertexCache[vertex] = newIndex;
                faceIndices.push_back(newIndex);
            }

            for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
            {
                indices.push_back(faceIndices[0]);
                indices.push_back(faceIndices[i]);
                indices.push_back(faceIndices[i + 1]);
            }
        }
    }

    file.close();

    // If no texture coordinates were loaded, generate them using spherical mapping
    if (tempTexCoords.empty() && !positions.empty())
    {
        OutputDebugStringA("No UV coordinates in OBJ, generating spherical mapping...\n");
        
        // Find bounding sphere center
        XMVECTOR minPt = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
        XMVECTOR maxPt = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
        for (const auto& p : positions)
        {
            XMVECTOR pos = XMLoadFloat3(&p);
            minPt = XMVectorMin(minPt, pos);
            maxPt = XMVectorMax(maxPt, pos);
        }
        XMVECTOR center = (minPt + maxPt) * 0.5f;
        
        // Generate spherical UV coordinates
        texCoords.clear();
        texCoords.reserve(positions.size());
        for (const auto& p : positions)
        {
            XMVECTOR pos = XMLoadFloat3(&p);
            XMVECTOR dir = XMVector3Normalize(pos - center);
            
            XMFLOAT3 d;
            XMStoreFloat3(&d, dir);
            
            // Spherical mapping
            float u = 0.5f + atan2f(d.z, d.x) / (2.0f * XM_PI);
            float v = 0.5f - asinf(d.y) / XM_PI;
            
            texCoords.push_back(XMFLOAT2(u, v));
        }
    }

    std::vector<XMFLOAT3> tangents(positions.size(), XMFLOAT3(1, 0, 0));
    outMesh.Name = "OBJMesh";
    
    char buf[256];
    sprintf_s(buf, "Loaded OBJ: %zu vertices, %zu triangles, UVs: %s\n", 
        positions.size(), indices.size() / 3,
        tempTexCoords.empty() ? "generated" : "from file");
    OutputDebugStringA(buf);

    return BuildMeshlets(positions, normals, texCoords, tangents, indices, outMesh);
}

bool MeshletBuilder::LoadOBJWithDirectStorage(
    const std::wstring& filename,
    MeshletMesh& outMesh,
    DirectStorageLoader* storageLoader)
{
    if (!storageLoader)
    {
        OutputDebugStringA("DirectStorage loader is null, falling back to standard loading\n");
        return LoadOBJ(filename, outMesh);
    }

    // Load file data using DirectStorage
    std::vector<uint8_t> fileData;
    OutputDebugStringA("Loading OBJ file via DirectStorage...\n");
    
    if (!storageLoader->LoadFileToMemory(filename, fileData))
    {
        OutputDebugStringA("DirectStorage failed to load file, falling back to standard loading\n");
        return LoadOBJ(filename, outMesh);
    }

    OutputDebugStringA("DirectStorage: File loaded successfully, parsing OBJ...\n");

    // Parse OBJ from memory buffer
    std::string fileContent(reinterpret_cast<char*>(fileData.data()), fileData.size());
    std::istringstream fileStream(fileContent);

    std::vector<XMFLOAT3> tempPositions;
    std::vector<XMFLOAT3> tempNormals;
    std::vector<XMFLOAT2> tempTexCoords;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texCoords;
    std::vector<uint32_t> indices;

    std::unordered_map<std::string, uint32_t> vertexCache;

    std::string line;
    while (std::getline(fileStream, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v")
        {
            XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            tempPositions.push_back(pos);
        }
        else if (prefix == "vn")
        {
            XMFLOAT3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            tempNormals.push_back(norm);
        }
        else if (prefix == "vt")
        {
            XMFLOAT2 tex;
            iss >> tex.x >> tex.y;
            tex.y = 1.0f - tex.y;
            tempTexCoords.push_back(tex);
        }
        else if (prefix == "f")
        {
            std::string vertex;
            std::vector<uint32_t> faceIndices;

            while (iss >> vertex)
            {
                auto it = vertexCache.find(vertex);
                if (it != vertexCache.end())
                {
                    faceIndices.push_back(it->second);
                    continue;
                }

                uint32_t posIdx = 0, texIdx = 0, normIdx = 0;
                
                if (sscanf_s(vertex.c_str(), "%u/%u/%u", &posIdx, &texIdx, &normIdx) == 3) {}
                else if (sscanf_s(vertex.c_str(), "%u//%u", &posIdx, &normIdx) == 2) { texIdx = 0; }
                else if (sscanf_s(vertex.c_str(), "%u/%u", &posIdx, &texIdx) == 2) { normIdx = 0; }
                else { sscanf_s(vertex.c_str(), "%u", &posIdx); texIdx = 0; normIdx = 0; }

                posIdx--;
                if (texIdx > 0) texIdx--;
                if (normIdx > 0) normIdx--;

                uint32_t newIndex = static_cast<uint32_t>(positions.size());
                
                positions.push_back(tempPositions[posIdx]);
                normals.push_back(normIdx < tempNormals.size() ? tempNormals[normIdx] : XMFLOAT3(0, 1, 0));
                texCoords.push_back(texIdx < tempTexCoords.size() ? tempTexCoords[texIdx] : XMFLOAT2(0, 0));

                vertexCache[vertex] = newIndex;
                faceIndices.push_back(newIndex);
            }

            for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
            {
                indices.push_back(faceIndices[0]);
                indices.push_back(faceIndices[i]);
                indices.push_back(faceIndices[i + 1]);
            }
        }
    }

    // If no texture coordinates were loaded, generate them using spherical mapping
    if (tempTexCoords.empty() && !positions.empty())
    {
        OutputDebugStringA("No UV coordinates in OBJ, generating spherical mapping...\n");
        
        // Find bounding sphere center
        XMVECTOR minPt = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
        XMVECTOR maxPt = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
        for (const auto& p : positions)
        {
            XMVECTOR pos = XMLoadFloat3(&p);
            minPt = XMVectorMin(minPt, pos);
            maxPt = XMVectorMax(maxPt, pos);
        }
        XMVECTOR center = (minPt + maxPt) * 0.5f;
        
        // Generate spherical UV coordinates
        texCoords.clear();
        texCoords.reserve(positions.size());
        for (const auto& p : positions)
        {
            XMVECTOR pos = XMLoadFloat3(&p);
            XMVECTOR dir = XMVector3Normalize(pos - center);
            
            XMFLOAT3 d;
            XMStoreFloat3(&d, dir);
            
            // Spherical mapping
            float u = 0.5f + atan2f(d.z, d.x) / (2.0f * XM_PI);
            float v = 0.5f - asinf(d.y) / XM_PI;
            
            texCoords.push_back(XMFLOAT2(u, v));
        }
    }

    std::vector<XMFLOAT3> tangents(positions.size(), XMFLOAT3(1, 0, 0));
    outMesh.Name = "OBJMesh_DirectStorage";
    
    char buf[256];
    sprintf_s(buf, "DirectStorage: Loaded OBJ: %zu vertices, %zu triangles, UVs: %s\n", 
        positions.size(), indices.size() / 3, 
        tempTexCoords.empty() ? "generated" : "from file");
    OutputDebugStringA(buf);

    return BuildMeshlets(positions, normals, texCoords, tangents, indices, outMesh);
}


void MeshletBuilder::GenerateMeshletsSimple(
    const std::vector<uint32_t>& indices,
    uint32_t vertexCount,
    std::vector<MeshletData>& meshlets,
    std::vector<uint32_t>& uniqueVertexIndices,
    std::vector<uint8_t>& primitiveIndices)
{
    meshlets.clear();
    uniqueVertexIndices.clear();
    primitiveIndices.clear();

    std::unordered_map<uint32_t, uint8_t> vertexMap;

    MeshletData currentMeshlet = {};
    currentMeshlet.VertexOffset = 0;
    currentMeshlet.PrimitiveOffset = 0;

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        uint32_t newVertices = 0;
        if (vertexMap.find(i0) == vertexMap.end()) newVertices++;
        if (vertexMap.find(i1) == vertexMap.end()) newVertices++;
        if (vertexMap.find(i2) == vertexMap.end()) newVertices++;

        if (currentMeshlet.VertexCount + newVertices > MAX_MESHLET_VERTICES ||
            currentMeshlet.PrimitiveCount >= MAX_MESHLET_PRIMITIVES)
        {
            meshlets.push_back(currentMeshlet);
            vertexMap.clear();
            currentMeshlet.VertexOffset = static_cast<uint32_t>(uniqueVertexIndices.size());
            currentMeshlet.PrimitiveOffset = static_cast<uint32_t>(primitiveIndices.size());
            currentMeshlet.VertexCount = 0;
            currentMeshlet.PrimitiveCount = 0;
        }

        auto addVertex = [&](uint32_t idx) -> uint8_t {
            auto it = vertexMap.find(idx);
            if (it != vertexMap.end())
                return it->second;

            uint8_t localIdx = static_cast<uint8_t>(currentMeshlet.VertexCount);
            vertexMap[idx] = localIdx;
            uniqueVertexIndices.push_back(idx);
            currentMeshlet.VertexCount++;
            return localIdx;
        };

        uint8_t local0 = addVertex(i0);
        uint8_t local1 = addVertex(i1);
        uint8_t local2 = addVertex(i2);

        primitiveIndices.push_back(local0);
        primitiveIndices.push_back(local1);
        primitiveIndices.push_back(local2);
        currentMeshlet.PrimitiveCount++;
    }

    if (currentMeshlet.PrimitiveCount > 0)
        meshlets.push_back(currentMeshlet);
}

void MeshletBuilder::ComputeMeshletBounds(
    const std::vector<XMFLOAT3>& positions,
    const std::vector<uint32_t>& uniqueVertexIndices,
    const std::vector<uint8_t>& primitiveIndices,
    const MeshletData& meshlet,
    MeshletBounds& outBounds)
{
    // Initialize with defaults
    outBounds.Center = XMFLOAT3(0, 0, 0);
    outBounds.Radius = 1.0f;
    outBounds.ConeAxis = XMFLOAT3(0, 0, 1);
    outBounds.ConeCutoff = 1.0f;
    outBounds.ConeApex = XMFLOAT3(0, 0, 0);
    outBounds.Padding = 0;

    if (meshlet.VertexCount == 0 || uniqueVertexIndices.empty() || positions.empty())
        return;

    XMVECTOR minPt = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
    XMVECTOR maxPt = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);

    for (uint32_t i = 0; i < meshlet.VertexCount; ++i)
    {
        size_t idx = meshlet.VertexOffset + i;
        if (idx >= uniqueVertexIndices.size())
            continue;
        
        uint32_t vertIdx = uniqueVertexIndices[idx];
        if (vertIdx >= positions.size())
            continue;
            
        XMVECTOR pos = XMLoadFloat3(&positions[vertIdx]);
        minPt = XMVectorMin(minPt, pos);
        maxPt = XMVectorMax(maxPt, pos);
    }

    XMVECTOR center = (minPt + maxPt) * 0.5f;
    XMStoreFloat3(&outBounds.Center, center);

    float maxDist = 0.0f;
    for (uint32_t i = 0; i < meshlet.VertexCount; ++i)
    {
        size_t idx = meshlet.VertexOffset + i;
        if (idx >= uniqueVertexIndices.size())
            continue;
            
        uint32_t vertIdx = uniqueVertexIndices[idx];
        if (vertIdx >= positions.size())
            continue;
            
        XMVECTOR pos = XMLoadFloat3(&positions[vertIdx]);
        float dist = XMVectorGetX(XMVector3Length(pos - center));
        maxDist = (std::max)(maxDist, dist);
    }
    outBounds.Radius = maxDist > 0 ? maxDist : 1.0f;
    outBounds.ConeApex = outBounds.Center;
}

void MeshletBuilder::BuildLODHierarchy(MeshletMesh& mesh, uint32_t maxLODLevels)
{
    mesh.ClusterNodes.clear();
    uint32_t meshletCount = static_cast<uint32_t>(mesh.Meshlets.size());

    for (uint32_t i = 0; i < meshletCount; ++i)
    {
        ClusterNode node = {};
        node.MeshletStart = i;
        node.MeshletCount = 1;
        node.ParentIndex = UINT32_MAX;
        node.ChildStart = UINT32_MAX;
        node.ChildCount = 0;
        node.LODError = 0.0f;
        node.BoundCenter = mesh.MeshletBoundsData[i].Center;
        node.BoundRadius = mesh.MeshletBoundsData[i].Radius;
        mesh.ClusterNodes.push_back(node);
    }

    mesh.LODCount = 1;

    uint32_t currentLevelStart = 0;
    uint32_t currentLevelCount = meshletCount;

    for (uint32_t lod = 1; lod < maxLODLevels && currentLevelCount > 1; ++lod)
    {
        uint32_t newLevelStart = static_cast<uint32_t>(mesh.ClusterNodes.size());
        uint32_t groupSize = 4;

        for (uint32_t i = 0; i < currentLevelCount; i += groupSize)
        {
            ClusterNode parent = {};
            parent.ChildStart = currentLevelStart + i;
            parent.ChildCount = (std::min)(groupSize, currentLevelCount - i);
            parent.MeshletStart = mesh.ClusterNodes[parent.ChildStart].MeshletStart;
            parent.MeshletCount = 0;
            parent.ParentIndex = UINT32_MAX;
            parent.LODError = static_cast<float>(lod) * 0.1f;

            XMVECTOR minPt = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
            XMVECTOR maxPt = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);

            for (uint32_t j = 0; j < parent.ChildCount; ++j)
            {
                auto& child = mesh.ClusterNodes[parent.ChildStart + j];
                child.ParentIndex = static_cast<uint32_t>(mesh.ClusterNodes.size());
                parent.MeshletCount += child.MeshletCount;

                XMVECTOR c = XMLoadFloat3(&child.BoundCenter);
                float r = child.BoundRadius;
                minPt = XMVectorMin(minPt, c - XMVectorReplicate(r));
                maxPt = XMVectorMax(maxPt, c + XMVectorReplicate(r));
            }

            XMVECTOR center = (minPt + maxPt) * 0.5f;
            XMStoreFloat3(&parent.BoundCenter, center);
            parent.BoundRadius = XMVectorGetX(XMVector3Length(maxPt - center));

            mesh.ClusterNodes.push_back(parent);
        }

        currentLevelStart = newLevelStart;
        currentLevelCount = static_cast<uint32_t>(mesh.ClusterNodes.size()) - newLevelStart;
        mesh.LODCount++;
    }
}
