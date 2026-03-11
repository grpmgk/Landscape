//***************************************************************************************
// MotionVectors.cpp
//***************************************************************************************

#include "MotionVectors.h"

using Microsoft::WRL::ComPtr;

MotionVectors::MotionVectors(ID3D12Device* device, UINT width, UINT height)
{
    md3dDevice = device;
    mWidth = width;
    mHeight = height;

    BuildResource();
}

UINT MotionVectors::Width() const
{
    return mWidth;
}

UINT MotionVectors::Height() const
{
    return mHeight;
}

ID3D12Resource* MotionVectors::Resource()
{
    return mMotionVectorMap.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE MotionVectors::Srv() const
{
    return mhGpuSrv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE MotionVectors::Rtv() const
{
    return mhCpuRtv;
}

void MotionVectors::BuildDescriptors(
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv)
{
    mhCpuSrv = hCpuSrv;
    mhGpuSrv = hGpuSrv;
    mhCpuRtv = hCpuRtv;

    BuildDescriptors();
}

void MotionVectors::OnResize(UINT newWidth, UINT newHeight)
{
    if ((mWidth != newWidth) || (mHeight != newHeight))
    {
        mWidth = newWidth;
        mHeight = newHeight;

        BuildResource();
        BuildDescriptors();
    }
}

void MotionVectors::BuildDescriptors()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = mFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    md3dDevice->CreateShaderResourceView(mMotionVectorMap.Get(), &srvDesc, mhCpuSrv);
    md3dDevice->CreateRenderTargetView(mMotionVectorMap.Get(), &rtvDesc, mhCpuRtv);
}

void MotionVectors::BuildResource()
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mWidth;
    texDesc.Height = mHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = mFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    CD3DX12_CLEAR_VALUE optClear(mFormat, clearColor);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &optClear,
        IID_PPV_ARGS(&mMotionVectorMap)));
}
