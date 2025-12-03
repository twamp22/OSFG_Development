// OSFG Simple Optical Flow Implementation
// Block-matching optical flow for Phase 1 proof of concept
// MIT License - Part of Open Source Frame Generation project

#include "simple_opticalflow.h"
#include <chrono>
#include <fstream>

// Embedded shader bytecode (compiled separately)
// For now, we'll compile at runtime using D3DCompile
static const char* g_OpticalFlowShaderSource = R"(
// Input textures
Texture2D<float4> g_CurrentFrame : register(t0);
Texture2D<float4> g_PreviousFrame : register(t1);

// Output texture (motion vectors)
RWTexture2D<int2> g_MotionVectors : register(u0);

// Constants
cbuffer OpticalFlowConstants : register(b0)
{
    uint2 g_InputSize;
    uint2 g_OutputSize;
    uint  g_BlockSize;
    uint  g_SearchRadius;
    float g_MinLuminance;
    float g_MaxLuminance;
};

float RGBToLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float ComputeBlockSAD(int2 currentBlockPos, int2 previousBlockPos)
{
    float sad = 0.0;

    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            int2 currentPos = currentBlockPos + int2(x, y);
            int2 previousPos = previousBlockPos + int2(x, y);

            currentPos = clamp(currentPos, int2(0, 0), int2(g_InputSize) - 1);
            previousPos = clamp(previousPos, int2(0, 0), int2(g_InputSize) - 1);

            float currentLum = RGBToLuminance(g_CurrentFrame[currentPos].rgb);
            float previousLum = RGBToLuminance(g_PreviousFrame[previousPos].rgb);

            sad += abs(currentLum - previousLum);
        }
    }

    return sad;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_OutputSize.x || dispatchThreadId.y >= g_OutputSize.y)
        return;

    int2 blockPos = int2(dispatchThreadId.xy) * g_BlockSize;

    float bestSAD = 1e10;
    int2 bestMotion = int2(0, 0);

    int searchRadius = (int)g_SearchRadius;

    for (int dy = -searchRadius; dy <= searchRadius; dy++)
    {
        for (int dx = -searchRadius; dx <= searchRadius; dx++)
        {
            int2 searchPos = blockPos + int2(dx, dy);

            if (searchPos.x < 0 || searchPos.y < 0 ||
                searchPos.x + g_BlockSize > g_InputSize.x ||
                searchPos.y + g_BlockSize > g_InputSize.y)
                continue;

            float sad = ComputeBlockSAD(blockPos, searchPos);

            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestMotion = int2(dx, dy);
            }
        }
    }

    g_MotionVectors[dispatchThreadId.xy] = bestMotion * 16;
}
)";

namespace OSFG {

SimpleOpticalFlow::SimpleOpticalFlow()
{
}

SimpleOpticalFlow::~SimpleOpticalFlow()
{
    Shutdown();
}

bool SimpleOpticalFlow::Initialize(ID3D12Device* device, const SimpleOpticalFlowConfig& config)
{
    if (m_initialized) {
        Shutdown();
    }

    if (!device) {
        m_lastError = "Device is null";
        return false;
    }

    m_device = device;
    m_config = config;

    // Calculate motion vector texture dimensions
    m_mvWidth = (config.width + config.blockSize - 1) / config.blockSize;
    m_mvHeight = (config.height + config.blockSize - 1) / config.blockSize;

    // Create descriptor heaps first
    if (!CreateDescriptorHeaps()) {
        return false;
    }

    // Create root signature
    if (!CreateRootSignature()) {
        return false;
    }

    // Create pipeline state
    if (!CreatePipelineState()) {
        return false;
    }

    // Create resources
    if (!CreateResources()) {
        return false;
    }

    m_initialized = true;
    m_stats = {};
    return true;
}

void SimpleOpticalFlow::Shutdown()
{
    m_pipelineState.Reset();
    m_rootSignature.Reset();
    m_motionVectorTexture.Reset();
    m_constantBuffer.Reset();
    m_srvUavHeap.Reset();
    m_device.Reset();
    m_initialized = false;
}

bool SimpleOpticalFlow::CreateDescriptorHeaps()
{
    // Create SRV/UAV heap for shader resources
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 4; // 2 SRVs + 1 UAV + spare
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvUavHeap));
    if (FAILED(hr)) {
        m_lastError = "Failed to create descriptor heap";
        return false;
    }

    m_srvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool SimpleOpticalFlow::CreateRootSignature()
{
    // Root parameters:
    // [0] CBV - Constant buffer
    // [1] Descriptor table - SRVs (current and previous frame)
    // [2] Descriptor table - UAV (motion vectors)

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // CBV
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV table
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV table
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            m_lastError = std::string("Root signature serialization failed: ") +
                         static_cast<const char*>(error->GetBufferPointer());
        } else {
            m_lastError = "Root signature serialization failed";
        }
        return false;
    }

    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                        IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) {
        m_lastError = "Failed to create root signature";
        return false;
    }

    return true;
}

