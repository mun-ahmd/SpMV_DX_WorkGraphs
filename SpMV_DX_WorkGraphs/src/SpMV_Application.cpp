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

static SpMV_Application::CSRMatrix LoadCSRBinFile(const std::string& csrbinFilePath) {
    SpMV_Application::CSRMatrix mat;
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

SpMV_Application::SpMV_Application(const Options& options) : workGraphTutorials_(options.tutorials)
{
    if (options.tutorial.has_value()) {
        // Init startup tutorial
        workGraphTutorial_ = *options.tutorial;
    }
    else if (!workGraphTutorials_.empty()) {
        // Init startup tutorial with first tutorial
        workGraphTutorial_ = workGraphTutorials_.front();
    }
    else {
        std::stringstream stream;
        stream << "No tutorials found. Please check to make sure at least one tutorial file is present in the "
            "following folders:\n";
        //modified by mun_ahmd: Remove tutorial folder stuff
        /*
        for (const auto& [tutorialFolder, _] : {TUTORIAL_FOLDER_LIST}) {
            stream << " - " << tutorialFolder << "\n";
        }
        */
        throw std::runtime_error(stream.str());
    }

    window_ = std::make_unique<Window>(options.title, options.windowWidth, options.windowHeight);
    device_ =
        std::make_unique<Device>(options.forceWarpAdapter, options.enableDebugLayer, options.enableGpuValidationLayer);
    swapchain_ = std::make_unique<Swapchain>(device_.get(), window_.get());

    // Create shader compiler with current device.
    // Shader compiler requires device to set shader defines for supported work graphs tier and adapter type.
    shaderCompiler_ = std::make_unique<ShaderCompiler>(device_.get());

    // Create ImGui context first, as writable backbuffer need ImGui descriptor heap
    CreateImGuiContext();

    CreateResourceDescriptorHeaps();
    //modified by mun_ahmd: todo set better limits on buffer size
    CreateSpMVBuffers(100000, 100000, 100000);
    CreateWritableBackbuffer(swapchain_->GetWidth(), swapchain_->GetHeight());
    CreateScratchBuffer();
    CreatePersistentScratchBuffer();

#ifdef ENABLE_MESH_NODES
    if (options.renderTargetSampleCount > 1) {
        CreateMsaaResources(swapchain_->GetWidth(), swapchain_->GetHeight(), options.renderTargetSampleCount, 0);
    }
#endif

    CreateFontBuffer();

    CreateWorkGraphRootSignature();
    CreateWorkGraph();

    UploadSpMV(LoadCSRBinFile("./matrices/1138_bus.csrbin"));
}

SpMV_Application::~SpMV_Application()
{
    DestroyImGuiContext();
}

void SpMV_Application::Run()
{
    do {
        // Check if resize is needed
        if ((window_->GetWidth() != swapchain_->GetWidth()) ||  //
            (window_->GetHeight() != swapchain_->GetHeight()))
        {
            // Resize swapchain
            OnResize(window_->GetWidth(), window_->GetHeight());
        }

        // Check if re-creation of work graph is required
        if (shaderCompiler_->CheckShaderSourceFiles()) {
            std::cout << "Changes to shader source files detected. Recompiling work graph..." << std::endl;
            // Recompile shaders & re-create work graph
            const bool success = CreateWorkGraph();

            if (success) {
                // Reset error message time
                errorMessageEndTime_ = std::chrono::high_resolution_clock::now();
            }
            else {
                using namespace std::chrono_literals;
                // Show error message pop-up for 5s
                errorMessageEndTime_ = std::chrono::high_resolution_clock::now() + 5s;
            }
        }

        // Check if tutorial was switched
        if ((workGraph_->GetTutorial() != workGraphTutorial_) ||
            (workGraph_->IsSampleSolution() != workGraphUseSampleSolution_))
        {
            std::cout << "Compiling ";
            if (workGraphUseSampleSolution_) {
                std::cout << "sample solution ";
            }
            std::cout << "work graph for tutorial \"" << workGraphTutorial_.name << "\"... " << std::endl;

            // Try to compile work graph for new tutorial
            const auto success = CreateWorkGraph();

            if (success) {
                // Clear persistent scratch buffer if work graph was changes successfully
                clearPersistentScratchBuffer_ = true;

                // Reset start and error message time
                startTime_ = errorMessageEndTime_ = std::chrono::high_resolution_clock::now();
            }
            else {
                // Set current tutorial index to current work graph.
                // This prevents endlessly re-creating the work graph in case the compilation fails
                workGraphTutorial_ = workGraph_->GetTutorial();
                workGraphUseSampleSolution_ = workGraph_->IsSampleSolution();

                using namespace std::chrono_literals;
                // Show error message pop-up for 5s
                errorMessageEndTime_ = std::chrono::high_resolution_clock::now() + 5s;
            }
        }

        // Advance to next command buffer
        auto* commandList = device_->GetNextFrameCommandList();
        const auto renderTarget = swapchain_->GetNextRenderTarget();

        // Advance ImGui to next frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Transition render target to RENDER_TARGET state
        {
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                renderTarget.colorResource.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandList->ResourceBarrier(1, &barrier);
        }

        if (msaaColorResource_ != nullptr) {
            const auto msaaRenderTarget = Swapchain::RenderTarget{
                .colorResource = msaaColorResource_,
                .colorDescriptorHandle = msaaColorDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
                .depthResource = msaaDepthResource_,
                .depthDescriptorHandle = msaaDepthDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
            };

            OnRender(commandList, msaaRenderTarget);
            ResolveMsaaRenderTarget(commandList, renderTarget, msaaRenderTarget);
        }
        else {
            OnRender(commandList, renderTarget);
        }

        OnRenderUserInterface(commandList, renderTarget);

        // Transition render target to PRESENT state
        {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                renderTarget.colorResource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            commandList->ResourceBarrier(1, &barrier);
        }

        // Execute command list
        device_->ExecuteCurrentFrameCommandList();
        // Present frame
        swapchain_->Present(vsync_);
    } while (window_->HandleEvents());

    device_->WaitForDevice();
}

void SpMV_Application::OnRender(ID3D12GraphicsCommandList10* commandList, const Swapchain::RenderTarget& renderTarget)
{
    // Clear and set render target
    {
        // Clear color target
        const float clearColor[] = { 1.f, 1.f, 1.f, 1.f };
        commandList->ClearRenderTargetView(renderTarget.colorDescriptorHandle, clearColor, 0, nullptr);

        // Clear depth target
        commandList->ClearDepthStencilView(
            renderTarget.depthDescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

        // Set swapchain render target
        commandList->OMSetRenderTargets(
            1, &renderTarget.colorDescriptorHandle, false, &renderTarget.depthDescriptorHandle);

        D3D12_VIEWPORT viewport = {
            .TopLeftX = 0.f,
            .TopLeftY = 0.f,
            .Width = static_cast<float>(swapchain_->GetWidth()),
            .Height = static_cast<float>(swapchain_->GetHeight()),
            .MinDepth = 0.f,
            .MaxDepth = 1.f,
        };
        D3D12_RECT scissorRect = {
            .left = 0,
            .top = 0,
            .right = static_cast<LONG>(swapchain_->GetWidth()),
            .bottom = static_cast<LONG>(swapchain_->GetHeight()),
        };

        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissorRect);
    }

    // Transistion writable backbuffer to writable state
    {
        std::array<D3D12_RESOURCE_BARRIER, 1> preBarriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(writableBackbuffer_.Get(),
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        };
        commandList->ResourceBarrier(preBarriers.size(), preBarriers.data());
    }

    // Clear shader resources (writable backbuffer & scratch buffer)
    ClearShaderResources(commandList);

    //modified by mun_ahmd: New root constants
    struct RootConstants {
        uint32_t NumRows;
        uint32_t NumNonZeros;     // number of non-zero entries
        uint32_t VectorCount;     // length of input vector (usually NumCols)
        uint32_t Dummy3;          // padding / reserved
        uint32_t Dummy4;          // padding / reserved
        uint32_t Dummy5;          // padding / reserved
    };

    //const auto& mousePos = ImGui::GetMousePos();

    RootConstants constants = {
        .NumRows = this->activeSpMV.nrows,
        .NumNonZeros = this->activeSpMV.nnz,
        .VectorCount = this->activeSpMV.ncols
    };

    //// Compute input state
    //constants.inputState |= ImGui::IsMouseDown(ImGuiMouseButton_Left) << 0U;
    //constants.inputState |= ImGui::IsMouseDown(ImGuiMouseButton_Middle) << 1U;
    //constants.inputState |= ImGui::IsMouseDown(ImGuiMouseButton_Right) << 2U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_Space) << 3U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_UpArrow) << 4U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_LeftArrow) << 5U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_DownArrow) << 6U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_RightArrow) << 7U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_W) << 8U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_A) << 9U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_S) << 10U;
    //constants.inputState |= ImGui::IsKeyDown(ImGuiKey_D) << 11U;

#ifdef ENABLE_MESH_NODES
    // If the work graph contains mesh nodes, it is created with the
    // D3D12_STATE_OBJECT_FLAG_WORK_GRAPHS_USE_GRAPHICS_STATE_FOR_GLOBAL_ROOT_SIGNATURE flag set.
    // This means that all nodes (compute and graphics) use the *graphics* root arguments on the command list.
    // See https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#graphics-node-resource-binding-and-root-arguments for more details.
    if (workGraph_->ContainsMeshNodes()) {
        // Set root signature for parameters
        commandList->SetGraphicsRootSignature(workGraphRootSignature_.Get());

        // Set root constants
        commandList->SetGraphicsRoot32BitConstants(0, 6, &constants, 0);

        // Set font buffer
        commandList->SetGraphicsRootShaderResourceView(1, fontBuffer_->GetGPUVirtualAddress());

        // Set descriptor heap & table
        commandList->SetDescriptorHeaps(1, resourceDescriptorHeap_.GetAddressOf());
        commandList->SetGraphicsRootDescriptorTable(2, resourceDescriptorHeap_->GetGPUDescriptorHandleForHeapStart());
    }
    else
#endif
    {
        // Set root signature for parameters
        commandList->SetComputeRootSignature(workGraphRootSignature_.Get());

        // Set root constants
        commandList->SetComputeRoot32BitConstants(0, 6, &constants, 0);

        // Set font buffer
        commandList->SetComputeRootShaderResourceView(1, fontBuffer_->GetGPUVirtualAddress());

        // Set descriptor heap & table
        commandList->SetDescriptorHeaps(1, resourceDescriptorHeap_.GetAddressOf());
        commandList->SetComputeRootDescriptorTable(2, resourceDescriptorHeap_->GetGPUDescriptorHandleForHeapStart());
    }

    //modified by mun_ahmd: Passing the dispatch grid size to the GPU
    //todo some sort of calculation of the grid size based on the active matrix
    constexpr uint32_t maxDispatchGrid = 1024;
    uint32_t dispatchGrid = std::min(this->activeSpMV.nrows, maxDispatchGrid);
    if (this->activeSpMV.nrows > maxDispatchGrid) {
        throw std::runtime_error("Currently do not support this many rows sorry");
    }
    WorkGraph::LaunchRecord record{.dispatchGrid=dispatchGrid};
    workGraph_->Dispatch(commandList, 1, sizeof(record), &record);

    // Transistion writable backbuffer back to pixel shader resource
    {
        std::array<D3D12_RESOURCE_BARRIER, 1> postBarriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(writableBackbuffer_.Get(),
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        };
        commandList->ResourceBarrier(postBarriers.size(), postBarriers.data());
    }
}

void SpMV_Application::OnRenderUserInterface(ID3D12GraphicsCommandList10* commandList,
    const Swapchain::RenderTarget& renderTarget)
{
    // Draw writable backbuffer output on top of swapchain render target using an ImGui window
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);

        const auto windowSize = ImVec2(swapchain_->GetWidth(), swapchain_->GetHeight());

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

        if (ImGui::Begin("wg_overlay",
            nullptr,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs))
        {
            const auto descriptorSize =
                device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const auto textureDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(
                uiDescriptorHeap_->GetGPUDescriptorHandleForHeapStart(), 1, descriptorSize);

            ImGui::Image(reinterpret_cast<ImTextureID>(textureDescriptor.ptr), windowSize);
        }

        ImGui::End();

        ImGui::PopStyleVar(2);
    }

    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
    ImGui::BeginMainMenuBar();

    if (!workGraphTutorials_.empty() && ImGui::BeginMenu("Tutorials")) {
        for (const auto& tutorial : workGraphTutorials_) {
            if (ImGui::MenuItem(tutorial.name.c_str(), nullptr, tutorial == workGraphTutorial_)) {
                workGraphTutorial_ = tutorial;
                // Reset sample solution
                workGraphUseSampleSolution_ = false;
            }
        }

        ImGui::EndMenu();
    }

    if (!workGraphTutorial_.solutionShaderFileName.empty()) {
        ImGui::Text("|");
        ImGui::Checkbox("Sample Solution", &workGraphUseSampleSolution_);
    }

    ImGui::Text("|");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5, 0, 1));
    ImGui::Text("Open %s to start this tutorial.", workGraphTutorial_.shaderFileName.c_str());
    ImGui::PopStyleColor();

    // Print current FPS to menu bar
    {
        const auto& io = ImGui::GetIO();
        const auto  frametimeTextSize = ImGui::CalcTextSize("Frametime: XXXXXms (XXXX FPS)");
        const auto  vsyncTextSize = ImGui::CalcTextSize("V-Sync");
        const auto  checkboxWidth = ImGui::GetFrameHeight();
        const auto  padding = 20;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
            (frametimeTextSize.x + vsyncTextSize.x + checkboxWidth + padding));
        ImGui::Checkbox("V-Sync", &vsync_);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - frametimeTextSize.x);
        ImGui::Text("Frametime: %5.1fms (%4.0f FPS)", io.DeltaTime * 1000.f, io.Framerate);
    }

    ImGui::EndMainMenuBar();
    ImGui::PopStyleColor(2);

    // Compilation error message window
    if (errorMessageEndTime_ >= std::chrono::high_resolution_clock::now()) {
        ImGui::SetNextWindowPos(
            ImVec2(swapchain_->GetWidth() / 2, swapchain_->GetHeight() - 20), ImGuiCond_Always, ImVec2(0.5, 1));

        if (ImGui::Begin("error",
            nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs))
        {
            ImGui::Text("Work Graph compilation failed. Check output for more details.");
        }

        ImGui::End();
    }

    // Info window
    {
        ImGui::SetNextWindowPos(ImVec2(0, swapchain_->GetHeight()), ImGuiCond_Always, ImVec2(0, 1));

        if (ImGui::Begin("info",
            nullptr,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
            ImGui::Text("Adapter: %s", device_->GetAdapterDescription().c_str());
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }

    // Message window at bottom
    {
        ImGui::SetNextWindowPos(
            ImVec2(swapchain_->GetWidth(), swapchain_->GetHeight()), ImGuiCond_Always, ImVec2(1, 1));

        if (ImGui::Begin("bottom",
            nullptr,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
            ImGui::Text("Work Graph Playground by AMD & HS Coburg");
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }

    // Render to render target
    {
        // Set render target
        commandList->OMSetRenderTargets(
            1, &renderTarget.colorDescriptorHandle, false, &renderTarget.depthDescriptorHandle);

        // Bind UI descriptor heap
        commandList->SetDescriptorHeaps(1, uiDescriptorHeap_.GetAddressOf());

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
    }
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

    CreateWritableBackbuffer(width, height);
    if (msaaColorResource_ != nullptr) {
        // Re-create MSAA resources with new resolution
        const auto& colorResourceDesc = msaaColorResource_->GetDesc();
        CreateMsaaResources(width, height, colorResourceDesc.SampleDesc.Count, colorResourceDesc.SampleDesc.Quality);
    }
}

void SpMV_Application::CreateImGuiContext()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Disbale ini and log files
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Create descriptor heap for ImGui
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors =
            2;  // 2 Descriptors: descriptor 0 for ImGui internally, descriptor 1 for writable backbuffer
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 1;
        ThrowIfFailed(device_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&uiDescriptorHeap_)));
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(window_->GetHandle());
    ImGui_ImplDX12_Init(device_->GetDevice(),
        Device::BufferedFramesCount,
        Swapchain::ColorTargetFormat,
        uiDescriptorHeap_.Get(),
        uiDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
        uiDescriptorHeap_->GetGPUDescriptorHandleForHeapStart());
}

