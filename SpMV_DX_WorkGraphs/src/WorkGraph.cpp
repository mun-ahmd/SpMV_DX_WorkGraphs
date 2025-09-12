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

#include "WorkGraph.h"

#include <iostream>

#include "Application.h"
#include "Swapchain.h"

WorkGraph::WorkGraph(const Device*        device,
                     ShaderCompiler*      shaderCompiler,
                     ID3D12RootSignature* rootSignature,
#ifdef ENABLE_MESH_NODES
                     DXGI_SAMPLE_DESC renderTargetSampleDesc,
#endif
                     WorkGraphTutorial tutorial,
                     const bool        sampleSolution)
    : tutorial_(std::move(tutorial)), sampleSolution_(sampleSolution)
{
    // Name for work graph program inside the state object
    static const wchar_t* WorkGraphProgramName = L"WorkGraph";
// Constants for compiling shaders
#ifdef ENABLE_MESH_NODES
    static const wchar_t* LibraryTarget     = L"lib_6_9";
    static const wchar_t* PixelShaderTarget = L"ps_6_9";
#else
    static const wchar_t* LibraryTarget = L"lib_6_8";
#endif

    // ===================================
    // Add shader libraries

    // Ensure sample solution exists
    if (sampleSolution_ && tutorial_.solutionShaderFileName.empty()) {
        throw std::runtime_error("selected tutorial does not provide a sample solution.");
    }

    // Select shader file name
    const auto shaderFileName = sampleSolution_ ? tutorial_.solutionShaderFileName : tutorial_.shaderFileName;

    // Create work graph
    CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_EXECUTABLE);

    // set root signature for work graph
    auto rootSignatureSubobject = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    rootSignatureSubobject->SetRootSignature(rootSignature);

    auto workgraphSubobject = stateObjectDesc.CreateSubobject<CD3DX12_WORK_GRAPH_SUBOBJECT>();
    workgraphSubobject->IncludeAllAvailableNodes();
    workgraphSubobject->SetProgramName(WorkGraphProgramName);

    const auto [libraryBlob, libraryFunctions] = shaderCompiler->CompileShader(shaderFileName, LibraryTarget, nullptr);

    // list of compiled shaders to be released once the work graph is created
    std::vector<ComPtr<IDxcBlob>> compiledShaders = {libraryBlob};

    // Add library to graph
    {
        auto shaderBytecode = CD3DX12_SHADER_BYTECODE(libraryBlob->GetBufferPointer(), libraryBlob->GetBufferSize());

        // add blob to state object
        auto librarySubobject = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        librarySubobject->SetDXILLibrary(&shaderBytecode);
    }

