//***************************************************************************************
// Default.hlsl - Main rendering shader with motion vector support
//***************************************************************************************

#include "Common.hlsl"

struct VertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC    : TEXCOORD;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float3 PosW    : POSITION0;
    float3 NormalW : NORMAL;
    float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;
    
    // Transform to world space
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    
    // Transform normal to world space
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    
    // Transform to clip space with jittered projection
    vout.PosH = mul(posW, gViewProj);
    
    // Transform texture coordinates
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = texC.xy;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // Sample texture
    float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC);
    
    // Normalize interpolated normal
    pin.NormalW = normalize(pin.NormalW);
    
    // Vector from point to eye
    float3 toEyeW = normalize(gEyePosW - pin.PosW);
    
    // Ambient lighting
    float4 ambient = gAmbientLight * diffuseAlbedo;
    
    // Direct lighting (simplified)
    float3 lightVec = -gLights[0].Direction;
    float ndotl = max(dot(pin.NormalW, lightVec), 0.0);
    float3 diffuse = gLights[0].Strength * ndotl * diffuseAlbedo.rgb;
    
    // Specular
    float3 halfVec = normalize(lightVec + toEyeW);
    float shininess = 64.0;
    float spec = pow(max(dot(pin.NormalW, halfVec), 0.0), shininess);
    float3 specular = spec * gLights[0].Strength * 0.3;
    
    float4 litColor = ambient + float4(diffuse + specular, 0.0);
    litColor.a = diffuseAlbedo.a;
    
    return litColor;
}
