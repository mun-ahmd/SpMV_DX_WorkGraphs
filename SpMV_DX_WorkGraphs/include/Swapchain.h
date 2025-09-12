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

#include "Device.h"
#include "Window.h"

class Swapchain {
public:
    static constexpr std::uint32_t BackbufferCount   = 3;
    static constexpr auto          ColorTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr auto          DepthTargetFormat = DXGI_FORMAT_D32_FLOAT;

    struct RenderTarget {
        ComPtr<ID3D12Resource>      colorResource;
        D3D12_CPU_DESCRIPTOR_HANDLE colorDescriptorHandle;
        ComPtr<ID3D12Resource>      depthResource;
        D3D12_CPU_DESCRIPTOR_HANDLE depthDescriptorHandle;
    };

    Swapchain(const Device* device, const Window* window);

    RenderTarget GetNextRenderTarget();
    void         Present(bool vsync = true);

    void Resize(std::uint32_t width, std::uint32_t height);

    std::uint32_t GetWidth() const;
    std::uint32_t GetHeight() const;

private:
    void PrepareRenderTargets();

    std::uint32_t width_;
    std::uint32_t height_;

    const Device* device_;

    ComPtr<IDXGISwapChain3> swapchain_;
    HANDLE                  swapchainWaitableObject_;

    struct FrameResources {
        ComPtr<ID3D12Resource>      resource;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle;
    };

    ComPtr<ID3D12DescriptorHeap>                rtvDescriptorHeap_;
    std::array<FrameResources, BackbufferCount> colorTargets_;

    ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
    ComPtr<ID3D12Resource>       depthResource_;
    D3D12_CPU_DESCRIPTOR_HANDLE  depthDescriptorHandle_;
};