void SpMV_Application::DestroyImGuiContext()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void SpMV_Application::CreateWorkGraphRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE descriptorRanges[2]{};
    descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0); // 4 SRVs -> t0–t3, starting at heap[0]
    descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 4); // 1 UAV -> u0, at heap[4]

    std::array<CD3DX12_ROOT_PARAMETER, 2> rootParameters;
    rootParameters[0].InitAsConstants(6, 0);  // 6 DWORDs at register b0, space 0
    rootParameters[1].InitAsDescriptorTable(2, descriptorRanges);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(device_->GetDevice()->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&workGraphRootSignature_)));
}

bool SpMV_Application::CreateWorkGraph()
{
    // Wait for all frames in fight before deleting old resources
    device_->WaitForDevice();

    try {
        workGraph_ = std::make_unique<WorkGraph>(
            device_.get(),
            shaderCompiler_.get(),
            workGraphRootSignature_.Get(),
#ifdef ENABLE_MESH_NODES
            msaaColorResource_ != nullptr ? msaaColorResource_->GetDesc().SampleDesc : DXGI_SAMPLE_DESC{ 1, 0 },
#endif
            workGraphTutorial_,
            workGraphUseSampleSolution_);
    }
    catch (const std::exception& e) {
        // Re-throw exception if no fallback work graph exists
        if (!workGraph_) {
            throw e;
        }

        std::cerr << "Failed to re-create work graph:\n" << e.what() << std::endl;

        return false;
    }

    return true;
}