#ifdef ENABLE_MESH_NODES
    // State subobject pointers for mesh-node-specific state subobjects
    CD3DX12_RASTERIZER_SUBOBJECT*            rasterizerSubobject;
    CD3DX12_PRIMITIVE_TOPOLOGY_SUBOBJECT*    primitiveTopologySubobject;
    CD3DX12_DEPTH_STENCIL_FORMAT_SUBOBJECT*  depthStencilSubobject;
    CD3DX12_RENDER_TARGET_FORMATS_SUBOBJECT* renderTargetSubobject;
    CD3DX12_SAMPLE_DESC_SUBOBJECT*           sampleDescSubobject;

    for (const auto& libraryFunction : libraryFunctions) {
        const std::string meshShaderSuffix  = "MeshShader";
        const std::string pixelShaderSuffix = "PixelShader";

        if (!libraryFunction.ends_with(meshShaderSuffix)) {
            continue;
        }

        const auto libraryFunctionW = ConvertStringToWString(libraryFunction);

        if (!containsMeshNodes_) {
            // This is the first mesh node in this graph, so we need to check if mesh nodes are supported.
            if (device->GetSupportedWorkGraphsTier() < D3D12_WORK_GRAPHS_TIER_1_1) {
                throw std::runtime_error(
                    "Work graphs tier 1.1 (mesh nodes) are not supported on the current device. Please check your GPU "
                    "driver or WARP installation.");
            }

            // Configure draw nodes to use graphics root signature
            auto configSubobject = stateObjectDesc.CreateSubobject<CD3DX12_STATE_OBJECT_CONFIG_SUBOBJECT>();
            configSubobject->SetFlags(D3D12_STATE_OBJECT_FLAG_WORK_GRAPHS_USE_GRAPHICS_STATE_FOR_GLOBAL_ROOT_SIGNATURE);

            // Create and configure common subobject for all mesh nodes

            // Create & configure rasterizer subobject with CCW triangles without backface culling
            rasterizerSubobject = stateObjectDesc.CreateSubobject<CD3DX12_RASTERIZER_SUBOBJECT>();
            rasterizerSubobject->SetFrontCounterClockwise(true);
            rasterizerSubobject->SetFillMode(D3D12_FILL_MODE_SOLID);
            rasterizerSubobject->SetCullMode(D3D12_CULL_MODE_NONE);

            // Create & configure primitive topology subobject for triangles
            primitiveTopologySubobject = stateObjectDesc.CreateSubobject<CD3DX12_PRIMITIVE_TOPOLOGY_SUBOBJECT>();
            primitiveTopologySubobject->SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

            // Create & configure depth stencil subobject for swapchain depth format
            depthStencilSubobject = stateObjectDesc.CreateSubobject<CD3DX12_DEPTH_STENCIL_FORMAT_SUBOBJECT>();
            depthStencilSubobject->SetDepthStencilFormat(Swapchain::DepthTargetFormat);

            // Create & configure render target subobject for swapchain color format
            renderTargetSubobject = stateObjectDesc.CreateSubobject<CD3DX12_RENDER_TARGET_FORMATS_SUBOBJECT>();
            renderTargetSubobject->SetNumRenderTargets(1);
            renderTargetSubobject->SetRenderTargetFormat(0, Swapchain::ColorTargetFormat);

            sampleDescSubobject = stateObjectDesc.CreateSubobject<CD3DX12_SAMPLE_DESC_SUBOBJECT>();
            sampleDescSubobject->SetCount(renderTargetSampleDesc.Count);
            sampleDescSubobject->SetQuality(renderTargetSampleDesc.Quality);

            containsMeshNodes_ = true;
        }

        // Create subobject for generic program (i.e. the mesh node)
        auto genericProgramSubobject = stateObjectDesc.CreateSubobject<CD3DX12_GENERIC_PROGRAM_SUBOBJECT>();

        // Use mesh shader name as program name. This name needs to be unique for any program.
        genericProgramSubobject->SetProgramName(libraryFunctionW.c_str());

        // Add mesh shader to generic program
        genericProgramSubobject->AddExport(libraryFunctionW.c_str());

        // Add subobject for pipeline configuration
        genericProgramSubobject->AddSubobject(*rasterizerSubobject);
        genericProgramSubobject->AddSubobject(*primitiveTopologySubobject);
        genericProgramSubobject->AddSubobject(*depthStencilSubobject);
        genericProgramSubobject->AddSubobject(*renderTargetSubobject);
        genericProgramSubobject->AddSubobject(*sampleDescSubobject);

        const auto pixelShaderEntryPoint =
            libraryFunction.substr(0, libraryFunction.size() - meshShaderSuffix.size()) + pixelShaderSuffix;
        const auto pixelShaderEntryPointW = ConvertStringToWString(pixelShaderEntryPoint);

        try {
            auto [pixelShaderBlob, _] =
                shaderCompiler->CompileShader(shaderFileName, PixelShaderTarget, pixelShaderEntryPointW.c_str());

            auto pixelShaderBytecode =
                CD3DX12_SHADER_BYTECODE(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize());

            // add blob to state object
            auto librarySubobject = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
            librarySubobject->SetDXILLibrary(&pixelShaderBytecode);

            // Add pixel shader to generic program
            genericProgramSubobject->AddExport(pixelShaderEntryPointW.c_str());

            compiledShaders.emplace_back(std::move(pixelShaderBlob));
        } catch (const std::exception& e) {
            // If pixel shader compilation fails, the entry point does not exist.
            // Any errors in the actual pixel shader would be already caught when compiling the library.
            std::cout << "Pixel shader \"" << pixelShaderEntryPoint << "\" for mesh shader \"" << libraryFunction
                      << "\" not found.\n";
        }
    }
