#pragma once
#include <cstdint>
#include <vector>
#include <iostream>
#include <assert.h>
#include "MathHelpers.h"
#include "Device.h"
#include "WorkGraph.h"


class SpMV {
public:
	struct SpMVData {
		uint32_t nrows;
		uint32_t ncols;
		uint32_t nnz;

		std::vector<uint32_t> rowPtr;   // length nrows+1
		std::vector<uint32_t> colIdx;  // length nnz
		std::vector<float> values;   // length nnz

		std::vector<float> random_vector; // length ncols
		std::vector<float> mult_result;   // length nrows
	};

	ComPtr<ID3D12Resource> readBackOutputBuffer(ID3D12Device* device, ID3D12GraphicsCommandList10* commandList, size_t readSize) {
		auto readbackbuffer = createBuffer(device, readSize, D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_TYPE_READBACK);
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				buffers.outputVector.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
			commandList->ResourceBarrier(1, &barrier);
		}
		commandList->CopyBufferRegion(readbackbuffer.Get(), 0, buffers.outputVector.Get(), 0, readSize);
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				buffers.outputVector.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			commandList->ResourceBarrier(1, &barrier);
		}
		return readbackbuffer;
	}

	void init(ID3D12Device9* device, std::unique_ptr<ShaderCompiler> shaderC) {
		shaderCompiler = std::move(shaderC);
		this->initDescriptors(device);
		this->initBuffers(device);
		this->initWorkGraph(device);
	}
	void setActiveSpMV(
		ID3D12Device* device,
		ID3D12GraphicsCommandList10* commandList,
		SpMVData data
	) {
		//changes the current spmv
		//hopes for fresh command list
		this->active = data;
		this->uploadBuffers(device, commandList);
	}

	void runGraph(ID3D12GraphicsCommandList10* commandList) {
		//hopes for fresh command list
		struct alignas(4) RootConstants {
			uint32_t NumRows;
			uint32_t NumNonZeros;     // number of non-zero entries
			uint32_t VectorCount;     // length of input vector (usually NumCols)
			uint32_t MaxRowsPerThread;  // Maximum number of rows a single thread can process
			uint32_t NumElementsInDenseRow; //how many elements should be in a matrix row for it to be considered dense
		};

		RootConstants constants = {
			.NumRows = this->active.nrows,
			.NumNonZeros = this->active.nnz,
			.VectorCount = this->active.ncols,
			.MaxRowsPerThread = 4,
			.NumElementsInDenseRow = (static_cast<uint32_t>(this->active.ncols/3))	//25% full
		};

		{
			// Set root signature for parameters
			commandList->SetComputeRootSignature(workgraph.rootSignature.Get());

			// Set root constants
			commandList->SetComputeRoot32BitConstants(0, 5, &constants, 0);

			// Set descriptor heap & table
			commandList->SetDescriptorHeaps(1, descriptorHeap.GetAddressOf());
			commandList->SetComputeRootDescriptorTable(1, descriptorHeap->GetGPUDescriptorHandleForHeapStart());
		
			commandList->SetComputeRootUnorderedAccessView(2, buffers.outputVector->GetGPUVirtualAddress());
		}
		constexpr uint32_t maxDispatchGrid = 1024 / 32;
		uint32_t dispatchGrid = std::min(this->active.nrows / 32, maxDispatchGrid);
		//if (this->active.nrows > maxDispatchGrid) {
		//	throw std::runtime_error("Currently do not support this many rows sorry");
		//}
		WorkGraph::LaunchRecord launchRecord{
			.dispatchGridX = dispatchGrid,
			.dispatchGridY = 1,
			.dispatchGridZ = 1 
		};
		workgraph.workGraph->Dispatch(commandList, 1, 0, &launchRecord);

		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(buffers.outputVector.Get());
			commandList->ResourceBarrier(1, &barrier);
		}
	}

	void copyResults(ID3D12GraphicsCommandList10* commandList) {
		//hopes for fresh command list
		std::array<D3D12_RESOURCE_BARRIER, 1> preBarriers = {
			CD3DX12_RESOURCE_BARRIER::Transition(buffers.outputVector.Get(),
												 D3D12_RESOURCE_STATE_COMMON,
												 D3D12_RESOURCE_STATE_COPY_SOURCE),
		};
		commandList->ResourceBarrier(preBarriers.size(), preBarriers.data());

		commandList->CopyBufferRegion(
			buffers.readback.Get(), 0,
			buffers.outputVector.Get(), 0,
			active.mult_result.size() * sizeof(float)
		);

		std::array<D3D12_RESOURCE_BARRIER, 1> postBarriers = {
			CD3DX12_RESOURCE_BARRIER::Transition(buffers.outputVector.Get(),
												 D3D12_RESOURCE_STATE_COPY_SOURCE,
												 D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};
		commandList->ResourceBarrier(postBarriers.size(), postBarriers.data());
	}

	std::vector<float> getResultsOnCPU() {
		CD3DX12_RANGE readRange(0, active.mult_result.size() * sizeof(float));
		float* readPtr = nullptr;
		ThrowIfFailed(buffers.readback->Map(0, &readRange, reinterpret_cast<void**>(&readPtr)));
		std::vector<float> cpuData(((readRange.End - readRange.Begin) / sizeof(float)), 99.0f);
		memcpy(cpuData.data(), readPtr, cpuData.size() * sizeof(float));
		//std::cout << "\nWonderland is here: " << cpuData[0] << "\n\n";
		buffers.readback->Unmap(0, nullptr);

		std::cout << "Computed SpMV Result vs Actual => " << cpuData[0] << " " << (compareWithTolerance(
			cpuData,
			active.mult_result, 1.0E-4F, 1.0E-4F
		) ? "Success" : "Failure") << std::endl;

		return cpuData;
	}

private:
	SpMVData active;

	struct {
		ComPtr<ID3D12Resource> rowPtr;
		ComPtr<ID3D12Resource> colIdx;
		ComPtr<ID3D12Resource> values;
		ComPtr<ID3D12Resource> inputVector;
		ComPtr<ID3D12Resource> outputVector;
		ComPtr<ID3D12Resource> readback;
	} buffers;

	struct {
		ComPtr<ID3D12RootSignature>     rootSignature;
		WorkGraph::WorkGraphTutorial tutorial;
		std::unique_ptr<WorkGraph>      workGraph;
	} workgraph;

	std::unique_ptr<ShaderCompiler> shaderCompiler;
	ComPtr<ID3D12DescriptorHeap> descriptorHeap;

	inline static ComPtr<ID3D12Resource> createBuffer(
		ID3D12Device* device,
		size_t bufferSize,
		D3D12_RESOURCE_FLAGS resourceFlag,
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT
	) {
		ComPtr<ID3D12Resource> buffer;
		//muneeb's  simplified buffer creator
		CD3DX12_HEAP_PROPERTIES heapProperties(heapType);
		CD3DX12_RESOURCE_DESC   resourceDescription = CD3DX12_RESOURCE_DESC::Buffer(
			bufferSize,
			resourceFlag
		);
		ThrowIfFailed(device->CreateCommittedResource(&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&buffer))
		);
		return buffer;
	}
	inline static void addBufferToDescriptor(
		ID3D12Device* device,
		ComPtr<ID3D12DescriptorHeap> descriptorHeap,
		INT descriptorIndex,
		UINT descriptorSize,
		bool isUAV,
		ComPtr<ID3D12Resource> buffer,
		uint32_t firstElement,
		uint32_t numElements,
		uint32_t elementStride
	) {
		//if not uav then assumed srv
		if (isUAV) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement = firstElement;
			uavDesc.Buffer.NumElements = numElements;
			uavDesc.Buffer.StructureByteStride = elementStride;
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
			device->CreateUnorderedAccessView(
				buffer.Get(),
				nullptr,
				&uavDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
					descriptorIndex,
					descriptorSize
				)
			);
		}
		else {
			//assume srv
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN; // Structured buffers use UNKNOWN
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Buffer.FirstElement = firstElement;
			srvDesc.Buffer.NumElements = numElements;
			srvDesc.Buffer.StructureByteStride = elementStride;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

			device->CreateShaderResourceView(
				buffer.Get(),
				&srvDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
					descriptorIndex,
					descriptorSize
				)
			);
		}
	}

	//below is some abhorrent code, to keep shared ptrs alive we do the darkest things
	public: inline static std::vector<ComPtr<ID3D12Resource>> theForbiddenUploadBufferVector{};
	private:
	inline static void uploadToBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList10* commandList,
		ComPtr<ID3D12Resource> buffer,
		UINT64 bufferSize,
		void* pData,
		D3D12_RESOURCE_STATES targetResourceState
	) {
		//muneeb's simplified upload and transition
		ComPtr<ID3D12Resource> upload;
		CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
		auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
		ThrowIfFailed(device->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&upload))
		);

		{
			//memcpy to upload buffer
			D3D12_RANGE copyRange{.Begin=0, .End=bufferSize};
			void* uBufPtr = nullptr;
			upload->Map(0, &copyRange, &uBufPtr);
			memcpy(uBufPtr, pData, bufferSize);
			upload->Unmap(0, &copyRange);
		}

		{
			//copy upload buffer to target buffer
			commandList->CopyBufferRegion(
				buffer.Get(), 0,
				upload.Get(), 0,
				bufferSize
			);
		}

		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			buffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			targetResourceState
		);
		commandList->ResourceBarrier(1, &barrier);
		theForbiddenUploadBufferVector.push_back(upload);
	}

	void initDescriptors(ID3D12Device* device) {
		// Create resource descriptor heap for shader resources
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 5;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ;
		//todo checkout why nodemask was previously set to 1
		desc.NodeMask = 0;
		ThrowIfFailed(
			device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap))
		);
	}

	void initBuffers(ID3D12Device* device) {
		//create all the committed resources
		//add them to descriptors too

		//hardcoded max limits
		uint32_t maxNumRows = 10000;
		uint32_t maxNumCols = 10000;
		uint32_t maxNumNonZeros = 10000;

		this->buffers.rowPtr = createBuffer(
			device,
			(maxNumRows + 1) * sizeof(uint32_t),
			D3D12_RESOURCE_FLAG_NONE
		);

		this->buffers.colIdx = createBuffer(
			device,
			maxNumNonZeros * sizeof(uint32_t),
			D3D12_RESOURCE_FLAG_NONE
		);

		this->buffers.values = createBuffer(
			device,
			maxNumNonZeros * sizeof(float),
			D3D12_RESOURCE_FLAG_NONE
		);

		this->buffers.inputVector = createBuffer(
			device,
			maxNumCols * sizeof(float),
			D3D12_RESOURCE_FLAG_NONE
		);

		this->buffers.outputVector = createBuffer(
			device,
			maxNumRows * sizeof(float),
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		this->buffers.readback = createBuffer(
			device,
			maxNumRows * sizeof(float),
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_HEAP_TYPE_READBACK
		);

		const auto descriptorIncrementSize =
			device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		addBufferToDescriptor(
			device, descriptorHeap,
			0, descriptorIncrementSize, false, buffers.rowPtr,
			0, maxNumRows + 1, sizeof(uint32_t)
		);
		addBufferToDescriptor(
			device, descriptorHeap,
			1, descriptorIncrementSize, false, buffers.colIdx,
			0, maxNumNonZeros, sizeof(uint32_t)
		);
		addBufferToDescriptor(
			device, descriptorHeap,
			2, descriptorIncrementSize, false, buffers.values,
			0, maxNumNonZeros, sizeof(float)
		);
		addBufferToDescriptor(
			device, descriptorHeap,
			3, descriptorIncrementSize, false, buffers.inputVector,
			0, maxNumCols, sizeof(float)
		);
		//addBufferToDescriptor(
		//	device, descriptorHeap,
		//	4, descriptorIncrementSize, true, buffers.outputVector,
		//	0, maxNumRows, sizeof(float)
		//);
	}

	void uploadBuffers(
		ID3D12Device* device,
		ID3D12GraphicsCommandList10* commandList
	) {
		//transition readback buffer
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				buffers.readback.Get(), D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_COPY_DEST, 0
			);
			commandList->ResourceBarrier(1, &barrier);
		}

		//upload all the buffers from active
		uploadToBuffer(
			device,
			commandList,
			buffers.rowPtr,
			active.rowPtr.size() * sizeof(uint32_t),
			active.rowPtr.data(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
		uploadToBuffer(
			device,
			commandList,
			buffers.colIdx,
			active.colIdx.size() * sizeof(uint32_t),
			active.colIdx.data(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
		uploadToBuffer(
			device,
			commandList,
			buffers.values,
			active.values.size() * sizeof(float),
			active.values.data(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
		uploadToBuffer(
			device,
			commandList,
			buffers.inputVector,
			active.random_vector.size() * sizeof(float),
			active.random_vector.data(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		);
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				buffers.outputVector.Get(),
				D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			);
			commandList->ResourceBarrier(1, &barrier);
		}
	}

	void initWorkGraph(ID3D12Device9* device) {
		//create signature
		{
			CD3DX12_DESCRIPTOR_RANGE descriptorRanges[1]{};
			descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0); // 4 SRVs -> t0 t3, starting at heap[0]
			//descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND); // 1 UAV -> u0, at heap[4]

			std::array<CD3DX12_ROOT_PARAMETER, 3> rootParameters{};
			rootParameters[0].InitAsConstants(5, 0);  // 5 DWORDs at register b0, space 0
			rootParameters[1].InitAsDescriptorTable(1, descriptorRanges);
			rootParameters[2].InitAsUnorderedAccessView(0);

			CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
			rootSignatureDesc.Init(rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

			ComPtr<ID3DBlob> signature;
			ComPtr<ID3DBlob> error;
			ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
			ThrowIfFailed(device->CreateRootSignature(
				0, signature->GetBufferPointer(),
				signature->GetBufferSize(), IID_PPV_ARGS(&workgraph.rootSignature)
			));
		}

		//create graph
		{
			try {
				workgraph.tutorial.name = "SpMV";
				workgraph.tutorial.shaderFileName = "shaders/basic.hlsl";
				workgraph.workGraph = std::make_unique<WorkGraph>(
					device,
					shaderCompiler.get(),
					workgraph.rootSignature.Get(),
					workgraph.tutorial,
					false,
					L"EntryV2"
				);
			}
			catch (const std::exception& e) {
				// Re-throw exception if no fallback work graph exists
				if (!workgraph.workGraph) {
					throw e;
				}
				throw std::runtime_error("Failed to re-create work graph:\n" + std::string(e.what()));
			}
		}
	}
};