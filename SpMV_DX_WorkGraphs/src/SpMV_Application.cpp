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

#include "SpMV_Application.h"
#include "MathHelpers.h"

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <iostream>
#include <fstream>
#include <sstream>

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

//modified by mun_ahmd: quick and dirty
class __TransferCommandListHelper {
public:
    static __TransferCommandListHelper& Instance(ID3D12Device* device) {
        // constructed once, destroyed at program exit 
        static __TransferCommandListHelper instance(device); // constructed once, destroyed at program exit
        //check if existing instance has same device as the one passed (ignore in case of nullptr)
        if (device && (instance.creation_device != device)) {
            throw std::runtime_error("Creation device and used device do not match");
        }
        return instance;
    }

    ~__TransferCommandListHelper() {
        // cleanup code here
        CloseHandle(fenceEvent);
    }

    ComPtr<ID3D12GraphicsCommandList10> CreateCommandList(ID3D12Device* device)
    {
        // Create a new command list for each call
        ComPtr<ID3D12GraphicsCommandList10> commandList;
        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)));

        //IDK Why you would do the below option
        // Close immediately so caller can Reset() when recording
        // ThrowIfFailed(commandList->Close());

        return commandList;
    }

    void submitAndWaitIdle(ID3D12GraphicsCommandList10* commandList) {
        // cmdList = recorded upload commands
        commandList->Close();

        // Submit to the copy queue
        ID3D12CommandList* ppCommandLists[] = { commandList };
        commandQueue->ExecuteCommandLists(1, ppCommandLists);

        //wait on fence
        fenceValue++;
        ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValue));
        if (fence->GetCompletedValue() < fenceValue) {
            fence->SetEventOnCompletion(fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    void copyIntoReadbackBuffer(ID3D12Device* device, ComPtr<ID3D12Resource> srcBuffer, ComPtr<ID3D12Resource> dstReadbackBuffer) {
        auto commandList = this->CreateCommandList(device);
        {
            //assumes srcBuffer is like that
            std::array<D3D12_RESOURCE_BARRIER, 1> preBarriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(srcBuffer.Get(),
                                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                     D3D12_RESOURCE_STATE_COPY_SOURCE),
            };
            commandList->ResourceBarrier(preBarriers.size(), preBarriers.data());
        }

        {
            // Copy GPU output to readback buffer
            commandList->CopyResource(dstReadbackBuffer.Get(), srcBuffer.Get());
        }

        {
            //assumes srcBuffer is like that
            std::array<D3D12_RESOURCE_BARRIER, 1> postBarriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(srcBuffer.Get(),
                                                     D3D12_RESOURCE_STATE_COPY_SOURCE,
                                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            };
            commandList->ResourceBarrier(postBarriers.size(), postBarriers.data());
        }
        this->submitAndWaitIdle(commandList.Get());
    }

    template<typename T>
    std::vector<T> mapAndReadBuffer(ComPtr<ID3D12Resource> srcBuffer, CD3DX12_RANGE readRange) {
        //todo create version that can accept vector of size as input
        T* readPtr = nullptr;
        ThrowIfFailed(srcBuffer->Map(0, &readRange, reinterpret_cast<void**>(&readPtr)));
        assert((readRange.End - readRange.Begin) % sizeof(T) == 0 && "Weird range passed to copy!");
        std::vector<T> cpuData(readPtr, readPtr + ((readRange.End - readRange.Begin) / sizeof(T)));
        srcBuffer->Unmap(0, nullptr);
        return cpuData;
    }

    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent;
    ID3D12Device* creation_device = nullptr;

private:
    __TransferCommandListHelper(ID3D12Device* device) {
        // Create a command allocator once
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocator)));

        // Create a command queue once
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;       // DIRECT, COMPUTE, or COPY
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;   // NORMAL (default) or HIGH
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;      // NONE or DISABLE_GPU_TIMEOUT
        desc.NodeMask = 0;          // single GPU node
        ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

        //Create Fence
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        this->creation_device = device;

        //Init Complete => Do not save device or take ComPtr
    };

    __TransferCommandListHelper(const __TransferCommandListHelper&) = delete;
    __TransferCommandListHelper& operator=(const __TransferCommandListHelper&) = delete;
};

