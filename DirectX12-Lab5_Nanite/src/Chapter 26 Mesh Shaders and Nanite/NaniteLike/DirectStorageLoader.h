//***************************************************************************************
// DirectStorageLoader.h - Fast asset loading using DirectStorage API
//***************************************************************************************

#pragma once

#include "../../Common/d3dUtil.h"
#include <dstorage.h>
#include <string>
#include <vector>
#include <queue>
#include <functional>

#pragma comment(lib, "dstorage.lib")

class DirectStorageLoader
{
public:
    DirectStorageLoader() = default;
    ~DirectStorageLoader();

    bool Initialize(ID3D12Device* device);
    void Shutdown();

    static bool IsAvailable();

    // Load file into CPU memory
    bool LoadFileToMemory(
        const std::wstring& filename,
        std::vector<uint8_t>& outData);

    // Load file directly to GPU buffer
    bool LoadFileToGPUBuffer(
        const std::wstring& filename,
        ID3D12Resource** outBuffer,
        UINT64* outSize);

    // Async loading with callback
    void LoadFileAsync(
        const std::wstring& filename,
        std::function<void(const std::vector<uint8_t>&)> callback);

    void ProcessCompletedRequests();
    void WaitForAll();
    UINT GetPendingCount() const { return mPendingCount; }

private:
    ID3D12Device* mDevice = nullptr;
    Microsoft::WRL::ComPtr<IDStorageFactory> mFactory;
    Microsoft::WRL::ComPtr<IDStorageQueue> mQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;
    HANDLE mFenceEvent = nullptr;
    UINT mPendingCount = 0;
    bool mInitialized = false;

    struct AsyncRequest
    {
        std::vector<uint8_t> data;
        std::function<void(const std::vector<uint8_t>&)> callback;
        UINT64 fenceValue;
    };
    std::queue<AsyncRequest> mPendingRequests;
};
