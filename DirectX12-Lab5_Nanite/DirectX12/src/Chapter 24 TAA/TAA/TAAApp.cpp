//***************************************************************************************
// TAAApp.cpp - Temporal Anti-Aliasing Demo Application
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "TemporalAA.h"
#include "MotionVectors.h"
#include "FSR3Upscaler.h"
#include <cstdio>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct TAAMaterial
{
    std::string Name;
    int MatCBIndex = -1;
    int DiffuseSrvHeapIndex = -1;
    int NormalSrvHeapIndex = -1;
    int NumFramesDirty = gNumFrameResources;
    
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct TAATexture
{
    std::string Name;
    std::wstring Filename;
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

struct RenderItem
{
    RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;
 
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4();  // Previous frame world matrix for motion vectors
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = -1;
    TAAMaterial* Mat = nullptr;
    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Count
};

class TAAApp : public D3DApp
{
public:
    TAAApp(HINSTANCE hInstance);
    TAAApp(const TAAApp& rhs) = delete;
    TAAApp& operator=(const TAAApp& rhs) = delete;
    ~TAAApp();

    virtual bool Initialize()override;

private:
    virtual void CreateRtvAndDsvDescriptorHeaps()override;
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
    virtual void OnKeyboardInput(const GameTimer& gt);

    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateMotionVectorPassCB(const GameTimer& gt);
    void UpdateTAACB(const GameTimer& gt);

    void LoadTextures();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    
    void DrawSceneToTexture();
    void DrawMotionVectors();
    void ResolveTAA();
    void ApplyFSR3();

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12RootSignature> mTAARootSignature = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<TAAMaterial>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<TAATexture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    PassConstants mMainPassCB;
    PassConstants mPrevPassCB;
    TAAConstants mTAACB;

    Camera mCamera;
    
    std::unique_ptr<TemporalAA> mTemporalAA;
    std::unique_ptr<MotionVectors> mMotionVectors;
    std::unique_ptr<FSR3Upscaler> mFSR3;
    
    ComPtr<ID3D12Resource> mSceneColorBuffer;
    ComPtr<ID3D12Resource> mSceneDepthBuffer;
    ComPtr<ID3D12Resource> mFSR3OutputBuffer;

    UINT mSceneColorSrvIndex = 0;
    UINT mSceneColorRtvIndex = 0;
    UINT mMotionVectorSrvIndex = 0;
    UINT mMotionVectorRtvIndex = 0;
    UINT mTAAOutputSrvIndex = 0;
    UINT mTAAOutputRtvIndex = 0;
    UINT mTAAHistorySrvIndex = 0;
    UINT mTAAHistoryRtvIndex = 0;
    UINT mSceneDepthSrvIndex = 0;

    int mFrameIndex = 0;
    bool mTAAEnabled = true;
    bool mFSR3Enabled = false;
    bool mFSR3NeedsReset = true;
    
    UINT mFSR3OutputUavIndex = 0;
    
    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // Create console window for debug output
    AllocConsole();
    FILE* pCout;
    freopen_s(&pCout, "CONOUT$", "w", stdout);
    printf("=== TAA / FSR 3 Demo ===\n");
    printf("Controls:\n");
    printf("  WASD - Move camera\n");
    printf("  Mouse - Look around\n");
    printf("  T - Toggle TAA (Temporal Anti-Aliasing)\n");
    printf("  F - Toggle FSR 3 (AMD FidelityFX Super Resolution)\n");
    printf("\n");
    printf("Note: TAA and FSR3 are mutually exclusive.\n");
    printf("\n");

    try
    {
        TAAApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

TAAApp::TAAApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

TAAApp::~TAAApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TAAApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCamera.SetPosition(0.0f, 2.0f, -15.0f);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    return true;
}

void TAAApp::CreateRtvAndDsvDescriptorHeaps()
{
    // Need RTVs for: swap chain buffers + scene color + motion vectors + TAA output + TAA history + FSR intermediate
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 6;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 2; // Main depth + scene depth
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void TAAApp::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    
    // Create SRV descriptor heap if not created yet
    if(mSrvDescriptorHeap == nullptr)
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 12; // Extra for FSR
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));
    }

    // Recreate TAA resources
    if(mTemporalAA != nullptr)
    {
        mTemporalAA->OnResize(mClientWidth, mClientHeight);
        mMotionVectors->OnResize(mClientWidth, mClientHeight);
        if(mFSR3)
            mFSR3->OnResize(mClientWidth, mClientHeight);
    }
    else
    {
        mTemporalAA = std::make_unique<TemporalAA>(
            md3dDevice.Get(), mClientWidth, mClientHeight, mBackBufferFormat);
        mMotionVectors = std::make_unique<MotionVectors>(
            md3dDevice.Get(), mClientWidth, mClientHeight);
        
        // Initialize FSR3
        mFSR3 = std::make_unique<FSR3Upscaler>();
        if(!mFSR3->Initialize(md3dDevice.Get(), mClientWidth, mClientHeight, FSR3QualityMode::NativeAA))
        {
            printf("Warning: FSR3 initialization failed!\n");
        }
    }

    // Build scene color buffer
    D3D12_RESOURCE_DESC colorDesc;
    ZeroMemory(&colorDesc, sizeof(D3D12_RESOURCE_DESC));
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = mClientWidth;
    colorDesc.Height = mClientHeight;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = mBackBufferFormat;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    CD3DX12_CLEAR_VALUE colorClear(mBackBufferFormat, clearColor);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &colorDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &colorClear,
        IID_PPV_ARGS(&mSceneColorBuffer)));

    // Build scene depth buffer with SRV support
    D3D12_RESOURCE_DESC depthDesc;
    ZeroMemory(&depthDesc, sizeof(D3D12_RESOURCE_DESC));
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = mClientWidth;
    depthDesc.Height = mClientHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    CD3DX12_CLEAR_VALUE depthClear(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&mSceneDepthBuffer)));

    // Create scene color RTV
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(SwapChainBufferCount, mRtvDescriptorSize);
    
    mSceneColorRtvIndex = SwapChainBufferCount;
    md3dDevice->CreateRenderTargetView(mSceneColorBuffer.Get(), nullptr, rtvHandle);
    
    mMotionVectorRtvIndex = SwapChainBufferCount + 1;
    mTAAOutputRtvIndex = SwapChainBufferCount + 2;
    mTAAHistoryRtvIndex = SwapChainBufferCount + 3;
    
    // Create scene depth DSV
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsvHandle.Offset(1, mDsvDescriptorSize);
    
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.Texture2D.MipSlice = 0;
    md3dDevice->CreateDepthStencilView(mSceneDepthBuffer.Get(), &dsvDesc, dsvHandle);

    // Setup SRV descriptors for TAA resolve shader
    // Order must match shader expectations:
    // t0: Current Frame (Scene Color)
    // t1: History Frame (TAA History)
    // t2: Motion Vectors
    // t3: Depth Map
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    // t0: Scene Color (Current Frame)
    mSceneColorSrvIndex = 0;
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    srvCpuHandle.Offset(mSceneColorSrvIndex, mCbvSrvUavDescriptorSize);
    srvDesc.Format = mBackBufferFormat;
    md3dDevice->CreateShaderResourceView(mSceneColorBuffer.Get(), &srvDesc, srvCpuHandle);
    
    // t1: TAA History Buffer
    mTAAHistorySrvIndex = 1;
    srvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    srvCpuHandle.Offset(mTAAHistorySrvIndex, mCbvSrvUavDescriptorSize);
    srvDesc.Format = mBackBufferFormat;
    md3dDevice->CreateShaderResourceView(mTemporalAA->HistoryResource(), &srvDesc, srvCpuHandle);
    
    // t2: Motion Vectors
    mMotionVectorSrvIndex = 2;
    srvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    srvCpuHandle.Offset(mMotionVectorSrvIndex, mCbvSrvUavDescriptorSize);
    srvGpuHandle.Offset(mMotionVectorSrvIndex, mCbvSrvUavDescriptorSize);
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mMotionVectorRtvIndex, mRtvDescriptorSize);
    mMotionVectors->BuildDescriptors(srvCpuHandle, srvGpuHandle, rtvHandle);
    
    // t3: Depth Map
    mSceneDepthSrvIndex = 3;
    srvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    srvCpuHandle.Offset(mSceneDepthSrvIndex, mCbvSrvUavDescriptorSize);
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    md3dDevice->CreateShaderResourceView(mSceneDepthBuffer.Get(), &srvDesc, srvCpuHandle);
    
    // TAA Output buffer (separate, not part of the TAA resolve input table)
    mTAAOutputSrvIndex = 4;
    srvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    srvGpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    srvCpuHandle.Offset(mTAAOutputSrvIndex, mCbvSrvUavDescriptorSize);
    srvGpuHandle.Offset(mTAAOutputSrvIndex, mCbvSrvUavDescriptorSize);
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mTAAOutputRtvIndex, mRtvDescriptorSize);
    srvDesc.Format = mBackBufferFormat;
    md3dDevice->CreateShaderResourceView(mTemporalAA->Resource(), &srvDesc, srvCpuHandle);
    md3dDevice->CreateRenderTargetView(mTemporalAA->Resource(), nullptr, rtvHandle);
    
    // TAA History RTV (for copying)
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mTAAHistoryRtvIndex, mRtvDescriptorSize);
    md3dDevice->CreateRenderTargetView(mTemporalAA->HistoryResource(), nullptr, rtvHandle);
    
    // FSR3 output buffer with UAV support
    D3D12_RESOURCE_DESC fsr3OutputDesc = {};
    fsr3OutputDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    fsr3OutputDesc.Width = mClientWidth;
    fsr3OutputDesc.Height = mClientHeight;
    fsr3OutputDesc.DepthOrArraySize = 1;
    fsr3OutputDesc.MipLevels = 1;
    fsr3OutputDesc.Format = mBackBufferFormat;
    fsr3OutputDesc.SampleDesc.Count = 1;
    fsr3OutputDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    fsr3OutputDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;  // Required for FSR3 compute output

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &fsr3OutputDesc,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        nullptr,
        IID_PPV_ARGS(&mFSR3OutputBuffer)));

    // FSR3 output UAV descriptor
    mFSR3OutputUavIndex = 6;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = mBackBufferFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    
    srvCpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    srvCpuHandle.Offset(mFSR3OutputUavIndex, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateUnorderedAccessView(mFSR3OutputBuffer.Get(), nullptr, &uavDesc, srvCpuHandle);
}

void TAAApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    // Cycle through frame resources
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
    UpdateMotionVectorPassCB(gt);
    UpdateTAACB(gt);
    
    mFrameIndex++;
}

void TAAApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // 1. Render scene to color buffer
    DrawSceneToTexture();

    // 2. Generate motion vectors
    DrawMotionVectors();

    // 3. Apply TAA or FSR3
    if(mTAAEnabled)
    {
        // First frame: initialize history buffer with current frame
        if(mFrameIndex == 0)
        {
            mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                mSceneColorBuffer.Get(),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_COPY_SOURCE));
            
            mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                mTemporalAA->HistoryResource(),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_COPY_DEST));
            
            mCommandList->CopyResource(mTemporalAA->HistoryResource(), mSceneColorBuffer.Get());
            
            mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                mSceneColorBuffer.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_GENERIC_READ));
            
            mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                mTemporalAA->HistoryResource(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_GENERIC_READ));
        }
        
        ResolveTAA();
        
        // Copy TAA output to back buffer
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalAA->Resource(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
        
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST));

        mCommandList->CopyResource(CurrentBackBuffer(), mTemporalAA->Resource());

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT));
        
        // Copy TAA output to history buffer for next frame
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalAA->HistoryResource(),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_COPY_DEST));

        mCommandList->CopyResource(mTemporalAA->HistoryResource(), mTemporalAA->Resource());

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalAA->HistoryResource(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ));
        
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mTemporalAA->Resource(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_GENERIC_READ));
    }
    else if(mFSR3Enabled && mFSR3 && mFSR3->IsInitialized())
    {
        // FSR3 - AMD FidelityFX Super Resolution
        ApplyFSR3();
        
        // Copy FSR3 output to back buffer
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST));

        mCommandList->CopyResource(CurrentBackBuffer(), mFSR3OutputBuffer.Get());

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT));
        
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mFSR3OutputBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
    }
    else
    {
        // No AA - copy scene color directly to back buffer
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mSceneColorBuffer.Get(),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_COPY_SOURCE));
        
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_COPY_DEST));

        mCommandList->CopyResource(CurrentBackBuffer(), mSceneColorBuffer.Get());

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT));
        
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mSceneColorBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_GENERIC_READ));
    }

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TAAApp::DrawSceneToTexture()
{
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColorBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mSceneColorRtvIndex, mRtvDescriptorSize);
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsvHandle.Offset(1, mDsvDescriptorSize);

    float clearColor[] = { 0.2f, 0.4f, 0.6f, 1.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    
    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColorBuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ));

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_GENERIC_READ));
}

