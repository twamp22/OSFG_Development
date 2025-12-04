// OSFG - Open Source Frame Generation
// FSR 3 Optical Flow Implementation
//
// AMD FidelityFX SDK Integration Status:
// - FidelityFX SDK v2.0 successfully built (FSR sample compiles)
// - Frame generation DLLs available in build/bin/Release/
//
// Integration Options:
//
// 1. FULL FRAME GENERATION (Recommended for best quality):
//    Use amd_fidelityfx_framegeneration_dx12.dll directly, which
//    provides optical flow + interpolation as a unified pipeline.
//    This requires restructuring OSFG to use FFX for both stages.
//
// 2. STANDALONE OPTICAL FLOW (Not directly supported):
//    The FidelityFX signed DLLs bundle optical flow internally.
//    Standalone optical flow requires building from source with
//    shader blob generation, which needs their internal tooling.
//
// Current OSFG approach:
// - Uses SimpleOpticalFlow (block-matching) for motion estimation
// - Custom FrameInterpolation for frame generation
// - No external DLL dependencies
//
// Future work: Integrate full FFX frame generation as an alternative
// high-quality backend alongside the current OSFG pipeline.

#include "fsr_opticalflow.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace OSFG {

// Track DLL availability status
static bool s_dllChecked = false;
static bool s_dllAvailable = false;
static std::wstring s_dllPath;

FSROpticalFlow::FSROpticalFlow() = default;

FSROpticalFlow::~FSROpticalFlow() {
    Shutdown();
}

bool FSROpticalFlow::IsAvailable() {
    if (!s_dllChecked) {
        // Check if the FidelityFX DLL is available
        HMODULE hModule = LoadLibraryW(L"amd_fidelityfx_framegeneration_dx12.dll");
        if (hModule) {
            // Get the full path for diagnostic purposes
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(hModule, path, MAX_PATH);
            s_dllPath = path;
            FreeLibrary(hModule);
            s_dllAvailable = true;
        }
        s_dllChecked = true;
    }

    // DLL may exist, but standalone optical flow API requires
    // building from source with shader blob generation.
    // Return false until full integration is implemented.
    return false;
}

bool FSROpticalFlow::IsDllPresent() {
    IsAvailable();  // Ensure we've checked
    return s_dllAvailable;
}

const std::wstring& FSROpticalFlow::GetDllPath() {
    IsAvailable();  // Ensure we've checked
    return s_dllPath;
}

bool FSROpticalFlow::Initialize(ID3D12Device* device, const FSROpticalFlowConfig& config) {
    if (s_dllAvailable) {
        // Convert wide string to narrow string properly
        int size = WideCharToMultiByte(CP_UTF8, 0, s_dllPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string narrowPath(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s_dllPath.c_str(), -1, &narrowPath[0], size, nullptr, nullptr);

        m_lastError = "FSR optical flow: FidelityFX DLL found at " + narrowPath + ". " +
                      "Full integration pending. Use SimpleOpticalFlow for now.";
    } else {
        m_lastError = "FSR optical flow: FidelityFX DLL not found. "
                      "Copy amd_fidelityfx_framegeneration_dx12.dll to application directory. "
                      "Using SimpleOpticalFlow for motion estimation.";
    }
    return false;
}

void FSROpticalFlow::Shutdown() {
    m_opticalFlowVector.Reset();
    m_opticalFlowSCD.Reset();
    m_device.Reset();
    m_initialized = false;
}

bool FSROpticalFlow::Dispatch(ID3D12Resource* currentFrame,
                               ID3D12GraphicsCommandList* commandList,
                               bool reset) {
    m_lastError = "FSR optical flow not initialized";
    return false;
}

ID3D12Resource* FSROpticalFlow::GetMotionVectorTexture() const {
    return m_opticalFlowVector.Get();
}

ID3D12Resource* FSROpticalFlow::GetSceneChangeTexture() const {
    return m_opticalFlowSCD.Get();
}

} // namespace OSFG
