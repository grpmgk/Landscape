//***************************************************************************************
// TemporalAA.h - Temporal Anti-Aliasing implementation
// 
// Implements TAA based on industry-standard techniques:
// - Halton (2,3) jitter sequence for 8-sample pattern
// - Variance-based neighborhood clamping
// - YCoCg color space for better clipping
// - Catmull-Rom filtering for history sampling
// - Adaptive blending based on motion and variance
// - Depth-based disocclusion detection
// - Velocity dilation for better edge quality
// - Sharpening pass to compensate for temporal blur
//
// References:
// - https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
// - https://sugulee.wordpress.com/2021/06/21/temporal-anti-aliasingtaa-tutorial/
// - https://alextardif.com/TAA.html
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"

class TemporalAA
{
public:
    TemporalAA(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
    
    TemporalAA(const TemporalAA& rhs) = delete;
    TemporalAA& operator=(const TemporalAA& rhs) = delete;
    ~TemporalAA() = default;

    UINT Width() const;
    UINT Height() const;
    ID3D12Resource* Resource();
    ID3D12Resource* HistoryResource();
    
    CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv() const;
    
    CD3DX12_GPU_DESCRIPTOR_HANDLE HistorySrv() const;
    CD3DX12_CPU_DESCRIPTOR_HANDLE HistoryRtv() const;

    D3D12_VIEWPORT Viewport() const;
    D3D12_RECT ScissorRect() const;

    void BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
        UINT srvDescriptorSize,
        UINT rtvDescriptorSize);

    void OnResize(UINT newWidth, UINT newHeight);
    
    // Swap current and history buffers after TAA resolve
    void SwapBuffers();
    
    // Get Halton (2,3) jitter offset for given frame index
    // Returns jitter in pixel space [-0.5, 0.5]
    // Uses 8-sample pattern for good temporal distribution
    static DirectX::XMFLOAT2 GetJitter(int frameIndex);

private:
    void BuildDescriptors();
    void BuildResource();

private:
    ID3D12Device* md3dDevice = nullptr;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;

    UINT mWidth = 0;
    UINT mHeight = 0;
    DXGI_FORMAT mFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuRtv;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhHistoryCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE mhHistoryGpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE mhHistoryCpuRtv;

    Microsoft::WRL::ComPtr<ID3D12Resource> mTAAOutput = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mHistoryBuffer = nullptr;
};
