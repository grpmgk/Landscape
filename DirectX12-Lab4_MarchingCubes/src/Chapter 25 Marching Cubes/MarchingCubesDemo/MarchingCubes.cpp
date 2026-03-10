//***************************************************************************************
// MarchingCubes.cpp - Isosurface extraction (Paul Bourke / Lorensen-Cline)
//***************************************************************************************

#include "MarchingCubes.h"
#include <cmath>
#include <algorithm>

// Edge table: 12-bit flag per cube config (which edges are cut)
static const int sEdgeTable[256] = {
    0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
};

// Cube corner positions (0-7) relative to cell origin
static const float sCornerOffsets[8][3] = {
    {0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
    {0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}
};
// Edge endpoints: edge e connects corner sEdgeConn[e][0] and sEdgeConn[e][1]
static const int sEdgeConn[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

// Triangle table: for each of 256 cases, list of edge indices (3 per triangle), -1 terminates
#include "MarchingCubesTriTable.inc"

void MarchingCubes::Generate(std::vector<MCVertex>& outVertices, std::vector<uint32_t>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    const int n = mResolution;
    const float scale = 2.0f / (float)n;  // cell size in [-1,1] space
    const float h = scale * 0.5f;  // for gradient sampling

    for (int k = 0; k < n; ++k)
    for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i)
    {
        float cornerVal[8];
        DirectX::XMFLOAT3 cornerPos[8];
        for (int c = 0; c < 8; ++c)
        {
            float gx = (float)(i + (int)(sCornerOffsets[c][0]));
            float gy = (float)(j + (int)(sCornerOffsets[c][1]));
            float gz = (float)(k + (int)(sCornerOffsets[c][2]));
            float x = -1.0f + 2.0f * gx / (float)n;
            float y = -1.0f + 2.0f * gy / (float)n;
            float z = -1.0f + 2.0f * gz / (float)n;
            cornerPos[c] = DirectX::XMFLOAT3(x, y, z);
            cornerVal[c] = SampleField(x, y, z);
        }

        int cubeIndex = 0;
        for (int c = 0; c < 8; ++c)
            if (cornerVal[c] <= mIsoLevel)
                cubeIndex |= (1 << c);

        int edges = sEdgeTable[cubeIndex];
        if (edges == 0) continue;

        // Interpolate vertex positions on each of the 12 edges
        DirectX::XMFLOAT3 vertList[12];
        for (int e = 0; e < 12; ++e)
        {
            if (!(edges & (1 << e))) continue;
            int a = sEdgeConn[e][0], b = sEdgeConn[e][1];
            float v1 = cornerVal[a], v2 = cornerVal[b];
            float t = (std::abs(v2 - v1) < 1e-6f) ? 0.5f : (mIsoLevel - v1) / (v2 - v1);
            t = (std::max)(0.0f, (std::min)(1.0f, t));
            vertList[e].x = cornerPos[a].x + t * (cornerPos[b].x - cornerPos[a].x);
            vertList[e].y = cornerPos[a].y + t * (cornerPos[b].y - cornerPos[a].y);
            vertList[e].z = cornerPos[a].z + t * (cornerPos[b].z - cornerPos[a].z);
        }

        // Output triangles from tri table
        const int* tri = kMarchingCubesTriTable[cubeIndex];
        for (int t = 0; t < 16 && tri[t] >= 0; t += 3)
        {
            int i0 = tri[t], i1 = tri[t+1], i2 = tri[t+2];
            DirectX::XMFLOAT3 n0 = Gradient(vertList[i0].x, vertList[i0].y, vertList[i0].z);
            DirectX::XMFLOAT3 n1 = Gradient(vertList[i1].x, vertList[i1].y, vertList[i1].z);
            DirectX::XMFLOAT3 n2 = Gradient(vertList[i2].x, vertList[i2].y, vertList[i2].z);
            uint32_t base = (uint32_t)outVertices.size();
            outVertices.push_back({ vertList[i0], n0 });
            outVertices.push_back({ vertList[i1], n1 });
            outVertices.push_back({ vertList[i2], n2 });
            outIndices.push_back(base);
            outIndices.push_back(base + 1);
            outIndices.push_back(base + 2);
        }
    }
}

float MarchingCubes::SampleField(float x, float y, float z) const
{
    switch (mFieldType)
    {
    case FieldType::Sphere: return FieldSphere(x, y, z);
    case FieldType::Metaballs: return FieldMetaballs(x, y, z);
    case FieldType::Noise: return FieldNoise(x, y, z);
    }
    return 0.0f;
}

float MarchingCubes::FieldSphere(float x, float y, float z) const
{
    float dx = x, dy = y, dz = z;
    float r = 0.7f;
    return r * r - (dx*dx + dy*dy + dz*dz);
}

float MarchingCubes::FieldMetaballs(float x, float y, float z) const
{
    float sum = 0.0f;
    for (int i = 0; i < kMaxMetaballs; ++i)
    {
        float dx = x - mMetaballCenters[i].x;
        float dy = y - mMetaballCenters[i].y;
        float dz = z - mMetaballCenters[i].z;
        float d2 = dx*dx + dy*dy + dz*dz + 0.01f;
        sum += 0.15f / d2;
    }
    return sum - 1.0f;
}

// Simple 3D value noise (hash-based)
static float hash(float n) { return std::fmod(std::sin(n) * 43758.5453f, 1.0f); }
static float noise3(float x, float y, float z)
{
    float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    fx = fx*fx*(3.f - 2.f*fx);
    fy = fy*fy*(3.f - 2.f*fy);
    fz = fz*fz*(3.f - 2.f*fz);
    int i = (int)ix, j = (int)iy, k = (int)iz;
    float n000 = hash((float)(i + j*57 + k*131));
    float n100 = hash((float)(i+1 + j*57 + k*131));
    float n010 = hash((float)(i + (j+1)*57 + k*131));
    float n110 = hash((float)(i+1 + (j+1)*57 + k*131));
    float n001 = hash((float)(i + j*57 + (k+1)*131));
    float n101 = hash((float)(i+1 + j*57 + (k+1)*131));
    float n011 = hash((float)(i + (j+1)*57 + (k+1)*131));
    float n111 = hash((float)(i+1 + (j+1)*57 + (k+1)*131));
    float nx00 = n000*(1.f-fx) + n100*fx;
    float nx10 = n010*(1.f-fx) + n110*fx;
    float nx01 = n001*(1.f-fx) + n101*fx;
    float nx11 = n011*(1.f-fx) + n111*fx;
    float nxy0 = nx00*(1.f-fy) + nx10*fy;
    float nxy1 = nx01*(1.f-fy) + nx11*fy;
    return nxy0*(1.f-fz) + nxy1*fz;
}

float MarchingCubes::Noise3D(float x, float y, float z) const
{
    float s = 3.0f;
    return noise3(x*s, y*s, z*s) + 0.5f*noise3(x*s*2.f, y*s*2.f, z*s*2.f) - 0.5f;
}

float MarchingCubes::FieldNoise(float x, float y, float z) const
{
    return Noise3D(x, y, z);
}

float MarchingCubes::SampleFieldGradX(float x, float y, float z, float h) const
{
    return (SampleField(x + h, y, z) - SampleField(x - h, y, z)) / (2.0f * h);
}
float MarchingCubes::SampleFieldGradY(float x, float y, float z, float h) const
{
    return (SampleField(x, y + h, z) - SampleField(x, y - h, z)) / (2.0f * h);
}
float MarchingCubes::SampleFieldGradZ(float x, float y, float z, float h) const
{
    return (SampleField(x, y, z + h) - SampleField(x, y, z - h)) / (2.0f * h);
}

DirectX::XMFLOAT3 MarchingCubes::Gradient(float x, float y, float z) const
{
    const float h = 0.02f;
    float gx = SampleFieldGradX(x, y, z, h);
    float gy = SampleFieldGradY(x, y, z, h);
    float gz = SampleFieldGradZ(x, y, z, h);
    float len = std::sqrt(gx*gx + gy*gy + gz*gz);
    if (len < 1e-6f) return DirectX::XMFLOAT3(1, 0, 0);
    len = 1.0f / len;
    return DirectX::XMFLOAT3(gx*len, gy*len, gz*len);
}

const int kMarchingCubesEdgeTable[256] = {
    0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
};
