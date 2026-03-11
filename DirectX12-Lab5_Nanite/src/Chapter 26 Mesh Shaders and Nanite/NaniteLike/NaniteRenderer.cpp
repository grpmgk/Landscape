//***************************************************************************************
// NaniteRenderer.cpp - Renderer with Mesh Shader pipeline and GPU culling
//***************************************************************************************

#include "NaniteRenderer.h"
#include "../../Common/d3dUtil.h"
#include "../../Common/DDSTextureLoader.h"
#include <dxcapi.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

NaniteRenderer::NaniteRenderer(ID3D12Device* device, DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthFormat)
    : mDevice(device), mBackBufferFormat(backBufferFormat), mDepthFormat(depthFormat)
{
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    mMeshShadersSupported = CheckMeshShaderSupport(device);
}

NaniteRenderer::~NaniteRenderer() = default;

bool NaniteRenderer::CheckMeshShaderSupport(ID3D12Device* device)
{
    ComPtr<ID3D12Device2> device2;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device2))))
        return false;

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    if (SUCCEEDED(device2->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
    {
        return options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    }
    return false;
}

void NaniteRenderer::Initialize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
    
    BuildDescriptorHeaps();
    CreateBuffers();
    
    if (mMeshShadersSupported)
    {
        printf("\033[32m[GPU]\033[0m Mesh Shaders SUPPORTED - building MS pipeline\n");
        BuildMeshShaderRootSignature();
        BuildMeshShaderPSO();
        
        if (mMeshShaderPSO)
        {
            mUseMeshShaders = true;
            printf("\033[32m[SUCCESS]\033[0m Mesh Shader pipeline ready with GPU culling!\n");
        }
        else
        {
            printf("\033[33m[WARNING]\033[0m MS PSO failed, falling back to VS/PS\n");
            mUseMeshShaders = false;
        }
    }
    else
    {
        printf("\033[33m[GPU]\033[0m Mesh Shaders NOT supported - using fallback\n");
        mUseMeshShaders = false;
    }
    
    // Always build fallback pipeline
    BuildRootSignature();
    BuildPSOs();
}

void NaniteRenderer::OnResize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;
}

void NaniteRenderer::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 20;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)));
}

void NaniteRenderer::BuildRootSignature()
{
    // Root parameters:
    // 0: CBV - Pass constants
    // 1: Descriptor table - Diffuse texture
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    
    CD3DX12_ROOT_PARAMETER rootParams[2];
    rootParams[0].InitAsConstantBufferView(0);
    rootParams[1].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    
    // Static sampler
    CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParams, 1, &linearWrap,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    
    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    
    if (errorBlob)
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    ThrowIfFailed(hr);
    
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));
}

void NaniteRenderer::BuildMeshShaderRootSignature()
{
    // Root parameters for Mesh Shader pipeline:
    // 0: CBV - Pass constants (b0)
    // 1: SRV - Vertices (t0)
    // 2: SRV - Meshlets (t1)
    // 3: SRV - MeshletBounds (t2)
    // 4: SRV - UniqueVertexIndices (t3)
    // 5: SRV - PrimitiveIndices (t4)
    // 6: SRV - Instances (t5)
    // 7: Descriptor Table - Diffuse Texture (t6)
    
    CD3DX12_DESCRIPTOR_RANGE1 texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    
    CD3DX12_ROOT_PARAMETER1 rootParams[8];
    rootParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[1].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[2].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[3].InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[4].InitAsShaderResourceView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[5].InitAsShaderResourceView(4, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[6].InitAsShaderResourceView(5, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[7].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    
    // Static sampler for texture
    CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init_1_1(_countof(rootParams), rootParams, 1, &linearWrap, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    
    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_1,
        &serializedRootSig, &errorBlob);
    
    if (errorBlob)
    {
        printf("\033[31m[ERROR]\033[0m Root sig: %s\n", (char*)errorBlob->GetBufferPointer());
    }
    
    if (FAILED(hr))
    {
        printf("\033[31m[ERROR]\033[0m Failed to serialize MS root signature\n");
        return;
    }
    
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mMeshShaderRootSig)));
}


