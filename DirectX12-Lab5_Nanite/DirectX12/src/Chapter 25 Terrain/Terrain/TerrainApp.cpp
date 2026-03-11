//***************************************************************************************
// TerrainApp.cpp - Terrain rendering with LOD and Frustum Culling
// 
// Features:
// - Single terrain mesh with multiple LOD levels
// - Distance-based LOD selection
// - Frustum culling for the entire terrain
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "../../Common/DDSTextureLoader.h"
#include "FrameResource.h"
#include "Terrain.h"
#include "QuadTree.h"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdio>

// Console for debug output
void CreateConsoleWindow()
{
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    std::cout.clear();
    std::cerr.clear();
    SetConsoleTitleA("Terrain Debug Console");
    std::cout << "=== Terrain Demo - Debug Console ===" << std::endl;
    std::cout << "LOD + Frustum Culling + Terrain Painting enabled" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  WASD - move camera, QE - up/down" << std::endl;
    std::cout << "  Mouse - look around (when not painting)" << std::endl;
    std::cout << "  LMB - paint on terrain" << std::endl;
    std::cout << "  R/G/B - change paint color" << std::endl;
    std::cout << "  +/- - change brush size" << std::endl;
    std::cout << "  1 - toggle wireframe" << std::endl;
    std::cout << "=========================================\n" << std::endl;
}

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Bounding box for frustum culling
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

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateTerrainCB(const GameTimer& gt);

    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildTerrainGeometry();
    void BuildPSOs();
    void BuildFrameResources();
    void DrawTerrain();
    
    // LOD and Culling
    int CalculateLOD(float distance);
    bool IsInFrustum(const TerrainBoundingBox& box, const XMFLOAT4* planes);
    void ExtractFrustumPlanes(XMFLOAT4* planes, const XMMATRIX& viewProj);
    void PrintDebugInfo();

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 2> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // Terrain
    std::unique_ptr<Terrain> mTerrain;
    TerrainBoundingBox mTerrainBounds;
    
    // QuadTree for LOD management
    std::unique_ptr<QuadTree> mQuadTree;
    std::vector<TerrainNode*> mVisibleNodes;
    
    // Textures
    ComPtr<ID3D12Resource> mHeightmapTexture;
    ComPtr<ID3D12Resource> mHeightmapUploadBuffer;
    ComPtr<ID3D12Resource> mDiffuseTexture;
    ComPtr<ID3D12Resource> mDiffuseUploadBuffer;
    ComPtr<ID3D12Resource> mNormalTexture;
    ComPtr<ID3D12Resource> mNormalUploadBuffer;
    ComPtr<ID3D12Resource> mWhiteTexture;
    ComPtr<ID3D12Resource> mWhiteTextureUpload;
    
    // Paint texture for mouse drawing
    ComPtr<ID3D12Resource> mPaintTexture;
    ComPtr<ID3D12Resource> mPaintUploadBuffer;
    std::vector<UINT> mPaintData;

    PassConstants mMainPassCB;
    TerrainConstants mTerrainCB;
    Camera mCamera;
    
    // Frustum planes
    XMFLOAT4 mFrustumPlanes[6];
    
    // Current state
    int mCurrentLOD = 0;
    bool mTerrainVisible = true;
    bool mWireframe = false;
    
    // LOD distances for QuadTree
    std::vector<float> mLodDistances = { 100.0f, 200.0f, 400.0f, 600.0f, 1000.0f };
    
    // Statistics
    int mLodCounts[5] = { 0, 0, 0, 0, 0 };
    int mCulledNodes = 0;
    
    // Debug
    float mDebugTimer = 0.0f;
    
    // Mouse painting
    bool mIsPainting = false;
    bool mPaintTextureNeedsUpdate = false;
    float mBrushSize = 30.0f; // Brush size in world units
    XMFLOAT3 mPaintColor = { 1.0f, 0.0f, 0.0f }; // Red paint
    
    POINT mLastMousePos;
    
    // Ray casting for terrain painting
    bool RayTerrainIntersect(int mouseX, int mouseY, XMFLOAT3& hitPoint);
    void PaintOnTerrain(const XMFLOAT3& worldPos);
    void UpdatePaintTexture();
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // Create console window for debug output
    CreateConsoleWindow();

    try
    {
        TerrainApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;
        return theApp.Run();
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

    mCamera.SetPosition(0.0f, 200.0f, -400.0f);
    mCamera.LookAt(mCamera.GetPosition3f(), XMFLOAT3(0, 50, 0), XMFLOAT3(0, 1, 0));

    // Create terrain
    mTerrain = std::make_unique<Terrain>(md3dDevice.Get(), mCommandList.Get(), 
                                          512.0f, 0.0f, 150.0f);
    
    if (!mTerrain->LoadHeightmapDDS(L"TerrainDetails/003/Height_Out.dds", md3dDevice.Get(), mCommandList.Get()))
    {
        mTerrain->GenerateProceduralHeightmap(256, 256, 4.0f, 6);
    }
    mTerrain->BuildGeometry(md3dDevice.Get(), mCommandList.Get());
    
    // Setup terrain bounding box for frustum culling
    float halfSize = mTerrain->GetTerrainSize() * 0.5f;
    float halfHeight = (mTerrain->GetMaxHeight() - mTerrain->GetMinHeight()) * 0.5f;
    mTerrainBounds.Center = XMFLOAT3(0.0f, mTerrain->GetMinHeight() + halfHeight, 0.0f);
    mTerrainBounds.Extents = XMFLOAT3(halfSize, halfHeight + 10.0f, halfSize);
    
    // Initialize QuadTree for LOD management
    mQuadTree = std::make_unique<QuadTree>();
    float minNodeSize = mTerrain->GetTerrainSize() / 8.0f; // Minimum node = 1/8 of terrain
    mQuadTree->SetLODDistances(mLodDistances);
    mQuadTree->Initialize(mTerrain->GetTerrainSize(), minNodeSize, 5);
    mQuadTree->SetHeightRange(0, 0, mTerrain->GetTerrainSize(), 
                              mTerrain->GetMinHeight(), mTerrain->GetMaxHeight());
    
    std::cout << "QuadTree initialized:" << std::endl;
    std::cout << "  Terrain size: " << mTerrain->GetTerrainSize() << std::endl;
    std::cout << "  Min node size: " << minNodeSize << std::endl;
    std::cout << "  Total nodes: " << mQuadTree->GetTotalNodeCount() << std::endl;
    std::cout << std::endl;
    
    OutputDebugStringA("=== Terrain Demo ===\n");
    OutputDebugStringA("QuadTree LOD + Frustum Culling\n");
    OutputDebugStringA("Controls: WASD-move, QE-up/down, Mouse-look, 1-wireframe\n\n");

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
    OnKeyboardInput(gt);
    UpdateCamera(gt);

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // Extract frustum planes
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    ExtractFrustumPlanes(mFrustumPlanes, viewProj);
    
    // Update QuadTree with camera position and frustum
    XMFLOAT3 camPos = mCamera.GetPosition3f();
    mQuadTree->Update(camPos, mFrustumPlanes);
    
    // Get visible nodes from QuadTree
    mVisibleNodes.clear();
    mQuadTree->GetVisibleNodes(mVisibleNodes);
    
    // Count LOD distribution and culled nodes
    memset(mLodCounts, 0, sizeof(mLodCounts));
    for (const auto* node : mVisibleNodes)
    {
        int lod = (node->LODLevel < 4) ? node->LODLevel : 4;
        mLodCounts[lod]++;
    }
    mCulledNodes = mQuadTree->GetTotalNodeCount() - (int)mVisibleNodes.size();
    
    // Overall terrain visibility
    mTerrainVisible = !mVisibleNodes.empty();

    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
    UpdateTerrainCB(gt);
    
    // Debug output every 0.5 seconds
    mDebugTimer += gt.DeltaTime();
    if (mDebugTimer >= 0.5f)
    {
        PrintDebugInfo();
        mDebugTimer = 0.0f;
    }
}

