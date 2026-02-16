#pragma once

#include "Common/d3dUtil.h"
#include "Common/MathHelper.h"

#include <vector>
#include <memory>

// Axis-aligned bounding box used for frustum tests.
struct BoundingBoxAABB
{
    DirectX::XMFLOAT3 Center{};
    DirectX::XMFLOAT3 Extents{};

    bool Intersects(const DirectX::XMFLOAT4* frustumPlanes) const;
};

// Terrain tile node (quadtree).
struct TerrainNode
{
    // Spatial info (center in world space)
    float X = 0.0f;
    float Z = 0.0f;
    float Size = 0.0f;

    // LOD info
    int LODLevel = 0;   // 0 = highest detail
    int MaxLOD = 0;

    // Culling bounds / height range
    BoundingBoxAABB Bounds{};
    float MinY = 0.0f;
    float MaxY = 0.0f;

    // Tree topology
    bool IsLeaf = true;
    std::unique_ptr<TerrainNode> Children[4]; // NW, NE, SW, SE

    // Render state
    bool IsVisible = false;
    UINT ObjectCBIndex = 0;

    TerrainNode() = default;
};

class QuadTree
{
public:
    QuadTree();
    ~QuadTree() = default;

    // Build a quadtree over a square terrain region.
    void Initialize(float terrainSize, float minNodeSize, int maxLODLevels);

    // Update LOD selection + frustum visibility.
    void Update(const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT4* frustumPlanes);

    // Collect leaf nodes that are visible and ready to draw.
    void GetVisibleNodes(std::vector<TerrainNode*>& outNodes);

    // Set height span for a given region (typically after heightmap is known).
    void SetHeightRange(float x, float z, float size, float minY, float maxY);

    // Counters (optional, but used by caller sometimes).
    int GetVisibleNodeCount() const { return mVisibleNodeCount; }
    int GetTotalNodeCount() const { return mTotalNodeCount; }

    // LOD distance thresholds (per level).
    void SetLODDistances(const std::vector<float>& distances) { mLODDistances = distances; }

private:
    void BuildTree(TerrainNode* node, float x, float z, float size, int depth);
    void UpdateNode(TerrainNode* node,
        const DirectX::XMFLOAT3& cameraPos,
        const DirectX::XMFLOAT4* frustumPlanes);

    void CollectVisibleNodes(TerrainNode* node, std::vector<TerrainNode*>& outNodes);

    int  CalculateLOD(const TerrainNode* node, const DirectX::XMFLOAT3& cameraPos) const;
    bool ShouldSubdivide(const TerrainNode* node, const DirectX::XMFLOAT3& cameraPos) const;

private:
    std::unique_ptr<TerrainNode> mRoot;

    float mTerrainSize = 0.0f;
    float mMinNodeSize = 0.0f;
    int   mMaxLODLevels = 0;

    std::vector<float> mLODDistances;

    int  mVisibleNodeCount = 0;
    int  mTotalNodeCount = 0;
    UINT mNextObjectCBIndex = 0;
};
