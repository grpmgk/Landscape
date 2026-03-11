//***************************************************************************************
// NaniteLikeApp.cpp - Main application demonstrating Nanite-like rendering
// Uses DirectXMesh for meshlet generation
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/Camera.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Meshlet.h"
#include "MeshletBuilder.h"
#include "NaniteRenderer.h"
#include <cstdio>
#include <io.h>
#include <fcntl.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

const int gNumFrameResources = 3;

// Console helper for debug output
void CreateConsoleWindow()
{
    AllocConsole();
    
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    
    // Set console title
    SetConsoleTitleA("Nanite-Like Renderer - Debug Console");
    
    // Set console size
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SMALL_RECT windowSize = {0, 0, 100, 30};
    SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
    
    // Enable colors
    DWORD mode;
    GetConsoleMode(hConsole, &mode);
    SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    
    printf("\033[36m"); // Cyan color
    printf("=============================================================\n");
    printf("       NANITE-LIKE RENDERER - DEBUG CONSOLE\n");
    printf("=============================================================\n");
    printf("\033[0m"); // Reset color
}

class NaniteLikeApp : public D3DApp
{
public:
    NaniteLikeApp(HINSTANCE hInstance);
    NaniteLikeApp(const NaniteLikeApp& rhs) = delete;
    NaniteLikeApp& operator=(const NaniteLikeApp& rhs) = delete;
    ~NaniteLikeApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdatePassCB(const GameTimer& gt);
    void BuildFrameResources();
    void BuildMeshletMeshes();
    void BuildInstances();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    std::unique_ptr<NaniteRenderer> mNaniteRenderer;

    std::vector<MeshletMesh> mMeshletMeshes;
    std::vector<MeshInstance> mInstances;

    Camera mCamera;
    POINT mLastMousePos;
    
    // Stats tracking
    float mStatsUpdateTimer = 0.0f;
    void PrintStats(const GameTimer& gt);
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
        NaniteLikeApp theApp(hInstance);
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

NaniteLikeApp::NaniteLikeApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"Nanite-Like Mesh Shader Demo (DirectXMesh)";
}

NaniteLikeApp::~NaniteLikeApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool NaniteLikeApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Position camera for viewing the statuette (tall model)
    mCamera.SetPosition(0.0f, 100.0f, -300.0f);
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 0.1f, 10000.0f);

    mNaniteRenderer = std::make_unique<NaniteRenderer>(
        md3dDevice.Get(), mBackBufferFormat, mDepthStencilFormat);
    mNaniteRenderer->Initialize(mCommandList.Get(), mClientWidth, mClientHeight);

    BuildMeshletMeshes();
    BuildInstances();
    BuildFrameResources();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();

    return true;
}

void NaniteLikeApp::OnResize()
{
    D3DApp::OnResize();
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 0.1f, 10000.0f);
    if (mNaniteRenderer)
        mNaniteRenderer->OnResize(mClientWidth, mClientHeight);
}

void NaniteLikeApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdatePassCB(gt);
}

void NaniteLikeApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    D3D12_RESOURCE_BARRIER barrierToRT = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrierToRT);

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::DarkSlateGray, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), 
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    mNaniteRenderer->Render(mCommandList.Get(), mCamera, CurrentBackBufferView(), DepthStencilView(), gt.TotalTime());

    D3D12_RESOURCE_BARRIER barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrierToPresent);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
    
    // Print stats to console periodically
    PrintStats(gt);
}

void NaniteLikeApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();
    float speed = 50.0f;
    
    // Shift for faster movement
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        speed *= 3.0f;

    if (GetAsyncKeyState('W') & 0x8000) mCamera.Walk(speed * dt);
    if (GetAsyncKeyState('S') & 0x8000) mCamera.Walk(-speed * dt);
    if (GetAsyncKeyState('A') & 0x8000) mCamera.Strafe(-speed * dt);
    if (GetAsyncKeyState('D') & 0x8000) mCamera.Strafe(speed * dt);
    if (GetAsyncKeyState('Q') & 0x8000) mCamera.SetPosition(
        mCamera.GetPosition3f().x, mCamera.GetPosition3f().y - speed * dt, mCamera.GetPosition3f().z);
    if (GetAsyncKeyState('E') & 0x8000) mCamera.SetPosition(
        mCamera.GetPosition3f().x, mCamera.GetPosition3f().y + speed * dt, mCamera.GetPosition3f().z);

    // Toggle meshlet visualization with M key
    static bool mKeyWasPressed = false;
    if (GetAsyncKeyState('M') & 0x8000)
    {
        if (!mKeyWasPressed)
        {
            mNaniteRenderer->ToggleMeshletVisualization();
            mKeyWasPressed = true;
        }
    }
    else
    {
        mKeyWasPressed = false;
    }
    
    // Toggle texture with T key
    static bool tKeyWasPressed = false;
    if (GetAsyncKeyState('T') & 0x8000)
    {
        if (!tKeyWasPressed)
        {
            mNaniteRenderer->ToggleTexture();
            tKeyWasPressed = true;
        }
    }
    else
    {
        tKeyWasPressed = false;
    }

    mCamera.UpdateViewMatrix();
}

