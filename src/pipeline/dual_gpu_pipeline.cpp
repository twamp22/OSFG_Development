// OSFG - Open Source Frame Generation
// Dual-GPU Pipeline Implementation

#include "dual_gpu_pipeline.h"
#include "capture/dxgi_capture.h"
#include "transfer/gpu_transfer.h"
#include "opticalflow/simple_opticalflow.h"
#include "interpolation/frame_interpolation.h"
#include "presentation/simple_presenter.h"
#include "ffx/ffx_loader.h"
#include "ffx/ffx_framegen.h"

#include <algorithm>
#include <sstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace osfg {

DualGPUPipeline::DualGPUPipeline() = default;

DualGPUPipeline::~DualGPUPipeline() {
    Shutdown();
}

bool DualGPUPipeline::Initialize(const DualGPUConfig& config) {
    if (m_initialized) {
        Shutdown();
    }

    m_config = config;
    m_frameGenEnabled = config.enableFrameGen;

    // Calculate target frame time based on multiplier
    // Assume 60fps base, multiply by frame gen factor
    double baseFrameTimeMs = 16.667;  // 60 fps
    m_targetFrameTimeMs = baseFrameTimeMs / static_cast<int>(config.multiplier);

    // Select backend
    if (config.backend == FrameGenBackend::Auto) {
        // Auto-select: prefer FidelityFX if available
        m_activeBackend = IsFidelityFXAvailable() ? FrameGenBackend::FidelityFX : FrameGenBackend::Native;
    } else if (config.backend == FrameGenBackend::FidelityFX) {
        // User requested FFX, verify it's available
        if (IsFidelityFXAvailable()) {
            m_activeBackend = FrameGenBackend::FidelityFX;
        } else {
            SetError("FidelityFX backend requested but DLLs not found. Falling back to Native.");
            m_activeBackend = FrameGenBackend::Native;
        }
    } else {
        m_activeBackend = FrameGenBackend::Native;
    }

    // Initialize pipeline stages
    if (!InitializeCapture()) {
        Shutdown();
        return false;
    }

    if (!InitializeTransfer()) {
        Shutdown();
        return false;
    }

    if (!InitializeCompute()) {
        Shutdown();
        return false;
    }

    if (!InitializePresentation()) {
        Shutdown();
        return false;
    }

    m_initialized = true;
    ResetStats();

    return true;
}

void DualGPUPipeline::Shutdown() {
    Stop();

    // Wait for GPU work to complete
    if (m_computeFence && m_computeFenceEvent) {
        if (m_computeFence->GetCompletedValue() < m_computeFenceValue) {
            m_computeFence->SetEventOnCompletion(m_computeFenceValue, m_computeFenceEvent);
            WaitForSingleObject(m_computeFenceEvent, 5000);
        }
    }

    // Release in reverse order
    // FFX backend
    m_ffxFrameGen.reset();

    // Native backend
    m_presenter.reset();
    m_interpolation.reset();
    m_opticalFlow.reset();

    for (auto& frame : m_generatedFrames) {
        frame.Reset();
    }

    if (m_computeFenceEvent) {
        CloseHandle(m_computeFenceEvent);
        m_computeFenceEvent = nullptr;
    }

    m_computeFence.Reset();
    m_computeCommandList.Reset();
    m_computeAllocator.Reset();
    m_computeQueue.Reset();
    m_computeDevice.Reset();

    m_transfer.reset();
    m_capture.reset();

    m_initialized = false;
}

bool DualGPUPipeline::InitializeCapture() {
    m_capture = std::make_unique<DXGICapture>();

    CaptureConfig captureConfig;
    captureConfig.adapterIndex = m_config.primaryGPU;
    captureConfig.outputIndex = m_config.captureMonitor;
    captureConfig.timeoutMs = m_config.captureTimeoutMs;

    if (!m_capture->Initialize(captureConfig)) {
        SetError("Failed to initialize capture: " + m_capture->GetLastError());
        return false;
    }

    // Update config with actual capture dimensions
    m_config.width = m_capture->GetWidth();
    m_config.height = m_capture->GetHeight();

    return true;
}

