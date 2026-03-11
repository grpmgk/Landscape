//***************************************************************************************
// MeshShader.hlsl - Mesh Shader pipeline for Nanite-like rendering
// Implements Amplification Shader (AS), Mesh Shader (MS), and Pixel Shader (PS)
//***************************************************************************************

#define MAX_MESHLET_VERTICES 64
#define MAX_MESHLET_PRIMITIVES 124
#define AS_GROUP_SIZE 32

// Vertex data - must match GPUVertex in C++ (48 bytes)
struct Vertex
{
    float3 Position;    // 12 bytes
    float3 Normal;      // 12 bytes
    float2 TexCoord;    // 8 bytes
    float3 Tangent;     // 12 bytes
    float Padding;      // 4 bytes - alignment to 48 bytes total
};

// Meshlet info
struct Meshlet
{
    uint VertexOffset;
    uint VertexCount;
    uint PrimitiveOffset;
    uint PrimitiveCount;
};

// Meshlet bounds for culling
struct MeshletBounds
{
    float3 Center;
    float Radius;
    float3 ConeAxis;
    float ConeCutoff;
    float3 ConeApex;
    float Padding;
};

// Instance data - matrices stored as row_major to match C++ layout
struct Instance
{
    row_major float4x4 World;
    row_major float4x4 InvTransposeWorld;
    uint MeshIndex;
    uint MaterialIndex;
    uint2 Padding;
};

// Pass constants
cbuffer PassConstants : register(b0)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gInvView;
    float3 gEyePosW;
    float gPadding1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float4 gFrustumPlanes[6];
    float gLODScale;
    uint gMeshletCount;
    uint gInstanceCount;
    uint gShowMeshletColors;  // 1 = show meshlet colors, 0 = solid gray
    uint gUseTexture;         // 1 = use diffuse texture, 0 = use colors
    uint3 gPadding2;
};


// Structured buffers - all must be StructuredBuffer for root descriptor compatibility
StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<Meshlet> Meshlets : register(t1);
StructuredBuffer<MeshletBounds> MeshletBoundsBuffer : register(t2);
StructuredBuffer<uint> UniqueVertexIndices : register(t3);
StructuredBuffer<uint> PrimitiveIndices : register(t4);
StructuredBuffer<Instance> Instances : register(t5);

// Diffuse texture
Texture2D gDiffuseMap : register(t6);

// Sampler
SamplerState gSamLinear : register(s0);

// Output from mesh shader to pixel shader
struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    uint MeshletIndex : MESHLET_INDEX;
};

// Payload from amplification shader to mesh shader
struct Payload
{
    uint MeshletIndices[AS_GROUP_SIZE];
    uint InstanceIndex;
};

// Frustum culling test
bool IsVisible(MeshletBounds bounds, float4x4 world)
{
    // Transform center to world space
    float3 centerW = mul(float4(bounds.Center, 1.0f), world).xyz;
    float radius = bounds.Radius;
    
    // Test against each frustum plane
    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        float dist = dot(float4(centerW, 1.0f), gFrustumPlanes[i]);
        if (dist < -radius)
            return false;
    }
    
    return true;
}

// Backface cone culling
bool IsConeVisible(MeshletBounds bounds, float4x4 world, float3 eyePos)
{
    // Skip if no valid cone
    if (bounds.ConeCutoff >= 1.0f)
        return true;
    
    float3 apexW = mul(float4(bounds.ConeApex, 1.0f), world).xyz;
    float3 axisW = normalize(mul(float4(bounds.ConeAxis, 0.0f), world).xyz);
    
    float3 viewDir = normalize(apexW - eyePos);
    
    return dot(viewDir, axisW) < bounds.ConeCutoff;
}


//=============================================================================
// Amplification Shader (Task Shader)
// Performs per-meshlet frustum and backface cone culling
//=============================================================================
groupshared Payload sharedPayload;
groupshared uint sharedVisibleCount;

[numthreads(AS_GROUP_SIZE, 1, 1)]
void ASMain(
    uint gtid : SV_GroupThreadID,
    uint dtid : SV_DispatchThreadID,
    uint gid : SV_GroupID)
{
    // Initialize shared counter
    if (gtid == 0)
        sharedVisibleCount = 0;
    GroupMemoryBarrierWithGroupSync();
    
    // Each AS group processes AS_GROUP_SIZE meshlets
    uint meshletIndex = gid * AS_GROUP_SIZE + gtid;
    
    bool isVisible = false;
    
    if (meshletIndex < gMeshletCount)
    {
        MeshletBounds bounds = MeshletBoundsBuffer[meshletIndex];
        
        // Frustum culling - test sphere against all 6 planes
        float3 center = bounds.Center;  // Already in object/world space
        float radius = bounds.Radius;
        
        isVisible = true;
        [unroll]
        for (int i = 0; i < 6; ++i)
        {
            // Plane equation: Ax + By + Cz + D = 0
            // Distance = dot(normal, point) + D = dot(float4(point, 1), plane)
            float dist = dot(float4(center, 1.0f), gFrustumPlanes[i]);
            if (dist < -radius)
            {
                isVisible = false;
                break;
            }
        }
        
        // Backface cone culling (only if still visible)
        if (isVisible && bounds.ConeCutoff < 1.0f)
        {
            float3 coneApex = bounds.ConeApex;
            float3 coneAxis = bounds.ConeAxis;
            float3 viewDir = normalize(coneApex - gEyePosW);
            
            // If view direction aligns with cone axis, meshlet faces away
            if (dot(viewDir, coneAxis) >= bounds.ConeCutoff)
            {
                isVisible = false;
            }
        }
    }
    
    // Compact visible meshlets using atomic operations
    if (isVisible)
    {
        uint slot;
        InterlockedAdd(sharedVisibleCount, 1, slot);
        sharedPayload.MeshletIndices[slot] = meshletIndex;
    }
    
    sharedPayload.InstanceIndex = 0;
    GroupMemoryBarrierWithGroupSync();
    
    // Dispatch mesh shader for visible meshlets
    DispatchMesh(sharedVisibleCount, 1, 1, sharedPayload);
}