void NaniteLikeApp::UpdatePassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    PassConstants passConstants;
    XMStoreFloat4x4(&passConstants.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&passConstants.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&passConstants.InvView, XMMatrixTranspose(XMMatrixInverse(nullptr, view)));
    XMStoreFloat4x4(&passConstants.InvProj, XMMatrixTranspose(XMMatrixInverse(nullptr, proj)));
    XMStoreFloat4x4(&passConstants.InvViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, viewProj)));
    passConstants.EyePosW = mCamera.GetPosition3f();
    passConstants.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    passConstants.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    passConstants.NearZ = 0.1f;
    passConstants.FarZ = 1000.0f;
    passConstants.TotalTime = gt.TotalTime();
    passConstants.DeltaTime = gt.DeltaTime();

    mCurrFrameResource->PassCB->CopyData(0, passConstants);
}

void NaniteLikeApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 1, (UINT)mInstances.size() + 1, 10));
    }
}

void NaniteLikeApp::BuildMeshletMeshes()
{
    MeshletMesh mesh;
    
    printf("\n\033[33m[LOADING]\033[0m Loading OBJ file...\n");
    SetWindowText(mhMainWnd, L"Loading OBJ file... Please wait");
    
    bool loaded = MeshletBuilder::LoadOBJ(
        L"OBJ/StGilesLychGate01/StGilesLychGate02.obj", mesh);
    
    if (!loaded)
    {
        printf("\033[31m[WARNING]\033[0m OBJ not found, using generated sphere\n");
        GeometryGenerator geoGen;
        auto sphereData = geoGen.CreateGeosphere(30.0f, 5);
        mesh.Name = "Sphere";
        MeshletBuilder::BuildFromGeometry(sphereData, mesh);
    }
    
    printf("\033[32m[SUCCESS]\033[0m Mesh loaded!\n");
    printf("  - Original vertices: %zu\n", mesh.Positions.size());
    printf("  - Triangles: %zu\n", mesh.Indices.size() / 3);
    
    printf("\n\033[33m[PROCESSING]\033[0m Building LOD hierarchy...\n");
    MeshletBuilder::BuildLODHierarchy(mesh);
    printf("\033[32m[SUCCESS]\033[0m LOD hierarchy built!\n");
    printf("  - LOD levels: %u\n", mesh.LODCount);
    printf("  - Cluster nodes: %zu\n", mesh.ClusterNodes.size());
    
    mMeshletMeshes.push_back(std::move(mesh));

    // Upload to GPU
    printf("\n\033[33m[UPLOADING]\033[0m Uploading mesh to GPU...\n");
    if (!mMeshletMeshes.empty())
    {
        mNaniteRenderer->UploadMesh(mCommandList.Get(), mMeshletMeshes[0], 0);
    }
    printf("\033[32m[SUCCESS]\033[0m Mesh uploaded to GPU!\n");
    printf("  - Meshlets: %u\n", mNaniteRenderer->GetMeshletCount());
    printf("  - GPU vertices: %u\n", mNaniteRenderer->GetVertexCount());
    printf("  - GPU triangles: %u\n", mNaniteRenderer->GetTriangleCount());
    
    // Try to load texture
    printf("\n\033[33m[TEXTURE]\033[0m Looking for texture...\n");
    if (mNaniteRenderer->LoadTexture(mCommandList.Get(), L"OBJ/StGilesLychGate01/wssop-5brlm.dds"))
    {
        printf("\033[32m[SUCCESS]\033[0m Texture loaded and applied!\n");
    }
    else
    {
        printf("\033[33m[INFO]\033[0m No texture found.\n");
    }

    SetWindowText(mhMainWnd, L"Nanite-Like Mesh Shader Demo");
    
    printf("\n\033[36m[READY]\033[0m Rendering started!\n\n");
}

void NaniteLikeApp::BuildInstances()
{
    // Single instance at origin
    MeshInstance inst;
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX invTranspose = XMMatrixIdentity();

    // Store without transpose - HLSL will use row_major
    XMStoreFloat4x4(&inst.World, world);
    XMStoreFloat4x4(&inst.InvTransposeWorld, invTranspose);
    inst.MeshIndex = 0;
    inst.MaterialIndex = 0;

    mInstances.push_back(inst);
    mNaniteRenderer->SetInstances(mCommandList.Get(), mInstances);
}

void NaniteLikeApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void NaniteLikeApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void NaniteLikeApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void NaniteLikeApp::PrintStats(const GameTimer& gt)
{
    mStatsUpdateTimer += gt.DeltaTime();
    
    // Update every 0.5 seconds
    if (mStatsUpdateTimer < 0.5f)
        return;
    mStatsUpdateTimer = 0.0f;
    
    // Move cursor to beginning and clear
    printf("\033[10;0H"); // Move to line 10
    printf("\033[J");     // Clear from cursor to end
    
    // Get stats from renderer
    const auto& stats = mNaniteRenderer->GetCullingStats();
    UINT meshletCount = mNaniteRenderer->GetMeshletCount();
    UINT vertexCount = mNaniteRenderer->GetVertexCount();
    UINT triangleCount = mNaniteRenderer->GetTriangleCount();
    
    // Camera info
    auto camPos = mCamera.GetPosition3f();
    auto camLook = mCamera.GetLook3f();
    
    // Colors
    printf("\033[33m"); // Yellow
    printf("-------------------- RENDER STATS --------------------\n");
    printf("\033[0m");
    
    printf("\033[32m"); // Green
    printf("FPS: %.1f  |  Frame Time: %.3f ms\n", 
        gt.DeltaTime() > 0 ? 1.0f / gt.DeltaTime() : 0.0f,
        gt.DeltaTime() * 1000.0f);
    printf("\033[0m");
    
    printf("\n\033[36m[GEOMETRY]\033[0m\n");
    printf("  Total Meshlets:   %u\n", meshletCount);
    printf("  Total Vertices:   %u\n", vertexCount);
    printf("  Total Triangles:  %u\n", triangleCount);
    
    printf("\n\033[36m[PIPELINE]\033[0m\n");
    if (mNaniteRenderer->IsMeshShaderEnabled())
    {
        printf("  \033[32mMesh Shader Pipeline (AS + MS + PS)\033[0m\n");
        printf("  GPU Frustum Culling: \033[32mENABLED\033[0m\n");
        printf("  GPU Cone Culling: \033[32mENABLED\033[0m\n");
    }
    else
    {
        printf("  \033[33mFallback Pipeline (VS + PS)\033[0m\n");
        printf("  GPU Culling: \033[31mDISABLED\033[0m\n");
    }
    
    printf("\n\033[36m[CULLING]\033[0m\n");
    printf("  Visible Meshlets: %u / %u (%.1f%%)\n", 
        stats.VisibleMeshlets, meshletCount,
        meshletCount > 0 ? (100.0f * stats.VisibleMeshlets / meshletCount) : 0.0f);
    printf("  Rendered Tris:    %u\n", stats.TotalTriangles);
    
    printf("\n\033[36m[CAMERA]\033[0m\n");
    printf("  Position: (%.1f, %.1f, %.1f)\n", camPos.x, camPos.y, camPos.z);
    printf("  Look Dir: (%.2f, %.2f, %.2f)\n", camLook.x, camLook.y, camLook.z);
    
    printf("\n\033[36m[MESH INFO]\033[0m\n");
    if (!mMeshletMeshes.empty())
    {
        const auto& mesh = mMeshletMeshes[0];
        printf("  Name: %s\n", mesh.Name.c_str());
        printf("  LOD Levels: %u\n", mesh.LODCount);
        printf("  Cluster Nodes: %zu\n", mesh.ClusterNodes.size());
        printf("  Bounding Sphere: center(%.1f, %.1f, %.1f) r=%.1f\n",
            mesh.BSphere.Center.x, mesh.BSphere.Center.y, mesh.BSphere.Center.z,
            mesh.BSphere.Radius);
    }
    
    printf("\n\033[36m[VISUALIZATION]\033[0m\n");
    if (mNaniteRenderer->IsUsingTexture())
        printf("  Mode: \033[32mTEXTURE\033[0m\n");
    else if (mNaniteRenderer->IsShowingMeshletColors())
        printf("  Mode: \033[36mMESHLET COLORS\033[0m\n");
    else
        printf("  Mode: \033[33mSOLID GRAY\033[0m\n");
    
    if (mNaniteRenderer->HasTexture())
        printf("  Texture: \033[32mLOADED\033[0m\n");
    else
        printf("  Texture: \033[33mNOT LOADED\033[0m\n");
    
    printf("\n\033[35m[CONTROLS]\033[0m\n");
    printf("  WASD - Move  |  QE - Up/Down  |  Mouse - Look\n");
    printf("  Shift - Fast  |  M - Meshlet colors  |  T - Toggle texture\n");
    
    printf("\033[33m");
    printf("------------------------------------------------------\n");
    printf("\033[0m");
}