void NaniteRenderer::BuildPSOs()
{
    ComPtr<ID3DBlob> vsBlob = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "VSMain", "vs_5_1");
    ComPtr<ID3DBlob> psBlob = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "PSMain", "ps_5_1");
    
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"MESHLET_ID", 0, DXGI_FORMAT_R32_UINT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = mDepthFormat;
    psoDesc.SampleDesc.Count = 1;
    
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}

void NaniteRenderer::BuildMeshShaderPSO()
{
    if (!mMeshShaderRootSig)
    {
        printf("\033[31m[ERROR]\033[0m MS Root signature not created\n");
        return;
    }
    
    // Compile shaders using DXC
    ComPtr<IDxcLibrary> library;
    ComPtr<IDxcCompiler> compiler;
    
    if (FAILED(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library))) ||
        FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
    {
        printf("\033[31m[ERROR]\033[0m Failed to create DXC instances\n");
        return;
    }
    
    // Try multiple paths for shader file
    const wchar_t* shaderPaths[] = {
        L"Shaders/MeshShader.hlsl",
        L"../../Chapter 26 Mesh Shaders and Nanite/NaniteLike/Shaders/MeshShader.hlsl",
        L"../../../Chapter 26 Mesh Shaders and Nanite/NaniteLike/Shaders/MeshShader.hlsl"
    };
    
    ComPtr<IDxcBlobEncoding> sourceBlob;
    bool loaded = false;
    for (const auto& path : shaderPaths)
    {
        if (SUCCEEDED(library->CreateBlobFromFile(path, nullptr, &sourceBlob)))
        {
            printf("\033[32m[SHADER]\033[0m Loaded from: %ls\n", path);
            loaded = true;
            break;
        }
    }
    
    if (!loaded)
    {
        printf("\033[31m[ERROR]\033[0m Failed to load MeshShader.hlsl from any path\n");
        return;
    }
    
    auto compileShader = [&](LPCWSTR entryPoint, LPCWSTR target) -> ComPtr<IDxcBlob> {
        ComPtr<IDxcOperationResult> result;
        HRESULT hr = compiler->Compile(sourceBlob.Get(), L"MeshShader.hlsl", entryPoint, target,
            nullptr, 0, nullptr, 0, nullptr, &result);
        
        if (FAILED(hr))
            return nullptr;
        
        HRESULT status;
        result->GetStatus(&status);
        if (FAILED(status))
        {
            ComPtr<IDxcBlobEncoding> errors;
            result->GetErrorBuffer(&errors);
            if (errors && errors->GetBufferSize() > 0)
            {
                printf("\033[31m[SHADER ERROR]\033[0m %s: %s\n", 
                    entryPoint ? "Compile" : "Unknown",
                    (char*)errors->GetBufferPointer());
            }
            return nullptr;
        }
        
        ComPtr<IDxcBlob> blob;
        result->GetResult(&blob);
        return blob;
    };
    
    printf("\033[33m[COMPILING]\033[0m Amplification Shader...\n");
    auto asBlob = compileShader(L"ASMain", L"as_6_5");
    if (!asBlob) return;
    
    printf("\033[33m[COMPILING]\033[0m Mesh Shader...\n");
    auto msBlob = compileShader(L"MSMain", L"ms_6_5");
    if (!msBlob) return;
    
    printf("\033[33m[COMPILING]\033[0m Pixel Shader...\n");
    auto psBlob = compileShader(L"PSMain", L"ps_6_5");
    if (!psBlob) return;
    
    // Create PSO using raw stream subobjects - manual approach for compatibility
    // Each subobject is: D3D12_PIPELINE_STATE_SUBOBJECT_TYPE (aligned) + data
    
    struct alignas(void*) StreamSubobject_RootSig {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
        ID3D12RootSignature* pRootSignature;
    };
    struct alignas(void*) StreamSubobject_AS {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
        D3D12_SHADER_BYTECODE Bytecode;
    };
    struct alignas(void*) StreamSubobject_MS {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
        D3D12_SHADER_BYTECODE Bytecode;
    };
    struct alignas(void*) StreamSubobject_PS {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
        D3D12_SHADER_BYTECODE Bytecode;
    };
    struct alignas(void*) StreamSubobject_Blend {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
        D3D12_BLEND_DESC Desc;
    };
    struct alignas(void*) StreamSubobject_SampleMask {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
        UINT SampleMask;
    };
    struct alignas(void*) StreamSubobject_Rasterizer {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
        D3D12_RASTERIZER_DESC Desc;
    };
    struct alignas(void*) StreamSubobject_DepthStencil {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
        D3D12_DEPTH_STENCIL_DESC Desc;
    };
    struct alignas(void*) StreamSubobject_RTFormats {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
        D3D12_RT_FORMAT_ARRAY Formats;
    };
    struct alignas(void*) StreamSubobject_DSFormat {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
        DXGI_FORMAT Format;
    };
    struct alignas(void*) StreamSubobject_SampleDesc {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
        DXGI_SAMPLE_DESC Desc;
    };
    
    struct MeshShaderPSOStream
    {
        StreamSubobject_RootSig RootSignature;
        StreamSubobject_AS AS;
        StreamSubobject_MS MS;
        StreamSubobject_PS PS;
        StreamSubobject_Blend Blend;
        StreamSubobject_SampleMask SampleMask;
        StreamSubobject_Rasterizer Rasterizer;
        StreamSubobject_DepthStencil DepthStencil;
        StreamSubobject_RTFormats RTFormats;
        StreamSubobject_DSFormat DSFormat;
        StreamSubobject_SampleDesc SampleDesc;
    };
    
    MeshShaderPSOStream psoStream = {};
    
    // Root signature
    psoStream.RootSignature.pRootSignature = mMeshShaderRootSig.Get();
    
    // Shaders
    psoStream.AS.Bytecode = { asBlob->GetBufferPointer(), asBlob->GetBufferSize() };
    psoStream.MS.Bytecode = { msBlob->GetBufferPointer(), msBlob->GetBufferSize() };
    psoStream.PS.Bytecode = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    
    // Blend state
    psoStream.Blend.Desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    
    // Sample mask
    psoStream.SampleMask.SampleMask = UINT_MAX;
    
    // Rasterizer
    psoStream.Rasterizer.Desc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    
    // Depth stencil
    psoStream.DepthStencil.Desc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    
    // Render target formats
    psoStream.RTFormats.Formats.NumRenderTargets = 1;
    psoStream.RTFormats.Formats.RTFormats[0] = mBackBufferFormat;
    for (int i = 1; i < 8; ++i)
        psoStream.RTFormats.Formats.RTFormats[i] = DXGI_FORMAT_UNKNOWN;
    
    // Depth stencil format
    psoStream.DSFormat.Format = mDepthFormat;
    
    // Sample desc
    psoStream.SampleDesc.Desc = { 1, 0 };
    
    printf("\033[33m[DEBUG]\033[0m PSO Stream size: %zu bytes\n", sizeof(psoStream));
    
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(psoStream);
    streamDesc.pPipelineStateSubobjectStream = &psoStream;
    
    ComPtr<ID3D12Device2> device2;
    if (FAILED(mDevice->QueryInterface(IID_PPV_ARGS(&device2))))
    {
        printf("\033[31m[ERROR]\033[0m Failed to get ID3D12Device2\n");
        return;
    }
    
    HRESULT hr = device2->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&mMeshShaderPSO));
    if (FAILED(hr))
    {
        printf("\033[31m[ERROR]\033[0m Failed to create MS PSO: 0x%08X\n", hr);
        
        // Try to get more info via debug layer
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(mDevice->QueryInterface(IID_PPV_ARGS(&infoQueue))))
        {
            UINT64 numMessages = infoQueue->GetNumStoredMessages();
            for (UINT64 i = 0; i < numMessages; ++i)
            {
                SIZE_T msgLen = 0;
                infoQueue->GetMessage(i, nullptr, &msgLen);
                if (msgLen > 0)
                {
                    std::vector<char> msgData(msgLen);
                    auto* msg = reinterpret_cast<D3D12_MESSAGE*>(msgData.data());
                    infoQueue->GetMessage(i, msg, &msgLen);
                    printf("\033[31m[D3D12]\033[0m %s\n", msg->pDescription);
                }
            }
            infoQueue->ClearStoredMessages();
        }
        return;
    }
    
    printf("\033[32m[SUCCESS]\033[0m Mesh Shader PSO created!\n");
}

