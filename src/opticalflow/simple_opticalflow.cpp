// OSFG Simple Optical Flow Implementation
// Block-matching optical flow for Phase 1 proof of concept
// MIT License - Part of Open Source Frame Generation project

#include "simple_opticalflow.h"
#include <chrono>
#include <fstream>

// Optimized Optical Flow Shader
// Uses shared memory caching and three-step search for better performance
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

// Shared memory for caching - sized for 8x8 block + 16 pixel search radius on each side
// Max size: (8 + 32) x (8 + 32) = 40x40 = 1600 floats per frame = 3200 total
#define TILE_SIZE 8
#define MAX_SEARCH 16
#define SHARED_SIZE (TILE_SIZE + MAX_SEARCH * 2)
groupshared float s_CurrentLum[TILE_SIZE][TILE_SIZE];
groupshared float s_PreviousLum[SHARED_SIZE][SHARED_SIZE];

float RGBToLuminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Compute SAD between current block (in shared mem) and a region of previous (in shared mem)
float ComputeSADShared(int2 offset, int searchRadius)
{
    float sad = 0.0;
    int baseOffset = searchRadius;  // Offset in shared memory for center

    [unroll]
    for (int y = 0; y < TILE_SIZE; y++)
    {
        [unroll]
        for (int x = 0; x < TILE_SIZE; x++)
        {
            float currLum = s_CurrentLum[y][x];
            float prevLum = s_PreviousLum[baseOffset + offset.y + y][baseOffset + offset.x + x];
            sad += abs(currLum - prevLum);
        }
    }
    return sad;
}

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CSMain(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint3 dispatchId : SV_DispatchThreadID)
{
    // Each thread group processes one block
    int2 blockPos = int2(groupId.xy) * TILE_SIZE;
    int2 localId = int2(threadId.xy);
    int localIdx = localId.y * TILE_SIZE + localId.x;

    // Bounds check for output
    if (groupId.x >= g_OutputSize.x || groupId.y >= g_OutputSize.y)
        return;

    int searchRadius = min((int)g_SearchRadius, MAX_SEARCH);
    int sharedSize = TILE_SIZE + searchRadius * 2;

    // Load current block into shared memory (one pixel per thread)
    {
        int2 pixelPos = blockPos + localId;
        pixelPos = clamp(pixelPos, int2(0, 0), int2(g_InputSize) - 1);
        s_CurrentLum[localId.y][localId.x] = RGBToLuminance(g_CurrentFrame[pixelPos].rgb);
    }

    // Load previous frame search region into shared memory
    // Need to load (TILE_SIZE + 2*searchRadius)^2 pixels with only TILE_SIZE^2 threads
    int totalPrevPixels = sharedSize * sharedSize;
    int pixelsPerThread = (totalPrevPixels + TILE_SIZE * TILE_SIZE - 1) / (TILE_SIZE * TILE_SIZE);

    for (int i = 0; i < pixelsPerThread; i++)
    {
        int pixelIdx = localIdx + i * (TILE_SIZE * TILE_SIZE);
        if (pixelIdx < totalPrevPixels)
        {
            int sy = pixelIdx / sharedSize;
            int sx = pixelIdx % sharedSize;

            int2 pixelPos = blockPos + int2(sx, sy) - int2(searchRadius, searchRadius);
            pixelPos = clamp(pixelPos, int2(0, 0), int2(g_InputSize) - 1);

            s_PreviousLum[sy][sx] = RGBToLuminance(g_PreviousFrame[pixelPos].rgb);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Only thread 0 performs the search and writes the result
    if (localIdx != 0)
        return;

    float bestSAD = 1e10;
    int2 bestMotion = int2(0, 0);

    // Three-Step Search algorithm for fast motion estimation
    // Step sizes: searchRadius/2, searchRadius/4, 1 (or similar progression)
    int step = max(searchRadius / 2, 1);
    int2 center = int2(0, 0);

    while (step >= 1)
    {
        // Search 9 positions around center at current step size
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int2 offset = center + int2(dx, dy) * step;

                // Bounds check
                if (offset.x < -searchRadius || offset.x > searchRadius ||
                    offset.y < -searchRadius || offset.y > searchRadius)
                    continue;

                // Check if search position is valid in image
                int2 searchPos = blockPos + offset;
                if (searchPos.x < 0 || searchPos.y < 0 ||
                    searchPos.x + TILE_SIZE > (int)g_InputSize.x ||
                    searchPos.y + TILE_SIZE > (int)g_InputSize.y)
                    continue;

                float sad = ComputeSADShared(offset, searchRadius);

                if (sad < bestSAD)
                {
                    bestSAD = sad;
                    bestMotion = offset;
                }
            }
        }

        // Move center to best position and reduce step
        center = bestMotion;
        step = step / 2;
    }

    // Final refinement: check immediate neighbors of best position
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0) continue;

            int2 offset = bestMotion + int2(dx, dy);

            if (offset.x < -searchRadius || offset.x > searchRadius ||
                offset.y < -searchRadius || offset.y > searchRadius)
                continue;

            int2 searchPos = blockPos + offset;
            if (searchPos.x < 0 || searchPos.y < 0 ||
                searchPos.x + TILE_SIZE > (int)g_InputSize.x ||
                searchPos.y + TILE_SIZE > (int)g_InputSize.y)
                continue;

            float sad = ComputeSADShared(offset, searchRadius);

            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestMotion = offset;
            }
        }
    }

    // Write result (scaled by 16 for sub-pixel precision)
    g_MotionVectors[groupId.xy] = bestMotion * 16;
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

    // Create GPU timestamp query heap (2 queries: start and end)
    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDesc.Count = 2;
    hr = m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_timestampQueryHeap));
    if (FAILED(hr)) {
        // Non-fatal: GPU timing just won't be available
        m_gpuTimingEnabled = false;
    } else {
        // Create readback buffer for timestamps
        D3D12_RESOURCE_DESC readbackDesc = {};
        readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackDesc.Width = 2 * sizeof(uint64_t);
        readbackDesc.Height = 1;
        readbackDesc.DepthOrArraySize = 1;
        readbackDesc.MipLevels = 1;
        readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
        readbackDesc.SampleDesc.Count = 1;
        readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES readbackHeap = {};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        hr = m_device->CreateCommittedResource(
            &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_timestampReadbackBuffer));

        m_gpuTimingEnabled = SUCCEEDED(hr);
    }

    return true;
}

