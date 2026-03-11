//***************************************************************************************
// Terrain.h - Terrain rendering with heightmap
//
// Features:
// - Heightmap-based terrain generation
// - Multiple LOD levels with different mesh densities
// - Normal map generation from heightmap
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include "QuadTree.h"
#include <vector>

struct TerrainVertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexC;
};

class Terrain
{
public:
    Terrain(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
            float terrainSize, float minHeight, float maxHeight);
    ~Terrain() = default;
    
    // Load heightmap from file (raw 16-bit or 8-bit)
    bool LoadHeightmap(const std::wstring& filename, UINT width, UINT height, bool is16Bit = true);
    
    // Load heightmap from DDS file
    bool LoadHeightmapDDS(const std::wstring& filename, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    
    // Generate procedural heightmap
    void GenerateProceduralHeightmap(UINT width, UINT height, float frequency, int octaves);
    
    // Build terrain meshes for different LOD levels
    void BuildGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    
    // Get height at world position
    float GetHeight(float x, float z) const;
    
    // Get normal at world position
    DirectX::XMFLOAT3 GetNormal(float x, float z) const;
    
    // Accessors
    float GetTerrainSize() const { return mTerrainSize; }
    float GetMinHeight() const { return mMinHeight; }
    float GetMaxHeight() const { return mMaxHeight; }
    UINT GetHeightmapWidth() const { return mHeightmapWidth; }
    UINT GetHeightmapHeight() const { return mHeightmapHeight; }
    
    // Get mesh geometry for rendering
    MeshGeometry* GetGeometry() { return mGeometry.get(); }
    
    // Get heightmap as texture resource
    ID3D12Resource* GetHeightmapResource() { return mHeightmapTexture.Get(); }
    
    // LOD mesh names
    static const char* GetLODMeshName(int lod);
    
private:
    void BuildLODMesh(int lodLevel, UINT gridSize);
    void CalculateNormals();
    float SampleHeight(int x, int z) const;
    
    // Perlin noise for procedural generation
    float PerlinNoise(float x, float z) const;
    float Fade(float t) const;
    float Lerp(float a, float b, float t) const;
    float Grad(int hash, float x, float z) const;
    
private:
    ID3D12Device* md3dDevice = nullptr;
    
    float mTerrainSize;
    float mMinHeight;
    float mMaxHeight;
    
    UINT mHeightmapWidth = 0;
    UINT mHeightmapHeight = 0;
    std::vector<float> mHeightmap; // Normalized [0,1] heights
    
    std::unique_ptr<MeshGeometry> mGeometry;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightmapTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHeightmapUploadBuffer;
    
    // Permutation table for Perlin noise
    std::vector<int> mPermutation;
};