void TAAApp::DrawMotionVectors()
{
    mCommandList->SetPipelineState(mPSOs["motionVectors"].Get());

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mMotionVectors->Resource(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Need to use depth buffer for proper motion vector generation
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_DEPTH_READ));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mMotionVectorRtvIndex, mRtvDescriptorSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsvHandle.Offset(1, mDsvDescriptorSize);

    float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Use depth buffer for depth testing but don't write to it
    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    
    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mMotionVectors->Resource(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_GENERIC_READ));

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_READ,
        D3D12_RESOURCE_STATE_GENERIC_READ));
}

void TAAApp::ResolveTAA()
{
    mCommandList->SetPipelineState(mPSOs["taaResolve"].Get());

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mTemporalAA->Resource(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    rtvHandle.Offset(mTAAOutputRtvIndex, mRtvDescriptorSize);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);

    mCommandList->SetGraphicsRootSignature(mTAARootSignature.Get());
    
    auto taaCB = mCurrFrameResource->TAACB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(0, taaCB->GetGPUVirtualAddress());

    // Bind all textures for TAA resolve:
    // t0: Current frame (scene color)
    // t1: History frame (TAA history)
    // t2: Motion vectors
    // t3: Depth map
    // The descriptor table starts at mSceneColorSrvIndex and contains 4 consecutive SRVs
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    srvHandle.Offset(mSceneColorSrvIndex, mCbvSrvUavDescriptorSize);
    mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle);

    // Draw full-screen triangle
    mCommandList->IASetVertexBuffers(0, 0, nullptr);
    mCommandList->IASetIndexBuffer(nullptr);
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawInstanced(3, 1, 0, 0);
    
    // Note: transition back to GENERIC_READ is done in Draw() before copy
}