void SimpleOpticalFlow::SetTimestampFrequency(ID3D12CommandQueue* cmdQueue)
{
    if (cmdQueue && m_gpuTimingEnabled) {
        cmdQueue->GetTimestampFrequency(&m_gpuTimestampFrequency);
    }
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

    // Read back previous frame's GPU timestamps (if available)
    if (m_gpuTimingEnabled && m_stats.framesProcessed > 0) {
        D3D12_RANGE readRange = { 0, 2 * sizeof(uint64_t) };
        uint64_t* timestamps = nullptr;
        if (SUCCEEDED(m_timestampReadbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&timestamps)))) {
            uint64_t startTs = timestamps[0];
            uint64_t endTs = timestamps[1];
            D3D12_RANGE writeRange = { 0, 0 };
            m_timestampReadbackBuffer->Unmap(0, &writeRange);

            if (m_gpuTimestampFrequency > 0 && endTs > startTs) {
                double gpuTimeMs = (double)(endTs - startTs) * 1000.0 / (double)m_gpuTimestampFrequency;
                m_stats.lastGpuTimeMs = gpuTimeMs;

                const double alpha = 0.1;
                if (m_stats.framesProcessed == 1) {
                    m_stats.avgGpuTimeMs = gpuTimeMs;
                } else {
                    m_stats.avgGpuTimeMs = alpha * gpuTimeMs + (1.0 - alpha) * m_stats.avgGpuTimeMs;
                }
            }
        }
    }

    // Transition motion vector texture back to UAV state if it was left in shader resource state
    if (m_stats.framesProcessed > 0) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_motionVectorTexture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    // Only recreate SRVs if textures changed (descriptor caching)
    bool needsDescriptorUpdate = !m_descriptorsValid ||
                                  m_cachedCurrentFrame != currentFrame ||
                                  m_cachedPreviousFrame != previousFrame;

    if (needsDescriptorUpdate) {
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

        // Current frame SRV - use the actual texture format
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = currentFrame->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        m_device->CreateShaderResourceView(currentFrame, &srvDesc, srvHandle);

        // Previous frame SRV - use its actual format
        srvHandle.ptr += m_srvUavDescriptorSize;
        srvDesc.Format = previousFrame->GetDesc().Format;
        m_device->CreateShaderResourceView(previousFrame, &srvDesc, srvHandle);

        m_cachedCurrentFrame = currentFrame;
        m_cachedPreviousFrame = previousFrame;
        m_descriptorsValid = true;
    }

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

    // GPU timestamp: start
    if (m_gpuTimingEnabled) {
        commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }

    // Dispatch - one thread group per block (each group is 8x8 threads working together)
    commandList->Dispatch(m_mvWidth, m_mvHeight, 1);

    // GPU timestamp: end
    if (m_gpuTimingEnabled) {
        commandList->EndQuery(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commandList->ResolveQueryData(m_timestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                       0, 2, m_timestampReadbackBuffer.Get(), 0);
    }

    // Transition motion vector texture from UAV to PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_motionVectorTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

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