void SpMV_Application::CreateResourceDescriptorHeaps()
{
    // Create descriptor heap to clear shader resources
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 3;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        ThrowIfFailed(device_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&clearDescriptorHeap_)));
    }
    // Create resource descriptor heap for shader resources
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 3;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 1;
        ThrowIfFailed(device_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&resourceDescriptorHeap_)));
    }
}

void SpMV_Application::CreateSpMVBuffers(uint32_t maxNumRows, uint32_t maxNumCols, uint32_t maxNumNonZeroes)
{
    // Reset all old resources
    readonlyCSR_rowPtr.Reset();
    readonlyCSR_colIdx.Reset();
    readonlyCSR_values.Reset();
    readonlyVector.Reset();
    writeableOutputVector.Reset();

    auto device = device_->GetDevice();

    //
    // Create CSR buffers (default heap, GPU local)
    //
    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);

    // rowPtr has (numRows + 1) elements
    UINT64 rowPtrSize = sizeof(uint32_t) * (maxNumRows + 1);
    auto descRowPtr = CD3DX12_RESOURCE_DESC::Buffer(rowPtrSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &descRowPtr,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readonlyCSR_rowPtr)));

    // colIdx has numNonZeroes elements
    UINT64 colIdxSize = sizeof(uint32_t) * maxNumNonZeroes;
    auto descColIdx = CD3DX12_RESOURCE_DESC::Buffer(colIdxSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &descColIdx,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readonlyCSR_colIdx)));

    // values has numNonZeroes elements
    UINT64 valuesSize = sizeof(float) * maxNumNonZeroes;
    auto descValues = CD3DX12_RESOURCE_DESC::Buffer(valuesSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &descValues,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readonlyCSR_values)));

    //
    // Dense vector input (float * numRows)
    //
    UINT64 vecSize = sizeof(float) * maxNumCols;
    auto descVector = CD3DX12_RESOURCE_DESC::Buffer(vecSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &descVector,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readonlyVector)));

    //
    // Output vector (writable UAV)
    //
    auto descOutput = CD3DX12_RESOURCE_DESC::Buffer(vecSize,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &descOutput,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&writeableOutputVector)));

    //Descriptor for RowPtr
    {
        const auto descriptorIndex = 0;
        const auto descriptorSize =
            device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffers use UNKNOWN
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(maxNumRows + 1); // rowPtr has (numRows+1) entries
        srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device_->GetDevice()->CreateShaderResourceView(
            readonlyCSR_rowPtr.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));

        device_->GetDevice()->CreateShaderResourceView(
            readonlyCSR_rowPtr.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }

    //Descriptor for ColIdx
    {
        const auto descriptorIndex = 1;
        const auto descriptorSize =
            device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffers use UNKNOWN
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(maxNumNonZeroes);
        srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device_->GetDevice()->CreateShaderResourceView(
            readonlyCSR_colIdx.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));

        device_->GetDevice()->CreateShaderResourceView(
            readonlyCSR_colIdx.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }

    //Descriptor for Values
    {
        const auto descriptorIndex = 2;
        const auto descriptorSize =
            device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffers use UNKNOWN
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(maxNumNonZeroes);
        srvDesc.Buffer.StructureByteStride = sizeof(float);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device_->GetDevice()->CreateShaderResourceView(
            readonlyCSR_values.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));

        device_->GetDevice()->CreateShaderResourceView(
            readonlyCSR_values.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }

    //Descriptor for Input vector
    {
        const auto descriptorIndex = 3;
        const auto descriptorSize =
            device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffers use UNKNOWN
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(maxNumRows);
        srvDesc.Buffer.StructureByteStride = sizeof(float);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device_->GetDevice()->CreateShaderResourceView(
            readonlyVector.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));

        device_->GetDevice()->CreateShaderResourceView(
            readonlyVector.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }

    //Descriptor for Output vector (UAV)
    {
        const auto descriptorIndex = 4;
        const auto descriptorSize =
            device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = maxNumRows;
        uavDesc.Buffer.StructureByteStride = sizeof(float);
        uavDesc.Buffer.CounterOffsetInBytes = 0; //not using counter
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        device_->GetDevice()->CreateUnorderedAccessView(
            writeableOutputVector.Get(),
            nullptr,
            &uavDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
        
        device_->GetDevice()->CreateUnorderedAccessView(
            writeableOutputVector.Get(),
            nullptr,
            &uavDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }
}

//modified by mun_ahmd: quick and dirty
struct __TransferCommandListHelper {
    static ComPtr<ID3D12GraphicsCommandList10> CreateCommandList(ID3D12Device* device)
    {
        if (!isInit)
        {
            // Create a command allocator once
            ThrowIfFailed(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COPY,
                IID_PPV_ARGS(&commandAllocator)));

            // Create a command queue once
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;       // DIRECT, COMPUTE, or COPY
            desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;   // NORMAL (default) or HIGH
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;      // NONE or DISABLE_GPU_TIMEOUT
            desc.NodeMask = 0;          // single GPU node
            ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

            //Create Fence
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
            HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

            //Init Complete
            isInit = true;
        }

        // Create a new command list for each call
        ComPtr<ID3D12GraphicsCommandList10> commandList;
        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)));

        // Close immediately so caller can Reset() when recording
        ThrowIfFailed(commandList->Close());

        return commandList;
    }

    static void submitAndWaitIdle(ComPtr<ID3D12GraphicsCommandList10> commandList) {
        // cmdList = recorded upload commands
        commandList->Close();

        // Submit to the copy queue
        ID3D12CommandList* ppCommandLists[] = { commandList.Get()};
        commandQueue->ExecuteCommandLists(1, ppCommandLists);

        //wait on fence
        fenceValue++;
        HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        commandQueue->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue)
            fence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    inline static ComPtr<ID3D12CommandQueue> commandQueue;
    inline static ComPtr<ID3D12CommandAllocator> commandAllocator;
    inline static ComPtr<ID3D12Fence> fence;
    inline static UINT64 fenceValue = 0;
    inline static bool isInit = false;
};

