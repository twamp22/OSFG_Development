// OSFG Frame Interpolation Implementation
// Uses motion vectors to generate intermediate frames via bi-directional warping
// MIT License - Part of Open Source Frame Generation project

#include "frame_interpolation.h"
#include <chrono>

namespace OSFG {

// Optimized HLSL shader for frame interpolation
// Uses 16x16 thread groups and simplified motion vector lookup for better performance
static const char* g_frameInterpolationShader = R"(
// Frame Interpolation Compute Shader - Optimized Version
// Uses bi-directional motion compensation to blend frames

cbuffer Constants : register(b0)
{
    uint g_Width;
    uint g_Height;
    uint g_MVWidth;
    uint g_MVHeight;
    float g_InterpolationFactor;  // 0.0 = prev frame, 1.0 = current frame, 0.5 = middle
    float g_MotionScale;          // Scale for motion vectors (1/16 for sub-pixel)
    float2 g_Padding;
};

// Input textures
Texture2D<float4> g_PreviousFrame : register(t0);
Texture2D<float4> g_CurrentFrame : register(t1);
Texture2D<int2> g_MotionVectors : register(t2);

// Output texture
RWTexture2D<float4> g_InterpolatedFrame : register(u0);

// Samplers
SamplerState g_LinearSampler : register(s0);

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Check bounds
    if (dispatchThreadId.x >= g_Width || dispatchThreadId.y >= g_Height)
        return;

    uint2 pixel = dispatchThreadId.xy;
    float2 uv = (float2(pixel) + 0.5) / float2(g_Width, g_Height);

    // Get motion vector at this location using nearest neighbor (faster than bilinear)
    // Map pixel to MV coordinates
    uint2 mvPixel = uint2(uv * float2(g_MVWidth, g_MVHeight));
    mvPixel = min(mvPixel, uint2(g_MVWidth - 1, g_MVHeight - 1));

    float2 motion = float2(g_MotionVectors[mvPixel]) * g_MotionScale;
    float2 motionUV = motion / float2(g_Width, g_Height);

    float t = g_InterpolationFactor;

    // Bi-directional warping
    float2 uvPrev = uv - motionUV * (1.0 - t);
    float2 uvCurr = uv + motionUV * t;

    // Clamp UVs to valid range
    uvPrev = saturate(uvPrev);
    uvCurr = saturate(uvCurr);

    // Sample both frames with hardware linear filtering
    float4 colorPrev = g_PreviousFrame.SampleLevel(g_LinearSampler, uvPrev, 0);
    float4 colorCurr = g_CurrentFrame.SampleLevel(g_LinearSampler, uvCurr, 0);

    // Simple weighted blend
    float4 result = colorPrev * (1.0 - t) + colorCurr * t;
    result.a = 1.0;

    g_InterpolatedFrame[pixel] = result;
}
)";

FrameInterpolation::FrameInterpolation() = default;

FrameInterpolation::~FrameInterpolation()
{
    Shutdown();
}

bool FrameInterpolation::Initialize(ID3D12Device* device, const FrameInterpolationConfig& config)
{
    if (m_initialized) {
        m_lastError = "Already initialized";
        return false;
    }

    if (!device) {
        m_lastError = "Invalid D3D12 device";
        return false;
    }

    m_device = device;
    m_config = config;

    // Create resources in order
    if (!CreateRootSignature()) return false;
    if (!CreatePipelineState()) return false;
    if (!CreateDescriptorHeaps()) return false;
    if (!CreateResources()) return false;

    m_initialized = true;
    return true;
}

void FrameInterpolation::Shutdown()
{
    m_pipelineState.Reset();
    m_rootSignature.Reset();
    m_interpolatedFrame.Reset();
    m_constantBuffer.Reset();
    m_srvUavHeap.Reset();
    m_device.Reset();
    m_initialized = false;
}

void FrameInterpolation::SetInterpolationFactor(float factor)
{
    m_config.interpolationFactor = (factor < 0.0f) ? 0.0f : ((factor > 1.0f) ? 1.0f : factor);
}

bool FrameInterpolation::CreateRootSignature()
{
    // Root parameters:
    // [0] CBV - Constants
    // [1] Descriptor table - SRVs (previous frame, current frame, motion vectors)
    // [2] Descriptor table - UAV (output)
    // [3] Static sampler

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;  // Previous, Current, Motion Vectors
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

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

    // Static sampler
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            m_lastError = "Root signature serialization failed: " +
                         std::string((char*)error->GetBufferPointer());
        } else {
            m_lastError = "Root signature serialization failed";
        }
        return false;
    }

    hr = m_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                        signature->GetBufferSize(),
                                        IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) {
        m_lastError = "Failed to create root signature";
        return false;
    }

    return true;
}

