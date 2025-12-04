// OSFG - Open Source Frame Generation
// FSR 3 Optical Flow Test Application
//
// Tests the AMD FidelityFX SDK optical flow integration status.

#include "opticalflow/fsr_opticalflow.h"

#include <cstdio>
#include <string>

int main() {
    printf("=== OSFG FSR 3 Optical Flow Status ===\n\n");

    // Check if FidelityFX DLL is present
    printf("Checking for FidelityFX DLL...\n");
    if (OSFG::FSROpticalFlow::IsDllPresent()) {
        std::wstring path = OSFG::FSROpticalFlow::GetDllPath();
        printf("  FidelityFX DLL FOUND: %ls\n\n", path.c_str());
    } else {
        printf("  FidelityFX DLL NOT found\n");
        printf("  Expected: amd_fidelityfx_framegeneration_dx12.dll\n\n");
    }

    // Check if FSR optical flow is available for use
    printf("Checking FSR optical flow availability...\n");
    if (OSFG::FSROpticalFlow::IsAvailable()) {
        printf("  FSR optical flow is available for use!\n");
    } else {
        printf("  FSR optical flow is NOT available for use.\n\n");

        if (OSFG::FSROpticalFlow::IsDllPresent()) {
            printf("  Status: DLL present but integration pending\n\n");
            printf("  The FidelityFX DLL is present but provides bundled frame generation\n");
            printf("  (optical flow + interpolation together). Integration options:\n\n");
            printf("  Option 1: Use Full Frame Generation (recommended for quality)\n");
            printf("    - Use amd_fidelityfx_framegeneration_dx12.dll API\n");
            printf("    - Replaces both optical flow and interpolation\n");
            printf("    - Highest quality results\n\n");
            printf("  Option 2: Build Optical Flow from Source\n");
            printf("    - Build FidelityFX-SDK with shader blob generation\n");
            printf("    - Enables standalone optical flow API\n");
            printf("    - More complex integration\n");
        } else {
            printf("  Status: DLL not present\n\n");
            printf("  To enable FidelityFX integration:\n");
            printf("    1. Build FidelityFX-SDK FSR sample\n");
            printf("    2. Copy DLLs to application directory:\n");
            printf("       - amd_fidelityfx_framegeneration_dx12.dll\n");
            printf("       - amd_fidelityfx_loader_dx12.dll\n");
            printf("       - amd_fidelityfx_upscaler_dx12.dll\n");
        }

        printf("\n  Current alternative: SimpleOpticalFlow (block-matching)\n");
        printf("    - Works without external dependencies\n");
        printf("    - Suitable for basic frame generation\n");
        printf("    - Used by default in OSFG pipeline\n");
    }

    // Test initialization (will fail until full integration)
    printf("\n--- Testing initialization ---\n");
    OSFG::FSROpticalFlow opticalFlow;
    OSFG::FSROpticalFlowConfig config;
    config.width = 1920;
    config.height = 1080;

    if (!opticalFlow.Initialize(nullptr, config)) {
        printf("Initialize returned: false\n");
        printf("Message: %s\n", opticalFlow.GetLastError().c_str());
    } else {
        printf("Initialize returned: true (FSR optical flow ready!)\n");
    }

    printf("\n=== Test Complete ===\n");
    return 0;
}
