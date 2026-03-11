//***************************************************************************************
// Default.hlsl - Fallback vertex/pixel shader for rendering meshlet meshes
// With meshlet visualization (each meshlet gets a unique color)
// Supports optional diffuse texture
//***************************************************************************************

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
    uint gShowMeshletColors;
    uint gUseTexture;
    uint3 gPadding2;
};

// Diffuse texture
Texture2D gDiffuseMap : register(t0);
SamplerState gSamLinear : register(s0);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentL : TANGENT;
    uint MeshletID : MESHLET_ID;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    nointerpolation uint MeshletID : MESHLET_ID;
};

// Generate a pseudo-random color from meshlet ID
float3 MeshletIDToColor(uint id)
{
    // Use hash function to generate varied colors
    uint hash = id;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;
    
    float3 color;
    color.r = float((hash >> 0) & 0xFF) / 255.0f;
    color.g = float((hash >> 8) & 0xFF) / 255.0f;
    color.b = float((hash >> 16) & 0xFF) / 255.0f;
    
    // Boost saturation for more vivid colors
    float maxC = max(max(color.r, color.g), color.b);
    float minC = min(min(color.r, color.g), color.b);
    if (maxC > 0.01f)
    {
        color = (color - minC) / (maxC - minC + 0.001f);
        color = lerp(float3(0.5f, 0.5f, 0.5f), color, 0.85f);
    }
    
    return saturate(color * 0.9f + 0.1f);
}

VertexOut VSMain(VertexIn vin)
{
    VertexOut vout;
    
    // Use position directly (world matrix is identity for now)
    vout.PosW = vin.PosL;
    vout.NormalW = vin.NormalL;
    
    // Transform to clip space
    vout.PosH = mul(float4(vin.PosL, 1.0f), gViewProj);
    vout.TexC = vin.TexC;
    vout.MeshletID = vin.MeshletID;
    
    return vout;
}

float4 PSMain(VertexOut pin) : SV_Target
{
    float3 baseColor;
    float alpha = 1.0f;
    
    if (gUseTexture)
    {
        float4 texColor = gDiffuseMap.Sample(gSamLinear, pin.TexC);
        baseColor = texColor.rgb;
        alpha = texColor.a;
    }
    else if (gShowMeshletColors)
    {
        baseColor = MeshletIDToColor(pin.MeshletID);
    }
    else
    {
        baseColor = float3(0.7f, 0.7f, 0.7f);
    }
    
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
