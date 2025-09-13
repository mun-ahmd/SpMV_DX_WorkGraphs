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

#include "ShaderCompiler.h"

#include <d3d12shader.h>

#include <iostream>
#include <sstream>
#include <thread>

namespace {
    std::filesystem::path GetApplicationFolder()
    {
        std::array<char, MAX_PATH> path;

        const auto length = GetModuleFileNameA(NULL, path.data(), path.size());

        return std::filesystem::path(std::string(path.data(), length)).parent_path();
    }
}  // namespace

// Include handler library to collect all included files for tracking
class FileTrackingIncludeHandler : public IDxcIncludeHandler {
public:
    FileTrackingIncludeHandler(ShaderCompiler& parent) : parent_(parent) {}

    HRESULT STDMETHODCALLTYPE LoadSource(_In_ LPCWSTR                             pFilename,
                                         _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
    {
        if (pFilename == nullptr) {
            return E_FAIL;
        }
        if (ppIncludeSource == nullptr) {
            return E_FAIL;
        }

        const auto shaderSourceFilePath = parent_.GetShaderSourceFilePath(pFilename);

        IDxcBlobEncoding* includeSource;
        const auto result = parent_.utils_->LoadFile(shaderSourceFilePath.wstring().c_str(), nullptr, &includeSource);

        *ppIncludeSource = includeSource;

        if (SUCCEEDED(result)) {
            // Update/insert last file write time for hot-reloading
            parent_.trackedFiles_[shaderSourceFilePath] = std::filesystem::last_write_time(shaderSourceFilePath);
        }

        return result;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
    {
        return E_FAIL;
    }

    ULONG STDMETHODCALLTYPE AddRef(void) override
    {
        return 0;
    }

    ULONG STDMETHODCALLTYPE Release(void) override
    {
        return 0;
    }

private:
    ShaderCompiler& parent_;
};

ShaderCompiler::ShaderCompiler(Device* device) : device_(device)
{
    HMODULE dxcompilerModule = LoadLibraryW(L"dxcompiler.dll");

    if (!dxcompilerModule) {
        throw std::runtime_error("Failed to load dxcompiler.dll");
    }

    DxcCreateInstanceProc pfnDxcCreateInstance =
        DxcCreateInstanceProc(GetProcAddress(dxcompilerModule, "DxcCreateInstance"));

    if (pfnDxcCreateInstance == nullptr) {
        throw std::runtime_error("Failed to load DxcCreateInstance from dxcompiler.dll");
    }

    ThrowIfFailed(pfnDxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_)));
    ThrowIfFailed(pfnDxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)));
    ThrowIfFailed(utils_->CreateDefaultIncludeHandler(&includeHandler_));
}