int TerrainApp::CalculateLOD(float distance)
{
    // Now handled by QuadTree, kept for compatibility
    for (int i = 0; i < (int)mLodDistances.size(); ++i)
    {
        if (distance < mLodDistances[i])
            return i;
    }
    return 4;
}

bool TerrainApp::IsInFrustum(const TerrainBoundingBox& box, const XMFLOAT4* planes)
{
    for (int i = 0; i < 6; ++i)
    {
        XMFLOAT3 positiveVertex;
        positiveVertex.x = (planes[i].x >= 0) ? (box.Center.x + box.Extents.x) : (box.Center.x - box.Extents.x);
        positiveVertex.y = (planes[i].y >= 0) ? (box.Center.y + box.Extents.y) : (box.Center.y - box.Extents.y);
        positiveVertex.z = (planes[i].z >= 0) ? (box.Center.z + box.Extents.z) : (box.Center.z - box.Extents.z);
        
        float dist = planes[i].x * positiveVertex.x +
                     planes[i].y * positiveVertex.y +
                     planes[i].z * positiveVertex.z +
                     planes[i].w;
        
        if (dist < 0)
            return false;
    }
    return true;
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
    planes[4] = { M._13, M._23, M._33, M._43 };
    planes[5] = { M._14 - M._13, M._24 - M._23, M._34 - M._33, M._44 - M._43 };

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = XMLoadFloat4(&planes[i]);
        p = XMPlaneNormalize(p);
        XMStoreFloat4(&planes[i], p);
    }
}

