//***************************************************************************************
// DirectStorageLoader.cpp
//***************************************************************************************

#include "DirectStorageLoader.h"
#include <fstream>

#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
using Microsoft::WRL::ComPtr;
#endif

DirectStorageLoader::~DirectStorageLoader()
{
    Shutdown();
}

bool DirectStorageLoader::IsAvailable()
{
#if defined(NANITELIKE_NO_DIRECTSTORAGE)
    return false;
#else
    ComPtr<IDStorageFactory> factory;
    HRESULT hr = DStorageGetFactory(IID_PPV_ARGS(&factory));
    return SUCCEEDED(hr);
#endif
}

bool DirectStorageLoader::Initialize(ID3D12Device* device)
{
    if (mInitialized)
        return true;

    mDevice = device;

#if defined(NANITELIKE_NO_DIRECTSTORAGE)
    mInitialized = true;
    return true;
#else
    HRESULT hr = DStorageGetFactory(IID_PPV_ARGS(&mFactory));
    if (FAILED(hr))
    {
        OutputDebugStringA("DirectStorage not available, using fallback loading\n");
        mInitialized = true;
        return true;
    }

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

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
    if (FAILED(hr))
        return false;

    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!mFenceEvent)
        return false;

    mInitialized = true;
    OutputDebugStringA("DirectStorage initialized successfully!\n");
    return true;
#endif
}

void DirectStorageLoader::Shutdown()
{
#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
    if (mFenceEvent)
    {
        WaitForAll();
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
    mQueue.Reset();
    mFactory.Reset();
    mFence.Reset();
#endif
    mInitialized = false;
}

bool DirectStorageLoader::LoadFileToMemory(
    const std::wstring& filename,
    std::vector<uint8_t>& outData)
{
#if defined(NANITELIKE_NO_DIRECTSTORAGE)
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;
    size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    outData.resize(fileSize);
    file.read(reinterpret_cast<char*>(outData.data()), fileSize);
    return true;
#else
    if (!mInitialized)
    {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return false;
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        outData.resize(fileSize);
        file.read(reinterpret_cast<char*>(outData.data()), fileSize);
        return true;
    }

    ComPtr<IDStorageFile> dsFile;
    HRESULT hr = mFactory->OpenFile(filename.c_str(), IID_PPV_ARGS(&dsFile));
    if (FAILED(hr))
        return false;

    BY_HANDLE_FILE_INFORMATION fileInfo;
    hr = dsFile->GetFileInformation(&fileInfo);
    if (FAILED(hr))
        return false;

    UINT64 fileSize = (static_cast<UINT64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    outData.resize(static_cast<size_t>(fileSize));

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
    mFenceValue++;
    mQueue->EnqueueSignal(mFence.Get(), mFenceValue);
    mQueue->Submit();

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }

    DSTORAGE_ERROR_RECORD errorRecord = {};
    mQueue->RetrieveErrorRecord(&errorRecord);
    return SUCCEEDED(errorRecord.FirstFailure.HResult);
#endif
}

bool DirectStorageLoader::LoadFileToGPUBuffer(
    const std::wstring& filename,
    ID3D12Resource** outBuffer,
    UINT64* outSize)
{
#if defined(NANITELIKE_NO_DIRECTSTORAGE)
    (void)filename;
    (void)outBuffer;
    (void)outSize;
    return false;
#else
    if (!mInitialized || !mDevice)
        return false;

    ComPtr<IDStorageFile> dsFile;
    HRESULT hr = mFactory->OpenFile(filename.c_str(), IID_PPV_ARGS(&dsFile));
    if (FAILED(hr))
        return false;

    BY_HANDLE_FILE_INFORMATION fileInfo;
    hr = dsFile->GetFileInformation(&fileInfo);
    if (FAILED(hr))
        return false;

    UINT64 fileSize = (static_cast<UINT64>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    *outSize = fileSize;

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
    mFenceValue++;
    mQueue->EnqueueSignal(mFence.Get(), mFenceValue);
    mQueue->Submit();

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }

    DSTORAGE_ERROR_RECORD errorRecord = {};
    mQueue->RetrieveErrorRecord(&errorRecord);
    if (FAILED(errorRecord.FirstFailure.HResult))
    {
        (*outBuffer)->Release();
        *outBuffer = nullptr;
        return false;
    }
    return true;
#endif
}

void DirectStorageLoader::LoadFileAsync(
    const std::wstring& filename,
    std::function<void(const std::vector<uint8_t>&)> callback)
{
#if defined(NANITELIKE_NO_DIRECTSTORAGE)
    std::vector<uint8_t> data;
    LoadFileToMemory(filename, data);
    callback(data);
#else
    AsyncRequest req;
    req.callback = callback;

    if (!mInitialized)
    {
        LoadFileToMemory(filename, req.data);
        callback(req.data);
        return;
    }

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
#endif
}

void DirectStorageLoader::ProcessCompletedRequests()
{
#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
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
            break;
    }
#endif
}

void DirectStorageLoader::WaitForAll()
{
#if !defined(NANITELIKE_NO_DIRECTSTORAGE)
    if (!mInitialized)
        return;
    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }
    ProcessCompletedRequests();
#endif
}
