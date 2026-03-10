//***************************************************************************************
// DirectStorageLoader.h - Fast asset loading (DirectStorage optional)
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"
#include <string>
#include <vector>
#include <queue>
#include <functional>

#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
#include <dstorage.h>
#pragma comment(lib, "dstorage.lib")
#endif

class DirectStorageLoader
{
public:
    DirectStorageLoader() = default;
    ~DirectStorageLoader();

    bool Initialize(ID3D12Device* device);
    void Shutdown();

    static bool IsAvailable();

    bool LoadFileToMemory(
        const std::wstring& filename,
        std::vector<uint8_t>& outData);

    bool LoadFileToGPUBuffer(
        const std::wstring& filename,
        ID3D12Resource** outBuffer,
        UINT64* outSize);

    void LoadFileAsync(
        const std::wstring& filename,
        std::function<void(const std::vector<uint8_t>&)> callback);

    void ProcessCompletedRequests();
    void WaitForAll();
    UINT GetPendingCount() const { return mPendingCount; }

private:
    ID3D12Device* mDevice = nullptr;
    UINT mPendingCount = 0;
    bool mInitialized = false;

#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
    Microsoft::WRL::ComPtr<IDStorageFactory> mFactory;
    Microsoft::WRL::ComPtr<IDStorageQueue> mQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;
    HANDLE mFenceEvent = nullptr;
#endif

    struct AsyncRequest
    {
        std::vector<uint8_t> data;
        std::function<void(const std::vector<uint8_t>&)> callback;
#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
        UINT64 fenceValue;
#endif
    };
    std::queue<AsyncRequest> mPendingRequests;
};