void TerrainApp::PrintDebugInfo()
{
    // Output to console window
    std::cout << "========== QuadTree Terrain Status ==========" << std::endl;
    std::cout << "Camera: (" << std::fixed << std::setprecision(1) 
              << mCamera.GetPosition3f().x << ", " 
              << mCamera.GetPosition3f().y << ", " 
              << mCamera.GetPosition3f().z << ")" << std::endl;
    std::cout << std::endl;
    
    std::cout << "--- Frustum Culling ---" << std::endl;
    std::cout << "Total nodes: " << mQuadTree->GetTotalNodeCount() << std::endl;
    std::cout << "Visible nodes: " << mVisibleNodes.size() << std::endl;
    std::cout << "Culled nodes: " << mCulledNodes << std::endl;
    std::cout << std::endl;
    
    std::cout << "--- LOD Distribution ---" << std::endl;
    std::cout << "LOD 0 (highest): " << mLodCounts[0] << " nodes" << std::endl;
    std::cout << "LOD 1: " << mLodCounts[1] << " nodes" << std::endl;
    std::cout << "LOD 2: " << mLodCounts[2] << " nodes" << std::endl;
    std::cout << "LOD 3: " << mLodCounts[3] << " nodes" << std::endl;
    std::cout << "LOD 4 (lowest): " << mLodCounts[4] << " nodes" << std::endl;
    std::cout << std::endl;
    
    std::cout << "--- Terrain Painting ---" << std::endl;
    std::cout << "Paint mode: " << (mIsPainting ? "ACTIVE" : "inactive") << std::endl;
    std::cout << "Brush size: " << std::fixed << std::setprecision(1) << mBrushSize << std::endl;
    std::cout << "Paint color: RGB(" << mPaintColor.x << ", " << mPaintColor.y << ", " << mPaintColor.z << ")" << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << std::endl;
}

void TerrainApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    if (mWireframe)
    {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["terrain_wireframe"].Get()));
    }
    else
    {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["terrain"].Get()));
    }

    // Update paint texture if needed
    if (mPaintTextureNeedsUpdate)
    {
        // Transition to copy destination
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mPaintTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST));
        
        // Update subresource
        D3D12_SUBRESOURCE_DATA paintData = {};
        paintData.pData = mPaintData.data();
        paintData.RowPitch = 512 * 4; // 4 bytes per pixel
        paintData.SlicePitch = paintData.RowPitch * 512;
        
        UpdateSubresources(mCommandList.Get(), mPaintTexture.Get(), mPaintUploadBuffer.Get(), 0, 0, 1, &paintData);
        
        // Transition back to shader resource
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mPaintTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        
        mPaintTextureNeedsUpdate = false;
    }

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), 
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto terrainCB = mCurrFrameResource->TerrainCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, terrainCB->GetGPUVirtualAddress());

    CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootDescriptorTable(3, texHandle);

    // Only draw if terrain passes frustum culling
    if (mTerrainVisible)
    {
        DrawTerrain();
    }

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TerrainApp::DrawTerrain()
{
    auto geo = mTerrain->GetGeometry();
    
    mCommandList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
    mCommandList->IASetIndexBuffer(&geo->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    // Draw each visible QuadTree node with its LOD level
    for (size_t i = 0; i < mVisibleNodes.size(); ++i)
    {
        const TerrainNode* node = mVisibleNodes[i];
        
        // Set constant buffer for this node
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + i * objCBByteSize;
        mCommandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        // Select mesh based on node's LOD level
        int lod = (node->LODLevel < 4) ? node->LODLevel : 4;
        const char* lodMesh = Terrain::GetLODMeshName(lod);
        auto& submesh = geo->DrawArgs[lodMesh];

        mCommandList->DrawIndexedInstanced(submesh.IndexCount, 1, 
            submesh.StartIndexLocation, submesh.BaseVertexLocation, 0);
    }
}


void TerrainApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    
    if ((btnState & MK_LBUTTON) != 0)
    {
        mIsPainting = true;
        XMFLOAT3 hitPoint;
        if (RayTerrainIntersect(x, y, hitPoint))
        {
            PaintOnTerrain(hitPoint);
            std::cout << "Painting at: (" << hitPoint.x << ", " << hitPoint.z << ")" << std::endl;
        }
    }
    
    SetCapture(mhMainWnd);
}

void TerrainApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    if (mIsPainting)
    {
        mIsPainting = false;
        std::cout << "Stopped painting" << std::endl;
    }
    ReleaseCapture();
}

void TerrainApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    // ЛКМ - рисование на террейне
    if ((btnState & MK_LBUTTON) != 0 && mIsPainting)
    {
        XMFLOAT3 hitPoint;
        if (RayTerrainIntersect(x, y, hitPoint))
        {
            PaintOnTerrain(hitPoint);
        }
    }
    
    // ПКМ - вращение камеры
    if ((btnState & MK_RBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }
    
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void TerrainApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();
    float speed = 100.0f;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        speed *= 3.0f;

    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(speed * dt);
    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-speed * dt);
    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-speed * dt);
    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(speed * dt);
    if (GetAsyncKeyState('Q') & 0x8000)
        mCamera.SetPosition(mCamera.GetPosition3f().x, mCamera.GetPosition3f().y + speed * dt, mCamera.GetPosition3f().z);
    if (GetAsyncKeyState('E') & 0x8000)
        mCamera.SetPosition(mCamera.GetPosition3f().x, mCamera.GetPosition3f().y - speed * dt, mCamera.GetPosition3f().z);

    static bool wKeyPressed = false;
    if (GetAsyncKeyState('1') & 0x8000)
    {
        if (!wKeyPressed) 
        { 
            mWireframe = !mWireframe; 
            wKeyPressed = true;
            OutputDebugStringA(mWireframe ? "Wireframe: ON\n" : "Wireframe: OFF\n");
        }
    }
    else { wKeyPressed = false; }
    
    // Brush size controls
    if (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) // + key
        mBrushSize = min(mBrushSize + 50.0f * dt, 100.0f);
    if (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) // - key
        mBrushSize = max(mBrushSize - 50.0f * dt, 5.0f);
    
    // Color selection
    static bool rKeyPressed = false, gKeyPressed = false, bKeyPressed = false;
    if (GetAsyncKeyState('R') & 0x8000)
    {
        if (!rKeyPressed) { mPaintColor = { 1.0f, 0.0f, 0.0f }; rKeyPressed = true; std::cout << "Paint color: RED" << std::endl; }
    }
    else { rKeyPressed = false; }
    
    if (GetAsyncKeyState('G') & 0x8000)
    {
        if (!gKeyPressed) { mPaintColor = { 0.0f, 1.0f, 0.0f }; gKeyPressed = true; std::cout << "Paint color: GREEN" << std::endl; }
    }
    else { gKeyPressed = false; }
    
    if (GetAsyncKeyState('B') & 0x8000)
    {
        if (!bKeyPressed) { mPaintColor = { 0.0f, 0.0f, 1.0f }; bKeyPressed = true; std::cout << "Paint color: BLUE" << std::endl; }
    }
    else { bKeyPressed = false; }
}

void TerrainApp::UpdateCamera(const GameTimer& gt)
{
    mCamera.UpdateViewMatrix();
}

void TerrainApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    float terrainSize = mTerrain->GetTerrainSize();
    
    // Update constant buffer for each visible QuadTree node
    for (size_t i = 0; i < mVisibleNodes.size(); ++i)
    {
        const TerrainNode* node = mVisibleNodes[i];
        
        // Calculate world transform for this node
        // Node position is in world space, node size determines scale
        // UV offset/scale for texture sampling
        float nodeScale = node->Size;
        float uvScale = node->Size / terrainSize;
        float uvOffsetX = (node->X / terrainSize) + 0.5f - uvScale * 0.5f;
        float uvOffsetZ = (node->Z / terrainSize) + 0.5f - uvScale * 0.5f;
        
        // World matrix: scale by node size, translate to node position
        XMMATRIX world = XMMatrixScaling(nodeScale, 1.0f, nodeScale) *
                         XMMatrixTranslation(node->X, 0.0f, node->Z);
        
        // Texture transform: scale and offset UV to sample correct portion
        XMMATRIX texTransform = XMMatrixScaling(uvScale, uvScale, 1.0f) *
                                XMMatrixTranslation(uvOffsetX, uvOffsetZ, 0.0f);

        ObjectConstants objConstants;
        XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
        objConstants.MaterialIndex = 0;
        objConstants.LODLevel = (node->LODLevel < 4) ? node->LODLevel : 4;

        currObjectCB->CopyData((int)i, objConstants);
    }
}

void TerrainApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
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
    mMainPassCB.FarZ = 3000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.3f, 0.3f, 0.35f, 1.0f };

    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.9f, 0.85f, 0.8f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void TerrainApp::UpdateTerrainCB(const GameTimer& gt)
{
    mTerrainCB.MinHeight = mTerrain->GetMinHeight();
    mTerrainCB.MaxHeight = mTerrain->GetMaxHeight();
    mTerrainCB.TerrainSize = mTerrain->GetTerrainSize();
    mTerrainCB.TexelSize = 1.0f / mTerrain->GetHeightmapWidth();
    mTerrainCB.HeightMapSize = XMFLOAT2((float)mTerrain->GetHeightmapWidth(), 
                                         (float)mTerrain->GetHeightmapHeight());

    auto currTerrainCB = mCurrFrameResource->TerrainCB.get();
    currTerrainCB->CopyData(0, mTerrainCB);
}

void TerrainApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0); // 4 textures now

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsConstantBufferView(2);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TerrainApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 4; // Added paint texture
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));
    
    // Initialize paint texture (512x512 RGBA)
    UINT paintWidth = 512;
    UINT paintHeight = 512;
    mPaintData.resize(paintWidth * paintHeight, 0x00000000); // Transparent black
    
    D3D12_RESOURCE_DESC paintTexDesc = {};
    paintTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    paintTexDesc.Width = paintWidth;
    paintTexDesc.Height = paintHeight;
    paintTexDesc.DepthOrArraySize = 1;
    paintTexDesc.MipLevels = 1;
    paintTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    paintTexDesc.SampleDesc.Count = 1;
    paintTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &paintTexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mPaintTexture)));

    const UINT64 paintUploadSize = GetRequiredIntermediateSize(mPaintTexture.Get(), 0, 1);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(paintUploadSize), D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mPaintUploadBuffer)));

    D3D12_SUBRESOURCE_DATA paintData = {};
    paintData.pData = mPaintData.data();
    paintData.RowPitch = paintWidth * 4; // 4 bytes per pixel (RGBA)
    paintData.SlicePitch = paintData.RowPitch * paintHeight;

    UpdateSubresources(mCommandList.Get(), mPaintTexture.Get(), mPaintUploadBuffer.Get(), 0, 0, 1, &paintData);
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        mPaintTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Load heightmap
    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Height_Out.dds",
        mHeightmapTexture, mHeightmapUploadBuffer);
    
    if (FAILED(hr))
    {
        // Fallback procedural heightmap
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
                heightData[z * width + x] = (h - mTerrain->GetMinHeight()) / (mTerrain->GetMaxHeight() - mTerrain->GetMinHeight());
            }
        }
        
        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = heightData.data();
        subData.RowPitch = width * sizeof(float);
        subData.SlicePitch = subData.RowPitch * height;
        
        UpdateSubresources(mCommandList.Get(), mHeightmapTexture.Get(), mHeightmapUploadBuffer.Get(), 0, 0, 1, &subData);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            mHeightmapTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }
    
    // Load diffuse
    hr = DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Weathering_Out.dds",
        mDiffuseTexture, mDiffuseUploadBuffer);
    
    // Load normal
    hr = DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(), L"TerrainDetails/003/Normals_Out.dds",
        mNormalTexture, mNormalUploadBuffer);
    
    // White texture fallback
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

    // Create SRVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;

    // Heightmap
    D3D12_RESOURCE_DESC hmDesc = mHeightmapTexture->GetDesc();
    srvDesc.Format = hmDesc.Format;
    srvDesc.Texture2D.MipLevels = hmDesc.MipLevels;
    md3dDevice->CreateShaderResourceView(mHeightmapTexture.Get(), &srvDesc, hDescriptor);
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

    // Diffuse
    if (mDiffuseTexture)
    {
        D3D12_RESOURCE_DESC diffDesc = mDiffuseTexture->GetDesc();
        srvDesc.Format = diffDesc.Format;
        srvDesc.Texture2D.MipLevels = diffDesc.MipLevels;
        md3dDevice->CreateShaderResourceView(mDiffuseTexture.Get(), &srvDesc, hDescriptor);
    }
    else
    {
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Texture2D.MipLevels = 1;
        md3dDevice->CreateShaderResourceView(mWhiteTexture.Get(), &srvDesc, hDescriptor);
    }
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

    // Normal
    if (mNormalTexture)
    {
        D3D12_RESOURCE_DESC normDesc = mNormalTexture->GetDesc();
        srvDesc.Format = normDesc.Format;
        srvDesc.Texture2D.MipLevels = normDesc.MipLevels;
        md3dDevice->CreateShaderResourceView(mNormalTexture.Get(), &srvDesc, hDescriptor);
    }
    else
    {
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Texture2D.MipLevels = 1;
        md3dDevice->CreateShaderResourceView(mWhiteTexture.Get(), &srvDesc, hDescriptor);
    }
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);

    // Paint texture
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.Texture2D.MipLevels = 1;
    md3dDevice->CreateShaderResourceView(mPaintTexture.Get(), &srvDesc, hDescriptor);
}

