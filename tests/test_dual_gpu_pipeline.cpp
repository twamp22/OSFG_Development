// OSFG - Open Source Frame Generation
// Dual-GPU Pipeline Test Application
//
// Tests the complete dual-GPU frame generation pipeline:
// - Capture on GPU 0
// - Transfer to GPU 1
// - Optical flow and interpolation on GPU 1
// - Presentation from GPU 1

#include "pipeline/dual_gpu_pipeline.h"
#include "transfer/gpu_transfer.h"
#include "app/config_manager.h"
#include "app/hotkey_handler.h"

#include <cstdio>
#include <chrono>
#include <thread>

using namespace osfg;

// Global state
bool g_running = true;
DualGPUPipeline* g_pipeline = nullptr;

void PrintGPUInfo() {
    printf("\n=== Available GPUs ===\n");

    auto gpus = GPUTransfer::EnumerateGPUs();
    for (const auto& gpu : gpus) {
        printf("  [%d] %ls\n", gpu.adapterIndex, gpu.description.c_str());
        printf("      VRAM: %.1f GB\n", gpu.dedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0));
        printf("      Cross-Adapter: %s\n", gpu.supportsCrossAdapterRowMajor ? "Yes" : "No");
        printf("      Type: %s\n", gpu.isIntegrated ? "Integrated" : "Discrete");
    }

    if (gpus.size() >= 2) {
        bool p2p = GPUTransfer::IsPeerToPeerAvailable(0, 1);
        printf("\n  Peer-to-Peer (GPU 0 <-> GPU 1): %s\n", p2p ? "Available" : "Not Available");
    }

    printf("\n");
}

const char* GetBackendName(FrameGenBackend backend) {
    switch (backend) {
        case FrameGenBackend::Native: return "Native";
        case FrameGenBackend::FidelityFX: return "FidelityFX";
        case FrameGenBackend::Auto: return "Auto";
        default: return "Unknown";
    }
}

void PrintStats(const PipelineStats& stats) {
    printf("\r");
    printf("[%s] ", GetBackendName(stats.activeBackend));
    printf("FPS: %.1f (base) / %.1f (output) | ", stats.baseFPS, stats.outputFPS);
    printf("Capture: %.1fms | ", stats.captureTimeMs);
    printf("Transfer: %.1fms | ", stats.transferTimeMs);
    printf("OF: %.1fms | ", stats.opticalFlowTimeMs);
    printf("Interp: %.1fms | ", stats.interpolationTimeMs);
    printf("Frames: %llu", stats.framesPresented);
    fflush(stdout);
}

void OnHotkey(HotkeyAction action) {
    if (!g_pipeline) return;

    switch (action) {
        case HotkeyAction::ToggleFrameGen:
            g_pipeline->SetFrameGenEnabled(!g_pipeline->IsFrameGenEnabled());
            printf("\nFrame Generation: %s\n",
                   g_pipeline->IsFrameGenEnabled() ? "ENABLED" : "DISABLED");
            break;

        case HotkeyAction::CycleMode: {
            auto current = g_pipeline->GetFrameMultiplier();
            FrameMultiplier next;
            switch (current) {
                case FrameMultiplier::X2: next = FrameMultiplier::X3; break;
                case FrameMultiplier::X3: next = FrameMultiplier::X4; break;
                case FrameMultiplier::X4: next = FrameMultiplier::X2; break;
                default: next = FrameMultiplier::X2; break;
            }
            g_pipeline->SetFrameMultiplier(next);
            printf("\nFrame Multiplier: %dX\n", static_cast<int>(next));
            break;
        }

        default:
            break;
    }
}