void NaniteRenderer::CreateBuffers()
{
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(d3dUtil::CalcConstantBufferByteSize(sizeof(MeshShaderPassConstants)));
    
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mPassConstantsBuffer)));
    
    // Create query heap for pipeline statistics
    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    queryHeapDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&mQueryHeap)));
    
    // Create readback buffer for query results
    auto readbackHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &readbackBufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&mQueryResultBuffer)));
}

bool NaniteRenderer::LoadTexture(ID3D12GraphicsCommandList* cmdList, const std::wstring& filename)
{
    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        mDevice, cmdList, filename.c_str(), 
        mDiffuseTexture, mDiffuseTextureUpload);
    
    if (FAILED(hr))
    {
        printf("\033[31m[ERROR]\033[0m Failed to load texture: %ls (0x%08X)\n", filename.c_str(), hr);
        mUseTexture = false;
        return false;
    }
    
    // Create SRV in descriptor heap (slot 0)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = mDiffuseTexture->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = mDiffuseTexture->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    
    mDevice->CreateShaderResourceView(mDiffuseTexture.Get(), &srvDesc,
        mSrvHeap->GetCPUDescriptorHandleForHeapStart());
    
    mUseTexture = true;
    printf("\033[32m[TEXTURE]\033[0m Loaded: %ls\n", filename.c_str());
    return true;
}