//=============================================================================
// Mesh Shader
// Processes one meshlet, outputs vertices and primitives
//=============================================================================
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void MSMain(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload Payload payload,
    out vertices VertexOut verts[MAX_MESHLET_VERTICES],
    out indices uint3 tris[MAX_MESHLET_PRIMITIVES])
{
    uint meshletIndex = payload.MeshletIndices[gid];
    Meshlet meshlet = Meshlets[meshletIndex];
    Instance inst = Instances[payload.InstanceIndex];
    
    // Set output counts
    SetMeshOutputCounts(meshlet.VertexCount, meshlet.PrimitiveCount);
    
    // Process vertices
    if (gtid < meshlet.VertexCount)
    {
        uint vertexIndex = UniqueVertexIndices[meshlet.VertexOffset + gtid];
        Vertex v = Vertices[vertexIndex];
        
        // Transform to world space
        float4 posW = mul(float4(v.Position, 1.0f), inst.World);
        float3 normalW = mul(v.Normal, (float3x3)inst.InvTransposeWorld);
        
        VertexOut vout;
        vout.PosW = posW.xyz;
        vout.PosH = mul(posW, gViewProj);
        vout.NormalW = normalize(normalW);
        vout.TexC = v.TexCoord;
        vout.MeshletIndex = meshletIndex;
        
        verts[gtid] = vout;
    }
    
    // Process primitives - each index is stored as separate uint32
    if (gtid < meshlet.PrimitiveCount)
    {
        uint primOffset = (meshlet.PrimitiveOffset + gtid) * 3;
        uint i0 = PrimitiveIndices[primOffset + 0];
        uint i1 = PrimitiveIndices[primOffset + 1];
        uint i2 = PrimitiveIndices[primOffset + 2];
        
        tris[gtid] = uint3(i0, i1, i2);
    }
}


//=============================================================================
// Pixel Shader
// Meshlet visualization with lighting
//=============================================================================

// Generate pseudo-random color from meshlet ID (same as Default.hlsl)
float3 MeshletIDToColor(uint id)
{
    uint hash = id;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;
    
    float3 color;
    color.r = float((hash >> 0) & 0xFF) / 255.0f;
    color.g = float((hash >> 8) & 0xFF) / 255.0f;
    color.b = float((hash >> 16) & 0xFF) / 255.0f;
    
    // Boost saturation
    float maxC = max(max(color.r, color.g), color.b);
    float minC = min(min(color.r, color.g), color.b);
    if (maxC > 0.01f)
    {
        color = (color - minC) / (maxC - minC + 0.001f);
        color = lerp(float3(0.5f, 0.5f, 0.5f), color, 0.85f);
    }
    
    return saturate(color * 0.9f + 0.1f);
}

float4 PSMain(VertexOut pin) : SV_Target
{
    // Get base color
    float3 baseColor;
    float alpha = 1.0f;
    
    if (gUseTexture)
    {
        // Simple texture sampling using model's UV coordinates
        float4 texColor = gDiffuseMap.Sample(gSamLinear, pin.TexC);
        baseColor = texColor.rgb;
        alpha = texColor.a;
    }
    else if (gShowMeshletColors)
    {
        baseColor = MeshletIDToColor(pin.MeshletIndex);
    }
    else
    {
        baseColor = float3(0.7f, 0.7f, 0.7f);
    }
    
    // Simple directional light
    float3 lightDir = normalize(float3(0.57735f, 0.57735f, -0.57735f));
    float3 lightColor = float3(1.0f, 0.98f, 0.95f);
    float3 ambient = float3(0.3f, 0.3f, 0.3f);
    
    float3 N = normalize(pin.NormalW);
    float3 V = normalize(gEyePosW - pin.PosW);
    
    if (dot(N, V) < 0)
        N = -N;
    
    float NdotL = max(dot(N, lightDir), 0.0f);
    float3 diffuse = NdotL * lightColor;
    
    float3 H = normalize(lightDir + V);
    float NdotH = max(dot(N, H), 0.0f);
    float3 specular = pow(NdotH, 32.0f) * lightColor * 0.1f;
    
    float3 finalColor = baseColor * (ambient + diffuse) + specular;
    finalColor = pow(saturate(finalColor), 1.0f / 2.2f);
    
    return float4(finalColor, alpha);
}