void TAAApp::ApplyFSR3()
{
    if(!mFSR3 || !mFSR3->IsInitialized())
        return;

    // Get jitter from FSR3
    float jitterX, jitterY;
    mFSR3->GetJitterOffset(mFrameIndex, jitterX, jitterY);

    // Transition resources for FSR3 compute
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColorBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mMotionVectors->Resource(),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mFSR3OutputBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    // Dispatch FSR3
    mFSR3->Dispatch(
        mCommandList.Get(),
        mSceneColorBuffer.Get(),
        mSceneDepthBuffer.Get(),
        mMotionVectors->Resource(),
        mFSR3OutputBuffer.Get(),
        jitterX, jitterY,
        mMainPassCB.DeltaTime * 1000.0f,  // Convert to milliseconds
        mMainPassCB.NearZ,
        mMainPassCB.FarZ,
        0.25f * MathHelper::Pi,  // FOV
        mFSR3NeedsReset
    );
    
    mFSR3NeedsReset = false;

    // Transition resources back
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColorBuffer.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_GENERIC_READ));
    
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneDepthBuffer.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_GENERIC_READ));
    
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mMotionVectors->Resource(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_GENERIC_READ));
}

void TAAApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void TAAApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TAAApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void TAAApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if(GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(10.0f*dt);

    if(GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-10.0f*dt);

    if(GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-10.0f*dt);

    if(GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(10.0f*dt);

    // Toggle TAA with T key (disables FSR3)
    static bool tKeyPressed = false;
    if(GetAsyncKeyState('T') & 0x8000)
    {
        if(!tKeyPressed)
        {
            mTAAEnabled = !mTAAEnabled;
            if (mTAAEnabled)
                mFSR3Enabled = false;  // Disable FSR3 when TAA is enabled
            printf("TAA: %s\n", mTAAEnabled ? "ON" : "OFF");
            tKeyPressed = true;
        }
    }
    else
    {
        tKeyPressed = false;
    }
    
    // Toggle FSR3 with F key (disables TAA)
    static bool fKeyPressed = false;
    if(GetAsyncKeyState('F') & 0x8000)
    {
        if(!fKeyPressed)
        {
            mFSR3Enabled = !mFSR3Enabled;
            if (mFSR3Enabled)
            {
                mTAAEnabled = false;  // Disable TAA when FSR3 is enabled
                mFSR3NeedsReset = true;  // Reset history when enabling FSR3
            }
            printf("FSR 3 (AMD FidelityFX): %s\n", mFSR3Enabled ? "ON" : "OFF");
            fKeyPressed = true;
        }
    }
    else
    {
        fKeyPressed = false;
    }

    mCamera.UpdateViewMatrix();
}

void TAAApp::AnimateMaterials(const GameTimer& gt)
{
    // Animate the first cylinder (index 1 in mAllRitems, after grid)
    // Grid is at index 0, first left cylinder is at index 1
    if(mAllRitems.size() > 1)
    {
        auto& cylinder = mAllRitems[1];
        
        // Save current world as previous world BEFORE updating
        cylinder->PrevWorld = cylinder->World;
        
        // Animate: move back and forth along X axis (slower for better TAA)
        float time = gt.TotalTime();
        float offsetX = sinf(time * 0.5f) * 2.0f;  // Slower oscillation, smaller amplitude
        
        XMMATRIX world = XMMatrixTranslation(-5.0f + offsetX, 1.5f, -10.0f);
        XMStoreFloat4x4(&cylinder->World, world);
        
        cylinder->NumFramesDirty = gNumFrameResources;
    }
}

void TAAApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for(auto& e : mAllRitems)
    {
        // Always update all objects to ensure PrevWorld is correct for motion vectors
        XMMATRIX world = XMLoadFloat4x4(&e->World);
        XMMATRIX prevWorld = XMLoadFloat4x4(&e->PrevWorld);
        XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

        ObjectConstants objConstants;
        XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(prevWorld));
        XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
        objConstants.MaterialIndex = e->Mat->MatCBIndex;

        currObjectCB->CopyData(e->ObjCBIndex, objConstants);

        if(e->NumFramesDirty > 0)
            e->NumFramesDirty--;
    }
}

