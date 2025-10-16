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

#include <span>

#include "Device.h"
#include "ShaderCompiler.h"

class WorkGraph {
public:
    struct LaunchRecord {
        uint32_t dispatchGridX;
        uint32_t dispatchGridY;
        uint32_t dispatchGridZ;
    };

    struct WorkGraphTutorial {
        std::string name;
        std::string shaderFileName;
        // Filename for sample solution. Empty string means no solution is available.
        std::string solutionShaderFileName = "";

        bool operator==(const WorkGraphTutorial&) const = default;
    };

    WorkGraph(ID3D12Device9*        device,
              ShaderCompiler*      shaderCompiler,
              ID3D12RootSignature* rootSignature,
#ifdef ENABLE_MESH_NODES
              DXGI_SAMPLE_DESC renderTargetSampleDesc,
#endif
              WorkGraphTutorial tutorial,
              bool              sampleSolution);


    //modified by mun_ahmd: to allow for passing records to initial node
    //todo should I change the signature to void* pRecords? :3
    void Dispatch(ID3D12GraphicsCommandList10* commandList, uint32_t numRecords, uint32_t recordStride, WorkGraph::LaunchRecord* pRecords);

    const WorkGraphTutorial& GetTutorial() const;
    bool                     IsSampleSolution() const;

#ifdef ENABLE_MESH_NODES
    bool ContainsMeshNodes() const;
#endif

private:
    WorkGraphTutorial tutorial_;
    bool              sampleSolution_;

#ifdef ENABLE_MESH_NODES
    bool containsMeshNodes_ = false;
#endif

    ComPtr<ID3D12StateObject> stateObject_;
    ComPtr<ID3D12Resource>    backingMemory_;
    D3D12_SET_PROGRAM_DESC    programDesc_ = {};
    std::uint32_t             entryPointIndex_;
};