void SpMV_Application::UploadSpMV(CSRMatrix matrix) {
    //sets & uploads this->activeSpMV to the gpu pretty much
    this->activeSpMV = matrix;
    this->UploadCSR(
        matrix.nrows, matrix.nnz,
        matrix.rowPtr.data(), matrix.colIdx.data(), matrix.values.data()
    );
    assert(matrix.random_vector.size() == matrix.ncols && "The sizes don't match sweetkins");
    this->UploadVector(matrix.random_vector.size(), matrix.random_vector.data());
}
//TODO FIX UPLOAD FUNCTIONS
//unfortunately I do not feel like making the upload functions coherent
void SpMV_Application::UploadCSR(
    uint32_t numRows,
    uint32_t numNonZeroes,
    const uint32_t* rowPtr,
    const uint32_t* colIdx,
    const float* values)
{
    auto device = device_->GetDevice();
    auto cmdList = __TransferCommandListHelper::CreateCommandList(device);

    //
    // RowPtr upload
    //
    {
        UINT64 size = sizeof(uint32_t) * (numRows + 1);
        ComPtr<ID3D12Resource> upload;
        CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)));

        D3D12_SUBRESOURCE_DATA data = {};
        data.pData = rowPtr;
        data.RowPitch = size;
        data.SlicePitch = size;

        UpdateSubresources<1>(cmdList.Get(), readonlyCSR_rowPtr.Get(), upload.Get(), 0, 0, 1, &data);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            readonlyCSR_rowPtr.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        );
        cmdList->ResourceBarrier(1, &barrier);
    }

    //
    // ColIdx upload
    //
    {
        UINT64 size = sizeof(uint32_t) * numNonZeroes;
        ComPtr<ID3D12Resource> upload;
        CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)));

        D3D12_SUBRESOURCE_DATA data = {};
        data.pData = colIdx;
        data.RowPitch = size;
        data.SlicePitch = size;

        UpdateSubresources<1>(cmdList.Get(), readonlyCSR_colIdx.Get(), upload.Get(), 0, 0, 1, &data);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            readonlyCSR_colIdx.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        );
        cmdList->ResourceBarrier(1, &barrier);
    }

    //
    // Values upload
    //
    {
        UINT64 size = sizeof(float) * numNonZeroes;
        ComPtr<ID3D12Resource> upload;
        CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)));

        D3D12_SUBRESOURCE_DATA data = {};
        data.pData = values;
        data.RowPitch = size;
        data.SlicePitch = size;

        UpdateSubresources<1>(cmdList.Get(), readonlyCSR_values.Get(), upload.Get(), 0, 0, 1, &data);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            readonlyCSR_values.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
        );
        cmdList->ResourceBarrier(1, &barrier);
    }
}


