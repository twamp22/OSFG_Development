// OSFG Optical Flow Module Implementation
// Wraps AMD FidelityFX FSR 3 Optical Flow for standalone use
// MIT License - Part of Open Source Frame Generation project

#include "osfg_opticalflow.h"

// FidelityFX SDK includes
#define FFX_CPU
#include "../../external/FidelityFX-SDK/Kits/FidelityFX/api/include/ffx_api_types.h"
#include "../../external/FidelityFX-SDK/Kits/FidelityFX/api/internal/ffx_internal_types.h"
#include "../../external/FidelityFX-SDK/Kits/FidelityFX/framegeneration/fsr3/include/ffx_opticalflow.h"
#include "../../external/FidelityFX-SDK/Kits/FidelityFX/backend/dx12/ffx_dx12.h"

#include <chrono>
#include <cassert>

namespace OSFG {

// Number of contexts needed for optical flow
static constexpr size_t FFX_OPTICAL_FLOW_CONTEXT_COUNT = 1;

// Block size used by optical flow (hardcoded in FSR 3)
static constexpr uint32_t OPTICAL_FLOW_BLOCK_SIZE = 8;

OpticalFlow::OpticalFlow()
    : m_output{}
    , m_stats{}
{
}

OpticalFlow::~OpticalFlow()
{
    Shutdown();
}

void OpticalFlow::GetMotionVectorSize(uint32_t inputWidth, uint32_t inputHeight,
                                      uint32_t& outWidth, uint32_t& outHeight)
{
    outWidth = (inputWidth + OPTICAL_FLOW_BLOCK_SIZE - 1) / OPTICAL_FLOW_BLOCK_SIZE;
    outHeight = (inputHeight + OPTICAL_FLOW_BLOCK_SIZE - 1) / OPTICAL_FLOW_BLOCK_SIZE;
}

bool OpticalFlow::Initialize(ID3D12Device* device,
                             ID3D12CommandQueue* commandQueue,
                             const OpticalFlowConfig& config)
{
    if (m_initialized) {
        Shutdown();
    }

    if (!device || !commandQueue) {
        return false;
    }

    m_device = device;
    m_commandQueue = commandQueue;
    m_config = config;

    // Reset stats
    m_stats = {};
    m_frameIndex = 0;

    // Get scratch memory size for DX12 backend
    m_scratchBufferSize = ffxGetScratchMemorySizeDX12(FFX_OPTICAL_FLOW_CONTEXT_COUNT);
    if (m_scratchBufferSize == 0) {
        return false;
    }

    // Allocate scratch buffer
    m_scratchBuffer = std::make_unique<uint8_t[]>(m_scratchBufferSize);
    if (!m_scratchBuffer) {
        return false;
    }
    memset(m_scratchBuffer.get(), 0, m_scratchBufferSize);

    // Allocate FfxInterface
    m_ffxInterface = new FfxInterface();
    if (!m_ffxInterface) {
        return false;
    }
    memset(m_ffxInterface, 0, sizeof(FfxInterface));

    // Get DX12 device handle for FidelityFX
    FfxDevice ffxDevice = ffxGetDeviceDX12(device);

    // Initialize the FfxInterface with DX12 backend
    FfxErrorCode result = ffxGetInterfaceDX12(
        m_ffxInterface,
        ffxDevice,
        m_scratchBuffer.get(),
        m_scratchBufferSize,
        FFX_OPTICAL_FLOW_CONTEXT_COUNT
    );

    if (result != FFX_OK) {
        delete m_ffxInterface;
        m_ffxInterface = nullptr;
        return false;
    }

    // Create FFX optical flow context first (required before getting resource descriptions)
    if (!CreateFfxContext()) {
        Shutdown();
        return false;
    }

    // Create output resources after context is created
    if (!CreateResources()) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

void OpticalFlow::Shutdown()
{
    DestroyFfxContext();
    DestroyResources();

    if (m_ffxInterface) {
        delete m_ffxInterface;
        m_ffxInterface = nullptr;
    }

    m_scratchBuffer.reset();
    m_scratchBufferSize = 0;

    m_device.Reset();
    m_commandQueue.Reset();

    m_initialized = false;
    m_frameIndex = 0;
}

bool OpticalFlow::CreateResources()
{
    // Calculate motion vector texture size
    uint32_t mvWidth, mvHeight;
    GetMotionVectorSize(m_config.width, m_config.height, mvWidth, mvHeight);

    // Create motion vector texture (R16G16_SINT)
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC mvDesc = {};
    mvDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    mvDesc.Width = mvWidth;
    mvDesc.Height = mvHeight;
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
        return false;
    }

    // Create scene change detection texture (R32_UINT, 3x1)
    D3D12_RESOURCE_DESC scdDesc = {};
    scdDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    scdDesc.Width = 3;
    scdDesc.Height = 1;
    scdDesc.DepthOrArraySize = 1;
    scdDesc.MipLevels = 1;
    scdDesc.Format = DXGI_FORMAT_R32_UINT;
    scdDesc.SampleDesc.Count = 1;
    scdDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &scdDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_sceneChangeTexture)
    );

    if (FAILED(hr)) {
        m_motionVectorTexture.Reset();
        return false;
    }

    // Update output structure
    m_output.motionVectors = m_motionVectorTexture.Get();
    m_output.sceneChangeData = m_sceneChangeTexture.Get();
    m_output.motionVectorWidth = mvWidth;
    m_output.motionVectorHeight = mvHeight;
    m_output.sceneChangeDetected = false;

    return true;
}

