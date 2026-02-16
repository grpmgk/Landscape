#include "Terrain.h"
#include "Common/DDSTextureLoader.h"

#include <fstream>
#include <random>
#include <cmath>
#include <algorithm>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const char* Terrain::GetLODMeshName(int lod)
{
    // Keep exact names because renderer expects them (DrawArgs lookup).
    static const char* kNames[5] = { "lod0", "lod1", "lod2", "lod3", "lod4" };
    if (lod < 0 || lod >= 5) return kNames[0];
    return kNames[lod];
}

Terrain::Terrain(ID3D12Device* device,
    ID3D12GraphicsCommandList* /*cmdList*/,
    float terrainSize,
    float minHeight,
    float maxHeight)
    : md3dDevice(device)
    , mTerrainSize(terrainSize)
    , mMinHeight(minHeight)
    , mMaxHeight(maxHeight)
{
    // Permutation table (256 duplicated -> 512) for Perlin noise
    std::vector<int> base(256);
    for (int i = 0; i < 256; ++i) base[i] = i;

    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(base.begin(), base.end(), rng);

    mPermutation.assign(512, 0);
    for (int i = 0; i < 256; ++i)
    {
        mPermutation[i] = base[i];
        mPermutation[i + 256] = base[i];
    }
}

bool Terrain::LoadHeightmap(const std::wstring& filename, UINT width, UINT height, bool is16Bit)
{
    std::ifstream in(filename, std::ios::binary);
    if (!in) return false;

    mHeightmapWidth = width;
    mHeightmapHeight = height;
    mHeightmap.assign((size_t)width * (size_t)height, 0.0f);

    const size_t count = (size_t)width * (size_t)height;

    if (is16Bit)
    {
        std::vector<std::uint16_t> raw(count);
        in.read(reinterpret_cast<char*>(raw.data()), (std::streamsize)(count * sizeof(std::uint16_t)));

        for (size_t i = 0; i < count; ++i)
            mHeightmap[i] = raw[i] / 65535.0f;
    }
    else
    {
        std::vector<std::uint8_t> raw(count);
        in.read(reinterpret_cast<char*>(raw.data()), (std::streamsize)(count * sizeof(std::uint8_t)));

        for (size_t i = 0; i < count; ++i)
            mHeightmap[i] = raw[i] / 255.0f;
    }

    return true;
}

bool Terrain::LoadHeightmapDDS(const std::wstring& filename, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    // GPU texture load
    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        device, cmdList, filename.c_str(),
        mHeightmapTexture, mHeightmapUploadBuffer);

    if (FAILED(hr))
        return false;

    const D3D12_RESOURCE_DESC desc = mHeightmapTexture->GetDesc();
    mHeightmapWidth = (UINT)desc.Width;
    mHeightmapHeight = desc.Height;

    // CPU-side heightmap is still needed for GetHeight/GetNormal queries.
    // Here we keep the same "procedural fallback" behavior.
    mHeightmap.assign((size_t)mHeightmapWidth * (size_t)mHeightmapHeight, 0.0f);

    for (UINT z = 0; z < mHeightmapHeight; ++z)
    {
        for (UINT x = 0; x < mHeightmapWidth; ++x)
        {
            const float fx = (float)x / (float)mHeightmapWidth;
            const float fz = (float)z / (float)mHeightmapHeight;

            const float n = PerlinNoise(fx * 4.0f, fz * 4.0f);
            mHeightmap[(size_t)z * (size_t)mHeightmapWidth + x] = n * 0.5f + 0.5f;
        }
    }

    return true;
}

void Terrain::GenerateProceduralHeightmap(UINT width, UINT height, float frequency, int octaves)
{
    mHeightmapWidth = width;
    mHeightmapHeight = height;
    mHeightmap.assign((size_t)width * (size_t)height, 0.0f);

    float hi = 0.0f;
    float lo = 1.0f;

    for (UINT z = 0; z < height; ++z)
    {
        for (UINT x = 0; x < width; ++x)
        {
            const float nx = (float)x / (float)width;
            const float nz = (float)z / (float)height;

            float sum = 0.0f;
            float amp = 1.0f;
            float freq = frequency;
            float ampSum = 0.0f;

            for (int o = 0; o < octaves; ++o)
            {
                sum += PerlinNoise(nx * freq, nz * freq) * amp;
                ampSum += amp;

                amp *= 0.5f;
                freq *= 2.0f;
            }

            float v = (sum / ampSum + 1.0f) * 0.5f;
            mHeightmap[(size_t)z * (size_t)width + x] = v;

            hi = (std::max)(hi, v);
            lo = (std::min)(lo, v);
        }
    }

    // Normalize to [0..1]
    const float range = hi - lo;
    if (range > 0.001f)
    {
        for (float& h : mHeightmap)
            h = (h - lo) / range;
    }
}