void SpMV_Application::UploadVector(uint32_t count, const float* values)
{
    auto device = device_->GetDevice();
    auto cmdList = __TransferCommandListHelper::CreateCommandList(device);

    UINT64 size = sizeof(float) * count;

    ComPtr<ID3D12Resource> upload;
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&upload)));

    D3D12_SUBRESOURCE_DATA data = {};
    data.pData = values;
    data.RowPitch = size;
    data.SlicePitch = size;

    UpdateSubresources<1>(cmdList.Get(), readonlyVector.Get(), upload.Get(), 0, 0, 1, &data);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        readonlyVector.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    );
    cmdList->ResourceBarrier(1, &barrier);
}


void SpMV_Application::CreateWritableBackbuffer(std::uint32_t width, std::uint32_t height)
{
    writableBackbuffer_.Reset();

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC   resourceDescription = CD3DX12_RESOURCE_DESC::Tex2D(
        Swapchain::ColorTargetFormat, width, height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescription,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&writableBackbuffer_)));

    const auto descriptorSize =
        device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    //modified by mun_ahmd: No need for UAV in workgraph shader (if use change descriptor index)
    /*
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = Swapchain::ColorTargetFormat;
        uavDesc.Texture2D.MipSlice = 0;
        uavDesc.Texture2D.PlaneSlice = 0;

        const auto descriptorIndex = 0;

        device_->GetDevice()->CreateUnorderedAccessView(
            writableBackbuffer_.Get(),
            nullptr,
            &uavDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
        device_->GetDevice()->CreateUnorderedAccessView(
            writableBackbuffer_.Get(),
            nullptr,
            &uavDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }
    */

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = Swapchain::ColorTargetFormat;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        const auto descriptorIndex = 1;

        device_->GetDevice()->CreateShaderResourceView(
            writableBackbuffer_.Get(),
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE(
                uiDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    }
}