void TerrainApp::BuildShadersAndInputLayout()
{
    mShaders["terrainVS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["terrainPS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["terrainWirePS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS_Wireframe", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC wirePsoDesc = psoDesc;
    wirePsoDesc.PS = { mShaders["terrainWirePS"]->GetBufferPointer(), mShaders["terrainWirePS"]->GetBufferSize() };
    wirePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&wirePsoDesc, IID_PPV_ARGS(&mPSOs["terrain_wireframe"])));
}

void TerrainApp::BuildFrameResources()
{
    // Allocate enough object CBs for all possible QuadTree nodes
    // QuadTree can have up to ~100 visible nodes at once
    const UINT maxObjects = 256;
    
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, maxObjects, 1));
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 2> TerrainApp::GetStaticSamplers()
{
    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    return { linearWrap, linearClamp };
}

// Ray-terrain intersection for mouse picking
bool TerrainApp::RayTerrainIntersect(int mouseX, int mouseY, XMFLOAT3& hitPoint)
{
    // Convert mouse coordinates to normalized device coordinates [-1, 1]
    float ndcX = (2.0f * mouseX) / mClientWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / mClientHeight;

    // Get view and projection matrices
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    
    // Compute inverse of view-projection
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);
    
    // Unproject near and far points
    XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
    XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
    
    // Ray direction
    XMVECTOR rayDir = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));

    // Ray-terrain intersection using heightmap
    XMFLOAT3 rayStart, rayDirection;
    XMStoreFloat3(&rayStart, nearPoint);
    XMStoreFloat3(&rayDirection, rayDir);

    // Step along ray and test height (binary search refinement)
    float terrainSize = mTerrain->GetTerrainSize();
    float halfSize = terrainSize * 0.5f;
    
    // Coarse search
    float lastT = 0.0f;
    for (float t = 1.0f; t < 3000.0f; t += 1.0f)
    {
        XMFLOAT3 testPoint = {
            rayStart.x + rayDirection.x * t,
            rayStart.y + rayDirection.y * t,
            rayStart.z + rayDirection.z * t
        };
        
        // Check if point is within terrain bounds
        if (testPoint.x >= -halfSize && testPoint.x <= halfSize &&
            testPoint.z >= -halfSize && testPoint.z <= halfSize)
        {
            float terrainHeight = mTerrain->GetHeight(testPoint.x, testPoint.z);
            
            if (testPoint.y <= terrainHeight)
            {
                // Binary search for precise intersection
                float lo = lastT, hi = t;
                for (int i = 0; i < 16; ++i)
                {
                    float mid = (lo + hi) * 0.5f;
                    XMFLOAT3 midPoint = {
                        rayStart.x + rayDirection.x * mid,
                        rayStart.y + rayDirection.y * mid,
                        rayStart.z + rayDirection.z * mid
                    };
                    float midHeight = mTerrain->GetHeight(midPoint.x, midPoint.z);
                    if (midPoint.y <= midHeight)
                        hi = mid;
                    else
                        lo = mid;
                }
                
                float finalT = (lo + hi) * 0.5f;
                hitPoint = {
                    rayStart.x + rayDirection.x * finalT,
                    mTerrain->GetHeight(rayStart.x + rayDirection.x * finalT, rayStart.z + rayDirection.z * finalT),
                    rayStart.z + rayDirection.z * finalT
                };
                return true;
            }
        }
        lastT = t;
    }
    
    return false;
}

