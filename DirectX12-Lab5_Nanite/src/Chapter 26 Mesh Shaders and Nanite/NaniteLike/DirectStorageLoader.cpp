//***************************************************************************************
// DirectStorageLoader.cpp - DirectStorage implementation for fast asset loading
//***************************************************************************************

#include "DirectStorageLoader.h"
#include <fstream>

using Microsoft::WRL::ComPtr;

DirectStorageLoader::~DirectStorageLoader()
{
    Shutdown();
}

bool DirectStorageLoader::IsAvailable()
{
    ComPtr<IDStorageFactory> factory;
    HRESULT hr = DStorageGetFactory(IID_PPV_ARGS(&factory));
    return SUCCEEDED(hr);
}

bool DirectStorageLoader::Initialize(ID3D12Device* device)
{
    if (mInitialized)
        return true;

    mDevice = device;

    // Get DirectStorage factory
    HRESULT hr = DStorageGetFactory(IID_PPV_ARGS(&mFactory));
    if (FAILED(hr))
    {
        OutputDebugStringA("DirectStorage not available, using fallback loading\n");
        return false;
    }

    // Create queue for file operations
    DSTORAGE_QUEUE_DESC queueDesc = {};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDesc.Device = device;

    hr = mFactory->CreateQueue(&queueDesc, IID_PPV_ARGS(&mQueue));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create DirectStorage queue\n");
        return false;
    }

    // Create fence for synchronization
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create fence for DirectStorage\n");
        return false;
    }

    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!mFenceEvent)
    {
        OutputDebugStringA("Failed to create fence event\n");
        return false;
    }

    mInitialized = true;
    OutputDebugStringA("DirectStorage initialized successfully!\n");
    return true;
}

void DirectStorageLoader::Shutdown()
{
    if (mFenceEvent)
    {
        WaitForAll();
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
    mQueue.Reset();
    mFactory.Reset();
    mFence.Reset();
    mInitialized = false;
}

bool DirectStorageLoader::LoadFileToMemory(
    const std::wstring& filename,
    std::vector<uint8_t>& outData)
{
    if (!mInitialized)
    {
        // Fallback to standard file loading
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return false;

        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        outData.resize(fileSize);
        file.read(reinterpret_cast<char*>(outData.data()), fileSize);
        file.close();
        return true;
    }

    // Open file with DirectStorage
    ComPtr<IDStorageFile> dsFile;
    HRESULT hr = mFactory->OpenFile(filename.c_str(), IID_PPV_ARGS(&dsFile));
    if (FAILED(hr))
    {
        OutputDebugStringA("DirectStorage: Failed to open file\n");
        return false;
    }

    // Get file info
    BY_HANDLE_FILE_INFORMATION fileInfo;
    hr = dsFile->GetFileInformation(&fileInfo);
    if (FAILED(hr))
        return false;

    UINT64 fileSize = (static_cast<UINT64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    outData.resize(static_cast<size_t>(fileSize));

    // Create request
    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Source.File.Source = dsFile.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = static_cast<UINT32>(fileSize);
    request.Destination.Memory.Buffer = outData.data();
    request.Destination.Memory.Size = static_cast<UINT32>(fileSize);
    request.UncompressedSize = static_cast<UINT32>(fileSize);

    mQueue->EnqueueRequest(&request);

    // Signal and wait
    mFenceValue++;
    mQueue->EnqueueSignal(mFence.Get(), mFenceValue);
    mQueue->Submit();

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }

    // Check for errors
    DSTORAGE_ERROR_RECORD errorRecord = {};
    mQueue->RetrieveErrorRecord(&errorRecord);
    if (FAILED(errorRecord.FirstFailure.HResult))
    {
        OutputDebugStringA("DirectStorage: Read failed\n");
        return false;
    }

    return true;
}


bool DirectStorageLoader::LoadFileToGPUBuffer(
    const std::wstring& filename,
    ID3D12Resource** outBuffer,
    UINT64* outSize)
{
    if (!mInitialized || !mDevice)
        return false;

    // Open file
    ComPtr<IDStorageFile> dsFile;
    HRESULT hr = mFactory->OpenFile(filename.c_str(), IID_PPV_ARGS(&dsFile));
    if (FAILED(hr))
        return false;

    // Get file size
    BY_HANDLE_FILE_INFORMATION fileInfo;
    hr = dsFile->GetFileInformation(&fileInfo);
    if (FAILED(hr))
        return false;

    UINT64 fileSize = (static_cast<UINT64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    *outSize = fileSize;

    // Create GPU buffer
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(fileSize);

    hr = mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(outBuffer));

    if (FAILED(hr))
        return false;

    // Create request to load directly to GPU
    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Source.File.Source = dsFile.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = static_cast<UINT32>(fileSize);
    request.Destination.Buffer.Resource = *outBuffer;
    request.Destination.Buffer.Offset = 0;
    request.Destination.Buffer.Size = static_cast<UINT32>(fileSize);
    request.UncompressedSize = static_cast<UINT32>(fileSize);

    mQueue->EnqueueRequest(&request);

    // Signal and wait
    mFenceValue++;
    mQueue->EnqueueSignal(mFence.Get(), mFenceValue);
    mQueue->Submit();

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }

    // Check errors
    DSTORAGE_ERROR_RECORD errorRecord = {};
    mQueue->RetrieveErrorRecord(&errorRecord);
    if (FAILED(errorRecord.FirstFailure.HResult))
    {
        (*outBuffer)->Release();
        *outBuffer = nullptr;
        return false;
    }

    return true;
}

void DirectStorageLoader::LoadFileAsync(
    const std::wstring& filename,
    std::function<void(const std::vector<uint8_t>&)> callback)
{
    AsyncRequest req;
    req.callback = callback;

    if (!mInitialized)
    {
        // Fallback - load synchronously
        LoadFileToMemory(filename, req.data);
        callback(req.data);
        return;
    }

    // Open file
    ComPtr<IDStorageFile> dsFile;
    HRESULT hr = mFactory->OpenFile(filename.c_str(), IID_PPV_ARGS(&dsFile));
    if (FAILED(hr))
    {
        callback(std::vector<uint8_t>());
        return;
    }

    BY_HANDLE_FILE_INFORMATION fileInfo;
    dsFile->GetFileInformation(&fileInfo);
    UINT64 fileSize = (static_cast<UINT64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;

    req.data.resize(static_cast<size_t>(fileSize));

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Source.File.Source = dsFile.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = static_cast<UINT32>(fileSize);
    request.Destination.Memory.Buffer = req.data.data();
    request.Destination.Memory.Size = static_cast<UINT32>(fileSize);
    request.UncompressedSize = static_cast<UINT32>(fileSize);

    mQueue->EnqueueRequest(&request);

    mFenceValue++;
    req.fenceValue = mFenceValue;
    mQueue->EnqueueSignal(mFence.Get(), mFenceValue);
    mQueue->Submit();

    mPendingRequests.push(std::move(req));
    mPendingCount++;
}

void DirectStorageLoader::ProcessCompletedRequests()
{
    if (!mInitialized || mPendingRequests.empty())
        return;

    UINT64 completedValue = mFence->GetCompletedValue();

    while (!mPendingRequests.empty())
    {
        auto& req = mPendingRequests.front();
        if (req.fenceValue <= completedValue)
        {
            req.callback(req.data);
            mPendingRequests.pop();
            mPendingCount--;
        }
        else
        {
            break;
        }
    }
}

void DirectStorageLoader::WaitForAll()
{
    if (!mInitialized)
        return;

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }

    ProcessCompletedRequests();
}