bool SimpleOpticalFlow::CreatePipelineState()
{
    // Compile shader at runtime
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompile(
        g_OpticalFlowShaderSource,
        strlen(g_OpticalFlowShaderSource),
        "OpticalFlow.hlsl",
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_0",
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            m_lastError = std::string("Shader compilation failed: ") +
                         static_cast<const char*>(errorBlob->GetBufferPointer());
        } else {
            m_lastError = "Shader compilation failed";
        }
        return false;
    }

    // Create compute pipeline state
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.CS.pShaderBytecode = shaderBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = shaderBlob->GetBufferSize();

    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr)) {
        m_lastError = "Failed to create pipeline state";
        return false;
    }

    return true;
}

bool SimpleOpticalFlow::CreateResources()
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Create motion vector texture (R16G16_SINT)
    D3D12_RESOURCE_DESC mvDesc = {};
    mvDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    mvDesc.Width = m_mvWidth;
    mvDesc.Height = m_mvHeight;
    mvDesc.DepthOrArraySize = 1;
    mvDesc.MipLevels = 1;
    mvDesc.Format = DXGI_FORMAT_R16G16_SINT;
    mvDesc.SampleDesc.Count = 1;
    mvDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &mvDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_motionVectorTexture)
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create motion vector texture";
        return false;
    }

    // Create constant buffer (upload heap for CPU writes)
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = (sizeof(ConstantBufferData) + 255) & ~255; // 256-byte aligned
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.Format = DXGI_FORMAT_UNKNOWN;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = m_device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_constantBuffer)
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create constant buffer";
        return false;
    }

    // Initialize constant buffer
    ConstantBufferData* cbData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    hr = m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&cbData));
    if (SUCCEEDED(hr)) {
        cbData->inputWidth = m_config.width;
        cbData->inputHeight = m_config.height;
        cbData->outputWidth = m_mvWidth;
        cbData->outputHeight = m_mvHeight;
        cbData->blockSize = m_config.blockSize;
        cbData->searchRadius = m_config.searchRadius;
        cbData->minLuminance = 0.0f;
        cbData->maxLuminance = 1.0f;
        m_constantBuffer->Unmap(0, nullptr);
    }

    // Create UAV for motion vectors
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
    uavHandle.ptr += 2 * m_srvUavDescriptorSize; // After 2 SRVs

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16_SINT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    m_device->CreateUnorderedAccessView(m_motionVectorTexture.Get(), nullptr, &uavDesc, uavHandle);

    return true;
}

bool SimpleOpticalFlow::Dispatch(ID3D12Resource* currentFrame,
                                  ID3D12Resource* previousFrame,
                                  ID3D12GraphicsCommandList* commandList)
{
    if (!m_initialized || !currentFrame || !previousFrame || !commandList) {
        m_lastError = "Invalid parameters or not initialized";
        return false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Create SRVs for input textures
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

    // Current frame SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Assuming BGRA input
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(currentFrame, &srvDesc, srvHandle);

    // Previous frame SRV
    srvHandle.ptr += m_srvUavDescriptorSize;
    m_device->CreateShaderResourceView(previousFrame, &srvDesc, srvHandle);

    // Set pipeline state and root signature
    commandList->SetComputeRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_pipelineState.Get());

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Set root parameters
    commandList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, gpuHandle); // SRVs

    gpuHandle.ptr += 2 * m_srvUavDescriptorSize;
    commandList->SetComputeRootDescriptorTable(2, gpuHandle); // UAV

    // Dispatch
    uint32_t threadGroupsX = (m_mvWidth + 7) / 8;
    uint32_t threadGroupsY = (m_mvHeight + 7) / 8;
    commandList->Dispatch(threadGroupsX, threadGroupsY, 1);

    // Update stats
    auto endTime = std::chrono::high_resolution_clock::now();
    double dispatchTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    m_stats.lastDispatchTimeMs = dispatchTimeMs;
    m_stats.framesProcessed++;

    const double alpha = 0.1;
    if (m_stats.framesProcessed == 1) {
        m_stats.avgDispatchTimeMs = dispatchTimeMs;
    } else {
        m_stats.avgDispatchTimeMs = alpha * dispatchTimeMs + (1.0 - alpha) * m_stats.avgDispatchTimeMs;
    }

    return true;
}

} // namespace OSFG