// Paint on terrain at world position
void TerrainApp::PaintOnTerrain(const XMFLOAT3& worldPos)
{
    float terrainSize = mTerrain->GetTerrainSize();
    float halfSize = terrainSize * 0.5f;
    
    // Convert world position to texture coordinates [0, 1]
    float u = (worldPos.x + halfSize) / terrainSize;
    float v = (worldPos.z + halfSize) / terrainSize;
    
    // Clamp to valid range
    u = max(0.0f, min(1.0f, u));
    v = max(0.0f, min(1.0f, v));
    
    // Convert to paint texture coordinates
    int paintWidth = 512;
    int paintHeight = 512;
    int centerX = (int)(u * (paintWidth - 1));
    int centerY = (int)(v * (paintHeight - 1));
    
    // Paint with brush - ensure minimum radius of 2 pixels
    int brushRadius = max(2, (int)(mBrushSize * paintWidth / terrainSize));
    
    for (int y = centerY - brushRadius; y <= centerY + brushRadius; ++y)
    {
        for (int x = centerX - brushRadius; x <= centerX + brushRadius; ++x)
        {
            if (x >= 0 && x < paintWidth && y >= 0 && y < paintHeight)
            {
                // Calculate distance from center
                float dx = (float)(x - centerX);
                float dy = (float)(y - centerY);
                float distance = sqrtf(dx * dx + dy * dy);
                
                if (distance <= (float)brushRadius)
                {
                    // Soft brush falloff
                    float alpha = 1.0f - (distance / (float)brushRadius);
                    alpha = alpha * alpha; // Quadratic falloff
                    
                    int index = y * paintWidth + x;
                    UINT& pixel = mPaintData[index];
                    
                    // Extract current color
                    float currentR = ((pixel >> 0) & 0xFF) / 255.0f;
                    float currentG = ((pixel >> 8) & 0xFF) / 255.0f;
                    float currentB = ((pixel >> 16) & 0xFF) / 255.0f;
                    float currentA = ((pixel >> 24) & 0xFF) / 255.0f;
                    
                    // Blend with paint color (stronger paint)
                    float blendAlpha = alpha * 0.5f; // Paint strength
                    float newR = currentR * (1.0f - blendAlpha) + mPaintColor.x * blendAlpha;
                    float newG = currentG * (1.0f - blendAlpha) + mPaintColor.y * blendAlpha;
                    float newB = currentB * (1.0f - blendAlpha) + mPaintColor.z * blendAlpha;
                    float newA = min(currentA + blendAlpha, 1.0f);
                    
                    // Pack back to UINT
                    UINT r = (UINT)(newR * 255.0f);
                    UINT g = (UINT)(newG * 255.0f);
                    UINT b = (UINT)(newB * 255.0f);
                    UINT a = (UINT)(newA * 255.0f);
                    
                    pixel = (a << 24) | (b << 16) | (g << 8) | r;
                }
            }
        }
    }
    
    // Mark for update
    UpdatePaintTexture();
}

// Update paint texture on GPU
void TerrainApp::UpdatePaintTexture()
{
    mPaintTextureNeedsUpdate = true;
}