int main(int argc, char* argv[]) {
    printf("=== OSFG Dual-GPU Pipeline Test ===\n");
    printf("Phase 2: Dual-GPU Frame Generation\n\n");

    // Print GPU information
    PrintGPUInfo();

    auto gpus = GPUTransfer::EnumerateGPUs();
    if (gpus.size() < 2) {
        printf("ERROR: Dual-GPU mode requires at least 2 GPUs.\n");
        printf("Found %zu GPU(s). Exiting.\n", gpus.size());
        return 1;
    }

    // Check FidelityFX availability
    printf("=== Backend Availability ===\n");
    printf("  Native (SimpleOpticalFlow): Always available\n");
    printf("  FidelityFX Frame Generation: %s\n",
           DualGPUPipeline::IsFidelityFXAvailable() ? "Available" : "Not available");
    printf("\n");

    // Configure pipeline
    DualGPUConfig config;
    config.primaryGPU = 0;
    config.secondaryGPU = 1;
    config.multiplier = FrameMultiplier::X2;
    config.enableFrameGen = true;
    config.vsync = true;
    config.captureMonitor = 0;
    config.windowTitle = L"OSFG Dual-GPU Test";
    config.enableDebugOutput = true;
    config.backend = FrameGenBackend::Auto;  // Auto-select best backend

    printf("Configuration:\n");
    printf("  Primary GPU (Capture):   [%d] %ls\n",
           config.primaryGPU, gpus[config.primaryGPU].description.c_str());
    printf("  Secondary GPU (Compute): [%d] %ls\n",
           config.secondaryGPU, gpus[config.secondaryGPU].description.c_str());
    printf("  Frame Multiplier: %dX\n", static_cast<int>(config.multiplier));
    printf("  VSync: %s\n", config.vsync ? "Enabled" : "Disabled");
    printf("  Backend: Auto (will select best available)\n");
    printf("\n");

    // Initialize pipeline
    printf("Initializing dual-GPU pipeline...\n");

    DualGPUPipeline pipeline;
    g_pipeline = &pipeline;

    pipeline.SetErrorCallback([](const std::string& error) {
        printf("\nPipeline Error: %s\n", error.c_str());
    });

    if (!pipeline.Initialize(config)) {
        printf("ERROR: Failed to initialize pipeline: %s\n",
               pipeline.GetLastError().c_str());
        return 1;
    }

    printf("Pipeline initialized successfully!\n");
    printf("  Active Backend: %s\n\n", GetBackendName(pipeline.GetActiveBackend()));

    // Initialize hotkeys
    HotkeyHandler hotkeys;
    if (hotkeys.Initialize()) {
        hotkeys.SetCallback(OnHotkey);
        hotkeys.RegisterDefaultHotkeys(VK_F10, VK_F11, VK_F12, true);
        printf("Hotkeys registered:\n");
        printf("  Alt+F10: Toggle frame generation\n");
        printf("  Alt+F12: Cycle multiplier (2X/3X/4X)\n");
        printf("  Escape:  Exit\n");
    }

    printf("\nStarting pipeline...\n");
    printf("Capture a window to see frame generation in action.\n\n");

    // Start pipeline
    if (!pipeline.Start()) {
        printf("ERROR: Failed to start pipeline: %s\n",
               pipeline.GetLastError().c_str());
        return 1;
    }

    // Main loop
    auto lastStatsTime = std::chrono::high_resolution_clock::now();

    while (pipeline.IsRunning() && pipeline.IsWindowOpen()) {
        // Process window messages
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }

            // Process hotkeys
            hotkeys.ProcessMessage(msg.message, msg.wParam, msg.lParam);

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!g_running) break;

        // Process one frame
        pipeline.ProcessFrame();

        // Print stats periodically
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatsTime);
        if (elapsed.count() >= 500) {
            PrintStats(pipeline.GetStats());
            lastStatsTime = now;
        }
    }

    printf("\n\nShutting down...\n");

    // Cleanup
    pipeline.Stop();
    pipeline.Shutdown();
    hotkeys.Shutdown();

    // Print final stats
    const auto& stats = pipeline.GetStats();
    printf("\n=== Final Statistics ===\n");
    printf("  Base Frames Captured: %llu\n", stats.baseFamesCaptured);
    printf("  Frames Generated:     %llu\n", stats.framesGenerated);
    printf("  Frames Presented:     %llu\n", stats.framesPresented);
    printf("  Frames Dropped:       %llu\n", stats.framesDropped);
    printf("\n  Average Timings:\n");
    printf("    Capture:       %.2f ms\n", stats.captureTimeMs);
    printf("    Transfer:      %.2f ms (%.1f MB/s)\n",
           stats.transferTimeMs, stats.transferThroughputMBps);
    printf("    Optical Flow:  %.2f ms\n", stats.opticalFlowTimeMs);
    printf("    Interpolation: %.2f ms\n", stats.interpolationTimeMs);
    printf("    Total:         %.2f ms\n", stats.totalPipelineTimeMs);
    printf("\n");

    g_pipeline = nullptr;
    return 0;
}
