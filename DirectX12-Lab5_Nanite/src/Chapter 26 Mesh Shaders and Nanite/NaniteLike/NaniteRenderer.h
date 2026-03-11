//***************************************************************************************
// NaniteRenderer.h - Nanite-like renderer using Mesh Shaders
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"
#include "../../Common/Camera.h"
#include "Meshlet.h"
#include <string>

class NaniteRenderer
{
public:
    NaniteRenderer(ID3D12Device* device, DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthFormat);
    ~NaniteRenderer();

    static bool CheckMeshShaderSupport(ID3D12Device* device);

    void Initialize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);
    void OnResize(UINT width, UINT height);

    void UploadMesh(ID3D12GraphicsCommandList* cmdList, const MeshletMesh& mesh, UINT meshIndex);
    void SetInstances(ID3D12GraphicsCommandList* cmdList, const std::vector<MeshInstance>& instances);
    
    // Texture loading
    bool LoadTexture(ID3D12GraphicsCommandList* cmdList, const std::wstring& filename);
    bool HasTexture() const { return mDiffuseTexture != nullptr; }

    void Render(
        ID3D12GraphicsCommandList* cmdList,
        const Camera& camera,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv);

    const CullingStats& GetCullingStats() const { return mCullingStats; }
    UINT GetMeshletCount() const { return mTotalMeshlets; }
    UINT GetVertexCount() const { return mTotalVertices; }
    UINT GetTriangleCount() const { return mTotalTriangles; }
    bool IsMeshShaderEnabled() const { return mMeshShadersSupported && mUseMeshShaders; }
    
    // Meshlet visualization toggle
    void ToggleMeshletVisualization() { mShowMeshletColors = !mShowMeshletColors; }
    bool IsShowingMeshletColors() const { return mShowMeshletColors; }
    
    // Texture toggle
    void ToggleTexture() { if (mDiffuseTexture) mUseTexture = !mUseTexture; }
    bool IsUsingTexture() const { return mUseTexture; }

private:
    void BuildRootSignature();
    void BuildMeshShaderRootSignature();
    void BuildDescriptorHeaps();
    void CreateBuffers();
    void BuildPSOs();
    void BuildMeshShaderPSO();
    void ExtractFrustumPlanes(const DirectX::XMMATRIX& viewProj, DirectX::XMFLOAT4* planes);

    // Fallback rendering (traditional VS/PS)
    void RenderFallback(ID3D12GraphicsCommandList* cmdList, const Camera& camera,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);
    
    // Mesh Shader rendering
    void RenderMeshShader(ID3D12GraphicsCommandList* cmdList, const Camera& camera,
        D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv);

private:
    ID3D12Device* mDevice = nullptr;
    DXGI_FORMAT mBackBufferFormat;
    DXGI_FORMAT mDepthFormat;
    UINT mWidth = 0;
    UINT mHeight = 0;

    // Root signatures
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;         // Fallback
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mMeshShaderRootSig;     // Mesh Shader
    
    // Pipeline states
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;                   // Fallback VS/PS
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mMeshShaderPSO;         // AS/MS/PS
    
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    UINT mCbvSrvUavDescriptorSize = 0;
    
    // Pipeline statistics query for GPU culling stats
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> mQueryHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> mQueryResultBuffer;
    D3D12_QUERY_DATA_PIPELINE_STATISTICS mLastPipelineStats = {};

    // GPU buffers for fallback rendering
    Microsoft::WRL::ComPtr<ID3D12Resource> mVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPassConstantsBuffer;
    
    // Upload buffers (must stay alive until GPU finishes)
    Microsoft::WRL::ComPtr<ID3D12Resource> mVertexUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexUploadBuffer;
    
    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView = {};

    // GPU buffers for Mesh Shader pipeline
    Microsoft::WRL::ComPtr<ID3D12Resource> mMSVertexBuffer;           // Structured buffer of vertices
    Microsoft::WRL::ComPtr<ID3D12Resource> mMeshletBuffer;            // Meshlet descriptors
    Microsoft::WRL::ComPtr<ID3D12Resource> mMeshletBoundsBuffer;      // Meshlet bounds for culling
    Microsoft::WRL::ComPtr<ID3D12Resource> mUniqueVertexIndicesBuffer;// Vertex indices per meshlet
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrimitiveIndicesBuffer;   // Triangle indices per meshlet
    Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceBuffer;           // Instance transforms
    
    // Upload buffers for mesh shader data
    Microsoft::WRL::ComPtr<ID3D12Resource> mMSVertexUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mMeshletUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mMeshletBoundsUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mUniqueVertexIndicesUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mPrimitiveIndicesUploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> mInstanceUploadBuffer;
    
    // Texture resources
    Microsoft::WRL::ComPtr<ID3D12Resource> mDiffuseTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> mDiffuseTextureUpload;
    bool mUseTexture = false;

    std::vector<MeshletMesh> mMeshes;
    std::vector<MeshInstance> mInstances;
    UINT mTotalMeshlets = 0;
    UINT mTotalVertices = 0;
    UINT mTotalIndices = 0;
    UINT mTotalTriangles = 0;

    CullingStats mCullingStats = {};
    bool mMeshShadersSupported = false;
    bool mUseMeshShaders = true;  // Toggle for mesh shader vs fallback
    bool mShowMeshletColors = true;  // Toggle meshlet color visualization
};

// GPU structures matching HLSL
struct GPUVertex
{
    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT2 TexCoord;
    DirectX::XMFLOAT3 Tangent;
    float Padding;
};

struct GPUMeshlet
{
    uint32_t VertexOffset;
    uint32_t VertexCount;
    uint32_t PrimitiveOffset;
    uint32_t PrimitiveCount;
};

struct GPUMeshletBounds
{
    DirectX::XMFLOAT3 Center;
    float Radius;
    DirectX::XMFLOAT3 ConeAxis;
    float ConeCutoff;
    DirectX::XMFLOAT3 ConeApex;
    float Padding;
};

struct GPUInstance
{
    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 InvTransposeWorld;
    uint32_t MeshIndex;
    uint32_t MaterialIndex;
    uint32_t Padding[2];
};

struct MeshShaderPassConstants
{
    DirectX::XMFLOAT4X4 View;
    DirectX::XMFLOAT4X4 Proj;
    DirectX::XMFLOAT4X4 ViewProj;
    DirectX::XMFLOAT4X4 InvView;
    DirectX::XMFLOAT3 EyePosW;
    float Padding1;
    DirectX::XMFLOAT2 RenderTargetSize;
    DirectX::XMFLOAT2 InvRenderTargetSize;
    DirectX::XMFLOAT4 FrustumPlanes[6];
    float LODScale;
    UINT MeshletCount;
    UINT InstanceCount;
    UINT ShowMeshletColors;  // 1 = show colors, 0 = solid color
    UINT UseTexture;         // 1 = use diffuse texture, 0 = use meshlet colors
    UINT Padding2[3];
};
