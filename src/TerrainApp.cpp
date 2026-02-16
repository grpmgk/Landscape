#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "Common/Camera.h"
#include "Common/DDSTextureLoader.h"
#include "FrameResource.h"
#include "Terrain.h"
#include "QuadTree.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const int gNumFrameResources = 3;

// Bounding box for potential frustum culling (kept for compatibility)
struct TerrainBoundingBox
{
    XMFLOAT3 Center;
    XMFLOAT3 Extents;
};

class TerrainApp : public D3DApp
{
public:
    TerrainApp(HINSTANCE hInstance);
    ~TerrainApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void HandleKeyboard(const GameTimer& gt);
    void TickCamera(const GameTimer& gt);

    void UpdatePerObjectCB(const GameTimer& gt);
    void UpdatePassCB(const GameTimer& gt);
    void UpdateTerrainCB(const GameTimer& gt);

    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildPSOs();
    void BuildFrameResources();

    void DrawTerrainNodes();

    void ExtractFrustumPlanes(XMFLOAT4* planes, const XMMATRIX& viewProj);
    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 2> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mActiveFrame = nullptr;
    int mActiveFrameIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap = nullptr;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // Terrain system
    std::unique_ptr<Terrain> mTerrain;
    TerrainBoundingBox mTerrainBounds; // kept (not printed/used directly here)

    // QuadTree LOD + culling
    std::unique_ptr<QuadTree> mQuadTree;
    std::vector<TerrainNode*> mDrawNodes;

    // Textures
    ComPtr<ID3D12Resource> mHeightmapTexture;
    ComPtr<ID3D12Resource> mHeightmapUploadBuffer;
    ComPtr<ID3D12Resource> mDiffuseTexture;
    ComPtr<ID3D12Resource> mDiffuseUploadBuffer;
    ComPtr<ID3D12Resource> mNormalTexture;
    ComPtr<ID3D12Resource> mNormalUploadBuffer;
    ComPtr<ID3D12Resource> mWhiteTexture;
    ComPtr<ID3D12Resource> mWhiteTextureUpload;

    PassConstants mMainPassCB;
    TerrainConstants mTerrainCB;
    Camera mCamera;

    // Frustum planes extracted each frame
    XMFLOAT4 mFrustumPlanes[6];

    // State
    bool mIsTerrainVisible = true;
    bool mUseWireframe = false;

    // LOD thresholds (QuadTree uses these)
    std::vector<float> mLodDistances = { 100.0f, 200.0f, 400.0f, 600.0f, 1000.0f };

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        TerrainApp app(hInstance);
        if (!app.Initialize())
            return 0;
        return app.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

TerrainApp::TerrainApp(HINSTANCE hInstance) : D3DApp(hInstance)
{
    mMainWndCaption = L"Terrain Demo - LOD + Frustum Culling";
}

TerrainApp::~TerrainApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool TerrainApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Camera setup
    mCamera.SetPosition(0.0f, 500.0f, -400.0f);
    mCamera.LookAt(mCamera.GetPosition3f(), XMFLOAT3(0, -150, 0), XMFLOAT3(0, 1, 0));

    // Terrain creation
    mTerrain = std::make_unique<Terrain>(md3dDevice.Get(), mCommandList.Get(), 512.0f, 0.0f, 150.0f);

    if (!mTerrain->LoadHeightmapDDS(L"TerrainDetails/003/Height_Out.dds", md3dDevice.Get(), mCommandList.Get()))
    {
        mTerrain->GenerateProceduralHeightmap(256, 256, 4.0f, 6);
    }

    mTerrain->BuildGeometry(md3dDevice.Get(), mCommandList.Get());

    // Terrain AABB (kept for compatibility)
    {
        const float halfSize = mTerrain->GetTerrainSize() * 0.5f;
        const float halfHeight = (mTerrain->GetMaxHeight() - mTerrain->GetMinHeight()) * 0.5f;

        mTerrainBounds.Center = XMFLOAT3(0.0f, mTerrain->GetMinHeight() + halfHeight, 0.0f);
        mTerrainBounds.Extents = XMFLOAT3(halfSize, halfHeight + 10.0f, halfSize);
    }

    // QuadTree init
    mQuadTree = std::make_unique<QuadTree>();
    const float minNodeSize = mTerrain->GetTerrainSize() / 8.0f;

    mQuadTree->SetLODDistances(mLodDistances);
    mQuadTree->Initialize(mTerrain->GetTerrainSize(), minNodeSize, 5);
    mQuadTree->SetHeightRange(0, 0, mTerrain->GetTerrainSize(),
        mTerrain->GetMinHeight(), mTerrain->GetMaxHeight());

    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildFrameResources();
    BuildPSOs();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();

