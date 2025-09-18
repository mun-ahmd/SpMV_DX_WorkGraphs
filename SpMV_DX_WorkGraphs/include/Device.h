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

#pragma once

#include <array>
#include <memory>

// Device.h is also the common header for all D3D12 & WRL headers
#include <d3dx12/d3dx12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

// List of annoying defines in Windows.h
#undef min
#undef max
#undef CreateWindow
#undef CreateFile
#undef GetMessage
#undef RGB
#undef Yield

// Helper for ComPtr
using Microsoft::WRL::ComPtr;

// Helpers for D3D12 methods
void ThrowIfFailed(HRESULT hr);

#include <string>
#include <Windows.h>

inline static std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), wide.data(), wideLen);
    return wide;
}

inline static std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) return {};
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}


class Device {
public:
    static constexpr std::uint32_t BufferedFramesCount = 3;

    Device(bool forceWarpAdapter, bool enableDebugLayer, bool enableGpuValidationLayer);

    void WaitForDevice();

    ID3D12GraphicsCommandList10* GetNextFrameCommandList();
    void                         ExecuteCurrentFrameCommandList();

    IDXGIFactory4*      GetDXGIFactory() const;
    ID3D12Device9*      GetDevice() const;
    ID3D12CommandQueue* GetCommandQueue() const;

    bool               IsSoftwareAdapter() const;
    const std::string& GetAdapterDescription() const;

    D3D12_WORK_GRAPHS_TIER GetSupportedWorkGraphsTier() const;

private:
    void                   CreateDXGIFactory(bool enableDebugLayer, bool enableGpuValidationLayer);
    ComPtr<ID3D12Device9>  CreateDevice(IDXGIAdapter1* adapter) const;
    D3D12_WORK_GRAPHS_TIER GetSupportedWorkGraphsTier(ID3D12Device9* device) const;
    void                   CreateDeviceResources();

    void RegisterDebugMessageCallback();

    ComPtr<IDXGIFactory6> dxgiFactory_;

    bool        softwareAdapter_    = false;
    std::string adapterDescription_ = "Unknown Adapter";

    ComPtr<ID3D12Device9>      device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;

    struct FrameContext {
        ComPtr<ID3D12CommandAllocator>      commandAllocator;
        ComPtr<ID3D12GraphicsCommandList10> commandList;
        std::uint64_t                       waitFenceValue = 0;
    };

    std::array<FrameContext, BufferedFramesCount> frameContexts_;
    std::uint32_t                                 frameIndex_;

    ComPtr<ID3D12Fence> fence_;
    HANDLE              fenceEvent_;
    std::uint64_t       signaledFenceValue_ = 0;
};