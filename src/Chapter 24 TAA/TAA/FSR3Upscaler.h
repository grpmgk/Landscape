#pragma once

#include "../../Common/d3dUtil.h"
#include <d3d12.h>

// FFX context is void* in the API
typedef void* ffxContext;

// FSR Quality modes
enum class FSR3QualityMode
{
    NativeAA = 0,           // 1.0x - Native resolution with AA
    Quality = 1,            // 1.5x upscale
    Balanced = 2,           // 1.7x upscale
    Performance = 3,        // 2.0x upscale
    UltraPerformance = 4    // 3.0x upscale
};

class FSR3Upscaler
{
public:
    FSR3Upscaler();
    ~FSR3Upscaler();

    // Initialize FSR3 context
    bool Initialize(
        ID3D12Device* device,
        UINT displayWidth,
        UINT displayHeight,
        FSR3QualityMode qualityMode = FSR3QualityMode::Quality
    );

    // Destroy FSR3 context
    void Destroy();

    // Resize (recreates context)
    void OnResize(UINT displayWidth, UINT displayHeight);

    // Execute FSR3 upscaling
    void Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* colorInput,         // Rendered scene at render resolution
        ID3D12Resource* depthInput,         // Depth buffer at render resolution
        ID3D12Resource* motionVectors,      // Motion vectors at render resolution
        ID3D12Resource* output,             // Output at display resolution
        float jitterX,
        float jitterY,
        float deltaTime,                    // Frame time in milliseconds
        float cameraNear,
        float cameraFar,
        float cameraFovY,                   // Vertical FOV in radians
        bool reset = false                  // Reset temporal history
    );

    // Get render resolution for current quality mode
    void GetRenderResolution(UINT& outWidth, UINT& outHeight) const;

    // Get jitter offset for current frame
    void GetJitterOffset(int frameIndex, float& outX, float& outY) const;

    // Get jitter phase count
    int GetJitterPhaseCount() const;

    // Setters
    void SetQualityMode(FSR3QualityMode mode);
    void SetSharpness(float sharpness) { mSharpness = sharpness; }
    void EnableSharpening(bool enable) { mEnableSharpening = enable; }

    // Getters
    FSR3QualityMode GetQualityMode() const { return mQualityMode; }
    float GetSharpness() const { return mSharpness; }
    bool IsInitialized() const { return mInitialized; }
    UINT GetRenderWidth() const { return mRenderWidth; }
    UINT GetRenderHeight() const { return mRenderHeight; }
    UINT GetDisplayWidth() const { return mDisplayWidth; }
    UINT GetDisplayHeight() const { return mDisplayHeight; }

private:
    bool CreateContext();
    void DestroyContext();

private:
    ID3D12Device* mDevice = nullptr;
    ffxContext mContext = nullptr;  // ffxContext is already void*
    
    UINT mDisplayWidth = 0;
    UINT mDisplayHeight = 0;
    UINT mRenderWidth = 0;
    UINT mRenderHeight = 0;
    
    FSR3QualityMode mQualityMode = FSR3QualityMode::Quality;
    float mSharpness = 0.2f;      // Subtle sharpening (0 = max, 1 = none)
    bool mEnableSharpening = true;
    bool mInitialized = false;
};