void NaniteRenderer::ExtractFrustumPlanes(const XMMATRIX& viewProj, XMFLOAT4* planes)
{
    // Extract frustum planes from ViewProj matrix
    // Using column extraction method for row-major matrix
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, XMMatrixTranspose(viewProj));  // Transpose to get column-major for extraction
    
    // Left:   row3 + row0
    planes[0] = XMFLOAT4(m._41 + m._11, m._42 + m._12, m._43 + m._13, m._44 + m._14);
    // Right:  row3 - row0
    planes[1] = XMFLOAT4(m._41 - m._11, m._42 - m._12, m._43 - m._13, m._44 - m._14);
    // Bottom: row3 + row1
    planes[2] = XMFLOAT4(m._41 + m._21, m._42 + m._22, m._43 + m._23, m._44 + m._24);
    // Top:    row3 - row1
    planes[3] = XMFLOAT4(m._41 - m._21, m._42 - m._22, m._43 - m._23, m._44 - m._24);
    // Near:   row2 (for D3D where z goes 0 to 1)
    planes[4] = XMFLOAT4(m._31, m._32, m._33, m._34);
    // Far:    row3 - row2
    planes[5] = XMFLOAT4(m._41 - m._31, m._42 - m._32, m._43 - m._33, m._44 - m._34);
    
    // Normalize planes
    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR plane = XMLoadFloat4(&planes[i]);
        float length = XMVectorGetX(XMVector3Length(plane));
        if (length > 0.0001f)
            XMStoreFloat4(&planes[i], XMVectorScale(plane, 1.0f / length));
    }
}