static SpMV::SpMVData LoadCSRBinFile(const std::string& csrbinFilePath) {
    SpMV::SpMVData mat;
    std::ifstream file(csrbinFilePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file " + csrbinFilePath);
    }

    // 1. Read dimensions
    file.read(reinterpret_cast<char*>(&mat.nrows), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&mat.ncols), sizeof(uint32_t));

    // 2. Read nnz
    file.read(reinterpret_cast<char*>(&mat.nnz), sizeof(uint32_t));

    // 3. Read indptr
    mat.rowPtr.resize(mat.nrows + 1);
    file.read(reinterpret_cast<char*>(mat.rowPtr.data()), (mat.nrows + 1) * sizeof(uint32_t));

    // 4. Read indices
    mat.colIdx.resize(mat.nnz);
    file.read(reinterpret_cast<char*>(mat.colIdx.data()), mat.nnz * sizeof(uint32_t));

    // 5. Read data
    mat.values.resize(mat.nnz);
    file.read(reinterpret_cast<char*>(mat.values.data()), mat.nnz * sizeof(float));

    // 6. Read random_vector
    mat.random_vector.resize(mat.ncols);
    file.read(reinterpret_cast<char*>(mat.random_vector.data()), mat.ncols * sizeof(float));

    // 7. Read mult_result
    mat.mult_result.resize(mat.nrows);
    file.read(reinterpret_cast<char*>(mat.mult_result.data()), mat.nrows * sizeof(float));

    return mat;
}

SpMV_Application::SpMV_Application(const Options& options)
{
    window_ = std::make_unique<Window>(options.title, options.windowWidth, options.windowHeight);
    device_ =
        std::make_unique<Device>(options.forceWarpAdapter, options.enableDebugLayer, options.enableGpuValidationLayer);
    swapchain_ = std::make_unique<Swapchain>(device_.get(), window_.get());
    spmv_ = std::make_unique<SpMV>();
    device_->WaitForDevice();
    spmv_->init(device_->GetDevice(), std::make_unique<ShaderCompiler>(device_.get()));
}

SpMV_Application::~SpMV_Application()
{
}

void SpMV_Application::Run()
{
    
    //set the active data
    auto* commandList = device_->GetNextFrameCommandList();
    spmv_->setActiveSpMV(device_->GetDevice(), commandList, LoadCSRBinFile("./matrices/1138_bus.csrbin"));
    device_->ExecuteCurrentFrameCommandList();
    device_->WaitForDevice();
    //the below line of code is a terrible secret
    SpMV::theForbiddenUploadBufferVector.clear();

    do {
        // Check if resize is needed
        if ((window_->GetWidth() != swapchain_->GetWidth()) ||  //
            (window_->GetHeight() != swapchain_->GetHeight()))
        {
            // Resize swapchain
            OnResize(window_->GetWidth(), window_->GetHeight());
        }

        // Render window
        commandList = device_->GetNextFrameCommandList();
        const auto renderTarget = swapchain_->GetNextRenderTarget();        
        device_->ExecuteCurrentFrameCommandList();
        device_->WaitForDevice();

        // Perform SpMV compute
        commandList = device_->GetNextFrameCommandList();
        spmv_->runGraph(commandList);
        device_->ExecuteCurrentFrameCommandList();
        device_->WaitForDevice();

        // Copy results to cpu
        commandList = device_->GetNextFrameCommandList();
        spmv_->copyResults(commandList);
        device_->ExecuteCurrentFrameCommandList();
        device_->WaitForDevice();

        {
            auto results = spmv_->getResultsOnCPU();
            results.clear();
        }

        swapchain_->Present(vsync_);
    } while (window_->HandleEvents());

    device_->WaitForDevice();
}

void SpMV_Application::OnResize(std::uint32_t width, std::uint32_t height)
{
    // If window is minimized, size is set to 0x0, thus we ignore this resize and render to the old resolution instead.
    if ((width == 0) || (height == 0)) {
        return;
    }

    // Wait for all frames in flight
    device_->WaitForDevice();

    swapchain_->Resize(width, height);
}