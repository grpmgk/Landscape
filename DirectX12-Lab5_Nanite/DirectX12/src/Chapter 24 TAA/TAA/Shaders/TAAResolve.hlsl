//***************************************************************************************
// TAAResolve.hlsl - Temporal Anti-Aliasing resolve pass
//
// Based on:
// - https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
// - https://sugulee.wordpress.com/2021/06/21/temporal-anti-aliasingtaa-tutorial/
// - https://alextardif.com/TAA.html
//***************************************************************************************

cbuffer cbTAA : register(b0)
{
    float2 gJitterOffset;
    float2 gScreenSize;
    float gBlendFactor;
    float gMotionScale;
    float2 gPadding;
};

Texture2D gCurrentFrame  : register(t0);
Texture2D gHistoryFrame  : register(t1);
Texture2D gMotionVectors : register(t2);
Texture2D gDepthMap      : register(t3);

SamplerState gsamPointClamp  : register(s1);
SamplerState gsamLinearClamp : register(s3);

struct VertexOut
{
    float4 PosH  : SV_POSITION;
    float2 TexC  : TEXCOORD;
};

// YCoCg color space for better clamping
float3 RGBToYCoCg(float3 rgb)
{
    return float3(
         0.25f * rgb.r + 0.5f * rgb.g + 0.25f * rgb.b,
         0.5f  * rgb.r - 0.5f  * rgb.b,
        -0.25f * rgb.r + 0.5f * rgb.g - 0.25f * rgb.b
    );
}

float3 YCoCgToRGB(float3 ycocg)
{
    return float3(
        ycocg.x + ycocg.y - ycocg.z,
        ycocg.x + ycocg.z,
        ycocg.x - ycocg.y - ycocg.z
    );
}

VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;
    vout.TexC = float2((vid << 1) & 2, vid & 2);
    vout.PosH = float4(vout.TexC * float2(2, -2) + float2(-1, 1), 0, 1);
    return vout;
}

// Velocity dilation - get the largest velocity in 3x3 neighborhood
// This fixes trailing edge artifacts on moving objects
float2 GetDilatedVelocity(float2 uv, float2 texelSize)
{
    float2 maxVel = float2(0, 0);
    float maxLenSq = 0;
    
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 vel = gMotionVectors.Sample(gsamPointClamp, uv + float2(x, y) * texelSize).rg;
            float lenSq = dot(vel, vel);
            if (lenSq > maxLenSq)
            {
                maxLenSq = lenSq;
                maxVel = vel;
            }
        }
    }
    return maxVel;
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float2 texelSize = 1.0f / gScreenSize;
    
    // Sample current frame
    float3 currentRGB = gCurrentFrame.Sample(gsamPointClamp, uv).rgb;
    
    // Get dilated velocity - fixes trailing edge on moving objects
    float2 velocity = GetDilatedVelocity(uv, texelSize);
    float2 historyUV = uv + velocity;
    
    // Bounds check
    if (any(historyUV < 0.0f) || any(historyUV > 1.0f))
    {
        return float4(currentRGB, 1.0f);
    }
    
    // Sample history
    float3 historyRGB = gHistoryFrame.Sample(gsamLinearClamp, historyUV).rgb;
    
    // Gather 3x3 neighborhood statistics in YCoCg space
    float3 m1 = float3(0, 0, 0);
    float3 m2 = float3(0, 0, 0);
    
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float3 s = gCurrentFrame.Sample(gsamPointClamp, uv + float2(x, y) * texelSize).rgb;
            float3 sYCoCg = RGBToYCoCg(s);
            m1 += sYCoCg;
            m2 += sYCoCg * sYCoCg;
        }
    }
    
    m1 /= 9.0f;
    m2 /= 9.0f;
    float3 sigma = sqrt(max(m2 - m1 * m1, 0.0f));
    
    // Variance clipping bounds
    // Use wider bounds (higher gamma) to allow more history blending for AA
    float velocityPixels = length(velocity * gScreenSize);
    float gamma = lerp(1.5f, 2.5f, saturate(velocityPixels * 0.1f));
    float3 aabbMin = m1 - gamma * sigma;
    float3 aabbMax = m1 + gamma * sigma;
    
    // Clamp history in YCoCg space
    float3 historyYCoCg = RGBToYCoCg(historyRGB);
    historyYCoCg = clamp(historyYCoCg, aabbMin, aabbMax);
    historyRGB = YCoCgToRGB(historyYCoCg);
    
    // Blend factor - keep low for good AA accumulation
    float blend = gBlendFactor;
    
    // Final blend
    float3 finalColor = lerp(historyRGB, currentRGB, blend);
    
    return float4(finalColor, 1.0f);
}