void NaniteRenderer::UploadMesh(ID3D12GraphicsCommandList* cmdList, const MeshletMesh& mesh, UINT meshIndex)
{
    if (meshIndex >= mMeshes.size())
        mMeshes.resize(meshIndex + 1);
    mMeshes[meshIndex] = mesh;
    
    UINT meshletCount = static_cast<UINT>(mesh.Meshlets.size());
    UINT vertexCount = static_cast<UINT>(mesh.Positions.size());
    
    // ========== Upload for Mesh Shader pipeline ==========
    if (mUseMeshShaders)
    {
        // 1. Vertices
        std::vector<GPUVertex> gpuVertices(vertexCount);
        for (UINT i = 0; i < vertexCount; ++i)
        {
            gpuVertices[i].Position = mesh.Positions[i];
            gpuVertices[i].Normal = i < mesh.Normals.size() ? mesh.Normals[i] : XMFLOAT3(0, 1, 0);
            gpuVertices[i].TexCoord = i < mesh.TexCoords.size() ? mesh.TexCoords[i] : XMFLOAT2(0, 0);
            gpuVertices[i].Tangent = i < mesh.Tangents.size() ? mesh.Tangents[i] : XMFLOAT3(1, 0, 0);
            gpuVertices[i].Padding = 0;
        }
        mMSVertexBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList, 
            gpuVertices.data(), gpuVertices.size() * sizeof(GPUVertex), mMSVertexUploadBuffer);
        
        // 2. Meshlets
        std::vector<GPUMeshlet> gpuMeshlets(meshletCount);
        for (UINT i = 0; i < meshletCount; ++i)
        {
            gpuMeshlets[i].VertexOffset = mesh.Meshlets[i].VertexOffset;
            gpuMeshlets[i].VertexCount = mesh.Meshlets[i].VertexCount;
            gpuMeshlets[i].PrimitiveOffset = mesh.Meshlets[i].PrimitiveOffset;
            gpuMeshlets[i].PrimitiveCount = mesh.Meshlets[i].PrimitiveCount;
        }
        mMeshletBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
            gpuMeshlets.data(), gpuMeshlets.size() * sizeof(GPUMeshlet), mMeshletUploadBuffer);
        
        // 3. Meshlet bounds
        std::vector<GPUMeshletBounds> gpuBounds(meshletCount);
        for (UINT i = 0; i < meshletCount; ++i)
        {
            gpuBounds[i].Center = mesh.MeshletBoundsData[i].Center;
            gpuBounds[i].Radius = mesh.MeshletBoundsData[i].Radius;
            gpuBounds[i].ConeAxis = mesh.MeshletBoundsData[i].ConeAxis;
            gpuBounds[i].ConeCutoff = mesh.MeshletBoundsData[i].ConeCutoff;
            gpuBounds[i].ConeApex = mesh.MeshletBoundsData[i].ConeApex;
            gpuBounds[i].Padding = 0;
        }
        mMeshletBoundsBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
            gpuBounds.data(), gpuBounds.size() * sizeof(GPUMeshletBounds), mMeshletBoundsUploadBuffer);
        
        // 4. Unique vertex indices
        mUniqueVertexIndicesBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
            mesh.UniqueVertexIndices.data(), mesh.UniqueVertexIndices.size() * sizeof(uint32_t), 
            mUniqueVertexIndicesUploadBuffer);
        
        // 5. Primitive indices - each uint8 becomes uint32 for StructuredBuffer compatibility
        std::vector<uint32_t> primIndices32(mesh.PrimitiveIndices.size());
        for (size_t i = 0; i < mesh.PrimitiveIndices.size(); ++i)
        {
            primIndices32[i] = mesh.PrimitiveIndices[i];
        }
        mPrimitiveIndicesBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
            primIndices32.data(), primIndices32.size() * sizeof(uint32_t), mPrimitiveIndicesUploadBuffer);
        
        printf("\033[32m[MS UPLOAD]\033[0m %u verts, %u meshlets, %zu unique, %zu prims\n",
            vertexCount, meshletCount, mesh.UniqueVertexIndices.size(), mesh.PrimitiveIndices.size());
    }
    
    // ========== Upload for fallback pipeline ==========
    std::vector<MeshletVertex> vertices;
    std::vector<uint32_t> indices;
    
    for (UINT meshletIdx = 0; meshletIdx < meshletCount; ++meshletIdx)
    {
        const auto& meshlet = mesh.Meshlets[meshletIdx];
        
        for (UINT primIdx = 0; primIdx < meshlet.PrimitiveCount; ++primIdx)
        {
            UINT primOffset = (meshlet.PrimitiveOffset + primIdx) * 3;
            if (primOffset + 2 >= mesh.PrimitiveIndices.size()) continue;
            
            uint8_t l0 = mesh.PrimitiveIndices[primOffset + 0];
            uint8_t l1 = mesh.PrimitiveIndices[primOffset + 1];
            uint8_t l2 = mesh.PrimitiveIndices[primOffset + 2];
            
            UINT v0 = meshlet.VertexOffset + l0;
            UINT v1 = meshlet.VertexOffset + l1;
            UINT v2 = meshlet.VertexOffset + l2;
            
            if (v0 >= mesh.UniqueVertexIndices.size() || v1 >= mesh.UniqueVertexIndices.size() || 
                v2 >= mesh.UniqueVertexIndices.size()) continue;
            
            uint32_t g0 = mesh.UniqueVertexIndices[v0];
            uint32_t g1 = mesh.UniqueVertexIndices[v1];
            uint32_t g2 = mesh.UniqueVertexIndices[v2];
            
            if (g0 >= mesh.Positions.size() || g1 >= mesh.Positions.size() || 
                g2 >= mesh.Positions.size()) continue;
            
            uint32_t base = static_cast<uint32_t>(vertices.size());
            
            auto addVert = [&](uint32_t gi) {
                MeshletVertex v;
                v.Position = mesh.Positions[gi];
                v.Normal = gi < mesh.Normals.size() ? mesh.Normals[gi] : XMFLOAT3(0, 1, 0);
                v.TexCoord = gi < mesh.TexCoords.size() ? mesh.TexCoords[gi] : XMFLOAT2(0, 0);
                v.Tangent = gi < mesh.Tangents.size() ? mesh.Tangents[gi] : XMFLOAT3(1, 0, 0);
                v.MeshletID = meshletIdx;
                vertices.push_back(v);
            };
            
            addVert(g0); addVert(g1); addVert(g2);
            indices.push_back(base); indices.push_back(base + 1); indices.push_back(base + 2);
        }
    }
    
    if (!vertices.empty())
    {
        mVertexBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList, 
            vertices.data(), vertices.size() * sizeof(MeshletVertex), mVertexUploadBuffer);
        mVertexBufferView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
        mVertexBufferView.StrideInBytes = sizeof(MeshletVertex);
        mVertexBufferView.SizeInBytes = static_cast<UINT>(vertices.size() * sizeof(MeshletVertex));
        
        mIndexBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
            indices.data(), indices.size() * sizeof(uint32_t), mIndexUploadBuffer);
        mIndexBufferView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
        mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
        mIndexBufferView.SizeInBytes = static_cast<UINT>(indices.size() * sizeof(uint32_t));
    }
    
    mTotalMeshlets = meshletCount;
    mTotalVertices = static_cast<UINT>(vertices.size());
    mTotalIndices = static_cast<UINT>(indices.size());
    mTotalTriangles = mTotalIndices / 3;
}