void OpticalFlow::DestroyResources()
{
    m_motionVectorTexture.Reset();
    m_sceneChangeTexture.Reset();
    m_output = {};
}

bool OpticalFlow::CreateFfxContext()
{
    if (!m_ffxInterface) {
        return false;
    }

    // Allocate context structure (fixed size defined by FFX_OPTICALFLOW_CONTEXT_SIZE)
    m_ffxContext = new FfxOpticalflowContext();
    if (!m_ffxContext) {
        return false;
    }
    memset(m_ffxContext, 0, sizeof(FfxOpticalflowContext));

    // Set up context description
    FfxOpticalflowContextDescription contextDesc = {};
    contextDesc.backendInterface = *m_ffxInterface;
    contextDesc.resolution.width = m_config.width;
    contextDesc.resolution.height = m_config.height;
    contextDesc.flags = 0;

    // Create the optical flow context
    FfxErrorCode result = ffxOpticalflowContextCreate(m_ffxContext, &contextDesc);
    if (result != FFX_OK) {
        delete m_ffxContext;
        m_ffxContext = nullptr;
        return false;
    }

    return true;
}

void OpticalFlow::DestroyFfxContext()
{
    if (m_ffxContext) {
        ffxOpticalflowContextDestroy(m_ffxContext);
        delete m_ffxContext;
        m_ffxContext = nullptr;
    }
}

bool OpticalFlow::Dispatch(ID3D12Resource* inputTexture,
                           ID3D12GraphicsCommandList* commandList,
                           bool reset)
{
    if (!m_initialized || !inputTexture || !commandList) {
        return false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Get input resource description
    FfxApiResourceDescription inputDesc = ffxGetResourceDescriptionDX12(
        inputTexture,
        FFX_API_RESOURCE_USAGE_READ_ONLY
    );

    // Create FFX resource wrapper for input
    FfxApiResource ffxInput = ffxGetResourceDX12(
        inputTexture,
        inputDesc,
        L"OSFG_InputColor",
        FFX_API_RESOURCE_STATE_COMPUTE_READ
    );

    // Create FFX resource wrapper for motion vector output
    FfxApiResourceDescription mvDesc = ffxGetResourceDescriptionDX12(
        m_motionVectorTexture.Get(),
        FFX_API_RESOURCE_USAGE_UAV
    );
    FfxApiResource ffxMotionVectors = ffxGetResourceDX12(
        m_motionVectorTexture.Get(),
        mvDesc,
        L"OSFG_MotionVectors",
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
    );

    // Create FFX resource wrapper for scene change output
    FfxApiResourceDescription scdDesc = ffxGetResourceDescriptionDX12(
        m_sceneChangeTexture.Get(),
        FFX_API_RESOURCE_USAGE_UAV
    );
    FfxApiResource ffxSceneChange = ffxGetResourceDX12(
        m_sceneChangeTexture.Get(),
        scdDesc,
        L"OSFG_SceneChange",
        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
    );

    // Get command list wrapper
    FfxCommandList ffxCommandList = ffxGetCommandListDX12(commandList);

    // Set up dispatch parameters
    FfxOpticalflowDispatchDescription dispatchDesc = {};
    dispatchDesc.commandList = ffxCommandList;
    dispatchDesc.color = ffxInput;
    dispatchDesc.opticalFlowVector = ffxMotionVectors;
    dispatchDesc.opticalFlowSCD = ffxSceneChange;
    dispatchDesc.reset = reset || (m_frameIndex == 0);
    dispatchDesc.backbufferTransferFunction = m_config.enableHDR ?
        FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ :
        FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    dispatchDesc.minMaxLuminance = { 0.0f, 1.0f };

    // Dispatch optical flow
    FfxErrorCode result = ffxOpticalflowContextDispatch(m_ffxContext, &dispatchDesc);
    if (result != FFX_OK) {
        return false;
    }

    // Update timing statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    double dispatchTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    m_stats.lastDispatchTimeMs = dispatchTimeMs;
    m_stats.totalFramesProcessed++;

    // Update rolling average
    const double alpha = 0.1; // Smoothing factor
    if (m_stats.totalFramesProcessed == 1) {
        m_stats.avgDispatchTimeMs = dispatchTimeMs;
    } else {
        m_stats.avgDispatchTimeMs = alpha * dispatchTimeMs + (1.0 - alpha) * m_stats.avgDispatchTimeMs;
    }

    m_frameIndex++;
    return true;
}

} // namespace OSFG