bool DualGPUPipeline::InitializeTransfer() {
    m_transfer = std::make_unique<GPUTransfer>();

    TransferConfig transferConfig;
    transferConfig.sourceAdapterIndex = m_config.primaryGPU;
    transferConfig.destAdapterIndex = m_config.secondaryGPU;
    transferConfig.width = m_config.width;
    transferConfig.height = m_config.height;
    transferConfig.bufferCount = m_config.transferBufferCount;
    transferConfig.preferPeerToPeer = m_config.preferPeerToPeer;

    if (!m_transfer->Initialize(transferConfig)) {
        SetError("Failed to initialize transfer: " + m_transfer->GetLastError());
        return false;
    }

    // Get the destination device for compute operations
    m_computeDevice = m_transfer->GetDestDevice();
    m_computeQueue = m_transfer->GetDestCommandQueue();

    return true;
}

bool DualGPUPipeline::InitializeCompute() {
    HRESULT hr;

    // Create command allocator
    hr = m_computeDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&m_computeAllocator));
    if (FAILED(hr)) {
        SetError("Failed to create compute command allocator");
        return false;
    }

    // Create command list
    hr = m_computeDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_computeAllocator.Get(), nullptr,
        IID_PPV_ARGS(&m_computeCommandList));
    if (FAILED(hr)) {
        SetError("Failed to create compute command list");
        return false;
    }
    m_computeCommandList->Close();

    // Create fence
    hr = m_computeDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence));
    if (FAILED(hr)) {
        SetError("Failed to create compute fence");
        return false;
    }

    m_computeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_computeFenceEvent) {
        SetError("Failed to create compute fence event");
        return false;
    }

    // Initialize optical flow
    m_opticalFlow = std::make_unique<OSFG::SimpleOpticalFlow>();

    OSFG::SimpleOpticalFlowConfig ofConfig;
    ofConfig.width = m_config.width;
    ofConfig.height = m_config.height;
    ofConfig.blockSize = m_config.opticalFlowBlockSize;
    ofConfig.searchRadius = m_config.opticalFlowSearchRadius;

    if (!m_opticalFlow->Initialize(m_computeDevice.Get(), ofConfig)) {
        SetError("Failed to initialize optical flow: " + m_opticalFlow->GetLastError());
        return false;
    }

    // Initialize interpolation
    m_interpolation = std::make_unique<OSFG::FrameInterpolation>();

    OSFG::FrameInterpolationConfig interpConfig;
    interpConfig.width = m_config.width;
    interpConfig.height = m_config.height;

    if (!m_interpolation->Initialize(m_computeDevice.Get(), interpConfig)) {
        SetError("Failed to initialize interpolation: " + m_interpolation->GetLastError());
        return false;
    }

    // Create frame buffers for generated frames
    m_generatedFrameCount = static_cast<uint32_t>(m_config.multiplier) - 1;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_config.width;
    texDesc.Height = m_config.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (uint32_t i = 0; i < m_generatedFrameCount; i++) {
        hr = m_computeDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE,
            &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&m_generatedFrames[i]));

        if (FAILED(hr)) {
            SetError("Failed to create generated frame buffer " + std::to_string(i));
            return false;
        }
    }

    return true;
}

bool DualGPUPipeline::InitializePresentation() {
    m_presenter = std::make_unique<OSFG::SimplePresenter>();

    OSFG::PresenterConfig presConfig;
    presConfig.width = m_config.width;
    presConfig.height = m_config.height;
    presConfig.vsync = m_config.vsync;
    presConfig.windowed = m_config.borderlessWindow;
    presConfig.windowTitle = m_config.windowTitle;
    presConfig.bufferCount = 2;

    if (!m_presenter->Initialize(m_computeDevice.Get(), m_computeQueue.Get(), presConfig)) {
        SetError("Failed to initialize presenter: " + m_presenter->GetLastError());
        return false;
    }

    return true;
}