void NaniteRenderer::SetInstances(ID3D12GraphicsCommandList* cmdList, const std::vector<MeshInstance>& instances)
{
    mInstances = instances;
    
    if (mUseMeshShaders && !instances.empty())
    {
        std::vector<GPUInstance> gpuInst(instances.size());
        for (size_t i = 0; i < instances.size(); ++i)
        {
            gpuInst[i].World = instances[i].World;
            gpuInst[i].InvTransposeWorld = instances[i].InvTransposeWorld;
            gpuInst[i].MeshIndex = instances[i].MeshIndex;
            gpuInst[i].MaterialIndex = instances[i].MaterialIndex;
            gpuInst[i].Padding[0] = gpuInst[i].Padding[1] = 0;
        }
        mInstanceBuffer = d3dUtil::CreateDefaultBuffer(mDevice, cmdList,
            gpuInst.data(), gpuInst.size() * sizeof(GPUInstance), mInstanceUploadBuffer);
    }
}

void NaniteRenderer::Render(ID3D12GraphicsCommandList* cmdList, const Camera& camera,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    if (mTotalMeshlets == 0) return;
    
    if (mUseMeshShaders && mMeshShaderPSO)
        RenderMeshShader(cmdList, camera, rtv, dsv);
    else
        RenderFallback(cmdList, camera, rtv, dsv);
}

