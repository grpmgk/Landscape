//***************************************************************************************
// QuadTree.cpp - QuadTree implementation for terrain LOD
//***************************************************************************************

#include "QuadTree.h"

using namespace DirectX;

// Check if AABB intersects with frustum (6 planes)
bool BoundingBoxAABB::Intersects(const XMFLOAT4* frustumPlanes) const
{
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&frustumPlanes[i]);
        
        // Get the positive vertex (furthest along plane normal)
        XMFLOAT3 positiveVertex;
        positiveVertex.x = (frustumPlanes[i].x >= 0) ? (Center.x + Extents.x) : (Center.x - Extents.x);
        positiveVertex.y = (frustumPlanes[i].y >= 0) ? (Center.y + Extents.y) : (Center.y - Extents.y);
        positiveVertex.z = (frustumPlanes[i].z >= 0) ? (Center.z + Extents.z) : (Center.z - Extents.z);
        
        // If positive vertex is behind plane, box is outside frustum
        float distance = frustumPlanes[i].x * positiveVertex.x +
                        frustumPlanes[i].y * positiveVertex.y +
                        frustumPlanes[i].z * positiveVertex.z +
                        frustumPlanes[i].w;
        
        if (distance < 0)
            return false;
    }
    return true;
}

QuadTree::QuadTree()
    : mTerrainSize(0), mMinNodeSize(0), mMaxLODLevels(0),
      mVisibleNodeCount(0), mTotalNodeCount(0), mNextObjectCBIndex(0)
{
}

void QuadTree::Initialize(float terrainSize, float minNodeSize, int maxLODLevels)
{
    mTerrainSize = terrainSize;
    mMinNodeSize = minNodeSize;
    mMaxLODLevels = maxLODLevels;
    mTotalNodeCount = 0;
    
    // Default LOD distances
    if (mLODDistances.empty())
    {
        mLODDistances.resize(maxLODLevels);
        for (int i = 0; i < maxLODLevels; ++i)
        {
            mLODDistances[i] = minNodeSize * (float)(1 << (maxLODLevels - i - 1)) * 2.0f;
        }
    }
    
    // Build the tree
    mRoot = std::make_unique<TerrainNode>();
    BuildTree(mRoot.get(), 0.0f, 0.0f, terrainSize, 0);
}

void QuadTree::BuildTree(TerrainNode* node, float x, float z, float size, int depth)
{
    node->X = x;
    node->Z = z;
    node->Size = size;
    node->LODLevel = depth;
    node->MaxLOD = mMaxLODLevels - 1;
    node->MinY = 0.0f;
    node->MaxY = 100.0f; // Default, will be updated from heightmap
    
    // Setup bounding box
    node->Bounds.Center = XMFLOAT3(x, (node->MinY + node->MaxY) * 0.5f, z);
    node->Bounds.Extents = XMFLOAT3(size * 0.5f, (node->MaxY - node->MinY) * 0.5f + 10.0f, size * 0.5f);
    
    mTotalNodeCount++;
    
    // Check if we should subdivide
    if (size > mMinNodeSize && depth < mMaxLODLevels - 1)
    {
        node->IsLeaf = false;
        float halfSize = size * 0.5f;
        float quarterSize = size * 0.25f;
        
        // Create children: NW, NE, SW, SE
        node->Children[0] = std::make_unique<TerrainNode>();
        node->Children[1] = std::make_unique<TerrainNode>();
        node->Children[2] = std::make_unique<TerrainNode>();
        node->Children[3] = std::make_unique<TerrainNode>();
        
        BuildTree(node->Children[0].get(), x - quarterSize, z + quarterSize, halfSize, depth + 1); // NW
        BuildTree(node->Children[1].get(), x + quarterSize, z + quarterSize, halfSize, depth + 1); // NE
        BuildTree(node->Children[2].get(), x - quarterSize, z - quarterSize, halfSize, depth + 1); // SW
        BuildTree(node->Children[3].get(), x + quarterSize, z - quarterSize, halfSize, depth + 1); // SE
    }
    else
    {
        node->IsLeaf = true;
    }
}

void QuadTree::Update(const XMFLOAT3& cameraPos, const XMFLOAT4* frustumPlanes)
{
    mVisibleNodeCount = 0;
    mNextObjectCBIndex = 0;
    
    if (mRoot)
    {
        UpdateNode(mRoot.get(), cameraPos, frustumPlanes);
    }
}

void QuadTree::UpdateNode(TerrainNode* node, const XMFLOAT3& cameraPos, const XMFLOAT4* frustumPlanes)
{
    // Frustum culling
    node->IsVisible = node->Bounds.Intersects(frustumPlanes);
    
    if (!node->IsVisible)
        return;
    
    // Calculate LOD based on distance
    int desiredLOD = CalculateLOD(node, cameraPos);
    node->LODLevel = desiredLOD;
    
    // Decide whether to use this node or subdivide
    if (!node->IsLeaf && ShouldSubdivide(node, cameraPos))
    {
        // Use children instead
        node->IsVisible = false;
        for (int i = 0; i < 4; ++i)
        {
            if (node->Children[i])
            {
                UpdateNode(node->Children[i].get(), cameraPos, frustumPlanes);
            }
        }
    }
    else
    {
        // Use this node for rendering
        node->ObjectCBIndex = mNextObjectCBIndex++;
        mVisibleNodeCount++;
    }
}

int QuadTree::CalculateLOD(const TerrainNode* node, const XMFLOAT3& cameraPos) const
{
    // Calculate distance from camera to node center
    float dx = cameraPos.x - node->X;
    float dy = cameraPos.y - (node->MinY + node->MaxY) * 0.5f;
    float dz = cameraPos.z - node->Z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    
    // Find appropriate LOD level
    for (int i = 0; i < (int)mLODDistances.size(); ++i)
    {
        if (distance < mLODDistances[i])
            return i;
    }
    return mMaxLODLevels - 1;
}

bool QuadTree::ShouldSubdivide(const TerrainNode* node, const XMFLOAT3& cameraPos) const
{
    if (node->IsLeaf)
        return false;
    
    // Calculate distance
    float dx = cameraPos.x - node->X;
    float dz = cameraPos.z - node->Z;
    float distance = sqrtf(dx * dx + dz * dz);
    
    // Subdivide if camera is close enough
    // Use node size as threshold - subdivide when distance < size * factor
    return distance < node->Size * 1.5f;
}

void QuadTree::GetVisibleNodes(std::vector<TerrainNode*>& outNodes)
{
    outNodes.clear();
    outNodes.reserve(mVisibleNodeCount);
    
    if (mRoot)
    {
        CollectVisibleNodes(mRoot.get(), outNodes);
    }
}

void QuadTree::CollectVisibleNodes(TerrainNode* node, std::vector<TerrainNode*>& outNodes)
{
    if (node->IsVisible)
    {
        outNodes.push_back(node);
    }
    else if (!node->IsLeaf)
    {
        // Check children
        for (int i = 0; i < 4; ++i)
        {
            if (node->Children[i])
            {
                CollectVisibleNodes(node->Children[i].get(), outNodes);
            }
        }
    }
}

void QuadTree::SetHeightRange(float x, float z, float size, float minY, float maxY)
{
    // TODO: Update height range for nodes in the specified region
    // For now, just update root
    if (mRoot)
    {
        mRoot->MinY = minY;
        mRoot->MaxY = maxY;
        mRoot->Bounds.Center.y = (minY + maxY) * 0.5f;
        mRoot->Bounds.Extents.y = (maxY - minY) * 0.5f + 10.0f;
    }
}
