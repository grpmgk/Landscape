//***************************************************************************************
// MarchingCubesApp.cpp - Lab 4 Marching Cubes (DirectX 12)
// Controls: WASD = fly, Mouse = look, ImGui = algorithm params
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include "MarchingCubes.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

const int gNumFrameResources = 3;

struct RenderItem
{
    RenderItem() = default;
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = 0;
    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class MarchingCubesApp : public D3DApp
{
public:
    MarchingCubesApp(HINSTANCE hInstance);
    MarchingCubesApp(const MarchingCubesApp&) = delete;
    MarchingCubesApp& operator=(const MarchingCubesApp&) = delete;
    ~MarchingCubesApp();

    virtual bool Initialize() override;
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitImGui();
    void DrawImGui();
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildMCGeometry();
    void BuildFrameResources();
    void BuildRenderItems();
    void BuildPSOs();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mOpaqueRitems;

    PassConstants mMainPassCB;
    Camera mCamera;
    POINT mLastMousePos;

    MarchingCubes mMarchingCubes;
    bool mMeshDirty = true;

    ComPtr<ID3D12DescriptorHeap> mImguiSrvHeap = nullptr;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) || defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    try
    {
        MarchingCubesApp theApp(hInstance);
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

MarchingCubesApp::MarchingCubesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"Lab 4 - Marching Cubes";
}

MarchingCubesApp::~MarchingCubesApp()
{
    if (md3dDevice != nullptr)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        FlushCommandQueue();
    }
}

LRESULT MarchingCubesApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return 1;
    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

bool MarchingCubesApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCamera.SetPosition(0.0f, 0.0f, -4.0f);
    mCamera.LookAt(XMFLOAT3(0.0f, 0.0f, -4.0f), XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildMCGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();
    InitImGui();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
    FlushCommandQueue();

    return true;
}

void MarchingCubesApp::OnResize()
{
    D3DApp::OnResize();
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 0.1f, 100.0f);
}

void MarchingCubesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    if (mMeshDirty)
    {
        ThrowIfFailed(mDirectCmdListAlloc->Reset());
        ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
        BuildMCGeometry();
        ThrowIfFailed(mCommandList->Close());
        ID3D12CommandList* uploadLists[] = { mCommandList.Get() };
        mCommandQueue->ExecuteCommandLists(1, uploadLists);
        FlushCommandQueue();

        RenderItem* mcRitem = mAllRitems[0].get();
        mcRitem->Geo = mGeometries["mcGeo"].get();
        const auto& submesh = mcRitem->Geo->DrawArgs["mc"];
        mcRitem->IndexCount = submesh.IndexCount;
        mcRitem->StartIndexLocation = submesh.StartIndexLocation;
        mcRitem->BaseVertexLocation = submesh.BaseVertexLocation;
        mcRitem->NumFramesDirty = gNumFrameResources;
        mMeshDirty = false;
    }

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
}

void MarchingCubesApp::Draw(const GameTimer& gt)
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    DrawImGui();
    ImGui::Render();
    ID3D12DescriptorHeap* imguiHeaps[] = { mImguiSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void MarchingCubesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void MarchingCubesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void MarchingCubesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if (!ImGui::GetIO().WantCaptureMouse && (btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * (float)(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * (float)(y - mLastMousePos.y));
        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }
    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void MarchingCubesApp::OnKeyboardInput(const GameTimer& gt)
{
    if (ImGui::GetIO().WantCaptureKeyboard)
        return;

    const float dt = gt.DeltaTime();
    bool changed = false;

    // WASD — полёт камеры (как в других лабах)
    const float moveSpeed = 8.0f;
    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(moveSpeed * dt);
    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-moveSpeed * dt);
    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-moveSpeed * dt);
    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(moveSpeed * dt);

    if (GetAsyncKeyState('I') & 0x8000)
    {
        mMarchingCubes.SetIsoLevel(mMarchingCubes.GetIsoLevel() + 0.05f * dt * 2.0f);
        changed = true;
    }
    if (GetAsyncKeyState('K') & 0x8000)
    {
        mMarchingCubes.SetIsoLevel(mMarchingCubes.GetIsoLevel() - 0.05f * dt * 2.0f);
        changed = true;
    }

    static bool wasBracketL = false, wasBracketR = false, was1 = false, was2 = false, was3 = false;
    bool bracketL = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0;
    bool bracketR = (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0;
    bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
    bool key2 = (GetAsyncKeyState('2') & 0x8000) != 0;
    bool key3 = (GetAsyncKeyState('3') & 0x8000) != 0;

    if (bracketL && !wasBracketL) { mMarchingCubes.SetResolution((std::max)(4, mMarchingCubes.GetResolution() - 4)); changed = true; }
    wasBracketL = bracketL;
    if (bracketR && !wasBracketR) { mMarchingCubes.SetResolution((std::min)(128, mMarchingCubes.GetResolution() + 4)); changed = true; }
    wasBracketR = bracketR;
    if (key1 && !was1) { mMarchingCubes.SetFieldType(MarchingCubes::FieldType::Sphere); changed = true; }
    was1 = key1;
    if (key2 && !was2) { mMarchingCubes.SetFieldType(MarchingCubes::FieldType::Metaballs); changed = true; }
    was2 = key2;
    if (key3 && !was3) { mMarchingCubes.SetFieldType(MarchingCubes::FieldType::Noise); changed = true; }
    was3 = key3;

    if (changed)
        mMeshDirty = true;

    mCamera.UpdateViewMatrix();
}

void MarchingCubesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            objConstants.TexTransform = MathHelper::Identity4x4();
            objConstants.MaterialIndex = 0;
            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
            e->NumFramesDirty--;
        }
    }
}

void MarchingCubesApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMVECTOR viewDet = XMMatrixDeterminant(view);
    XMVECTOR projDet = XMMatrixDeterminant(proj);
    XMVECTOR viewProjDet = XMMatrixDeterminant(viewProj);
    XMMATRIX invView = XMMatrixInverse(&viewDet, view);
    XMMATRIX invProj = XMMatrixInverse(&projDet, proj);
    XMMATRIX invViewProj = XMMatrixInverse(&viewProjDet, viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 0.1f;
    mMainPassCB.FarZ = 100.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = XMFLOAT4(0.25f, 0.25f, 0.35f, 1.0f);
    mMainPassCB.Lights[0].Direction = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);
    mMainPassCB.Lights[0].Strength = XMFLOAT3(0.9f, 0.9f, 0.9f);

    mCurrFrameResource->PassCB->CopyData(0, mMainPassCB);
}

void MarchingCubesApp::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(MainWnd());

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 1;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mImguiSrvHeap)));

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = md3dDevice.Get();
    initInfo.CommandQueue = mCommandQueue.Get();
    initInfo.NumFramesInFlight = gNumFrameResources;
    initInfo.RTVFormat = mBackBufferFormat;
    initInfo.DSVFormat = mDepthStencilFormat;
    initInfo.SrvDescriptorHeap = mImguiSrvHeap.Get();
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        *outCpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        *outGpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
    initInfo.LegacySingleSrvCpuDescriptor = mImguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = mImguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
    ImGui_ImplDX12_Init(&initInfo);
}

void MarchingCubesApp::DrawImGui()
{
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Marching Cubes", nullptr, flags))
    {
        ImGui::Text("Iso level (surface threshold)");
        float iso = mMarchingCubes.GetIsoLevel();
        if (ImGui::SliderFloat("##iso", &iso, -2.0f, 2.0f, "%.3f"))
        {
            mMarchingCubes.SetIsoLevel(iso);
            mMeshDirty = true;
        }

        ImGui::Spacing();
        ImGui::Text("Grid resolution (cells per axis)");
        int res = mMarchingCubes.GetResolution();
        if (ImGui::SliderInt("##res", &res, 4, 128, "%d"))
        {
            mMarchingCubes.SetResolution(res);
            mMeshDirty = true;
        }

        ImGui::Spacing();
        ImGui::Text("Field type");
        int type = (int)mMarchingCubes.GetFieldType();
        const char* names[] = { "Sphere", "Metaballs", "Noise" };
        if (ImGui::Combo("##field", &type, names, 3))
        {
            mMarchingCubes.SetFieldType((MarchingCubes::FieldType)type);
            mMeshDirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("WASD: move | Mouse: look");
    }
    ImGui::End();
}

void MarchingCubesApp::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0); // Object CB
    slotRootParameter[1].InitAsConstantBufferView(1); // Pass CB

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()));
    if (errorBlob != nullptr)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(md3dDevice->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void MarchingCubesApp::BuildShadersAndInputLayout()
{
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
    mInputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void MarchingCubesApp::BuildMCGeometry()
{
    std::vector<MCVertex> vertices;
    std::vector<uint32_t> indices;
    mMarchingCubes.Generate(vertices, indices);

    if (vertices.empty() || indices.empty())
    {
        vertices.push_back({ XMFLOAT3(0,0,0), XMFLOAT3(0,1,0) });
        indices = { 0, 0, 0 };
    }

    const UINT vbByteSize = (UINT)(vertices.size() * sizeof(MCVertex));
    const UINT ibByteSize = (UINT)(indices.size() * sizeof(uint32_t));

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "mcGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        vertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(MCVertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    geo->DrawArgs["mc"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void MarchingCubesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, (UINT)mAllRitems.size()));
}

void MarchingCubesApp::BuildRenderItems()
{
    auto mcRitem = std::make_unique<RenderItem>();
    mcRitem->World = MathHelper::Identity4x4();
    mcRitem->TexTransform = MathHelper::Identity4x4();
    mcRitem->ObjCBIndex = 0;
    mcRitem->Geo = mGeometries["mcGeo"].get();
    mcRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mcRitem->IndexCount = mcRitem->Geo->DrawArgs["mc"].IndexCount;
    mcRitem->StartIndexLocation = mcRitem->Geo->DrawArgs["mc"].StartIndexLocation;
    mcRitem->BaseVertexLocation = mcRitem->Geo->DrawArgs["mc"].BaseVertexLocation;
    mOpaqueRitems.push_back(mcRitem.get());
    mAllRitems.push_back(std::move(mcRitem));
}

void MarchingCubesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), mShaders["standardVS"]->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()), mShaders["opaquePS"]->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
}

void MarchingCubesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];
        if (ri->Geo == nullptr || ri->IndexCount == 0)
            continue;
        D3D12_VERTEX_BUFFER_VIEW vbv = ri->Geo->VertexBufferView();
        D3D12_INDEX_BUFFER_VIEW ibv = ri->Geo->IndexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}