bool FrameInterpolation::CreatePipelineState()
{
    // Compile the compute shader at runtime
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompile(
        g_frameInterpolationShader,
        strlen(g_frameInterpolationShader),
        "FrameInterpolation.hlsl",
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
            m_lastError = "Shader compilation failed: " +
                         std::string((char*)errorBlob->GetBufferPointer());
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

bool FrameInterpolation::CreateDescriptorHeaps()
{
    // Create SRV/UAV heap: 3 SRVs + 1 UAV = 4 descriptors
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 4;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvUavHeap));
    if (FAILED(hr)) {
        m_lastError = "Failed to create descriptor heap";
        return false;
    }

    m_srvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    return true;
}

bool FrameInterpolation::CreateResources()
{
    // Create output texture (interpolated frame)
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_config.width;
    texDesc.Height = m_config.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = m_config.format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_interpolatedFrame)
    );

    if (FAILED(hr)) {
        m_lastError = "Failed to create interpolated frame texture";
        return false;
    }

    // Create constant buffer
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = (sizeof(ConstantBufferData) + 255) & ~255;  // 256-byte aligned
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

    return true;
}

bool FrameInterpolation::Dispatch(ID3D12Resource* previousFrame,
                                   ID3D12Resource* currentFrame,
                                   ID3D12Resource* motionVectors,
                                   ID3D12GraphicsCommandList* commandList)
{
    if (!m_initialized) {
        m_lastError = "Not initialized";
        return false;
    }

    if (!previousFrame || !currentFrame || !motionVectors || !commandList) {
        m_lastError = "Invalid parameters";
        return false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Transition interpolated frame to UAV state if it was left in shader resource state
    if (m_stats.framesInterpolated > 0) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_interpolatedFrame.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    // Get motion vector dimensions from resource
    D3D12_RESOURCE_DESC mvDesc = motionVectors->GetDesc();
    uint32_t mvWidth = static_cast<uint32_t>(mvDesc.Width);
    uint32_t mvHeight = mvDesc.Height;

    // Update constant buffer
    ConstantBufferData cbData = {};
    cbData.width = m_config.width;
    cbData.height = m_config.height;
    cbData.mvWidth = mvWidth;
    cbData.mvHeight = mvHeight;
    cbData.interpolationFactor = m_config.interpolationFactor;
    cbData.motionScale = 1.0f / 16.0f;

    void* mappedData;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_constantBuffer->Map(0, &readRange, &mappedData);
    if (SUCCEEDED(hr)) {
        memcpy(mappedData, &cbData, sizeof(cbData));
        m_constantBuffer->Unmap(0, nullptr);
    }

    // Only recreate descriptors if textures changed (major performance optimization)
    bool needsDescriptorUpdate = !m_descriptorsValid ||
                                  m_cachedPrevFrame != previousFrame ||
                                  m_cachedCurrFrame != currentFrame ||
                                  m_cachedMotionVectors != motionVectors;

    if (needsDescriptorUpdate) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

        // SRV for previous frame
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = previousFrame->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(previousFrame, &srvDesc, cpuHandle);

        // SRV for current frame
        cpuHandle.ptr += m_srvUavDescriptorSize;
        srvDesc.Format = currentFrame->GetDesc().Format;
        m_device->CreateShaderResourceView(currentFrame, &srvDesc, cpuHandle);

        // SRV for motion vectors (R16G16_SINT)
        cpuHandle.ptr += m_srvUavDescriptorSize;
        srvDesc.Format = DXGI_FORMAT_R16G16_SINT;
        m_device->CreateShaderResourceView(motionVectors, &srvDesc, cpuHandle);

        // UAV for output
        cpuHandle.ptr += m_srvUavDescriptorSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = m_config.format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->CreateUnorderedAccessView(m_interpolatedFrame.Get(), nullptr, &uavDesc, cpuHandle);

        // Cache the texture pointers
        m_cachedPrevFrame = previousFrame;
        m_cachedCurrFrame = currentFrame;
        m_cachedMotionVectors = motionVectors;
        m_descriptorsValid = true;
    }

    // Set pipeline state
    commandList->SetComputeRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_pipelineState.Get());

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_srvUavHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Set root parameters
    commandList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, gpuHandle);  // SRVs

    gpuHandle.ptr += m_srvUavDescriptorSize * 3;
    commandList->SetComputeRootDescriptorTable(2, gpuHandle);  // UAV

    // Dispatch compute shader with 16x16 thread groups
    uint32_t dispatchX = (m_config.width + 15) / 16;
    uint32_t dispatchY = (m_config.height + 15) / 16;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // Transition interpolated frame from UAV to PIXEL_SHADER_RESOURCE for presentation
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_interpolatedFrame.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Update statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    double dispatchTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    m_stats.lastInterpolationTimeMs = dispatchTimeMs;
    m_stats.framesInterpolated++;
    m_stats.avgInterpolationTimeMs = (m_stats.avgInterpolationTimeMs * (m_stats.framesInterpolated - 1) +
                                       dispatchTimeMs) / m_stats.framesInterpolated;

    return true;
}

} // namespace OSFG
