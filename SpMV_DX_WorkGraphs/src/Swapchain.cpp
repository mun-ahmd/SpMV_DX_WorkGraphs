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

#include "Swapchain.h"

Swapchain::Swapchain(const Device* device, const Window* window) : device_(device)
{
    width_  = window->GetWidth();
    height_ = window->GetHeight();

    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};

    swapchainDesc.Width              = width_;
    swapchainDesc.Height             = height_;
    swapchainDesc.Format             = ColorTargetFormat;
    swapchainDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.BufferCount        = BackbufferCount;
    swapchainDesc.SampleDesc.Count   = 1;
    swapchainDesc.SampleDesc.Quality = 0;
    swapchainDesc.Scaling            = DXGI_SCALING_STRETCH;
    swapchainDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
    swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapchainDesc = {0};
    fsSwapchainDesc.Windowed                        = true;

    auto* const factory      = device->GetDXGIFactory();
    auto* const commandQueue = device->GetCommandQueue();
    const auto  windowHandle = window->GetHandle();

    ComPtr<IDXGISwapChain1> swapchain1;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        commandQueue, windowHandle, &swapchainDesc, &fsSwapchainDesc, nullptr, &swapchain1));

    // Query Swapchain3 interface
    ThrowIfFailed(swapchain1->QueryInterface(IID_PPV_ARGS(&swapchain_)));

    swapchain_->SetMaximumFrameLatency(BackbufferCount);
    swapchainWaitableObject_ = swapchain_->GetFrameLatencyWaitableObject();

    factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);

    auto* const d3dDevice = device->GetDevice();

    // Create RTV descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors             = BackbufferCount;
        desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask                   = 1;
        ThrowIfFailed(device->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescriptorHeap_)));

        const auto descriptorSize =
            device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (std::uint32_t index = 0; index < BackbufferCount; ++index) {
            colorTargets_[index].descriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
                rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), index, descriptorSize);
        }
    }

    // Create DSV descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.NumDescriptors             = 1;
        desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask                   = 1;
        ThrowIfFailed(device->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&dsvDescriptorHeap_)));

        depthDescriptorHandle_ = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    }

    PrepareRenderTargets();
}

Swapchain::RenderTarget Swapchain::GetNextRenderTarget()
{
    // Wait for swapchain biffer
    WaitForSingleObject(swapchainWaitableObject_, INFINITE);

    const auto  backbufferIndex = swapchain_->GetCurrentBackBufferIndex();
    const auto& colorTarget     = colorTargets_[backbufferIndex];

    return RenderTarget{
        .colorResource         = colorTarget.resource.Get(),
        .colorDescriptorHandle = colorTarget.descriptorHandle,
        .depthResource         = depthResource_.Get(),
        .depthDescriptorHandle = depthDescriptorHandle_,
    };
}

void Swapchain::Present(const bool vsync)
{
    if (vsync) {
        ThrowIfFailed(swapchain_->Present(1, 0));
    } else {
        ThrowIfFailed(swapchain_->Present(0, DXGI_PRESENT_ALLOW_TEARING));
    }
}

void Swapchain::Resize(std::uint32_t width, std::uint32_t height)
{
    // Release current resources
    for (std::uint32_t index = 0; index < BackbufferCount; ++index) {
        colorTargets_[index].resource.Reset();
    }
    depthResource_.Reset();

    // Update width & height
    width_  = width;
    height_ = height;

    // Resize swapchain
    swapchain_->ResizeBuffers(BackbufferCount,
                              width,
                              height,
                              ColorTargetFormat,
                              DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

    PrepareRenderTargets();
}

std::uint32_t Swapchain::GetWidth() const
{
    return width_;
}

std::uint32_t Swapchain::GetHeight() const
{
    return height_;
}

void Swapchain::PrepareRenderTargets()
{
    // Fetch color targets & create color render target views
    for (std::uint32_t index = 0; index < BackbufferCount; ++index) {
        ComPtr<ID3D12Resource> resource;

        ThrowIfFailed(swapchain_->GetBuffer(index, IID_PPV_ARGS(&resource)));
        device_->GetDevice()->CreateRenderTargetView(resource.Get(), nullptr, colorTargets_[index].descriptorHandle);

        colorTargets_[index].resource = resource;
    }

    //  Create depth buffer & create depth-stencil view
    {
        D3D12_CLEAR_VALUE clearValue    = {};
        clearValue.Format               = DepthTargetFormat;
        clearValue.DepthStencil.Depth   = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC   resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DepthTargetFormat, width_, height_, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
                                                                    D3D12_HEAP_FLAG_NONE,
                                                                    &resourceDesc,
                                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                    &clearValue,
                                                                    IID_PPV_ARGS(&depthResource_)));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                        = DepthTargetFormat;
        dsvDesc.ViewDimension                 = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags                         = D3D12_DSV_FLAG_NONE;
        device_->GetDevice()->CreateDepthStencilView(
            depthResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
    }
}