#endif

    // Create work graph state object
    ThrowIfFailed(device->GetDevice()->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&stateObject_)));

    // release all compiled shaders
    compiledShaders.clear();

    // Get work graph properties
    ComPtr<ID3D12StateObjectProperties1> stateObjectProperties;
    ComPtr<ID3D12WorkGraphProperties>    workGraphProperties;

    ThrowIfFailed(stateObject_->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
    ThrowIfFailed(stateObject_->QueryInterface(IID_PPV_ARGS(&workGraphProperties)));

    // Get the index of our work graph inside the state object (state object can contain multiple work graphs)
    const auto workGraphIndex = workGraphProperties->GetWorkGraphIndex(WorkGraphProgramName);

#ifdef ENABLE_MESH_NODES
    if (containsMeshNodes_) {
        ComPtr<ID3D12WorkGraphProperties1> workGraphProperties1;
        ThrowIfFailed(stateObject_->QueryInterface(IID_PPV_ARGS(&workGraphProperties1)));

        workGraphProperties1->SetMaximumInputRecords(workGraphIndex, 1, 1);
    }
#endif

    // Create backing memory buffer
    // See https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#getworkgraphmemoryrequirements
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS memoryRequirements = {};
    workGraphProperties->GetWorkGraphMemoryRequirements(workGraphIndex, &memoryRequirements);

    // Work graphs can also request no backing memory (i.e., MaxSizeInBytes = 0)
    if (memoryRequirements.MaxSizeInBytes > 0) {
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC   resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(memoryRequirements.MaxSizeInBytes,
                                                                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(device->GetDevice()->CreateCommittedResource(&heapProperties,
                                                                   D3D12_HEAP_FLAG_NONE,
                                                                   &resourceDesc,
                                                                   D3D12_RESOURCE_STATE_COMMON,
                                                                   NULL,
                                                                   IID_PPV_ARGS(&backingMemory_)));
    }

    // Prepare work graph desc
    // See https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#d3d12_set_program_desc
    programDesc_.Type                        = D3D12_PROGRAM_TYPE_WORK_GRAPH;
    programDesc_.WorkGraph.ProgramIdentifier = stateObjectProperties->GetProgramIdentifier(WorkGraphProgramName);
    // Set flag to initialize backing memory.
    // We'll clear this flag once we've run the work graph for the first time.
    programDesc_.WorkGraph.Flags             = D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
    // Set backing memory
    if (backingMemory_) {
        programDesc_.WorkGraph.BackingMemory.StartAddress = backingMemory_->GetGPUVirtualAddress();
        programDesc_.WorkGraph.BackingMemory.SizeInBytes  = backingMemory_->GetDesc().Width;
    }

    // All tutorial work graphs must declare a node named "Entry" with an empty record (i.e., no input record).
    // The D3D12_DISPATCH_GRAPH_DESC uses entrypoint indices instead of string-based node IDs to reference the enty
    // node. GetEntrypointIndex allows us to translate from a node ID (i.e., node name and node array index) to an
    // entrypoint index. See https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#getentrypointindex
    entryPointIndex_ = workGraphProperties->GetEntrypointIndex(workGraphIndex, {L"Entry", 0});

    // Check if entrypoint was found.
    if (entryPointIndex_ == 0xFFFFFFFFU) {
        throw std::runtime_error("work graph does not contain an entry node with [NodeId(\"Entry\", 0)].");
    }
}

void WorkGraph::Dispatch(ID3D12GraphicsCommandList10* commandList)
{
    D3D12_DISPATCH_GRAPH_DESC dispatchDesc        = {};
    dispatchDesc.Mode                             = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
    dispatchDesc.NodeCPUInput                     = {};
    dispatchDesc.NodeCPUInput.EntrypointIndex     = entryPointIndex_;
    // Launch graph with one record
    dispatchDesc.NodeCPUInput.NumRecords          = 1;
    // Record does not contain any data
    dispatchDesc.NodeCPUInput.RecordStrideInBytes = 0;
    dispatchDesc.NodeCPUInput.pRecords            = nullptr;

    // Set program and dispatch the work graphs.
    // See
    // https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#setprogram
    // https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#dispatchgraph

    commandList->SetProgram(&programDesc_);
    commandList->DispatchGraph(&dispatchDesc);

    // Clear backing memory initialization flag, as the graph has run at least once now
    // See https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html#d3d12_set_work_graph_flags
    programDesc_.WorkGraph.Flags &= ~D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
}

const WorkGraph::WorkGraphTutorial& WorkGraph::GetTutorial() const
{
    return tutorial_;
}

bool WorkGraph::IsSampleSolution() const
{
    return sampleSolution_;
}

#ifdef ENABLE_MESH_NODES
bool WorkGraph::ContainsMeshNodes() const
{
    return containsMeshNodes_;
}
#endif
