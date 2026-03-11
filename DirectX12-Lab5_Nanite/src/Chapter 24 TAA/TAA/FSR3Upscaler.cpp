#include "FSR3Upscaler.h"

// FidelityFX SDK includes
#include "ffx_api.h"
#include "ffx_api_types.h"
#include "dx12/ffx_api_dx12.h"
#include "ffx_upscale.h"

#include <cmath>
#include <cstdio>

// Library is linked via project settings

FSR3Upscaler::FSR3Upscaler()
{
}

FSR3Upscaler::~FSR3Upscaler()
{
    Destroy();
}

bool FSR3Upscaler::Initialize(
    ID3D12Device* device,
    UINT displayWidth,
    UINT displayHeight,
    FSR3QualityMode qualityMode)
{
    if (mInitialized)
        Destroy();

    mDevice = device;
    mDisplayWidth = displayWidth;
    mDisplayHeight = displayHeight;
    mQualityMode = qualityMode;

    // Calculate render resolution based on quality mode
    GetRenderResolution(mRenderWidth, mRenderHeight);

    return CreateContext();
}

void FSR3Upscaler::Destroy()
{
    DestroyContext();
    mDevice = nullptr;
    mInitialized = false;
}

void FSR3Upscaler::OnResize(UINT displayWidth, UINT displayHeight)
{
    if (displayWidth == mDisplayWidth && displayHeight == mDisplayHeight)
        return;

    mDisplayWidth = displayWidth;
    mDisplayHeight = displayHeight;
    GetRenderResolution(mRenderWidth, mRenderHeight);

    // Recreate context with new resolution
    DestroyContext();
    CreateContext();
}

