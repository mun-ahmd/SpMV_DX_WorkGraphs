// This file is part of the AMD & HSC Work Graph Playground.
//
// Copyright (C) 2025 Advanced Micro Devices, Inc. and Coburg University of Applied Sciences and Arts.
// All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "Device.h"

#include <codecvt>
#include <iostream>
#include <locale>
#include <sstream>
#include <system_error>

// Declarations for Microsoft.D3D.D3D12 Agility SDK NuGet package.
// D3D12SDKVersion needs to be updated if newer NuGet package is used.
extern "C" {
#ifdef ENABLE_MESH_NODES
__declspec(dllexport) extern const unsigned int D3D12SDKVersion = 715;
#else
__declspec(dllexport) extern const unsigned int D3D12SDKVersion = 613;
#endif
}
extern "C" {
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\";
}

void ThrowIfFailed(const HRESULT hr)
{
    if (FAILED(hr)) {
        const auto cond = std::system_category().default_error_condition(hr);

        std::stringstream stream;
        stream << "Operation Failed: " << cond.category().name() <<      //
            " (" << cond.value() << ") (" << std::hex << hr << "): " <<  //
            cond.message();

        throw std::runtime_error(stream.str());
    }
}

Device::Device(const bool forceWarpAdapter, const bool enableDebugLayer, const bool enableGpuValidationLayer)
{
#ifdef ENABLE_MESH_NODES
    {
        // Enable experimental D3D12 features for mesh nodes
        std::array<UUID, 2> meshNodesExperimentalFeatures = {D3D12ExperimentalShaderModels,
                                                             D3D12StateObjectsExperiment};
        const HRESULT result = D3D12EnableExperimentalFeatures(static_cast<UINT>(meshNodesExperimentalFeatures.size()),
                                                               meshNodesExperimentalFeatures.data(),
                                                               nullptr,
                                                               nullptr);

        if (FAILED(result)) {
            throw std::runtime_error(
                "Failed to enable D3D12 experimental features for mesh nodes. "
                "Please check if the Windows Developer Mode is enabled. "
                "See readme.md#using-mesh-nodes for more details.\n"
                "If you cannot or do not want to enable Windows Developer Mode, "
                "please use or build a non-mesh-nodes version of the app.");
        }
    }
#endif

    CreateDXGIFactory(enableDebugLayer, enableGpuValidationLayer);

    if (forceWarpAdapter) {
        ComPtr<IDXGIAdapter1> adapter;
        dxgiFactory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter));

        device_ = CreateDevice(adapter.Get());
    } else {
        // Try to find suitable adapter, fallback to WARP
        for (std::uint32_t adapterId = 0; true; ++adapterId) {
            ComPtr<IDXGIAdapter1> adapter;

            if (dxgiFactory_->EnumAdapterByGpuPreference(
                    adapterId, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
            {
                // No more adapters to check
                break;
            }

            device_ = CreateDevice(adapter.Get());

            // End search if adapter creation was successful.
            if (device_) {
                break;
            }
        }
    }

    // Check if an adapter was found
    if (!device_) {
        throw std::runtime_error("No device with work graphs support was found.");
    }

    do {
        // Query adapter via LUID
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(dxgiFactory_->EnumAdapterByLuid(device_->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
            continue;
        }

        // Query DXGI_ADAPTER_DESC1
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc))) {
            continue;
        }

        softwareAdapter_ = desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE;

        // Convert adapter description to std::string
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        adapterDescription_ = converter.to_bytes(desc.Description);
    } while (false);

    if (enableDebugLayer) {
        // Register callback to print D3D12 debug messages to std::cout
        RegisterDebugMessageCallback();
    }

    // Create D3D12 resources (queue, command lists)
    CreateDeviceResources();
}

void Device::WaitForDevice()
{
    // Increment signaled value and set fence
    signaledFenceValue_++;
    commandQueue_->Signal(fence_.Get(), signaledFenceValue_);

    // Fence is already signaled
    if (fence_->GetCompletedValue() >= signaledFenceValue_) {
        return;
    }

    fence_->SetEventOnCompletion(signaledFenceValue_, fenceEvent_);
    WaitForSingleObject(fenceEvent_, INFINITE);
}

