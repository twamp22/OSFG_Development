// OSFG - Open Source Frame Generation
// DXGI Capture Test Application
//
// This test captures frames from the desktop and measures capture latency.
// Run this while a game or video is playing to test capture performance.

#include "../src/capture/dxgi_capture.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <csignal>

std::atomic<bool> g_running{true};

void SignalHandler(int signal) {
    g_running = false;
}

void PrintUsage() {
    std::cout << "OSFG DXGI Capture Test\n";
    std::cout << "======================\n\n";
    std::cout << "This test captures desktop frames and measures latency.\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
}

void PrintStats(const osfg::CaptureStats& stats, uint32_t width, uint32_t height) {
    std::cout << "\r";
    std::cout << "Frames: " << std::setw(6) << stats.framesCapture;
    std::cout << " | Missed: " << std::setw(4) << stats.framesMissed;
    std::cout << " | Res: " << width << "x" << height;
    std::cout << " | Lat(ms) Avg: " << std::fixed << std::setprecision(2) << std::setw(6) << stats.avgCaptureTimeMs;
    std::cout << " Min: " << std::setw(5) << stats.minCaptureTimeMs;
    std::cout << " Max: " << std::setw(6) << stats.maxCaptureTimeMs;
    std::cout << "     " << std::flush;
}

int main(int argc, char* argv[]) {
    // Set up signal handler for clean exit
    std::signal(SIGINT, SignalHandler);

    PrintUsage();

    // Configure capture
    osfg::CaptureConfig config;
    config.outputIndex = 0;        // Primary monitor
    config.adapterIndex = 0;       // Primary GPU
    config.timeoutMs = 100;        // Wait up to 100ms for a frame
    config.createStagingTexture = false;  // Don't need CPU readback for this test

    // Parse command line
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) {
            config.outputIndex = std::stoi(argv[++i]);
        } else if (arg == "--adapter" && i + 1 < argc) {
            config.adapterIndex = std::stoi(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            config.timeoutMs = std::stoi(argv[++i]);
        }
    }

    std::cout << "Configuration:\n";
    std::cout << "  Adapter: " << config.adapterIndex << "\n";
    std::cout << "  Output: " << config.outputIndex << "\n";
    std::cout << "  Timeout: " << config.timeoutMs << "ms\n\n";

    // Initialize capture
    osfg::DXGICapture capture;
    if (!capture.Initialize(config)) {
        std::cerr << "Failed to initialize capture: " << capture.GetLastError() << "\n";
        return 1;
    }

    std::cout << "Capture initialized successfully!\n";
    std::cout << "Display resolution: " << capture.GetWidth() << "x" << capture.GetHeight() << "\n\n";
    std::cout << "Capturing frames... (Press Ctrl+C to stop)\n\n";

    // Capture loop
    osfg::CapturedFrame frame;
    auto lastStatsTime = std::chrono::steady_clock::now();

    while (g_running) {
        if (capture.CaptureFrame(frame)) {
            // Frame captured successfully
            // In a real application, we'd process the frame here

            // Release the frame immediately to minimize latency
            capture.ReleaseFrame();
        }

        // Print stats every 500ms
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatsTime).count() >= 500) {
            PrintStats(capture.GetStats(), capture.GetWidth(), capture.GetHeight());
            lastStatsTime = now;
        }
    }

    // Final stats
    std::cout << "\n\n=== Final Statistics ===\n";
    const auto& stats = capture.GetStats();
    std::cout << "Total frames captured: " << stats.framesCapture << "\n";
    std::cout << "Frames missed: " << stats.framesMissed << "\n";
    std::cout << "Average capture latency: " << std::fixed << std::setprecision(3) << stats.avgCaptureTimeMs << " ms\n";
    std::cout << "Min capture latency: " << stats.minCaptureTimeMs << " ms\n";
    std::cout << "Max capture latency: " << stats.maxCaptureTimeMs << " ms\n";

    // Target check
    std::cout << "\n=== Target Check ===\n";
    if (stats.avgCaptureTimeMs < 5.0) {
        std::cout << "[PASS] Average latency < 5ms target\n";
    } else {
        std::cout << "[FAIL] Average latency exceeds 5ms target\n";
    }

    capture.Shutdown();
    return 0;
}
