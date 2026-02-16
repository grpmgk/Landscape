#include "QuadTree.h"
#include <cmath>
#include <algorithm>

using namespace DirectX;

// AABB vs frustum planes (6 planes).
bool BoundingBoxAABB::Intersects(const XMFLOAT4* frustumPlanes) const
{
    // Using "positive vertex" test for each plane.
    for (int p = 0; p < 6; ++p)
    {
        const XMFLOAT4& pl = frustumPlanes[p];

        XMFLOAT3 v;
        v.x = (pl.x >= 0.0f) ? (Center.x + Extents.x) : (Center.x - Extents.x);
        v.y = (pl.y >= 0.0f) ? (Center.y + Extents.y) : (Center.y - Extents.y);
        v.z = (pl.z >= 0.0f) ? (Center.z + Extents.z) : (Center.z - Extents.z);

        const float d = pl.x * v.x + pl.y * v.y + pl.z * v.z + pl.w;
        if (d < 0.0f)
            return false;
    }
    return true;
}

QuadTree::QuadTree()
{
    // Keep defaults; header already sets zero-initialization too.
    mTerrainSize = 0.0f;
    mMinNodeSize = 0.0f;
    mMaxLODLevels = 0;
    mVisibleNodeCount = 0;
    mTotalNodeCount = 0;
    mNextObjectCBIndex = 0;
}

void QuadTree::Initialize(float terrainSize, float minNodeSize, int maxLODLevels)
{
    mTerrainSize = terrainSize;
    mMinNodeSize = minNodeSize;
    mMaxLODLevels = maxLODLevels;

    mVisibleNodeCount = 0;
    mTotalNodeCount = 0;
    mNextObjectCBIndex = 0;

    // Fill default LOD thresholds only when user didn't provide them.
    if (mLODDistances.empty())
    {
        mLODDistances.resize((size_t)maxLODLevels);

        // Same idea as original: larger thresholds for coarser levels.
        for (int i = 0; i < maxLODLevels; ++i)
        {
            const int shift = (maxLODLevels - i - 1);
            const float base = mMinNodeSize * (float)(1 << shift);
            mLODDistances[(size_t)i] = base * 2.0f;
        }
    }

    mRoot = std::make_unique<TerrainNode>();
    BuildTree(mRoot.get(), 0.0f, 0.0f, mTerrainSize, 0);
}

void QuadTree::BuildTree(TerrainNode* node, float x, float z, float size, int depth)
{
    node->X = x;
    node->Z = z;
    node->Size = size;

    node->LODLevel = depth;
    node->MaxLOD = mMaxLODLevels - 1;

    node->MinY = 0.0f;
    node->MaxY = 100.0f; // placeholder until heightmap range applied

    // Bounds setup (AABB)
    const float yMid = (node->MinY + node->MaxY) * 0.5f;
    const float yExt = (node->MaxY - node->MinY) * 0.5f + 10.0f;

    node->Bounds.Center = XMFLOAT3(x, yMid, z);
    node->Bounds.Extents = XMFLOAT3(size * 0.5f, yExt, size * 0.5f);

    ++mTotalNodeCount;

    const bool canSplit = (size > mMinNodeSize) && (depth < (mMaxLODLevels - 1));
    if (!canSplit)
    {
        node->IsLeaf = true;
        return;
    }

    node->IsLeaf = false;

    const float half = size * 0.5f;
    const float quarter = size * 0.25f;

    // Allocate children
    for (int i = 0; i < 4; ++i)
        node->Children[i] = std::make_unique<TerrainNode>();

    // NW, NE, SW, SE
    BuildTree(node->Children[0].get(), x - quarter, z + quarter, half, depth + 1);
    BuildTree(node->Children[1].get(), x + quarter, z + quarter, half, depth + 1);
    BuildTree(node->Children[2].get(), x - quarter, z - quarter, half, depth + 1);
    BuildTree(node->Children[3].get(), x + quarter, z - quarter, half, depth + 1);
}

void QuadTree::Update(const XMFLOAT3& cameraPos, const XMFLOAT4* frustumPlanes)
{
    mVisibleNodeCount = 0;
    mNextObjectCBIndex = 0;

    if (mRoot)
        UpdateNode(mRoot.get(), cameraPos, frustumPlanes);
}

void QuadTree::UpdateNode(TerrainNode* node, const XMFLOAT3& cameraPos, const XMFLOAT4* frustumPlanes)
{
    // Culling first
    node->IsVisible = node->Bounds.Intersects(frustumPlanes);
    if (!node->IsVisible)
        return;

    // LOD selection
    node->LODLevel = CalculateLOD(node, cameraPos);

    // Decide whether to descend
    const bool descend = (!node->IsLeaf) && ShouldSubdivide(node, cameraPos);

    if (descend)
    {
        // Not rendered itself; children will be considered instead.
        node->IsVisible = false;

        for (int i = 0; i < 4; ++i)
        {
            if (node->Children[i])
                UpdateNode(node->Children[i].get(), cameraPos, frustumPlanes);
        }
        return;
    }

    // Render this node
    node->ObjectCBIndex = mNextObjectCBIndex++;
    ++mVisibleNodeCount;
}

int QuadTree::CalculateLOD(const TerrainNode* node, const XMFLOAT3& cameraPos) const
{
    const float cx = node->X;
    const float cz = node->Z;
    const float cy = (node->MinY + node->MaxY) * 0.5f;

    const float dx = cameraPos.x - cx;
    const float dy = cameraPos.y - cy;
    const float dz = cameraPos.z - cz;

    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Pick first threshold that contains the distance.
    for (int i = 0; i < (int)mLODDistances.size(); ++i)
    {
        if (dist < mLODDistances[(size_t)i])
            return i;
    }

    return mMaxLODLevels - 1;
}

bool QuadTree::ShouldSubdivide(const TerrainNode* node, const XMFLOAT3& cameraPos) const
{
    if (node->IsLeaf)
        return false;

    // Horizontal distance only (same behavior as original)
    const float dx = cameraPos.x - node->X;
    const float dz = cameraPos.z - node->Z;

    const float distXZ = std::sqrt(dx * dx + dz * dz);

    // Split when close to node.
    return distXZ < (node->Size * 1.5f);
}

void QuadTree::GetVisibleNodes(std::vector<TerrainNode*>& outNodes)
{
    outNodes.clear();
    outNodes.reserve((size_t)mVisibleNodeCount);

    if (mRoot)
        CollectVisibleNodes(mRoot.get(), outNodes);
}

void QuadTree::CollectVisibleNodes(TerrainNode* node, std::vector<TerrainNode*>& outNodes)
{
    if (node->IsVisible)
    {
        outNodes.push_back(node);
        return;
    }

    if (node->IsLeaf)
        return;

    for (int i = 0; i < 4; ++i)
    {
        if (node->Children[i])
            CollectVisibleNodes(node->Children[i].get(), outNodes);
    }
}

void QuadTree::SetHeightRange(float x, float z, float size, float minY, float maxY)
{
    // Same placeholder behavior: apply to root only for now.
    // Parameters kept to preserve API.
    (void)x; (void)z; (void)size;

    if (!mRoot)
        return;

    mRoot->MinY = minY;
    mRoot->MaxY = maxY;

    mRoot->Bounds.Center.y = (minY + maxY) * 0.5f;
    mRoot->Bounds.Extents.y = (maxY - minY) * 0.5f + 10.0f;
}
