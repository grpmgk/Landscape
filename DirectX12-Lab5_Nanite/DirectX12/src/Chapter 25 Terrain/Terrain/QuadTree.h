//***************************************************************************************
// QuadTree.h - QuadTree for terrain LOD management
//
// Implements a quadtree structure for:
// - Level of Detail (LOD) selection based on camera distance
// - Frustum culling for efficient rendering
// - Terrain tile management
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/MathHelper.h"
#include <vector>
#include <memory>

// Bounding box for frustum culling
struct BoundingBoxAABB
{
    DirectX::XMFLOAT3 Center;
    DirectX::XMFLOAT3 Extents;
    
    bool Intersects(const DirectX::XMFLOAT4* frustumPlanes) const;
};

// Single terrain node in the quadtree
struct TerrainNode
{
    // Position and size
    float X, Z;           // World position (center)
    float Size;           // Size of this node
    int LODLevel;         // Current LOD level (0 = highest detail)
    int MaxLOD;           // Maximum LOD level for this node
    
    // Bounding box for culling
    BoundingBoxAABB Bounds;
    float MinY, MaxY;     // Height range
    
    // Tree structure
    bool IsLeaf;
    std::unique_ptr<TerrainNode> Children[4]; // NW, NE, SW, SE
    
    // Rendering
    bool IsVisible;       // After frustum culling
    UINT ObjectCBIndex;   // Index in constant buffer
    
    TerrainNode() : X(0), Z(0), Size(0), LODLevel(0), MaxLOD(0),
                   MinY(0), MaxY(0), IsLeaf(true), IsVisible(false), ObjectCBIndex(0) {}
};

class QuadTree
{
public:
    QuadTree();
    ~QuadTree() = default;
    
    // Initialize the quadtree
    void Initialize(float terrainSize, float minNodeSize, int maxLODLevels);
    
    // Update LOD based on camera position
    void Update(const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT4* frustumPlanes);
    
    // Get visible nodes for rendering
    void GetVisibleNodes(std::vector<TerrainNode*>& outNodes);
    
    // Set height range for a region (call after loading heightmap)
    void SetHeightRange(float x, float z, float size, float minY, float maxY);
    
    // Statistics
    int GetVisibleNodeCount() const { return mVisibleNodeCount; }
    int GetTotalNodeCount() const { return mTotalNodeCount; }
    
    // LOD distance thresholds
    void SetLODDistances(const std::vector<float>& distances) { mLODDistances = distances; }
    
private:
    void BuildTree(TerrainNode* node, float x, float z, float size, int depth);
    void UpdateNode(TerrainNode* node, const DirectX::XMFLOAT3& cameraPos, 
                   const DirectX::XMFLOAT4* frustumPlanes);
    void CollectVisibleNodes(TerrainNode* node, std::vector<TerrainNode*>& outNodes);
    int CalculateLOD(const TerrainNode* node, const DirectX::XMFLOAT3& cameraPos) const;
    bool ShouldSubdivide(const TerrainNode* node, const DirectX::XMFLOAT3& cameraPos) const;
    
private:
    std::unique_ptr<TerrainNode> mRoot;
    
    float mTerrainSize;
    float mMinNodeSize;
    int mMaxLODLevels;
    
    std::vector<float> mLODDistances; // Distance thresholds for each LOD level
    
    int mVisibleNodeCount;
    int mTotalNodeCount;
    UINT mNextObjectCBIndex;
};