ID3D12GraphicsCommandList10* Device::GetNextFrameCommandList()
{
    // Increment frame index to next frame
    frameIndex_ = (frameIndex_ + 1) % BufferedFramesCount;

    const auto& frameContext = frameContexts_[frameIndex_];

    // Only wait if frame context has been signaled and
    // if fence does not have the signaled value yet.
    if ((frameContext.waitFenceValue != 0) &&  //
        (fence_->GetCompletedValue() < frameContext.waitFenceValue))
    {
        fence_->SetEventOnCompletion(frameContext.waitFenceValue, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    ThrowIfFailed(frameContext.commandAllocator->Reset());
    ThrowIfFailed(frameContext.commandList->Reset(frameContext.commandAllocator.Get(), nullptr));

    return frameContext.commandList.Get();
}

void Device::ExecuteCurrentFrameCommandList()
{
    auto& frameContext = frameContexts_[frameIndex_];

    // Close command list
    ThrowIfFailed(frameContext.commandList->Close());

    // Submit command list
    commandQueue_->ExecuteCommandLists(
        1, reinterpret_cast<ID3D12CommandList* const*>(frameContext.commandList.GetAddressOf()));

    // Incrment signaled fence value & signale fence
    signaledFenceValue_++;
    commandQueue_->Signal(fence_.Get(), signaledFenceValue_);

    // Store fence value to frame context
    frameContext.waitFenceValue = signaledFenceValue_;
}

IDXGIFactory4* Device::GetDXGIFactory() const
{
    return dxgiFactory_.Get();
}

ID3D12Device9* Device::GetDevice() const
{
    return device_.Get();
}

ID3D12CommandQueue* Device::GetCommandQueue() const
{
    return commandQueue_.Get();
}

bool Device::IsSoftwareAdapter() const
{
    return softwareAdapter_;
}

const std::string& Device::GetAdapterDescription() const
{
    return adapterDescription_;
}

D3D12_WORK_GRAPHS_TIER Device::GetSupportedWorkGraphsTier() const
{
    // Call internal helper with current device
    return GetSupportedWorkGraphsTier(device_.Get());
}

void Device::CreateDXGIFactory(bool enableDebugLayer, bool enableGpuValidationLayer)
{
    if (enableDebugLayer) {
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.

        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        } else {
            OutputDebugString(TEXT("WARNING: Direct3D Debug Device is not available\n"));
        }

        ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue)))) {
            ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgiFactory_)));

            dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
        }

        if (enableGpuValidationLayer) {
            ComPtr<ID3D12Debug1> debugController1;
            if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)))) {
                debugController1->SetEnableGPUBasedValidation(true);
            } else {
                OutputDebugString(TEXT("WARNING: Direct3D Debug Device for GPU based validation is not available\n"));
            }
        }
    }

    if (!dxgiFactory_) {
        // Fallback if enabling debug layer did not work, or debug layer is disabled
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_)));
    }
}

ComPtr<ID3D12Device9> Device::CreateDevice(IDXGIAdapter1* adapter) const
{
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(adapter->GetDesc1(&desc))) {
        std::cout << "Could not get adapter description for adapter." << std::endl;
        return {};
    }

    std::wcout << "Testing adapter \"" << desc.Description << "\": ";

    ComPtr<ID3D12Device9> device;

    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device)))) {
        std::cout << "Failed to create D3D12 device." << std::endl;

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            std::cout
                << "WARP adapter does not support D3D feature level 12.2 and work graphs.\n"
                   " See readme.md#running-on-gpus-without-work-graphs-support for instructions on installing latest "
                   "WARP adapter or run DownloadWarpAdapter.bat if you are using pre-built binaries."
                << std::endl;
        }

        return {};
    }

    if (GetSupportedWorkGraphsTier(device.Get()) == D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED) {
        std::cout << "Device does not support work graphs." << std::endl;

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            std::cout
                << "WARP adapter does not support work graphs.\n"
                   " See readme.md#running-on-gpus-without-work-graphs-support for instructions on installing latest "
                   "WARP adapter or run DownloadWarpAdapter.bat if you are using pre-built binaries."
                << std::endl;
        }

        return {};
    }

    // Adapter does support work graphs.
    std::cout << "Device supports work graphs." << std::endl;

    return device;
}

D3D12_WORK_GRAPHS_TIER Device::GetSupportedWorkGraphsTier(ID3D12Device9* device) const
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options = {};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &options, sizeof(options)))) {
        return D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;
    }

    return options.WorkGraphsTier;
}

void Device::CreateDeviceResources()
{
    // Create graphics command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags                    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type                     = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue_)));

    for (auto& frameContext : frameContexts_) {
        ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_PPV_ARGS(&frameContext.commandAllocator)));
        ThrowIfFailed(device_->CreateCommandList(0,
                                                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 frameContext.commandAllocator.Get(),
                                                 nullptr,
                                                 IID_PPV_ARGS(&frameContext.commandList)));

        // Close all created command lists
        ThrowIfFailed(frameContext.commandList->Close());
    }

    // Create wait fence & event
    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence_)));
    fenceEvent_ = CreateEventA(nullptr, false, false, nullptr);
}

void Device::RegisterDebugMessageCallback()
{
    static const auto callback = [](D3D12_MESSAGE_CATEGORY category,
                                    D3D12_MESSAGE_SEVERITY severity,
                                    D3D12_MESSAGE_ID,
                                    LPCSTR description,
                                    void*  context) {
        if (severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || severity == D3D12_MESSAGE_SEVERITY_ERROR) {
            std::cout << "[D3D12] " << description << std::endl;
        }
    };

    ComPtr<ID3D12InfoQueue1> infoQueue;
    if (SUCCEEDED(device_.As(&infoQueue))) {
        DWORD callbackCookie;
        infoQueue->RegisterMessageCallback(callback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, nullptr, &callbackCookie);
    }
}