void TAAApp::UpdateMaterialBuffer(const GameTimer& gt)
{
    auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
    for(auto& e : mMaterials)
    {
        TAAMaterial* mat = e.second.get();
        if(mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
            matData.NormalMapIndex = mat->NormalSrvHeapIndex;

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            mat->NumFramesDirty--;
        }
    }
}

void TAAApp::UpdateMainPassCB(const GameTimer& gt)
{
    // Save previous frame's UNJITTERED ViewProj for motion vectors
    XMFLOAT4X4 prevUnjitteredViewProj = mMainPassCB.UnjitteredViewProj;

    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    
    // Calculate unjittered ViewProj first (for motion vectors)
    XMMATRIX unjitteredViewProj = XMMatrixMultiply(view, proj);
    
    // Store unjittered ViewProj
    XMStoreFloat4x4(&mMainPassCB.UnjitteredViewProj, XMMatrixTranspose(unjitteredViewProj));
    
    // Store previous frame's unjittered ViewProj for motion vectors
    if(mFrameIndex > 0)
    {
        mMainPassCB.PrevViewProj = prevUnjitteredViewProj;
    }
    else
    {
        mMainPassCB.PrevViewProj = mMainPassCB.UnjitteredViewProj;
    }

    // Apply jitter when TAA or FSR3 is enabled (both need temporal jitter)
    if (mTAAEnabled || mFSR3Enabled)
    {
        float jitterX, jitterY;
        
        if (mFSR3Enabled && mFSR3 && mFSR3->IsInitialized())
        {
            // Get jitter from FSR3
            mFSR3->GetJitterOffset(mFrameIndex, jitterX, jitterY);
            // FSR3 returns jitter in pixels, convert to NDC
            // Y is negated per AMD documentation (DirectX coordinate system)
            jitterX = (2.0f * jitterX) / (float)mClientWidth;
            jitterY = (-2.0f * jitterY) / (float)mClientHeight;
        }
        else
        {
            // Use TAA jitter
            XMFLOAT2 jitter = TemporalAA::GetJitter(mFrameIndex);
            jitterX = (2.0f * jitter.x) / (float)mClientWidth;
            jitterY = (2.0f * jitter.y) / (float)mClientHeight;
        }
        
        // Modify projection matrix directly (offset in third row)
        XMFLOAT4X4 projMat;
        XMStoreFloat4x4(&projMat, proj);
        projMat._31 += jitterX;  // Horizontal offset
        projMat._32 += jitterY;  // Vertical offset
        proj = XMLoadFloat4x4(&projMat);
    }

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void TAAApp::UpdateMotionVectorPassCB(const GameTimer& gt)
{
    // Motion vector pass uses same constants as main pass
    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(1, mMainPassCB);
}

void TAAApp::UpdateTAACB(const GameTimer& gt)
{
    XMFLOAT2 jitter = TemporalAA::GetJitter(mFrameIndex);
    
    mTAACB.JitterOffset = jitter;
    mTAACB.ScreenSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mTAACB.BlendFactor = 0.04f;  // Lower for more stable history (4% current, 96% history)
    mTAACB.MotionScale = 1.0f;

    auto currTAACB = mCurrFrameResource->TAACB.get();
    currTAACB->CopyData(0, mTAACB);
}

void TAAApp::LoadTextures()
{
    // Create a simple white texture
    auto whiteTex = std::make_unique<TAATexture>();
    whiteTex->Name = "whiteTex";
    whiteTex->Filename = L"";
    
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&whiteTex->Resource)));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(whiteTex->Resource.Get(), 0, 1);

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&whiteTex->UploadHeap)));

    UINT pixel = 0xFFFFFFFF;
    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = &pixel;
    textureData.RowPitch = 4;
    textureData.SlicePitch = 4;

    UpdateSubresources(mCommandList.Get(), whiteTex->Resource.Get(), whiteTex->UploadHeap.Get(),
        0, 0, 1, &textureData);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        whiteTex->Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    mTextures[whiteTex->Name] = std::move(whiteTex);
}

void TAAApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[3];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));

    // TAA root signature
    CD3DX12_DESCRIPTOR_RANGE taaTexTable;
    taaTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0); // current, history, motion, depth

    CD3DX12_ROOT_PARAMETER taaRootParameter[2];
    taaRootParameter[0].InitAsConstantBufferView(0);
    taaRootParameter[1].InitAsDescriptorTable(1, &taaTexTable, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC taaRootSigDesc(2, taaRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> taaSerializedRootSig = nullptr;
    hr = D3D12SerializeRootSignature(&taaRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        taaSerializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        taaSerializedRootSig->GetBufferPointer(),
        taaSerializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mTAARootSignature.GetAddressOf())));
}

void TAAApp::BuildDescriptorHeaps()
{
    // Create SRV heap if not already created (may be created in OnResize)
    if(mSrvDescriptorHeap == nullptr)
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 10;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto whiteTex = mTextures["whiteTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = whiteTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = whiteTex->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    // Skip first 5 slots for TAA resources
    hDescriptor.Offset(5, mCbvSrvUavDescriptorSize);
    md3dDevice->CreateShaderResourceView(whiteTex.Get(), &srvDesc, hDescriptor);
}

void TAAApp::BuildShadersAndInputLayout()
{
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
    
    mShaders["motionVectorsVS"] = d3dUtil::CompileShader(L"Shaders\\MotionVectors.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["motionVectorsPS"] = d3dUtil::CompileShader(L"Shaders\\MotionVectors.hlsl", nullptr, "PS", "ps_5_1");
    
    mShaders["taaResolveVS"] = d3dUtil::CompileShader(L"Shaders\\TAAResolve.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["taaResolvePS"] = d3dUtil::CompileShader(L"Shaders\\TAAResolve.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void TAAApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;
    for(size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for(size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for(size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for(size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void TAAApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = 1;
    opaquePsoDesc.SampleDesc.Quality = 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // Motion vectors PSO - uses depth test but doesn't write to depth
    D3D12_GRAPHICS_PIPELINE_STATE_DESC motionVectorsPsoDesc = opaquePsoDesc;
    motionVectorsPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["motionVectorsVS"]->GetBufferPointer()),
        mShaders["motionVectorsVS"]->GetBufferSize()
    };
    motionVectorsPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["motionVectorsPS"]->GetBufferPointer()),
        mShaders["motionVectorsPS"]->GetBufferSize()
    };
    motionVectorsPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
    motionVectorsPsoDesc.DSVFormat = mDepthStencilFormat;
    motionVectorsPsoDesc.DepthStencilState.DepthEnable = TRUE;
    motionVectorsPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Read only
    motionVectorsPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&motionVectorsPsoDesc, IID_PPV_ARGS(&mPSOs["motionVectors"])));

    // TAA resolve PSO (full-screen pass)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC taaResolvePsoDesc = opaquePsoDesc;
    taaResolvePsoDesc.pRootSignature = mTAARootSignature.Get();
    taaResolvePsoDesc.InputLayout = { nullptr, 0 };
    taaResolvePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["taaResolveVS"]->GetBufferPointer()),
        mShaders["taaResolveVS"]->GetBufferSize()
    };
    taaResolvePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["taaResolvePS"]->GetBufferPointer()),
        mShaders["taaResolvePS"]->GetBufferSize()
    };
    taaResolvePsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    taaResolvePsoDesc.DepthStencilState.DepthEnable = FALSE;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&taaResolvePsoDesc, IID_PPV_ARGS(&mPSOs["taaResolve"])));
}

void TAAApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            2, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void TAAApp::BuildMaterials()
{
    auto white = std::make_unique<TAAMaterial>();
    white->Name = "white";
    white->MatCBIndex = 0;
    white->DiffuseSrvHeapIndex = 5;
    white->NormalSrvHeapIndex = 5;
    white->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    white->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    white->Roughness = 0.3f;

    auto red = std::make_unique<TAAMaterial>();
    red->Name = "red";
    red->MatCBIndex = 1;
    red->DiffuseSrvHeapIndex = 5;
    red->NormalSrvHeapIndex = 5;
    red->DiffuseAlbedo = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);
    red->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    red->Roughness = 0.3f;

    auto green = std::make_unique<TAAMaterial>();
    green->Name = "green";
    green->MatCBIndex = 2;
    green->DiffuseSrvHeapIndex = 5;
    green->NormalSrvHeapIndex = 5;
    green->DiffuseAlbedo = XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f);
    green->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    green->Roughness = 0.3f;

    auto blue = std::make_unique<TAAMaterial>();
    blue->Name = "blue";
    blue->MatCBIndex = 3;
    blue->DiffuseSrvHeapIndex = 5;
    blue->NormalSrvHeapIndex = 5;
    blue->DiffuseAlbedo = XMFLOAT4(0.2f, 0.2f, 1.0f, 1.0f);
    blue->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    blue->Roughness = 0.3f;

    mMaterials["white"] = std::move(white);
    mMaterials["red"] = std::move(red);
    mMaterials["green"] = std::move(green);
    mMaterials["blue"] = std::move(blue);
}

