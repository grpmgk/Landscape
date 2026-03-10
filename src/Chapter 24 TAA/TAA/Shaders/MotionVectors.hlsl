//***************************************************************************************
// MotionVectors.hlsl - Generate per-pixel motion vectors for TAA
//
// Computes screen-space velocity by comparing current and previous frame positions
// Output: RG texture with motion vectors in texture space [0,1]
// Motion vectors point from current position to previous position (for reprojection)
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
    float4 PosH         : SV_POSITION;
    float4 CurrPosH     : POSITION0;  // Current frame clip space position
    float4 PrevPosH     : POSITION1;  // Previous frame clip space position
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    // Transform to world space using CURRENT world matrix
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    
    // Transform to world space using PREVIOUS world matrix (for moving objects)
    float4 prevPosW = mul(float4(vin.PosL, 1.0f), gPrevWorld);
    
    // Current frame clip space position WITHOUT jitter (for motion vectors)
    vout.CurrPosH = mul(posW, gUnjitteredViewProj);
    
    // For rasterization, use jittered position
    vout.PosH = mul(posW, gViewProj);
    
    // Previous frame clip space position using PREVIOUS world position (also without jitter)
    vout.PrevPosH = mul(prevPosW, gPrevViewProj);
    
    return vout;
}

float2 PS(VertexOut pin) : SV_Target
{
    // Convert clip space to NDC [-1, 1]
    float2 currNDC = pin.CurrPosH.xy / pin.CurrPosH.w;
    float2 prevNDC = pin.PrevPosH.xy / pin.PrevPosH.w;
    
    // Velocity in NDC space
    float2 velocityNDC = prevNDC - currNDC;
    
    // Convert NDC velocity to UV velocity
    // NDC range is [-1,1] = 2 units, UV range is [0,1] = 1 unit
    // So multiply by 0.5
    // Y is flipped between NDC (Y up) and UV (Y down), so negate Y
    float2 velocity = float2(velocityNDC.x * 0.5f, velocityNDC.y * -0.5f);
    
    return velocity;
}
