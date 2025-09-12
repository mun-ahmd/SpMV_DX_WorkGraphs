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

#include "Application.h"

std::optional<WorkGraph::WorkGraphTutorial> LoadTutorial(const std::filesystem::path& sourceFilePath,
                                                         bool                         allowSolution = false);
std::vector<WorkGraph::WorkGraphTutorial>   LoadTutorials();

int main(int argc, char* argv[])
{
    Application::Options options = {
        // Load list of tutorials
        .tutorials = LoadTutorials(),
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

        if (ArgumentFlag("--forceWarpAdapter", options.forceWarpAdapter)) {
            continue;
        }
        if (ArgumentFlag("--enableDebugLayer", options.enableDebugLayer)) {
            continue;
        }
        if (ArgumentFlag("--enableGpuValidationLayer", options.enableGpuValidationLayer)) {
            continue;
        }
        if (std::uint32_t tutorialIndex = 0; ArgumentUint("--tutorial", tutorialIndex)) {
            if (tutorialIndex >= options.tutorials.size()) {
                std::cerr << "Tutorial index is out of range. Use --tutorial to select index in range 0-"
                          << (options.tutorials.size() - 1) << std::endl;

                exit(1);
            }

            options.tutorial = options.tutorials[tutorialIndex];
            continue;
        }
#ifdef ENABLE_MESH_NODES
        if (ArgumentUint("--msaa", options.renderTargetSampleCount)) {
            continue;
        }
#endif

        if (arg == "-h"s || arg == "--help"s) {
            std::cout << "Work Graph Playground\n";
            std::cout << "Usage: WorkGraphPlayground.exe [OPTIONS] [path]\n\n";
            std::cout << "Positionals:\n";
            std::cout << "  path                       Path to a .hlsl playground tutorial file.\n\n";
            std::cout << "Options:\n";
            std::cout << "  -h,--help                  Print this help message and exit.\n";
            std::cout << "  --forceWarpAdapter         Force usage of software WARP adapter instead of GPU.\n";
            std::cout << "  --enableDebugLayer         Enable D3D Debug Layer.\n";
            std::cout << "  --enableGpuValidationLayer Enable D3D GPU Validation Layer.\n";
            std::cout << "  --tutorial UINT            Select tutorial to start with.\n";
            if (options.tutorials.empty()) {
                std::cout << "                               No tutorials found. Please check to make sure at least "
                             "one tutorial file is present in the "
                             "following folders:\n";
                for (const auto& [tutorialFolder, _] : {TUTORIAL_FOLDER_LIST}) {
                    std::cout << "                               - " << tutorialFolder << "\n";
                }
            } else {
                for (std::uint32_t tutorialIndex = 0; tutorialIndex < options.tutorials.size(); ++tutorialIndex) {
                    std::cout << "                               [" << std::setw(2) << tutorialIndex << "] "
                              << options.tutorials[tutorialIndex].name << "\n";
                }
            }
#ifdef ENABLE_MESH_NODES
            std::cout << "  --msaa UINT                Number of samples in mesh node render target. Default = 1.\n";
#endif

            exit(0);
        }

        // Check if tutorial file was selected via path
        if ((argIdx == (argc - 1)) && std::filesystem::exists(arg)) {
            // check if "--tutorial" argument was set
            if (options.tutorial.has_value()) {
                std::cerr << "Tutorial already selected with --tutorial argument." << std::endl;

                exit(1);
            }

            auto tutorial = LoadTutorial(arg, true);

            if (!tutorial.has_value()) {
                std::cerr << "Failed to load tutorial from file \"" << arg << "\"." << std::endl;

                exit(1);
            }

            options.tutorials.insert(options.tutorials.begin(), *tutorial);
            options.tutorial = *tutorial;

            continue;
        }

        std::cerr << "Unknown argument \"" << arg << "\"." << std::endl;
        exit(1);
    }

    try {
        Application app(options);
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

std::optional<WorkGraph::WorkGraphTutorial> LoadTutorial(const std::filesystem::path& sourceFilePath,
                                                         const bool                   allowSolution)
{
    // Ignore non-HLSL files
    if (sourceFilePath.extension() != ".hlsl") {
        return std::nullopt;
    }
    // Ignore solution
    if (sourceFilePath.stem().string().ends_with("Solution") && !allowSolution) {
        return std::nullopt;
    }

    const auto stem = sourceFilePath.stem().string();

    std::stringstream nameStream;

    // Compute tutorial name
    {
        bool lastUpper = true;
        bool lastAlpha = true;

        for (const auto c : stem) {
            const auto upper = std::isupper(c);
            const auto alpha = std::isupper(c);

            // Insert space between camel-case names
            if (upper && !lastUpper) {
                nameStream << " ";
            }

            nameStream << c;

            lastUpper = upper;
            lastAlpha = alpha;
        }
    }

    WorkGraph::WorkGraphTutorial tutorial = {};
    tutorial.name                         = nameStream.str();
    tutorial.shaderFileName =
        std::filesystem::proximate(sourceFilePath, std::filesystem::current_path()).generic_string();

    const auto solutionFilename = sourceFilePath.parent_path() / (stem + "Solution.hlsl");

    if (std::filesystem::exists(solutionFilename)) {
        tutorial.solutionShaderFileName =
            std::filesystem::proximate(solutionFilename, std::filesystem::current_path()).generic_string();
    }

    return tutorial;
}

std::vector<WorkGraph::WorkGraphTutorial> LoadTutorials()
{
    std::vector<WorkGraph::WorkGraphTutorial> result;

    for (const auto& [tutorialFolder, tutorialPrefix] : {TUTORIAL_FOLDER_LIST}) {
        if (!std::filesystem::exists(tutorialFolder)) {
            continue;
        }

        // Numbering of tutorials resets for every folder
        std::uint32_t tutorialIndex = 0;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(tutorialFolder)) {
            const auto& path = entry.path();

            auto tutorial = LoadTutorial(entry.path());

            if (tutorial.has_value()) {
                std::stringstream namePrefixStream;
                namePrefixStream << tutorialPrefix << " " << tutorialIndex++ << ": " << tutorial->name;

                tutorial->name = namePrefixStream.str();

                result.emplace_back(std::move(*tutorial));
            }
        }
    }

    return result;
}