void SpMV_Application::CreateScratchBuffer()
{
    scratchBuffer_.Reset();

    const auto elementCount = 100 * 1024;
    const auto elementSize = sizeof(std::uint32_t);

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC   resourceDescription =
        CD3DX12_RESOURCE_DESC::Buffer(elementCount * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescription,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&scratchBuffer_)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = elementCount;
    uavDesc.Buffer.StructureByteStride = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    const auto descriptorSize =
        device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto descriptorIndex = 1;

    device_->GetDevice()->CreateUnorderedAccessView(
        scratchBuffer_.Get(),
        nullptr,
        &uavDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(
            clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    device_->GetDevice()->CreateUnorderedAccessView(
        scratchBuffer_.Get(),
        nullptr,
        &uavDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(
            resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
}

void SpMV_Application::CreatePersistentScratchBuffer()
{
    persistentScratchBuffer_.Reset();

    const auto elementCount = 100 * 1024 * 1024;
    const auto elementSize = sizeof(std::uint32_t);

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC   resourceDescription =
        CD3DX12_RESOURCE_DESC::Buffer(elementCount * elementSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescription,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&persistentScratchBuffer_)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = elementCount;
    uavDesc.Buffer.StructureByteStride = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

    const auto descriptorSize =
        device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto descriptorIndex = 2;

    device_->GetDevice()->CreateUnorderedAccessView(
        persistentScratchBuffer_.Get(),
        nullptr,
        &uavDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(
            clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
    device_->GetDevice()->CreateUnorderedAccessView(
        persistentScratchBuffer_.Get(),
        nullptr,
        &uavDesc,
        CD3DX12_CPU_DESCRIPTOR_HANDLE(
            resourceDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize));
}

void SpMV_Application::ClearShaderResources(ID3D12GraphicsCommandList10* commandList)
{
    // Set descriptor heap for clear
    commandList->SetDescriptorHeaps(1, resourceDescriptorHeap_.GetAddressOf());

    const auto descriptorSize =
        device_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Clear writable backbuffer
    {
        const auto descriptorIndex = 0;
        const auto gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            resourceDescriptorHeap_->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);
        const auto cpuDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);

        float clearValue[4] = { 0.f, 0.f, 0.f, 0.f };
        commandList->ClearUnorderedAccessViewFloat(
            gpuDescriptorHandle, cpuDescriptorHandle, writableBackbuffer_.Get(), clearValue, 0, nullptr);
    }

    // Clear scratch buffer
    {
        const auto descriptorIndex = 1;
        const auto gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            resourceDescriptorHeap_->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);
        const auto cpuDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);

        std::uint32_t clearValue[4] = { 0, 0, 0, 0 };
        commandList->ClearUnorderedAccessViewUint(
            gpuDescriptorHandle, cpuDescriptorHandle, scratchBuffer_.Get(), clearValue, 0, nullptr);
    }

    // Clear persistent scratch buffer
    if (clearPersistentScratchBuffer_) {
        const auto descriptorIndex = 2;
        const auto gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            resourceDescriptorHeap_->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);
        const auto cpuDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            clearDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(), descriptorIndex, descriptorSize);

        std::uint32_t clearValue[4] = { 0, 0, 0, 0 };
        commandList->ClearUnorderedAccessViewUint(
            gpuDescriptorHandle, cpuDescriptorHandle, persistentScratchBuffer_.Get(), clearValue, 0, nullptr);

        // Reset clear
        clearPersistentScratchBuffer_ = false;
    }

    std::array<D3D12_RESOURCE_BARRIER, 3> uavBarriers = {
        CD3DX12_RESOURCE_BARRIER::UAV(writableBackbuffer_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(scratchBuffer_.Get()),
        CD3DX12_RESOURCE_BARRIER::UAV(persistentScratchBuffer_.Get()),
    };

    // Barrier for clear operation
    commandList->ResourceBarrier(uavBarriers.size(), uavBarriers.data());
}

void SpMV_Application::CreateMsaaResources(const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t sampleCount,
    const std::uint32_t sampleQuality)
{
    msaaColorResource_.Reset();
    msaaDepthResource_.Reset();

    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS colorFeatureData = {
            .Format = Swapchain::ColorTargetFormat,
            .SampleCount = sampleCount,
            .Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
        };
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depthFeatureData = {
            .Format = Swapchain::DepthTargetFormat,
            .SampleCount = sampleCount,
            .Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE,
        };

        // Check color MSAA support
        ThrowIfFailed(device_->GetDevice()->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &colorFeatureData, sizeof(colorFeatureData)));
        // Check depth MSAA support
        ThrowIfFailed(device_->GetDevice()->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &depthFeatureData, sizeof(depthFeatureData)));

        if ((sampleQuality >= colorFeatureData.NumQualityLevels) ||
            (sampleQuality >= depthFeatureData.NumQualityLevels))
        {
            std::stringstream stream;
            stream << "Render target multi-sample count " << sampleCount << " not supported on current device.";

            throw std::runtime_error(stream.str());
        }
    }

    // Create RTV descriptor heap
    if (msaaColorDescriptorHeap_ == nullptr) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        ThrowIfFailed(device_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&msaaColorDescriptorHeap_)));
    }

    // Create color resource
    {
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = Swapchain::ColorTargetFormat;
        clearValue.Color[0] = 1.f;
        clearValue.Color[1] = 1.f;
        clearValue.Color[2] = 1.f;
        clearValue.Color[3] = 1.f;

        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC   resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(Swapchain::ColorTargetFormat,
            width,
            height,
            1,
            1,
            sampleCount,
            sampleQuality,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&msaaColorResource_)));

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = Swapchain::ColorTargetFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        device_->GetDevice()->CreateRenderTargetView(
            msaaColorResource_.Get(), &rtvDesc, msaaColorDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
    }

    // Create DSV descriptor heap
    if (msaaDepthDescriptorHeap_ == nullptr) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        ThrowIfFailed(device_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&msaaDepthDescriptorHeap_)));
    }

    // Create depth resource
    {
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = Swapchain::DepthTargetFormat;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC   resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(Swapchain::DepthTargetFormat,
            width,
            height,
            1,
            1,
            sampleCount,
            sampleQuality,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&msaaDepthResource_)));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = Swapchain::DepthTargetFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        device_->GetDevice()->CreateDepthStencilView(
            msaaDepthResource_.Get(), &dsvDesc, msaaDepthDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
    }
}

