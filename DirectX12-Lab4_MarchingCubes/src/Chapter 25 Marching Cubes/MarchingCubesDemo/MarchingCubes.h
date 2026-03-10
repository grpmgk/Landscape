//***************************************************************************************
// MarchingCubes.h - Marching Cubes isosurface extraction
// Procedural fields: sphere, metaballs, 3D noise
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"
#include <vector>
#include <cstdint>

struct MCVertex
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
};

class MarchingCubes
{
public:
    enum class FieldType { Sphere = 0, Metaballs = 1, Noise = 2 };

    MarchingCubes() = default;

    // Grid: resolution^3 cells, domain [-1,1]^3
    void SetResolution(int resolution) { mResolution = (resolution < 4) ? 4 : resolution; }
    int GetResolution() const { return mResolution; }

    void SetIsoLevel(float iso) { mIsoLevel = iso; }
    float GetIsoLevel() const { return mIsoLevel; }

    void SetFieldType(FieldType t) { mFieldType = t; }
    FieldType GetFieldType() const { return mFieldType; }

    // Generate mesh into vertices and indices (triangle list)
    void Generate(std::vector<MCVertex>& outVertices, std::vector<uint32_t>& outIndices);

private:
    float SampleField(float x, float y, float z) const;
    float SampleFieldGradX(float x, float y, float z, float h) const;
    float SampleFieldGradY(float x, float y, float z, float h) const;
    float SampleFieldGradZ(float x, float y, float z, float h) const;
    DirectX::XMFLOAT3 Gradient(float x, float y, float z) const;

    // Procedural field implementations
    float FieldSphere(float x, float y, float z) const;
    float FieldMetaballs(float x, float y, float z) const;
    float FieldNoise(float x, float y, float z) const;
    float Noise3D(float x, float y, float z) const;

    int mResolution = 32;
    float mIsoLevel = 0.0f;
    FieldType mFieldType = FieldType::Sphere;

    // Metaball centers (world space in [-1,1])
    static constexpr int kMaxMetaballs = 4;
    DirectX::XMFLOAT3 mMetaballCenters[kMaxMetaballs] = {
        { 0.3f, 0.2f, 0.0f }, { -0.3f, -0.2f, 0.2f }, { 0.0f, 0.4f, -0.3f }, { -0.2f, 0.0f, 0.4f }
    };
};

// Lookup tables (extern in .cpp)
extern const int kMarchingCubesEdgeTable[256];
extern const int kMarchingCubesTriTable[256][16];