void TAAApp::BuildRenderItems()
{
    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->PrevWorld = MathHelper::Identity4x4();  // Initialize PrevWorld
    gridRitem->ObjCBIndex = 0;
    gridRitem->Mat = mMaterials["white"].get();
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
    mAllRitems.push_back(std::move(gridRitem));

    UINT objCBIndex = 1;
    for(int i = 0; i < 5; ++i)
    {
        auto leftCylRitem = std::make_unique<RenderItem>();
        auto rightCylRitem = std::make_unique<RenderItem>();
        auto leftSphereRitem = std::make_unique<RenderItem>();
        auto rightSphereRitem = std::make_unique<RenderItem>();

        XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
        XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f);

        XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
        XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);

        XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
        XMStoreFloat4x4(&leftCylRitem->PrevWorld, leftCylWorld);  // Initialize PrevWorld
        leftCylRitem->ObjCBIndex = objCBIndex++;
        leftCylRitem->Mat = mMaterials["red"].get();
        leftCylRitem->Geo = mGeometries["shapeGeo"].get();
        leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
        XMStoreFloat4x4(&rightCylRitem->PrevWorld, rightCylWorld);  // Initialize PrevWorld
        rightCylRitem->ObjCBIndex = objCBIndex++;
        rightCylRitem->Mat = mMaterials["green"].get();
        rightCylRitem->Geo = mGeometries["shapeGeo"].get();
        rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        XMStoreFloat4x4(&leftSphereRitem->PrevWorld, leftSphereWorld);  // Initialize PrevWorld
        leftSphereRitem->ObjCBIndex = objCBIndex++;
        leftSphereRitem->Mat = mMaterials["blue"].get();
        leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
        leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        XMStoreFloat4x4(&rightSphereRitem->PrevWorld, rightSphereWorld);  // Initialize PrevWorld
        rightSphereRitem->ObjCBIndex = objCBIndex++;
        rightSphereRitem->Mat = mMaterials["red"].get();
        rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
        rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
        mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
        mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
        mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

        mAllRitems.push_back(std::move(leftCylRitem));
        mAllRitems.push_back(std::move(rightCylRitem));
        mAllRitems.push_back(std::move(leftSphereRitem));
        mAllRitems.push_back(std::move(rightSphereRitem));
    }
}

void TAAApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;

        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
        
        // Set texture SRV
        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(2, tex);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> TAAApp::GetStaticSamplers()
{
    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        8);

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f,
        8);

    const CD3DX12_STATIC_SAMPLER_DESC shadow(
        6,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f,
        16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp,
        shadow
    };
}
