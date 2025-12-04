// OSFG - Open Source Frame Generation
// FidelityFX Loader Test
//
// Tests the dynamic loading of FidelityFX SDK DLLs

#include "ffx/ffx_loader.h"
#include <cstdio>

int main() {
    printf("=== OSFG FidelityFX Loader Test ===\n\n");

    // Check if FFX is available
    printf("[1/3] Checking FidelityFX availability...\n");
    if (OSFG::FFXLoader::IsAvailable()) {
        printf("      FidelityFX DLLs are available!\n\n");
    } else {
        printf("      FidelityFX DLLs NOT found.\n");
        printf("      Ensure these DLLs are in the application directory:\n");
        printf("        - amd_fidelityfx_loader_dx12.dll\n");
        printf("        - amd_fidelityfx_framegeneration_dx12.dll\n");
        printf("        - amd_fidelityfx_upscaler_dx12.dll\n\n");
        return 1;
    }

    // Load the FFX libraries
    printf("[2/3] Loading FidelityFX libraries...\n");
    OSFG::FFXLoader& loader = OSFG::FFXLoader::Instance();

    if (!loader.Load()) {
        printf("      FAILED: %s\n", loader.GetLastError().c_str());
        return 1;
    }

    printf("      Loaded successfully!\n\n");

    // Display loaded DLL paths
    printf("[3/3] Loaded DLL information:\n");
    printf("      Loader DLL:    %ls\n", loader.GetLoaderDllPath().c_str());
    printf("      FrameGen DLL:  %ls\n", loader.GetFrameGenDllPath().c_str());
    if (!loader.GetUpscalerDllPath().empty()) {
        printf("      Upscaler DLL:  %ls\n", loader.GetUpscalerDllPath().c_str());
    }
    printf("\n");

    // Verify function pointers
    printf("Loaded FFX API functions:\n");
    printf("  ffxCreateContext:  %s\n", loader.CreateContext ? "OK" : "MISSING");
    printf("  ffxDestroyContext: %s\n", loader.DestroyContext ? "OK" : "MISSING");
    printf("  ffxConfigure:      %s\n", loader.Configure ? "OK" : "MISSING");
    printf("  ffxQuery:          %s\n", loader.Query ? "OK" : "MISSING");
    printf("  ffxDispatch:       %s\n", loader.Dispatch ? "OK" : "MISSING");
    printf("\n");

    // Check if all functions are loaded
    bool allLoaded = loader.CreateContext && loader.DestroyContext &&
                     loader.Configure && loader.Query && loader.Dispatch;

    if (allLoaded) {
        printf("=== FFX Loader Test PASSED ===\n");
        printf("\nFidelityFX SDK is ready for integration.\n");
        printf("See docs/fidelityfx-integration-design.md for next steps.\n");
    } else {
        printf("=== FFX Loader Test FAILED ===\n");
        printf("\nSome FFX functions could not be loaded.\n");
        return 1;
    }

    return 0;
}
