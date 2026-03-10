//***************************************************************************************
// Terrain.cpp - Terrain implementation
//***************************************************************************************

#include "Terrain.h"
#include "../../Common/DDSTextureLoader.h"
#include <fstream>
#include <random>
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const char* Terrain::GetLODMeshName(int lod)
{
    static const char* names[] = { "lod0", "lod1", "lod2", "lod3", "lod4" };
    return (lod >= 0 && lod < 5) ? names[lod] : "lod0";
}

Terrain::Terrain(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                 float terrainSize, float minHeight, float maxHeight)
    : md3dDevice(device), mTerrainSize(terrainSize), 
      mMinHeight(minHeight), mMaxHeight(maxHeight)
{
    // Initialize permutation table for Perlin noise
    mPermutation.resize(512);
    std::vector<int> p(256);
    for (int i = 0; i < 256; ++i) p[i] = i;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(p.begin(), p.end(), gen);
    
    for (int i = 0; i < 256; ++i)
    {
        mPermutation[i] = p[i];
        mPermutation[256 + i] = p[i];
    }
}

bool Terrain::LoadHeightmap(const std::wstring& filename, UINT width, UINT height, bool is16Bit)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
        return false;
    
    mHeightmapWidth = width;
    mHeightmapHeight = height;
    mHeightmap.resize(width * height);
    
    if (is16Bit)
    {
        std::vector<uint16_t> rawData(width * height);
        file.read(reinterpret_cast<char*>(rawData.data()), width * height * 2);
        
        for (UINT i = 0; i < width * height; ++i)
            mHeightmap[i] = rawData[i] / 65535.0f;
    }
    else
    {
        std::vector<uint8_t> rawData(width * height);
        file.read(reinterpret_cast<char*>(rawData.data()), width * height);
        
        for (UINT i = 0; i < width * height; ++i)
            mHeightmap[i] = rawData[i] / 255.0f;
    }
    
    file.close();
    return true;
}

bool Terrain::LoadHeightmapDDS(const std::wstring& filename, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // Try to load DDS texture using CreateDDSTextureFromFile12
    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        device, cmdList, filename.c_str(),
        mHeightmapTexture, mHeightmapUploadBuffer);
    
    if (FAILED(hr))
        return false;
    
    // Get texture description
    D3D12_RESOURCE_DESC texDesc = mHeightmapTexture->GetDesc();
    mHeightmapWidth = (UINT)texDesc.Width;
    mHeightmapHeight = texDesc.Height;
    
    // Generate CPU-side heightmap for collision/sampling
    // For now, generate procedural as fallback for CPU queries
    mHeightmap.resize(mHeightmapWidth * mHeightmapHeight);
    for (UINT z = 0; z < mHeightmapHeight; ++z)
    {
        for (UINT x = 0; x < mHeightmapWidth; ++x)
        {
            float nx = (float)x / mHeightmapWidth;
            float nz = (float)z / mHeightmapHeight;
            // Simple procedural fallback for CPU height queries
            mHeightmap[z * mHeightmapWidth + x] = PerlinNoise(nx * 4.0f, nz * 4.0f) * 0.5f + 0.5f;
        }
    }
    
    return true;
}

void Terrain::GenerateProceduralHeightmap(UINT width, UINT height, float frequency, int octaves)
{
    mHeightmapWidth = width;
    mHeightmapHeight = height;
    mHeightmap.resize(width * height);
    
    float maxVal = 0.0f;
    float minVal = 1.0f;
    
    for (UINT z = 0; z < height; ++z)
    {
        for (UINT x = 0; x < width; ++x)
        {
            float nx = (float)x / width;
            float nz = (float)z / height;
            
            float value = 0.0f;
            float amplitude = 1.0f;
            float freq = frequency;
            float maxAmplitude = 0.0f;
            
            for (int o = 0; o < octaves; ++o)
            {
                value += PerlinNoise(nx * freq, nz * freq) * amplitude;
                maxAmplitude += amplitude;
                amplitude *= 0.5f;
                freq *= 2.0f;
            }
            
            value = (value / maxAmplitude + 1.0f) * 0.5f;
            mHeightmap[z * width + x] = value;
            
            maxVal = max(maxVal, value);
            minVal = min(minVal, value);
        }
    }
    
    // Normalize to [0,1]
    float range = maxVal - minVal;
    if (range > 0.001f)
    {
        for (auto& h : mHeightmap)
            h = (h - minVal) / range;
    }
}