std::pair<ComPtr<IDxcBlob>, std::vector<std::string>> ShaderCompiler::CompileShader(const std::string& shaderFile,
                                                                                    const wchar_t*     target,
                                                                                    const wchar_t*     entryPoint)
{
    const auto shaderSourceFilePath = GetShaderSourceFilePath(shaderFile);

    HRESULT                  loadSourceResult;
    ComPtr<IDxcBlobEncoding> source;

    loadSourceResult = utils_->LoadFile(shaderSourceFilePath.wstring().c_str(), nullptr, &source);

    if (FAILED(loadSourceResult) || (source == nullptr)) {
        // try load source again after 100ms.
        // Sometimes loading the file for hot-reloading will fail if the file is still being written to.
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(100ms);

        loadSourceResult = utils_->LoadFile(shaderSourceFilePath.wstring().c_str(), nullptr, &source);
    }

    if (FAILED(loadSourceResult) || (source == nullptr)) {
        // Second attempt failed as well. Throw error
        throw std::runtime_error("Failed to load shader file \"" + shaderFile + "\"");
    }

    std::vector<std::wstring> arguments = {
        L"-T",
        target,
        L"-enable-16bit-types",
        // use HLSL 2021
        L"-HV",
        L"2021",
#if ENABLE_MESH_NODES
        // Mesh Nodes use experimental shader model 6.9, which is not supported by external DXIL validation
        L"-select-validator",
        L"internal",
#endif
        // column major matrices
        DXC_ARG_PACK_MATRIX_COLUMN_MAJOR,
    };

    if (entryPoint != nullptr) {
        arguments.push_back(L"-E");
        arguments.push_back(entryPoint);
    }

    //modified by mun_ahmd: Do not include from tutorial folders, use custom import path
    /*
    // Add include arguments for tutorial folders
    for (const auto& [tutorialFolder, _] : {TUTORIAL_FOLDER_LIST}) {
        // We look for the tutorial folder in the current working directory and next to the executable file.
        for (const auto& parentFolder : {std::filesystem::current_path(), GetApplicationFolder()}) {
            const auto tutorialFolderAbsolutePath = parentFolder / tutorialFolder;

            // check if include folder exists
            if (std::filesystem::exists(tutorialFolderAbsolutePath)) {
                arguments.push_back(L"-I" + tutorialFolderAbsolutePath.wstring());
                break;
            }
        }
    }
    */

    // Add define for software (WARP) adapter
    if (device_->IsSoftwareAdapter()) {
        arguments.push_back(L"-D");
        arguments.push_back(L"USING_SOFTWARE_ADAPTER=1");
    }

    // Add define for supported work graphs tier
    {
        arguments.push_back(L"-D");
        arguments.push_back(L"DEVICE_SUPPORTED_WORK_GRAPHS_TIER=" +
                            std::to_wstring(device_->GetSupportedWorkGraphsTier()));
    }

    // Add shader source file path argument
    arguments.push_back(shaderSourceFilePath.wstring());

    FileTrackingIncludeHandler includeHandler(*this);

    DxcBuffer sourceBuffer = {
        .Ptr      = source->GetBufferPointer(),
        .Size     = source->GetBufferSize(),
        .Encoding = 0u,
    };

    std::vector<const wchar_t*> argumentPtrs(arguments.size());
    std::transform(
        arguments.begin(), arguments.end(), argumentPtrs.begin(), [](const auto& arg) { return arg.c_str(); });

    ComPtr<IDxcResult> result = nullptr;
    ThrowIfFailed(compiler_->Compile(
        &sourceBuffer, argumentPtrs.data(), argumentPtrs.size(), &includeHandler, IID_PPV_ARGS(&result)));

    HRESULT compileStatus;
    ThrowIfFailed(result->GetStatus(&compileStatus));

    std::string errorString = "";

    // try get error string from DXC result
    {
        ComPtr<IDxcBlobEncoding> errorStringBlob = nullptr;
        if (SUCCEEDED(result->GetErrorBuffer(&errorStringBlob)) && (errorStringBlob != nullptr)) {
            ComPtr<IDxcBlobUtf8> errorStringBlob8 = nullptr;
            utils_->GetBlobAsUtf8(errorStringBlob.Get(), &errorStringBlob8);

            errorString = std::string(errorStringBlob8->GetStringPointer(), errorStringBlob8->GetStringLength());
        }
    }

    if (FAILED(compileStatus)) {
        std::stringstream stream;
        stream << "Failed to compile shader \"" << shaderFile << "\" as " << ConvertWStringToString(target);
        if (entryPoint) {
            stream << " for entry point \"" << ConvertWStringToString(entryPoint) << "\"";
        }
        stream << ":\n" << errorString;

        throw std::runtime_error(stream.str());
    }

    ComPtr<IDxcBlob> outputBlob;
    ThrowIfFailed(result->GetResult(&outputBlob));

    ComPtr<IDxcBlob> reflectionBlob;
    ThrowIfFailed(result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr));

    const DxcBuffer reflectionBuffer{
        .Ptr      = reflectionBlob->GetBufferPointer(),
        .Size     = reflectionBlob->GetBufferSize(),
        .Encoding = 0,
    };

    std::vector<std::string> libraryFunctions;

    ComPtr<ID3D12LibraryReflection> libraryReflection{};
    utils_->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&libraryReflection));

    // check if library reflection is available
    if (libraryReflection) {
        D3D12_LIBRARY_DESC libraryDesc{};
        libraryReflection->GetDesc(&libraryDesc);

        libraryFunctions.reserve(libraryDesc.FunctionCount);

        for (auto functionIndex = 0; functionIndex < libraryDesc.FunctionCount; ++functionIndex) {
            const auto function = libraryReflection->GetFunctionByIndex(functionIndex);

            D3D12_FUNCTION_DESC functionDesc;
            function->GetDesc(&functionDesc);

            libraryFunctions.emplace_back(functionDesc.Name);
        }
    }

    // Update/insert last file write time for hot-reloading
    trackedFiles_[shaderSourceFilePath] = std::filesystem::last_write_time(shaderSourceFilePath);

    return std::make_pair(outputBlob, std::move(libraryFunctions));
}

bool ShaderCompiler::CheckShaderSourceFiles()
{
    bool result = false;

    for (auto& [file, writeTime] : trackedFiles_) {
        try {
            const auto newFileWriteTime = std::filesystem::last_write_time(file);

            // Return true if any file was modified
            result |= (writeTime != newFileWriteTime);

            // Update file timestamp to only trigger update once
            writeTime = newFileWriteTime;
        } catch (const std::filesystem::filesystem_error& e) {
            // last_write_time can throw an error if the file is currently being written to
            continue;
        }
    }

    return result;
}

std::filesystem::path ShaderCompiler::GetShaderSourceFilePath(const std::string& shaderFile)
{
    return std::filesystem::absolute(shaderFile).generic_string();
}

std::filesystem::path ShaderCompiler::GetShaderSourceFilePath(const std::wstring& shaderFile)
{
    return std::filesystem::absolute(shaderFile).generic_string();
}

std::wstring ConvertStringToWString(const std::string_view str)
{
    std::wstring_convert<std::codecvt<wchar_t, char, mbstate_t>, wchar_t> converter;

    const auto begin = &*str.cbegin();
    const auto end   = begin + str.size();

    return converter.from_bytes(begin, end);
}

std::string ConvertWStringToString(const std::wstring_view str)
{
    std::wstring_convert<std::codecvt<wchar_t, char, mbstate_t>, wchar_t> converter;

    const auto begin = &*str.cbegin();
    const auto end   = begin + str.size();

    return converter.to_bytes(begin, end);
}