void NaniteRenderer::RenderMeshShader(ID3D12GraphicsCommandList* cmdList, const Camera& camera,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    MeshShaderPassConstants pc = {};
    XMMATRIX view = camera.GetView();
    XMMATRIX proj = camera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    
    XMStoreFloat4x4(&pc.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&pc.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&pc.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&pc.InvView, XMMatrixTranspose(XMMatrixInverse(nullptr, view)));
    pc.EyePosW = camera.GetPosition3f();
    pc.RenderTargetSize = XMFLOAT2((float)mWidth, (float)mHeight);
    pc.InvRenderTargetSize = XMFLOAT2(1.0f / mWidth, 1.0f / mHeight);
    pc.MeshletCount = mTotalMeshlets;
    pc.InstanceCount = (UINT)mInstances.size();
    pc.LODScale = 1.0f;
    pc.ShowMeshletColors = mShowMeshletColors ? 1 : 0;
    pc.UseTexture = mUseTexture ? 1 : 0;
    ExtractFrustumPlanes(viewProj, pc.FrustumPlanes);
    
    void* mapped;
    mPassConstantsBuffer->Map(0, nullptr, &mapped);
    memcpy(mapped, &pc, sizeof(pc));
    mPassConstantsBuffer->Unmap(0, nullptr);
    
    ComPtr<ID3D12GraphicsCommandList6> cmdList6;
    if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList6))))
    {
        RenderFallback(cmdList, camera, rtv, dsv);
        return;
    }
    
    // Read previous frame's query results
    if (mQueryResultBuffer)
    {
        void* queryData;
        D3D12_RANGE readRange = { 0, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) };
        if (SUCCEEDED(mQueryResultBuffer->Map(0, &readRange, &queryData)))
        {
            memcpy(&mLastPipelineStats, queryData, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
            D3D12_RANGE writeRange = { 0, 0 };
            mQueryResultBuffer->Unmap(0, &writeRange);
        }
    }
    
    cmdList6->SetPipelineState(mMeshShaderPSO.Get());
    cmdList6->SetGraphicsRootSignature(mMeshShaderRootSig.Get());
    
    // Set descriptor heap for texture
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList6->SetDescriptorHeaps(1, heaps);
    
    cmdList6->SetGraphicsRootConstantBufferView(0, mPassConstantsBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootShaderResourceView(1, mMSVertexBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootShaderResourceView(2, mMeshletBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootShaderResourceView(3, mMeshletBoundsBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootShaderResourceView(4, mUniqueVertexIndicesBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootShaderResourceView(5, mPrimitiveIndicesBuffer->GetGPUVirtualAddress());
    cmdList6->SetGraphicsRootShaderResourceView(6, mInstanceBuffer->GetGPUVirtualAddress());
    
    // Set texture descriptor table
    if (mUseTexture)
        cmdList6->SetGraphicsRootDescriptorTable(7, mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    
    cmdList6->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    
    // Begin pipeline statistics query
    cmdList6->BeginQuery(mQueryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    
    const UINT AS_GROUP_SIZE = 32;
    UINT dispatchCount = (mTotalMeshlets + AS_GROUP_SIZE - 1) / AS_GROUP_SIZE;
    dispatchCount *= (UINT)mInstances.size();
    
    cmdList6->DispatchMesh(dispatchCount, 1, 1);
    
    // End query and resolve to readback buffer
    cmdList6->EndQuery(mQueryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    cmdList6->ResolveQueryData(mQueryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1,
        mQueryResultBuffer.Get(), 0);
    
    // Update stats from GPU query (from previous frame)
    // PSPrimitives = primitives that passed all culling and were actually rendered
    UINT renderedPrimitives = (UINT)mLastPipelineStats.CInvocations;  // Clipper invocations = triangles sent to rasterizer
    if (renderedPrimitives == 0)
        renderedPrimitives = (UINT)mLastPipelineStats.IAPrimitives;  // Fallback
    
    // Estimate visible meshlets from rendered triangles
    // Average triangles per meshlet = total / meshlet count
    float avgTrisPerMeshlet = mTotalMeshlets > 0 ? (float)mTotalTriangles / mTotalMeshlets : 1.0f;
    UINT estimatedVisibleMeshlets = avgTrisPerMeshlet > 0 ? (UINT)(renderedPrimitives / avgTrisPerMeshlet) : mTotalMeshlets;
    estimatedVisibleMeshlets = (std::min)(estimatedVisibleMeshlets, mTotalMeshlets);
    
    mCullingStats.VisibleMeshlets = estimatedVisibleMeshlets;
    mCullingStats.TotalTriangles = renderedPrimitives;
}

void NaniteRenderer::RenderFallback(ID3D12GraphicsCommandList* cmdList, const Camera& camera,
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    if (mTotalIndices == 0) return;
    
    MeshShaderPassConstants pc = {};
    XMMATRIX view = camera.GetView();
    XMMATRIX proj = camera.GetProj();
    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    
    XMStoreFloat4x4(&pc.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&pc.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&pc.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&pc.InvView, XMMatrixTranspose(XMMatrixInverse(nullptr, view)));
    pc.EyePosW = camera.GetPosition3f();
    pc.RenderTargetSize = XMFLOAT2((float)mWidth, (float)mHeight);
    pc.InvRenderTargetSize = XMFLOAT2(1.0f / mWidth, 1.0f / mHeight);
    pc.ShowMeshletColors = mShowMeshletColors ? 1 : 0;
    pc.UseTexture = mUseTexture ? 1 : 0;
    
    void* mapped;
    mPassConstantsBuffer->Map(0, nullptr, &mapped);
    memcpy(mapped, &pc, sizeof(pc));
    mPassConstantsBuffer->Unmap(0, nullptr);
    
    cmdList->SetPipelineState(mPSO.Get());
    cmdList->SetGraphicsRootSignature(mRootSignature.Get());
    
    // Set descriptor heap for texture
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    
    cmdList->SetGraphicsRootConstantBufferView(0, mPassConstantsBuffer->GetGPUVirtualAddress());
    
    // Set texture if available
    if (mUseTexture)
        cmdList->SetGraphicsRootDescriptorTable(1, mSrvHeap->GetGPUDescriptorHandleForHeapStart());
    
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    cmdList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    cmdList->IASetIndexBuffer(&mIndexBufferView);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawIndexedInstanced(mTotalIndices, 1, 0, 0, 0);
    
    mCullingStats.VisibleMeshlets = mTotalMeshlets;
    mCullingStats.TotalTriangles = mTotalTriangles;
}