bool FSR3Upscaler::CreateContext()
{
    if (!mDevice)
        return false;

    // Initialize context to null
    mContext = nullptr;

    // Setup version descriptor (chain: upscale -> backend -> version)
    ffxCreateContextDescUpscaleVersion versionDesc = {};
    versionDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    versionDesc.header.pNext = nullptr;
    versionDesc.version = FFX_UPSCALER_VERSION;

    // Setup DX12 backend descriptor
    ffxCreateBackendDX12Desc backendDesc = {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.header.pNext = &versionDesc.header;
    backendDesc.device = mDevice;

    // Setup upscale context descriptor
    ffxCreateContextDescUpscale upscaleDesc = {};
    upscaleDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    upscaleDesc.header.pNext = &backendDesc.header;
    upscaleDesc.maxRenderSize.width = mRenderWidth;
    upscaleDesc.maxRenderSize.height = mRenderHeight;
    upscaleDesc.maxUpscaleSize.width = mDisplayWidth;
    upscaleDesc.maxUpscaleSize.height = mDisplayHeight;
    
    // Flags for FSR upscaler
    upscaleDesc.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |    // Use auto exposure
                        FFX_UPSCALE_ENABLE_DEPTH_INVERTED;    // Our depth is inverted (1=near, 0=far)
    upscaleDesc.fpMessage = nullptr;

    // Create context
    ffxReturnCode_t result = ffxCreateContext(&mContext, &upscaleDesc.header, nullptr);
    if (result != FFX_API_RETURN_OK)
    {
        printf("FSR3: Failed to create context, error code: %u\n", result);
        mContext = nullptr;
        return false;
    }

    printf("FSR3: Context created successfully (%ux%u -> %ux%u)\n", 
           mRenderWidth, mRenderHeight, mDisplayWidth, mDisplayHeight);
    
    mInitialized = true;
    return true;
}

void FSR3Upscaler::DestroyContext()
{
    if (mContext)
    {
        ffxDestroyContext(&mContext, nullptr);
        mContext = nullptr;
    }
    mInitialized = false;
}

void FSR3Upscaler::Dispatch(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* colorInput,
    ID3D12Resource* depthInput,
    ID3D12Resource* motionVectors,
    ID3D12Resource* output,
    float jitterX,
    float jitterY,
    float deltaTime,
    float cameraNear,
    float cameraFar,
    float cameraFovY,
    bool reset)
{
    if (!mInitialized || mContext == nullptr)
        return;

    // Setup dispatch descriptor
    ffxDispatchDescUpscale dispatchDesc = {};
    dispatchDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dispatchDesc.commandList = cmdList;

    // Setup resources using helper function
    dispatchDesc.color = ffxApiGetResourceDX12(colorInput, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatchDesc.depth = ffxApiGetResourceDX12(depthInput, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatchDesc.motionVectors = ffxApiGetResourceDX12(motionVectors, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dispatchDesc.output = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    // No reactive/transparency masks for now (optional resources)
    dispatchDesc.reactive.resource = nullptr;
    dispatchDesc.transparencyAndComposition.resource = nullptr;
    dispatchDesc.exposure.resource = nullptr;

    // Jitter offset (in pixels, subpixel)
    dispatchDesc.jitterOffset.x = jitterX;
    dispatchDesc.jitterOffset.y = jitterY;

    // Motion vector scale
    // Our motion vectors are in UV space [0,1], pointing from current to previous
    // FSR expects motion vectors in pixels
    // Scale: UV * renderSize = pixels
    // Y is negated because UV space Y is flipped relative to NDC
    dispatchDesc.motionVectorScale.x = (float)mRenderWidth;
    dispatchDesc.motionVectorScale.y = -(float)mRenderHeight;

    // Resolution
    dispatchDesc.renderSize.width = mRenderWidth;
    dispatchDesc.renderSize.height = mRenderHeight;
    dispatchDesc.upscaleSize.width = mDisplayWidth;
    dispatchDesc.upscaleSize.height = mDisplayHeight;

    // Sharpening (RCAS)
    dispatchDesc.enableSharpening = mEnableSharpening;
    dispatchDesc.sharpness = mSharpness;  // 0 = max sharpness, 1 = no sharpness

    // Timing - frame time in milliseconds
    dispatchDesc.frameTimeDelta = deltaTime;

    // Camera parameters
    dispatchDesc.cameraNear = cameraNear;
    dispatchDesc.cameraFar = cameraFar;
    dispatchDesc.cameraFovAngleVertical = cameraFovY;
    dispatchDesc.viewSpaceToMetersFactor = 1.0f;  // 1 unit = 1 meter

    // Pre-exposure (1.0 = no pre-exposure applied to input)
    dispatchDesc.preExposure = 1.0f;

    // Reset flag (for camera cuts, teleports, scene changes)
    dispatchDesc.reset = reset;

    // Flags
    dispatchDesc.flags = 0;

    // Dispatch FSR
    ffxReturnCode_t result = ffxDispatch(&mContext, &dispatchDesc.header);
    if (result != FFX_API_RETURN_OK)
    {
        printf("FSR3: Dispatch failed, error code: %u\n", result);
    }
}

void FSR3Upscaler::GetRenderResolution(UINT& outWidth, UINT& outHeight) const
{
    float ratio = 1.0f;
    
    switch (mQualityMode)
    {
    case FSR3QualityMode::NativeAA:
        ratio = 1.0f;
        break;
    case FSR3QualityMode::Quality:
        ratio = 1.5f;
        break;
    case FSR3QualityMode::Balanced:
        ratio = 1.7f;
        break;
    case FSR3QualityMode::Performance:
        ratio = 2.0f;
        break;
    case FSR3QualityMode::UltraPerformance:
        ratio = 3.0f;
        break;
    }

    outWidth = static_cast<UINT>(mDisplayWidth / ratio);
    outHeight = static_cast<UINT>(mDisplayHeight / ratio);

    // Ensure minimum size
    outWidth = max(outWidth, 1u);
    outHeight = max(outHeight, 1u);
}

void FSR3Upscaler::GetJitterOffset(int frameIndex, float& outX, float& outY) const
{
    int phaseCount = GetJitterPhaseCount();
    int index = frameIndex % phaseCount;

    // Query jitter offset from FSR (global query, doesn't need context)
    ffxQueryDescUpscaleGetJitterOffset jitterQuery = {};
    jitterQuery.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
    jitterQuery.index = index;
    jitterQuery.phaseCount = phaseCount;
    jitterQuery.pOutX = &outX;
    jitterQuery.pOutY = &outY;

    ffxQuery(nullptr, &jitterQuery.header);
}

int FSR3Upscaler::GetJitterPhaseCount() const
{
    int32_t phaseCount = 0;
    
    // Global query - doesn't need context
    ffxQueryDescUpscaleGetJitterPhaseCount phaseQuery = {};
    phaseQuery.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    phaseQuery.renderWidth = mRenderWidth;
    phaseQuery.displayWidth = mDisplayWidth;
    phaseQuery.pOutPhaseCount = &phaseCount;

    ffxQuery(nullptr, &phaseQuery.header);

    return phaseCount > 0 ? phaseCount : 8;
}

void FSR3Upscaler::SetQualityMode(FSR3QualityMode mode)
{
    if (mode == mQualityMode)
        return;

    mQualityMode = mode;
    
    // Recalculate render resolution
    GetRenderResolution(mRenderWidth, mRenderHeight);

    // Recreate context with new settings
    if (mInitialized)
    {
        DestroyContext();
        CreateContext();
    }
}
