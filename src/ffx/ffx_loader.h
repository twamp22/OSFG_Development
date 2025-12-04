// OSFG - Open Source Frame Generation
// FidelityFX SDK Dynamic Loader
//
// Dynamically loads the FidelityFX DLLs and provides access to the API.
// This allows OSFG to run without FidelityFX (falling back to SimpleOpticalFlow)
// while enabling high-quality frame generation when available.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <string>
#include <functional>

namespace OSFG {

// Forward declare FFX types (avoids including full FFX headers)
using ffxContext = void*;
using ffxReturnCode_t = uint32_t;

// FFX API return codes
enum class FFXReturnCode : uint32_t {
    Ok = 0,
    Error = 1,
    ErrorUnknownDescType = 2,
    ErrorRuntimeError = 3,
    ErrorNoProvider = 4,
    ErrorMemory = 5,
    ErrorParameter = 6,
    ErrorProviderNoSupportNewDescType = 7
};

// Memory allocation callbacks
struct FFXAllocationCallbacks {
    void* pUserData;
    void* (*alloc)(void* pUserData, uint64_t size);
    void (*dealloc)(void* pUserData, void* pMem);
};

// FFX API header (base for all descriptors)
struct FFXApiHeader {
    uint64_t type;
    FFXApiHeader* pNext;
};

// Function pointer types for FFX API
using PfnFfxCreateContext = ffxReturnCode_t(*)(ffxContext* context, FFXApiHeader* desc, const FFXAllocationCallbacks* memCb);
using PfnFfxDestroyContext = ffxReturnCode_t(*)(ffxContext* context, const FFXAllocationCallbacks* memCb);
using PfnFfxConfigure = ffxReturnCode_t(*)(ffxContext* context, const FFXApiHeader* desc);
using PfnFfxQuery = ffxReturnCode_t(*)(ffxContext* context, FFXApiHeader* desc);
using PfnFfxDispatch = ffxReturnCode_t(*)(ffxContext* context, const FFXApiHeader* desc);

// FidelityFX SDK dynamic loader
class FFXLoader {
public:
    FFXLoader();
    ~FFXLoader();

    // Non-copyable
    FFXLoader(const FFXLoader&) = delete;
    FFXLoader& operator=(const FFXLoader&) = delete;

    // Get singleton instance
    static FFXLoader& Instance();

    // Check if FidelityFX DLLs are available
    static bool IsAvailable();

    // Load the FidelityFX libraries
    bool Load();

    // Unload the libraries
    void Unload();

    // Check if loaded
    bool IsLoaded() const { return m_loaded; }

    // Get last error message
    const std::string& GetLastError() const { return m_lastError; }

    // Get DLL paths
    const std::wstring& GetLoaderDllPath() const { return m_loaderPath; }
    const std::wstring& GetFrameGenDllPath() const { return m_frameGenPath; }
    const std::wstring& GetUpscalerDllPath() const { return m_upscalerPath; }

    // FFX API function pointers (valid after Load())
    PfnFfxCreateContext CreateContext = nullptr;
    PfnFfxDestroyContext DestroyContext = nullptr;
    PfnFfxConfigure Configure = nullptr;
    PfnFfxQuery Query = nullptr;
    PfnFfxDispatch Dispatch = nullptr;

    // Helper to convert return code
    static FFXReturnCode ToReturnCode(ffxReturnCode_t rc) {
        return static_cast<FFXReturnCode>(rc);
    }

    // Check if return code is success
    static bool Succeeded(ffxReturnCode_t rc) {
        return rc == 0;
    }

private:
    bool LoadDll(const wchar_t* name, HMODULE& hModule, std::wstring& path);
    bool LoadFunctions();

    HMODULE m_hLoader = nullptr;
    HMODULE m_hFrameGen = nullptr;
    HMODULE m_hUpscaler = nullptr;

    std::wstring m_loaderPath;
    std::wstring m_frameGenPath;
    std::wstring m_upscalerPath;

    bool m_loaded = false;
    std::string m_lastError;
};

} // namespace OSFG