bool DualGPUPipeline::Start() {
    if (!m_initialized) {
        SetError("Pipeline not initialized");
        return false;
    }

    if (m_running) {
        return true;  // Already running
    }

    m_running = true;
    m_lastPresentTime = std::chrono::high_resolution_clock::now();

    return true;
}

void DualGPUPipeline::Stop() {
    m_running = false;
}

bool DualGPUPipeline::ProcessFrame() {
    if (!m_initialized || !m_running) {
        return false;
    }

    m_frameStartTime = std::chrono::high_resolution_clock::now();

    // Stage 1: Capture frame from primary GPU
    if (!CaptureFrame()) {
        return false;
    }

    // Stage 2: Transfer to secondary GPU
    if (!TransferFrame()) {
        return false;
    }

    // Stage 3: Compute optical flow
    if (!ComputeOpticalFlow()) {
        return false;
    }

    // Stage 4: Generate interpolated frames
    if (m_frameGenEnabled) {
        if (!GenerateFrames()) {
            return false;
        }
    }

    // Stage 5: Present frames with proper pacing
    if (!PresentFrames()) {
        return false;
    }

    // Update statistics
    UpdateStats();

    // Advance transfer buffer
    m_transfer->AdvanceBuffer();

    return true;
}

void DualGPUPipeline::Run() {
    if (!Start()) {
        return;
    }

    while (m_running && IsWindowOpen()) {
        // Process window messages
        if (!m_presenter->ProcessMessages()) {
            break;
        }

        // Process one frame
        ProcessFrame();
    }

    Stop();
}

bool DualGPUPipeline::CaptureFrame() {
    auto startTime = std::chrono::high_resolution_clock::now();

    CapturedFrame frame;
    if (!m_capture->CaptureFrame(frame)) {
        // No new frame available - not an error
        return false;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double captureTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.captureTimeMs = captureTimeMs;
        m_stats.baseFamesCaptured++;
    }

    // Transfer the captured frame immediately
    // The D3D11 texture needs to be copied to D3D12 via staging
    if (!m_transfer->TransferFrame(nullptr)) {
        // Note: TransferFrame with nullptr uses internal staging
        // We need to modify this to accept the D3D11 texture
        // For now, this is a placeholder
    }

    m_capture->ReleaseFrame();

    return true;
}

bool DualGPUPipeline::TransferFrame() {
    auto startTime = std::chrono::high_resolution_clock::now();

    // Wait for transfer to complete
    m_transfer->WaitForTransfer();

    auto endTime = std::chrono::high_resolution_clock::now();
    double transferTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.transferTimeMs = transferTimeMs;
        m_stats.transferThroughputMBps = m_transfer->GetStats().throughputMBps;
        m_stats.usingPeerToPeer = (m_transfer->GetTransferMethod() == TransferMethod::CrossAdapterHeap);
    }

    return true;
}

bool DualGPUPipeline::ComputeOpticalFlow() {
    auto startTime = std::chrono::high_resolution_clock::now();

    // Reset command list
    HRESULT hr = m_computeAllocator->Reset();
    if (FAILED(hr)) return false;

    hr = m_computeCommandList->Reset(m_computeAllocator.Get(), nullptr);
    if (FAILED(hr)) return false;

    // Get current and previous frames from transfer
    ID3D12Resource* currentFrame = m_transfer->GetDestinationTexture();
    ID3D12Resource* previousFrame = m_transfer->GetPreviousTexture();

    if (!currentFrame || !previousFrame) {
        // Need at least 2 frames for optical flow
        m_computeCommandList->Close();
        return true;  // Not an error, just need more frames
    }

    // Compute optical flow
    if (!m_opticalFlow->Dispatch(currentFrame, previousFrame, m_computeCommandList.Get())) {
        SetError("Optical flow computation failed");
        return false;
    }

    // Close and execute command list
    hr = m_computeCommandList->Close();
    if (FAILED(hr)) return false;

    ID3D12CommandList* cmdLists[] = { m_computeCommandList.Get() };
    m_computeQueue->ExecuteCommandLists(1, cmdLists);

    // Signal fence
    m_computeFenceValue++;
    m_computeQueue->Signal(m_computeFence.Get(), m_computeFenceValue);

    // Wait for completion
    if (m_computeFence->GetCompletedValue() < m_computeFenceValue) {
        m_computeFence->SetEventOnCompletion(m_computeFenceValue, m_computeFenceEvent);
        WaitForSingleObject(m_computeFenceEvent, INFINITE);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double ofTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.opticalFlowTimeMs = ofTimeMs;
    }

    return true;
}