void Terrain::BuildGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    mGeometry = std::make_unique<MeshGeometry>();
    mGeometry->Name = "terrainGeo";
    
    std::vector<TerrainVertex> allVertices;
    std::vector<std::uint32_t> allIndices;
    
    // Build LOD meshes: LOD0=256x256, LOD1=128x128, LOD2=64x64, LOD3=32x32, LOD4=16x16
    UINT lodSizes[] = { 256, 128, 64, 32, 16 };
    
    for (int lod = 0; lod < 5; ++lod)
    {
        UINT gridSize = lodSizes[lod];
        UINT vertexOffset = (UINT)allVertices.size();
        UINT indexOffset = (UINT)allIndices.size();
        
        // Generate grid vertices
        float step = 1.0f / gridSize;
        for (UINT z = 0; z <= gridSize; ++z)
        {
            for (UINT x = 0; x <= gridSize; ++x)
            {
                TerrainVertex v;
                float u = x * step;
                float w = z * step;
                
                v.Pos = XMFLOAT3(u - 0.5f, 0.0f, w - 0.5f); // Centered at origin, unit size
                v.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                v.TexC = XMFLOAT2(u, w);
                
                allVertices.push_back(v);
            }
        }
        
        // Generate indices
        for (UINT z = 0; z < gridSize; ++z)
        {
            for (UINT x = 0; x < gridSize; ++x)
            {
                UINT topLeft = z * (gridSize + 1) + x + vertexOffset;
                UINT topRight = topLeft + 1;
                UINT bottomLeft = (z + 1) * (gridSize + 1) + x + vertexOffset;
                UINT bottomRight = bottomLeft + 1;
                
                // Two triangles per quad
                allIndices.push_back(topLeft);
                allIndices.push_back(bottomLeft);
                allIndices.push_back(topRight);
                
                allIndices.push_back(topRight);
                allIndices.push_back(bottomLeft);
                allIndices.push_back(bottomRight);
            }
        }
        
        SubmeshGeometry submesh;
        submesh.IndexCount = gridSize * gridSize * 6;
        submesh.StartIndexLocation = indexOffset;
        submesh.BaseVertexLocation = 0;
        
        mGeometry->DrawArgs[GetLODMeshName(lod)] = submesh;
    }
    
    const UINT vbByteSize = (UINT)allVertices.size() * sizeof(TerrainVertex);
    const UINT ibByteSize = (UINT)allIndices.size() * sizeof(std::uint32_t);
    
    ThrowIfFailed(D3DCreateBlob(vbByteSize, &mGeometry->VertexBufferCPU));
    CopyMemory(mGeometry->VertexBufferCPU->GetBufferPointer(), allVertices.data(), vbByteSize);
    
    ThrowIfFailed(D3DCreateBlob(ibByteSize, &mGeometry->IndexBufferCPU));
    CopyMemory(mGeometry->IndexBufferCPU->GetBufferPointer(), allIndices.data(), ibByteSize);
    
    mGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList,
        allVertices.data(), vbByteSize, mGeometry->VertexBufferUploader);
    
    mGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList,
        allIndices.data(), ibByteSize, mGeometry->IndexBufferUploader);
    
    mGeometry->VertexByteStride = sizeof(TerrainVertex);
    mGeometry->VertexBufferByteSize = vbByteSize;
    mGeometry->IndexFormat = DXGI_FORMAT_R32_UINT;
    mGeometry->IndexBufferByteSize = ibByteSize;
}

float Terrain::GetHeight(float x, float z) const
{
    if (mHeightmap.empty()) return 0.0f;
    
    // Convert world coords to heightmap coords
    float u = (x / mTerrainSize + 0.5f) * mHeightmapWidth;
    float v = (z / mTerrainSize + 0.5f) * mHeightmapHeight;
    
    int x0 = (int)floorf(u);
    int z0 = (int)floorf(v);
    float fx = u - x0;
    float fz = v - z0;
    
    // Bilinear interpolation
    float h00 = SampleHeight(x0, z0);
    float h10 = SampleHeight(x0 + 1, z0);
    float h01 = SampleHeight(x0, z0 + 1);
    float h11 = SampleHeight(x0 + 1, z0 + 1);
    
    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    float h = h0 + (h1 - h0) * fz;
    
    return mMinHeight + h * (mMaxHeight - mMinHeight);
}

XMFLOAT3 Terrain::GetNormal(float x, float z) const
{
    float delta = mTerrainSize / mHeightmapWidth;
    float hL = GetHeight(x - delta, z);
    float hR = GetHeight(x + delta, z);
    float hD = GetHeight(x, z - delta);
    float hU = GetHeight(x, z + delta);
    
    XMFLOAT3 normal;
    normal.x = hL - hR;
    normal.y = 2.0f * delta;
    normal.z = hD - hU;
    
    XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&normal));
    XMStoreFloat3(&normal, n);
    return normal;
}

float Terrain::SampleHeight(int x, int z) const
{
    x = max(0, min(x, (int)mHeightmapWidth - 1));
    z = max(0, min(z, (int)mHeightmapHeight - 1));
    return mHeightmap[z * mHeightmapWidth + x];
}

float Terrain::PerlinNoise(float x, float z) const
{
    int X = (int)floorf(x) & 255;
    int Z = (int)floorf(z) & 255;
    
    x -= floorf(x);
    z -= floorf(z);
    
    float u = Fade(x);
    float v = Fade(z);
    
    int A = mPermutation[X] + Z;
    int B = mPermutation[X + 1] + Z;
    
    return Lerp(
        Lerp(Grad(mPermutation[A], x, z), Grad(mPermutation[B], x - 1, z), u),
        Lerp(Grad(mPermutation[A + 1], x, z - 1), Grad(mPermutation[B + 1], x - 1, z - 1), u),
        v);
}

float Terrain::Fade(float t) const { return t * t * t * (t * (t * 6 - 15) + 10); }
float Terrain::Lerp(float a, float b, float t) const { return a + t * (b - a); }

float Terrain::Grad(int hash, float x, float z) const
{
    int h = hash & 3;
    float u = h < 2 ? x : z;
    float v = h < 2 ? z : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}