void SpMV_Application::ResolveMsaaRenderTarget(ID3D12GraphicsCommandList* commandList,
    const Swapchain::RenderTarget& swapchainRenderTarget,
    const Swapchain::RenderTarget& msaaRenderTarget)
{
    std::array<D3D12_RESOURCE_BARRIER, 2> preBarriers = {
        CD3DX12_RESOURCE_BARRIER::Transition(swapchainRenderTarget.colorResource.Get(),
                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_RESOLVE_DEST),
        CD3DX12_RESOURCE_BARRIER::Transition(msaaRenderTarget.colorResource.Get(),
                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
    };
    commandList->ResourceBarrier(preBarriers.size(), preBarriers.data());

    commandList->ResolveSubresource(swapchainRenderTarget.colorResource.Get(),
        0,
        msaaRenderTarget.colorResource.Get(),
        0,
        Swapchain::ColorTargetFormat);

    std::array<D3D12_RESOURCE_BARRIER, 2> postBarriers = {
        CD3DX12_RESOURCE_BARRIER::Transition(swapchainRenderTarget.colorResource.Get(),
                                             D3D12_RESOURCE_STATE_RESOLVE_DEST,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(msaaRenderTarget.colorResource.Get(),
                                             D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    commandList->ResourceBarrier(postBarriers.size(), postBarriers.data());
}

void SpMV_Application::CreateFontBuffer()
{
    fontBuffer_.Reset();

    std::array<std::uint64_t, 128> fontData = {
        0x0000000000000000,  // nul
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  //
        0x0000000000000000,  // space
        0x183C3C1818001800,  // !
        0x3636000000000000,  // "
        0x36367F367F363600,  // #
        0x0C3E031E301F0C00,  // $
        0x006333180C666300,  // %
        0x1C361C6E3B336E00,  // &
        0x0606030000000000,  // '
        0x180C0606060C1800,  // (
        0x060C1818180C0600,  // )
        0x00663CFF3C660000,  // *
        0x000C0C3F0C0C0000,  // +
        0x00000000000C0C06,  // ,
        0x0000003F00000000,  // -
        0x00000000000C0C00,  // .
        0x6030180C06030100,  // /
        0x3E63737B6F673E00,  // 0
        0x0C0E0C0C0C0C3F00,  // 1
        0x1E33301C06333F00,  // 2
        0x1E33301C30331E00,  // 3
        0x383C36337F307800,  // 4
        0x3F031F3030331E00,  // 5
        0x1C06031F33331E00,  // 6
        0x3F3330180C0C0C00,  // 7
        0x1E33331E33331E00,  // 8
        0x1E33333E30180E00,  // 9
        0x000C0C00000C0C00,  // :
        0x000C0C00000C0C06,  // ;
        0x180C0603060C1800,  // <
        0x00003F00003F0000,  // =
        0x060C1830180C0600,  // >
        0x1E3330180C000C00,  // ?
        0x3E637B7B7B031E00,  // @
        0x0C1E33333F333300,  // A
        0x3F66663E66663F00,  // B
        0x3C66030303663C00,  // C
        0x1F36666666361F00,  // D
        0x7F46161E16467F00,  // E
        0x7F46161E16060F00,  // F
        0x3C66030373667C00,  // G
        0x3333333F33333300,  // H
        0x1E0C0C0C0C0C1E00,  // I
        0x7830303033331E00,  // J
        0x6766361E36666700,  // K
        0x0F06060646667F00,  // L
        0x63777F7F6B636300,  // M
        0x63676F7B73636300,  // N
        0x1C36636363361C00,  // O
        0x3F66663E06060F00,  // P
        0x1E3333333B1E3800,  // Q
        0x3F66663E36666700,  // R
        0x1E33070E38331E00,  // S
        0x3F2D0C0C0C0C1E00,  // T
        0x3333333333333F00,  // U
        0x33333333331E0C00,  // V
        0x6363636B7F776300,  // W
        0x6363361C1C366300,  // X
        0x3333331E0C0C1E00,  // Y
        0x7F6331184C667F00,  // Z
        0x1E06060606061E00,  // [
        0x03060C1830604000,  //
        0x1E18181818181E00,  // ]
        0x081C366300000000,  // ^
        0x00000000000000FF,  // _
        0x0C0C180000000000,  // `
        0x00001E303E336E00,  // a
        0x0706063E66663B00,  // b
        0x00001E3303331E00,  // c
        0x3830303e33336E00,  // d
        0x00001E333f031E00,  // e
        0x1C36060f06060F00,  // f
        0x00006E33333E301F,  // g
        0x0706366E66666700,  // h
        0x0C000E0C0C0C1E00,  // i
        0x300030303033331E,  // j
        0x070666361E366700,  // k
        0x0E0C0C0C0C0C1E00,  // l
        0x0000337F7F6B6300,  // m
        0x00001F3333333300,  // n
        0x00001E3333331E00,  // o
        0x00003B66663E060F,  // p
        0x00006E33333E3078,  // q
        0x00003B6E66060F00,  // r
        0x00003E031E301F00,  // s
        0x080C3E0C0C2C1800,  // t
        0x0000333333336E00,  // u
        0x00003333331E0C00,  // v
        0x0000636B7F7F3600,  // w
        0x000063361C366300,  // x
        0x00003333333E301F,  // y
        0x00003F190C263F00,  // z
        0x380C0C070C0C3800,  // {
        0x1818180018181800,  // |
        0x070C0C380C0C0700,  // }
        0x6E3B000000000000,  // ~
        0x0000000000000000,
    };

    // Storing this buffer in upload heap is not ideal, but does work for these small examples
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   resourceDescription =
        CD3DX12_RESOURCE_DESC::Buffer(fontData.size() * sizeof(std::uint64_t), D3D12_RESOURCE_FLAG_NONE);
    ThrowIfFailed(device_->GetDevice()->CreateCommittedResource(&heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDescription,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        nullptr,
        IID_PPV_ARGS(&fontBuffer_)));

    void* mappedData;
    ThrowIfFailed(fontBuffer_->Map(0, nullptr, &mappedData));

    memcpy(mappedData, fontData.data(), fontData.size() * sizeof(std::uint64_t));

    fontBuffer_->Unmap(0, nullptr);
}