bool DualGPUPipeline::GenerateFrames() {
    auto startTime = std::chrono::high_resolution_clock::now();

    ID3D12Resource* currentFrame = m_transfer->GetDestinationTexture();
    ID3D12Resource* previousFrame = m_transfer->GetPreviousTexture();
    ID3D12Resource* motionVectors = m_opticalFlow->GetMotionVectorTexture();

    if (!currentFrame || !previousFrame || !motionVectors) {
        return true;  // Not enough data yet
    }

    // Generate intermediate frames based on multiplier
    int numGenFrames = static_cast<int>(m_config.multiplier) - 1;

    for (int i = 0; i < numGenFrames; i++) {
        // Calculate interpolation factor
        float t = static_cast<float>(i + 1) / static_cast<float>(m_config.multiplier);

        // Reset command list
        m_computeAllocator->Reset();
        m_computeCommandList->Reset(m_computeAllocator.Get(), nullptr);

        // Set interpolation factor and generate frame
        m_interpolation->SetInterpolationFactor(t);
        if (!m_interpolation->Dispatch(previousFrame, currentFrame, motionVectors,
                                        m_computeCommandList.Get())) {
            SetError("Frame interpolation failed");
            return false;
        }

        // Copy to our frame buffer
        // (In a real implementation, we'd manage this more efficiently)

        m_computeCommandList->Close();

        ID3D12CommandList* cmdLists[] = { m_computeCommandList.Get() };
        m_computeQueue->ExecuteCommandLists(1, cmdLists);

        m_computeFenceValue++;
        m_computeQueue->Signal(m_computeFence.Get(), m_computeFenceValue);
    }

    // Wait for all generation to complete
    if (m_computeFence->GetCompletedValue() < m_computeFenceValue) {
        m_computeFence->SetEventOnCompletion(m_computeFenceValue, m_computeFenceEvent);
        WaitForSingleObject(m_computeFenceEvent, INFINITE);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double interpTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.interpolationTimeMs = interpTimeMs;
        m_stats.framesGenerated += numGenFrames;
    }

    return true;
}

bool DualGPUPipeline::PresentFrames() {
    auto startTime = std::chrono::high_resolution_clock::now();

    ID3D12Resource* currentFrame = m_transfer->GetDestinationTexture();
    if (!currentFrame) {
        return true;
    }

    int totalFrames = m_frameGenEnabled ? static_cast<int>(m_config.multiplier) : 1;
    uint32_t syncInterval = m_config.vsync ? 1 : 0;

    // Helper lambda to present and flip a single frame
    auto presentSingleFrame = [&](ID3D12Resource* frame) -> bool {
        // Reset command list
        m_computeAllocator->Reset();
        m_computeCommandList->Reset(m_computeAllocator.Get(), nullptr);

        // Record copy to back buffer
        m_presenter->Present(frame, m_computeCommandList.Get());

        // Close and execute
        m_computeCommandList->Close();
        ID3D12CommandList* cmdLists[] = { m_computeCommandList.Get() };
        m_computeQueue->ExecuteCommandLists(1, cmdLists);

        // Wait for execution to complete
        m_computeFenceValue++;
        m_computeQueue->Signal(m_computeFence.Get(), m_computeFenceValue);
        if (m_computeFence->GetCompletedValue() < m_computeFenceValue) {
            m_computeFence->SetEventOnCompletion(m_computeFenceValue, m_computeFenceEvent);
            WaitForSingleObject(m_computeFenceEvent, INFINITE);
        }

        // Flip the swap chain
        m_presenter->Flip(syncInterval, 0);

        {
            std::lock_guard<std::mutex> lock(m_statsMutex);
            m_stats.framesPresented++;
        }

        return true;
    };

    if (m_frameGenEnabled && m_generatedFrameCount > 0) {
        // Present interleaved: gen0, gen1, ..., real
        for (uint32_t i = 0; i < m_generatedFrameCount; i++) {
            // Frame pacing
            WaitForFramePacing(i, totalFrames);

            // Present generated frame
            ID3D12Resource* genFrame = m_interpolation->GetInterpolatedFrame();
            if (genFrame) {
                presentSingleFrame(genFrame);
            }
        }
    }

    // Present the real frame last
    WaitForFramePacing(totalFrames - 1, totalFrames);
    presentSingleFrame(currentFrame);

    auto endTime = std::chrono::high_resolution_clock::now();
    double presentTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.presentTimeMs = presentTimeMs;
    }

    m_lastPresentTime = endTime;

    return true;
}