    return true;
}

void TerrainApp::OnResize()
{
    D3DApp::OnResize();
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 3000.0f);
}

void TerrainApp::Update(const GameTimer& gt)
{
    HandleKeyboard(gt);
    TickCamera(gt);

    // Rotate frame resources
    mActiveFrameIndex = (mActiveFrameIndex + 1) % gNumFrameResources;
    mActiveFrame = mFrameResources[mActiveFrameIndex].get();

    if (mActiveFrame->Fence != 0 && mFence->GetCompletedValue() < mActiveFrame->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mActiveFrame->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // Build view-projection and extract planes
    {
        const XMMATRIX view = mCamera.GetView();
        const XMMATRIX proj = mCamera.GetProj();
        const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
        ExtractFrustumPlanes(mFrustumPlanes, viewProj);
    }

    // QuadTree update -> visible nodes
    {
        const XMFLOAT3 eye = mCamera.GetPosition3f();
        mQuadTree->Update(eye, mFrustumPlanes);

        mDrawNodes.clear();
        mQuadTree->GetVisibleNodes(mDrawNodes);

        mIsTerrainVisible = !mDrawNodes.empty();
    }

    UpdatePerObjectCB(gt);
    UpdatePassCB(gt);
    UpdateTerrainCB(gt);
}

void TerrainApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mActiveFrame->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    ThrowIfFailed(mCommandList->Reset(
        cmdListAlloc.Get(),
        mUseWireframe ? mPSOs["terrain_wireframe"].Get() : mPSOs["terrain"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // Root params layout stays the same (dependent files/shaders expect this)
    auto passCB = mActiveFrame->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto terrainCB = mActiveFrame->TerrainCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, terrainCB->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootDescriptorTable(3, texHandle);

    if (mIsTerrainVisible)
        DrawTerrainNodes();

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mActiveFrame->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TerrainApp::DrawTerrainNodes()
{
    auto geo = mTerrain->GetGeometry();

    mCommandList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
    mCommandList->IASetIndexBuffer(&geo->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto objectCB = mActiveFrame->ObjectCB->Resource();
    const UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    for (size_t i = 0; i < mDrawNodes.size(); ++i)
    {
        const TerrainNode* node = mDrawNodes[i];

        // per-node object constants
        const D3D12_GPU_VIRTUAL_ADDRESS objAddr =
            objectCB->GetGPUVirtualAddress() + i * objCBByteSize;

        mCommandList->SetGraphicsRootConstantBufferView(0, objAddr);

        const int lod = (node->LODLevel < 4) ? node->LODLevel : 4;
        const char* meshName = Terrain::GetLODMeshName(lod);
        auto& submesh = geo->DrawArgs[meshName];

        mCommandList->DrawIndexedInstanced(
            submesh.IndexCount, 1,
            submesh.StartIndexLocation,
            submesh.BaseVertexLocation, 0);
    }
}

void TerrainApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void TerrainApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void TerrainApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        const float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        const float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void TerrainApp::HandleKeyboard(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();
    float moveSpeed = 100.0f;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        moveSpeed *= 3.0f;

    if (GetAsyncKeyState('W') & 0x8000) mCamera.Walk(moveSpeed * dt);
    if (GetAsyncKeyState('S') & 0x8000) mCamera.Walk(-moveSpeed * dt);
    if (GetAsyncKeyState('A') & 0x8000) mCamera.Strafe(-moveSpeed * dt);
    if (GetAsyncKeyState('D') & 0x8000) mCamera.Strafe(moveSpeed * dt);

    if (GetAsyncKeyState('Q') & 0x8000)
        mCamera.SetPosition(mCamera.GetPosition3f().x, mCamera.GetPosition3f().y + moveSpeed * dt, mCamera.GetPosition3f().z);

    if (GetAsyncKeyState('E') & 0x8000)
        mCamera.SetPosition(mCamera.GetPosition3f().x, mCamera.GetPosition3f().y - moveSpeed * dt, mCamera.GetPosition3f().z);

    // Toggle wireframe
    static bool keyLatch = false;
    if (GetAsyncKeyState('1') & 0x8000)
    {
        if (!keyLatch)
        {
            mUseWireframe = !mUseWireframe;
            keyLatch = true;
        }
    }
    else
    {
        keyLatch = false;
    }
}

void TerrainApp::TickCamera(const GameTimer& gt)
{
    mCamera.UpdateViewMatrix();
}

void TerrainApp::UpdatePerObjectCB(const GameTimer& gt)
{
    auto currObjectCB = mActiveFrame->ObjectCB.get();
    const float terrainSize = mTerrain->GetTerrainSize();

    for (size_t i = 0; i < mDrawNodes.size(); ++i)
    {
        const TerrainNode* node = mDrawNodes[i];

        const float nodeScale = node->Size;
        const float uvScale = node->Size / terrainSize;
        const float uvOffsetX = (node->X / terrainSize) + 0.5f - uvScale * 0.5f;
        const float uvOffsetZ = (node->Z / terrainSize) + 0.5f - uvScale * 0.5f;

        const XMMATRIX world =
            XMMatrixScaling(nodeScale, 1.0f, nodeScale) *
            XMMatrixTranslation(node->X, 0.0f, node->Z);

        const XMMATRIX texTransform =
            XMMatrixScaling(uvScale, uvScale, 1.0f) *
            XMMatrixTranslation(uvOffsetX, uvOffsetZ, 0.0f);

        ObjectConstants obj{};
        XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&obj.TexTransform, XMMatrixTranspose(texTransform));
        obj.MaterialIndex = 0;
        obj.LODLevel = (node->LODLevel < 4) ? node->LODLevel : 4;

        currObjectCB->CopyData((int)i, obj);
    }
}

void TerrainApp::UpdatePassCB(const GameTimer& gt)
{
    const XMMATRIX view = mCamera.GetView();
    const XMMATRIX proj = mCamera.GetProj();
    const XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    const XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    const XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    const XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

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
    mMainPassCB.FarZ = 3000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.3f, 0.3f, 0.35f, 1.0f };

    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.9f, 0.85f, 0.8f };

    auto passCB = mActiveFrame->PassCB.get();
    passCB->CopyData(0, mMainPassCB);
}

void TerrainApp::UpdateTerrainCB(const GameTimer& gt)
{
    mTerrainCB.MinHeight = mTerrain->GetMinHeight();
    mTerrainCB.MaxHeight = mTerrain->GetMaxHeight();
    mTerrainCB.TerrainSize = mTerrain->GetTerrainSize();
    mTerrainCB.TexelSize = 1.0f / mTerrain->GetHeightmapWidth();
    mTerrainCB.HeightMapSize = XMFLOAT2((float)mTerrain->GetHeightmapWidth(),
        (float)mTerrain->GetHeightmapHeight());

    auto currTerrainCB = mActiveFrame->TerrainCB.get();
    currTerrainCB->CopyData(0, mTerrainCB);
}

void TerrainApp::ExtractFrustumPlanes(XMFLOAT4* planes, const XMMATRIX& viewProj)
{
    XMFLOAT4X4 M;
    XMStoreFloat4x4(&M, viewProj);

    // Left, Right, Bottom, Top, Near, Far
    planes[0] = { M._14 + M._11, M._24 + M._21, M._34 + M._31, M._44 + M._41 };
    planes[1] = { M._14 - M._11, M._24 - M._21, M._34 - M._31, M._44 - M._41 };
    planes[2] = { M._14 + M._12, M._24 + M._22, M._34 + M._32, M._44 + M._42 };
    planes[3] = { M._14 - M._12, M._24 - M._22, M._34 - M._32, M._44 - M._42 };
    planes[4] = { M._13,         M._23,         M._33,         M._43 };
    planes[5] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = XMLoadFloat4(&planes[i]);
        p = XMPlaneNormalize(p);
        XMStoreFloat4(&planes[i], p);
    }
}

void TerrainApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsConstantBufferView(2);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TerrainApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 3;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));

    // Heightmap
    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Height_Out.dds",
        mHeightmapTexture, mHeightmapUploadBuffer);

    if (FAILED(hr))
    {
        // Fallback procedural heightmap upload (same logic)
        UINT width = mTerrain->GetHeightmapWidth();
        UINT height = mTerrain->GetHeightmapHeight();

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mHeightmapTexture)));

        const UINT64 uploadSize = GetRequiredIntermediateSize(mHeightmapTexture.Get(), 0, 1);
        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadSize), D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&mHeightmapUploadBuffer)));

        std::vector<float> heightData(width * height);
        for (UINT z = 0; z < height; ++z)
        {
            for (UINT x = 0; x < width; ++x)
            {
                float worldX = (float)x / width * mTerrain->GetTerrainSize() - mTerrain->GetTerrainSize() * 0.5f;
                float worldZ = (float)z / height * mTerrain->GetTerrainSize() - mTerrain->GetTerrainSize() * 0.5f;
                float h = mTerrain->GetHeight(worldX, worldZ);
                heightData[z * width + x] =
                    (h - mTerrain->GetMinHeight()) / (mTerrain->GetMaxHeight() - mTerrain->GetMinHeight());
            }
        }

        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = heightData.data();
        subData.RowPitch = width * sizeof(float);
        subData.SlicePitch = subData.RowPitch * height;

        UpdateSubresources(mCommandList.Get(), mHeightmapTexture.Get(), mHeightmapUploadBuffer.Get(),
            0, 0, 1, &subData);

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mHeightmapTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }

    // Diffuse
    DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Weathering_Out.dds",
        mDiffuseTexture, mDiffuseUploadBuffer);

    // Normal
    DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Normals_Out.dds",
        mNormalTexture, mNormalUploadBuffer);

    // White fallback texture
    D3D12_RESOURCE_DESC whiteTexDesc = {};
    whiteTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    whiteTexDesc.Width = 1;
    whiteTexDesc.Height = 1;
    whiteTexDesc.DepthOrArraySize = 1;
    whiteTexDesc.MipLevels = 1;
    whiteTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    whiteTexDesc.SampleDesc.Count = 1;
    whiteTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &whiteTexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mWhiteTexture)));

    const UINT64 whiteUploadSize = GetRequiredIntermediateSize(mWhiteTexture.Get(), 0, 1);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(whiteUploadSize), D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mWhiteTextureUpload)));

    UINT whitePixel = 0xFFFFFFFF;
    D3D12_SUBRESOURCE_DATA whiteData = {};
    whiteData.pData = &whitePixel;
    whiteData.RowPitch = 4;
    whiteData.SlicePitch = 4;

    UpdateSubresources(mCommandList.Get(), mWhiteTexture.Get(), mWhiteTextureUpload.Get(), 0, 0, 1, &whiteData);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mWhiteTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // SRVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;

    // Heightmap SRV
    {
        D3D12_RESOURCE_DESC hmDesc = mHeightmapTexture->GetDesc();
        srvDesc.Format = hmDesc.Format;
        srvDesc.Texture2D.MipLevels = hmDesc.MipLevels;
        md3dDevice->CreateShaderResourceView(mHeightmapTexture.Get(), &srvDesc, hDescriptor);
        hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    }

    // Diffuse SRV (or white)
    {
        if (mDiffuseTexture)
        {
            D3D12_RESOURCE_DESC d = mDiffuseTexture->GetDesc();
            srvDesc.Format = d.Format;
            srvDesc.Texture2D.MipLevels = d.MipLevels;
            md3dDevice->CreateShaderResourceView(mDiffuseTexture.Get(), &srvDesc, hDescriptor);
        }
        else
        {
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.Texture2D.MipLevels = 1;
            md3dDevice->CreateShaderResourceView(mWhiteTexture.Get(), &srvDesc, hDescriptor);
        }
        hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    }

    // Normal SRV (or white)
    {
        if (mNormalTexture)
        {
            D3D12_RESOURCE_DESC n = mNormalTexture->GetDesc();
            srvDesc.Format = n.Format;
            srvDesc.Texture2D.MipLevels = n.MipLevels;
            md3dDevice->CreateShaderResourceView(mNormalTexture.Get(), &srvDesc, hDescriptor);
        }
        else
        {
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.Texture2D.MipLevels = 1;
            md3dDevice->CreateShaderResourceView(mWhiteTexture.Get(), &srvDesc, hDescriptor);
        }
    }
}

void TerrainApp::BuildShadersAndInputLayout()
{
    mShaders["terrainVS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["terrainPS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["terrainWirePS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS_Wireframe", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void TerrainApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { mShaders["terrainVS"]->GetBufferPointer(), mShaders["terrainVS"]->GetBufferSize() };
    psoDesc.PS = { mShaders["terrainPS"]->GetBufferPointer(), mShaders["terrainPS"]->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.DSVFormat = mDepthStencilFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["terrain"])));

    auto wireDesc = psoDesc;
    wireDesc.PS = { mShaders["terrainWirePS"]->GetBufferPointer(), mShaders["terrainWirePS"]->GetBufferSize() };
    wireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&wireDesc, IID_PPV_ARGS(&mPSOs["terrain_wireframe"])));
}

void TerrainApp::BuildFrameResources()
{
    // Keep same idea: enough object slots for possible visible nodes
    const UINT maxObjects = 256;

    for (int i = 0; i < gNumFrameResources; ++i)
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, maxObjects, 1));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 2> TerrainApp::GetStaticSamplers()
{
    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    return { linearWrap, linearClamp };
}