void Terrain::BuildGeometry(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    mGeometry = std::make_unique<MeshGeometry>();
    mGeometry->Name = "terrainGeo";

    std::vector<TerrainVertex> verts;
    std::vector<std::uint32_t> inds;

    // LOD0..LOD4 grid sizes
    const UINT kGrid[5] = { 256, 128, 64, 32, 16 };

    for (int lod = 0; lod < 5; ++lod)
    {
        const UINT grid = kGrid[lod];

        const UINT vtxStart = (UINT)verts.size();
        const UINT idxStart = (UINT)inds.size();

        const float inv = 1.0f / (float)grid;

        // Vertices: unit patch centered at origin
        for (UINT z = 0; z <= grid; ++z)
        {
            const float w = z * inv;
            for (UINT x = 0; x <= grid; ++x)
            {
                const float u = x * inv;

                TerrainVertex tv{};
                tv.Pos = XMFLOAT3(u - 0.5f, 0.0f, w - 0.5f);
                tv.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                tv.TexC = XMFLOAT2(u, w);

                verts.push_back(tv);
            }
        }

        // Indices: 2 triangles per quad
        const UINT rowStride = grid + 1;
        for (UINT z = 0; z < grid; ++z)
        {
            for (UINT x = 0; x < grid; ++x)
            {
                const UINT i0 = vtxStart + z * rowStride + x;
                const UINT i1 = i0 + 1;
                const UINT i2 = vtxStart + (z + 1) * rowStride + x;
                const UINT i3 = i2 + 1;

                inds.push_back(i0);
                inds.push_back(i2);
                inds.push_back(i1);

                inds.push_back(i1);
                inds.push_back(i2);
                inds.push_back(i3);
            }
        }

        SubmeshGeometry sm{};
        sm.IndexCount = grid * grid * 6;
        sm.StartIndexLocation = idxStart;
        sm.BaseVertexLocation = 0; // keep same behavior as original

        mGeometry->DrawArgs[GetLODMeshName(lod)] = sm;
    }

    const UINT vbBytes = (UINT)verts.size() * sizeof(TerrainVertex);
    const UINT ibBytes = (UINT)inds.size() * sizeof(std::uint32_t);

    ThrowIfFailed(D3DCreateBlob(vbBytes, &mGeometry->VertexBufferCPU));
    std::memcpy(mGeometry->VertexBufferCPU->GetBufferPointer(), verts.data(), vbBytes);

    ThrowIfFailed(D3DCreateBlob(ibBytes, &mGeometry->IndexBufferCPU));
    std::memcpy(mGeometry->IndexBufferCPU->GetBufferPointer(), inds.data(), ibBytes);

    mGeometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        device, cmdList, verts.data(), vbBytes, mGeometry->VertexBufferUploader);

    mGeometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        device, cmdList, inds.data(), ibBytes, mGeometry->IndexBufferUploader);

    mGeometry->VertexByteStride = sizeof(TerrainVertex);
    mGeometry->VertexBufferByteSize = vbBytes;
    mGeometry->IndexFormat = DXGI_FORMAT_R32_UINT;
    mGeometry->IndexBufferByteSize = ibBytes;
}

float Terrain::GetHeight(float x, float z) const
{
    if (mHeightmap.empty())
        return 0.0f;

    // World -> heightmap space (same mapping)
    const float u = (x / mTerrainSize + 0.5f) * (float)mHeightmapWidth;
    const float v = (z / mTerrainSize + 0.5f) * (float)mHeightmapHeight;

    const int x0 = (int)std::floor(u);
    const int z0 = (int)std::floor(v);

    const float fx = u - (float)x0;
    const float fz = v - (float)z0;

    // Bilinear
    const float h00 = SampleHeight(x0, z0);
    const float h10 = SampleHeight(x0 + 1, z0);
    const float h01 = SampleHeight(x0, z0 + 1);
    const float h11 = SampleHeight(x0 + 1, z0 + 1);

    const float hx0 = h00 + (h10 - h00) * fx;
    const float hx1 = h01 + (h11 - h01) * fx;
    const float h = hx0 + (hx1 - hx0) * fz;

    return mMinHeight + h * (mMaxHeight - mMinHeight);
}

XMFLOAT3 Terrain::GetNormal(float x, float z) const
{
    const float step = mTerrainSize / (float)mHeightmapWidth;

    const float hL = GetHeight(x - step, z);
    const float hR = GetHeight(x + step, z);
    const float hD = GetHeight(x, z - step);
    const float hU = GetHeight(x, z + step);

    XMFLOAT3 n;
    n.x = hL - hR;
    n.y = 2.0f * step;
    n.z = hD - hU;

    XMVECTOR nn = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, nn);
    return n;
}

float Terrain::SampleHeight(int x, int z) const
{
    // Clamp to edges (same as before)
    const int maxX = (int)mHeightmapWidth - 1;
    const int maxZ = (int)mHeightmapHeight - 1;

    x = (std::max)(0, (std::min)(x, maxX));
    z = (std::max)(0, (std::min)(z, maxZ));

    return mHeightmap[(size_t)z * (size_t)mHeightmapWidth + (size_t)x];
}

float Terrain::PerlinNoise(float x, float z) const
{
    // Same Perlin as original, just reorganized
    const int X = ((int)std::floor(x)) & 255;
    const int Z = ((int)std::floor(z)) & 255;

    x -= std::floor(x);
    z -= std::floor(z);

    const float u = Fade(x);
    const float v = Fade(z);

    const int A = mPermutation[X] + Z;
    const int B = mPermutation[X + 1] + Z;

    const float g00 = Grad(mPermutation[A], x, z);
    const float g10 = Grad(mPermutation[B], x - 1, z);
    const float g01 = Grad(mPermutation[A + 1], x, z - 1);
    const float g11 = Grad(mPermutation[B + 1], x - 1, z - 1);

    const float ix0 = Lerp(g00, g10, u);
    const float ix1 = Lerp(g01, g11, u);

    return Lerp(ix0, ix1, v);
}

float Terrain::Fade(float t) const
{
    // 6t^5 - 15t^4 + 10t^3
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Terrain::Lerp(float a, float b, float t) const
{
    return a + t * (b - a);
}

float Terrain::Grad(int hash, float x, float z) const
{
    const int h = hash & 3;
    const float u = (h < 2) ? x : z;
    const float v = (h < 2) ? z : x;

    const float a = (h & 1) ? -u : u;
    const float b = (h & 2) ? -v : v;
    return a + b;
}
