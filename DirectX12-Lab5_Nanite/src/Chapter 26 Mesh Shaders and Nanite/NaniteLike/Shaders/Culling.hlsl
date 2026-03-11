//***************************************************************************************
// Culling.hlsl - GPU-driven culling compute shader
// Performs hierarchical culling for Nanite-like LOD selection
//***************************************************************************************

#define THREAD_GROUP_SIZE 64

struct MeshletBounds
{
    float3 Center;
    float Radius;
    float3 ConeAxis;
    float ConeCutoff;
    float3 ConeApex;
    float Padding;
};

struct ClusterNode
{
    uint MeshletStart;
    uint MeshletCount;
    uint ParentIndex;
    uint ChildStart;
    uint ChildCount;
    float LODError;
    float3 BoundCenter;
    float BoundRadius;
};

struct Instance
{
    float4x4 World;
    float4x4 InvTransposeWorld;
    uint MeshIndex;
    uint MaterialIndex;
    uint2 Padding;
};

struct DispatchIndirectArgs
{
    uint ThreadGroupCountX;
    uint ThreadGroupCountY;
    uint ThreadGroupCountZ;
};

cbuffer CullConstants : register(b0)
{
    float4x4 gViewProj;
    float3 gEyePosW;
    float gLODScale;
    float4 gFrustumPlanes[6];
    float2 gScreenSize;
    uint gMeshletCount;
    uint gClusterCount;
    uint gInstanceCount;
    float gErrorThreshold;
    uint2 gPadding;
};

// Input buffers
StructuredBuffer<MeshletBounds> MeshletBoundsBuffer : register(t0);
StructuredBuffer<ClusterNode> ClusterNodes : register(t1);
StructuredBuffer<Instance> Instances : register(t2);

// Output buffers
RWStructuredBuffer<uint> VisibleMeshlets : register(u0);
RWStructuredBuffer<DispatchIndirectArgs> IndirectArgs : register(u1);
RWStructuredBuffer<uint> CullingStats : register(u2);


// Frustum culling
bool FrustumCull(float3 center, float radius)
{
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float dist = dot(float4(center, 1.0f), gFrustumPlanes[i]);
        if (dist < -radius)
            return false;
    }
    return true;
}

// Calculate screen-space error for LOD selection
float CalculateScreenError(float3 center, float radius, float lodError)
{
    float dist = length(center - gEyePosW);
    dist = max(dist, 0.001f);
    
    // Project error to screen space
    float screenError = (lodError * gScreenSize.y) / (dist * gLODScale);
    return screenError;
}

// Occlusion culling using Hi-Z (simplified - would need depth pyramid)
bool OcclusionCull(float3 center, float radius)
{
    // Placeholder - full implementation would sample Hi-Z pyramid
    return true;
}

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint meshletIndex = dtid.x;
    
    if (meshletIndex >= gMeshletCount)
        return;
    
    MeshletBounds bounds = MeshletBoundsBuffer[meshletIndex];
    
    // For each instance
    for (uint instIdx = 0; instIdx < gInstanceCount; ++instIdx)
    {
        Instance inst = Instances[instIdx];
        
        // Transform bounds to world space
        float3 centerW = mul(float4(bounds.Center, 1.0f), inst.World).xyz;
        float radius = bounds.Radius; // Assume uniform scale
        
        // Frustum culling
        if (!FrustumCull(centerW, radius))
        {
            InterlockedAdd(CullingStats[1], 1); // Culled count
            continue;
        }
        
        // Occlusion culling
        if (!OcclusionCull(centerW, radius))
        {
            InterlockedAdd(CullingStats[1], 1);
            continue;
        }
        
        // Add to visible list
        uint visibleIndex;
        InterlockedAdd(CullingStats[0], 1, visibleIndex); // Visible count
        
        // Pack instance and meshlet index
        uint packedIndex = (instIdx << 20) | meshletIndex;
        VisibleMeshlets[visibleIndex] = packedIndex;
    }
}

// Reset indirect args
[numthreads(1, 1, 1)]
void CSResetArgs(uint3 dtid : SV_DispatchThreadID)
{
    IndirectArgs[0].ThreadGroupCountX = 0;
    IndirectArgs[0].ThreadGroupCountY = 1;
    IndirectArgs[0].ThreadGroupCountZ = 1;
    
    CullingStats[0] = 0; // Visible
    CullingStats[1] = 0; // Culled
    CullingStats[2] = 0; // Total triangles
}

// Finalize indirect args after culling
[numthreads(1, 1, 1)]
void CSFinalizeArgs(uint3 dtid : SV_DispatchThreadID)
{
    uint visibleCount = CullingStats[0];
    IndirectArgs[0].ThreadGroupCountX = visibleCount;
}
