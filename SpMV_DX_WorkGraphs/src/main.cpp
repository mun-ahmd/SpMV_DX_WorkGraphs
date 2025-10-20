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

#include <iostream>

#include "SpMV_Application.h"

std::optional<WorkGraph::WorkGraphTutorial> LoadShader(const std::filesystem::path& sourceFilePath);

std::optional<WorkGraph::WorkGraphTutorial> LoadTutorial(const std::filesystem::path& sourceFilePath,
                                                         bool                         allowSolution = false);
std::vector<WorkGraph::WorkGraphTutorial>   LoadTutorials();

int main(int argc, char* argv[])
{
    SpMV_Application::Options options = {
        // Load list of tutorials
        //modified by mun_ahmd: Nope
    };

    // Simple arg parsing for flags
    for (int argIdx = 1; argIdx < argc; ++argIdx) {
        using namespace std::string_literals;

        const auto arg = argv[argIdx];

        const auto ArgumentFlag = [&](const std::string_view flag, bool& value) {
            return arg == flag ? (value = true) : false;
        };
        const auto ArgumentUint = [&](const std::string_view name, std::uint32_t& value) {
            if (arg != name) {
                return false;
            }

            // check if next arg is available
            if (argIdx < (argc - 1)) {
                // move to next arg
                argIdx++;
                // parse argument
                value = std::stoul(argv[argIdx]);
            } else {
                std::cerr << "\"" << name << "\" argument requires an index to be specified." << std::endl;
                exit(1);
            }

            return true;
        };

        if (argIdx == 1) {
            options.inputCSRBinFile = std::filesystem::path(arg);
            if ((!std::filesystem::exists(options.inputCSRBinFile)) || (options.inputCSRBinFile.extension() != ".csrbin")) {
                std::cerr << "invalid input csrbin filepath: " << arg << std::endl;
                exit(2);
            }
            else {
                continue;
            }
        }
        if (ArgumentFlag("--forceWarpAdapter", options.forceWarpAdapter)) {
            continue;
        }
        if (ArgumentFlag("--enableDebugLayer", options.enableDebugLayer)) {
            continue;
        }
        if (ArgumentFlag("--enableGpuValidationLayer", options.enableGpuValidationLayer)) {
            continue;
        }
        if (arg == "-h"s || arg == "--help"s) {
            std::cout << "SpMV Multiplier using DX12 Work Graphs\n";
            std::cout << "Usage: SpMV_WorkGraphs.exe [OPTIONS] [path]\n\n";
            std::cout << "Positionals:\n";
            std::cout << "  path                       Path to a .csrbin SpMV example file (see PreprocessCSR.py).\n\n";
            std::cout << "Options:\n";
            std::cout << "  -h,--help                  Print this help message and exit.\n";
            std::cout << "  --forceWarpAdapter         Force usage of software WARP adapter instead of GPU.\n";
            std::cout << "  --enableDebugLayer         Enable D3D Debug Layer.\n";
            std::cout << "  --enableGpuValidationLayer Enable D3D GPU Validation Layer.\n";
            exit(0);
        }
        std::cerr << "Unknown argument \"" << arg << "\"." << std::endl;
        exit(1);
    }

    try {
        SpMV_Application app(options);
         app.Run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;

        if (DWORD consoleProcessId;  // Check if a console window is attached
            (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR) &&
            (GetConsoleProcessList(&consoleProcessId, 1) == 1))
        {
            // Keep console window open to show output
            system("pause");
        }

        return 1;
    }

    return 0;
}