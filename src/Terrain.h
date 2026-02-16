#pragma once

#include "Common/d3dUtil.h"
#include "Common/MathHelper.h"
#include "QuadTree.h"

#include <vector>
#include <memory>

struct TerrainVertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
};

class Terrain
{
public:
    Terrain(ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList,
        float terrainSize,
        float minHeight,
        float maxHeight);

    ~Terrain() = default;

    // Heightmap loading
    bool LoadHeightmap(const std::wstring& filename,
        UINT width,
        UINT height,
        bool is16Bit = true);

    bool LoadHeightmapDDS(const std::wstring& filename,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList);

    // Heightmap generation
    void GenerateProceduralHeightmap(UINT width,
        UINT height,
        float frequency,
        int octaves);

    // Build LOD meshes into a single MeshGeometry container
    void BuildGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    // Query helpers
    float GetHeight(float x, float z) const;
    DirectX::XMFLOAT3 GetNormal(float x, float z) const;

    // Basic info
    float GetTerrainSize() const { return mTerrainSize; }
    float GetMinHeight() const { return mMinHeight; }
    float GetMaxHeight() const { return mMaxHeight; }
    UINT  GetHeightmapWidth() const { return mHeightmapWidth; }
    UINT  GetHeightmapHeight() const { return mHeightmapHeight; }

    // Rendering resources
    MeshGeometry* GetGeometry() { return mGeometry.get(); }
    ID3D12Resource* GetHeightmapResource() { return mHeightmapTexture.Get(); }

    // LOD mesh naming (used by renderer)
    static const char* GetLODMeshName(int lod);

private:
    // Mesh/heightmap internals
    void BuildLODMesh(int lodLevel, UINT gridSize);
    void CalculateNormals();
    float SampleHeight(int x, int z) const;

    // Procedural noise utilities (Perlin)
    float PerlinNoise(float x, float z) const;
    float Fade(float t) const;
    float Lerp(float a, float b, float t) const;
    float Grad(int hash, float x, float z) const;

private:
    ID3D12Device* md3dDevice = nullptr;

    float mTerrainSize = 0.0f;
    float mMinHeight = 0.0f;
    float mMaxHeight = 0.0f;

    UINT mHeightmapWidth = 0;
    UINT mHeightmapHeight = 0;

    // Normalized [0..1] height values (CPU copy)
    std::vector<float> mHeightmap;

    // GPU resources (optional if heightmap comes from DDS)
    std::unique_ptr<MeshGeometry> mGeometry;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightmapTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightmapUploadBuffer;

    // Permutation table for Perlin noise
    std::vector<int> mPermutation;
};
