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

#include <chrono>
#include <vector>

#include "Device.h"
#include "ShaderCompiler.h"
#include "Swapchain.h"
#include "Window.h"
#include "WorkGraph.h"
#include "SpMV.h"


class SpMV_Application {
public:
    struct Options {
        std::wstring  title        = L"Work Graph Playground";
        std::uint32_t windowWidth  = 1280;
        std::uint32_t windowHeight = 720;

        std::vector<WorkGraph::WorkGraphTutorial> tutorials;

        std::optional<WorkGraph::WorkGraphTutorial> tutorial;

#ifdef ENABLE_MESH_NODES
        // MSAA sample count for render target
        std::uint32_t renderTargetSampleCount = 1;
#endif

        bool forceWarpAdapter         = false;
        bool enableDebugLayer         = true;
        bool enableGpuValidationLayer = true;
    };

    SpMV_Application(const Options& options);
    ~SpMV_Application();

    void Run();

private:
    void OnResize(std::uint32_t width, std::uint32_t height);

    std::unique_ptr<Window>    window_;
    std::unique_ptr<Device>    device_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<SpMV> spmv_;

    bool vsync_ = true;
    // Clear persistent scratch buffer after work graph switch
    bool clearPersistentScratchBuffer_ = true;

    // Timeout to show compilation error message
    std::chrono::high_resolution_clock::time_point errorMessageEndTime_ = std::chrono::high_resolution_clock::now();
    // Start time of current tutorial. Delta to current time is available in the shader as "Time"
    std::chrono::high_resolution_clock::time_point startTime_           = std::chrono::high_resolution_clock::now();
};