void DualGPUPipeline::WaitForFramePacing(int frameIndex, int totalFrames) {
    if (!m_config.vsync) {
        return;  // No pacing needed without vsync
    }

    // Calculate target time for this frame within the base frame period
    double baseFrameTimeMs = 16.667;  // Assume 60fps base
    double targetOffsetMs = (baseFrameTimeMs / totalFrames) * frameIndex;

    auto targetTime = m_frameStartTime + std::chrono::microseconds(
        static_cast<int64_t>(targetOffsetMs * 1000));

    auto now = std::chrono::high_resolution_clock::now();
    if (now < targetTime) {
        auto waitTime = std::chrono::duration_cast<std::chrono::microseconds>(targetTime - now);
        if (waitTime.count() > 0 && waitTime.count() < 20000) {  // Max 20ms wait
            std::this_thread::sleep_for(waitTime);
        }
    }
}

void DualGPUPipeline::SetFrameGenEnabled(bool enabled) {
    m_frameGenEnabled = enabled;
}

void DualGPUPipeline::SetFrameMultiplier(FrameMultiplier multiplier) {
    m_config.multiplier = multiplier;
    m_generatedFrameCount = static_cast<uint32_t>(multiplier) - 1;
    m_targetFrameTimeMs = 16.667 / static_cast<int>(multiplier);
}

void DualGPUPipeline::ResetStats() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats = PipelineStats{};
    m_stats.activeBackend = m_activeBackend;
}

void DualGPUPipeline::UpdateStats() {
    std::lock_guard<std::mutex> lock(m_statsMutex);

    // Calculate total pipeline time
    auto now = std::chrono::high_resolution_clock::now();
    m_stats.totalPipelineTimeMs = std::chrono::duration<double, std::milli>(
        now - m_frameStartTime).count();

    // Calculate FPS
    if (m_stats.totalPipelineTimeMs > 0) {
        m_stats.baseFPS = 1000.0 / m_stats.totalPipelineTimeMs;
        m_stats.outputFPS = m_stats.baseFPS * static_cast<int>(m_config.multiplier);
    }
}

HWND DualGPUPipeline::GetWindowHandle() const {
    if (m_activeBackend == FrameGenBackend::FidelityFX && m_ffxFrameGen) {
        // FFX manages its own swap chain, window handle comes from config
        return nullptr;  // FFX doesn't expose window handle directly
    }
    return m_presenter ? m_presenter->GetHWND() : nullptr;
}

bool DualGPUPipeline::IsWindowOpen() const {
    if (m_activeBackend == FrameGenBackend::FidelityFX && m_ffxFrameGen) {
        return m_ffxFrameGen->IsInitialized();
    }
    return m_presenter ? m_presenter->IsWindowOpen() : false;
}

void DualGPUPipeline::SetError(const std::string& error) {
    m_lastError = error;
    ReportError(error);
}

void DualGPUPipeline::ReportError(const std::string& error) {
    if (m_errorCallback) {
        m_errorCallback(error);
    }

    if (m_config.enableDebugOutput) {
        OutputDebugStringA(("OSFG Error: " + error + "\n").c_str());
    }
}

bool DualGPUPipeline::IsFidelityFXAvailable() {
    return OSFG::FFXLoader::IsAvailable();
}

} // namespace osfg
