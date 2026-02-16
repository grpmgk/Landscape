struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

#define MaxLights 16

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    uint gMaterialIndex;
    uint gLODLevel;
    uint gObjPad1;
    uint gObjPad2;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerPassPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    float4 gFrustumPlanes[6];
    Light gLights[MaxLights];
};

cbuffer cbTerrain : register(b2)
{
    float gMinHeight;
    float gMaxHeight;
    float gTerrainSize;
    float gTexelSize;
    float2 gHeightMapSize;
    float2 gTerrainPadding;
};

Texture2D gHeightMap : register(t0);
Texture2D gDiffuseMap : register(t1);
Texture2D gNormalMap : register(t2);

SamplerState gsamLinearWrap : register(s0);
SamplerState gsamLinearClamp : register(s1);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

// --- helpers -------------------------------------------------------------

static float HeightRange()
{
    return (gMaxHeight - gMinHeight);
}

static float2 HeightTexel()
{
    // keep the same idea as before: based on actual heightmap size
    return 1.0 / gHeightMapSize;
}

static float FetchHeight01(float2 uv)
{
    // height stored in .r, clamp UV to avoid sampling outside
    return gHeightMap.SampleLevel(gsamLinearClamp, saturate(uv), 0).r;
}

static float FetchHeightWorld(float2 uv)
{
    // same mapping as original: min + saturate(h)*range
    float h01 = saturate(FetchHeight01(uv));
    return gMinHeight + h01 * HeightRange();
}

static float3 NormalFromHeight(float2 uv)
{
    // central difference in UV space
    float2 d = HeightTexel();

    float hL = FetchHeightWorld(uv + float2(-d.x, 0.0));
    float hR = FetchHeightWorld(uv + float2(d.x, 0.0));
    float hD = FetchHeightWorld(uv + float2(0.0, -d.y));
    float hU = FetchHeightWorld(uv + float2(0.0, d.y));

    // match original “scale” idea for Y component
    float yScale = 2.0 * gTerrainSize / gHeightMapSize.x;

    float3 n;
    n.x = (hL - hR);
    n.y = yScale;
    n.z = (hD - hU);

    return normalize(n);
}

static float2 ApplyTexTransform(float2 localUV)
{
    // localUV -> global UV for the node
    float4 uv4 = mul(float4(localUV, 0.0, 1.0), gTexTransform);
    return uv4.xy;
}

// --- shaders -------------------------------------------------------------

VertexOut VS(VertexIn vin)
{
    VertexOut o;

    float2 uvGlobal = ApplyTexTransform(vin.TexC);

    // local vertex: keep XZ, y comes from heightmap
    float3 pLocal = vin.PosL;
    pLocal.y = 0.0;

    float4 pWorld = mul(float4(pLocal, 1.0), gWorld);
    pWorld.y = FetchHeightWorld(uvGlobal);

    o.PosW = pWorld.xyz;
    o.NormalW = NormalFromHeight(uvGlobal);
    o.PosH = mul(pWorld, gViewProj);
    o.TexC = uvGlobal;

    return o;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 n = normalize(pin.NormalW);

    // diffuse texture color
    float3 albedo = gDiffuseMap.Sample(gsamLinearClamp, pin.TexC).rgb;

    // single directional light (index 0), same as original
    float3 L = normalize(-gLights[0].Direction);
    float ndotl = max(dot(n, L), 0.0);

    float3 ambient = gAmbientLight.rgb * 0.4;
    float3 lit = ambient + (gLights[0].Strength * ndotl);

    return float4(lit * albedo, 1.0);
}

float4 PS_Wireframe(VertexOut pin) : SV_Target
{
    // same mapping LOD->color, но оформлено иначе
    uint id = (gLODLevel % 5);

    float3 c0 = float3(1, 0, 0);
    float3 c1 = float3(0, 1, 0);
    float3 c2 = float3(0, 0, 1);
    float3 c3 = float3(1, 1, 0);
    float3 c4 = float3(1, 0, 1);

    float3 outColor =
        (id == 0) ? c0 :
        (id == 1) ? c1 :
        (id == 2) ? c2 :
        (id == 3) ? c3 : c4;

    return float4(outColor, 1.0);
}

