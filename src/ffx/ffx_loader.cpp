// OSFG - Open Source Frame Generation
// FidelityFX SDK Dynamic Loader Implementation

#include "ffx_loader.h"

namespace OSFG {

// DLL names
static const wchar_t* FFX_LOADER_DLL = L"amd_fidelityfx_loader_dx12.dll";
static const wchar_t* FFX_FRAMEGEN_DLL = L"amd_fidelityfx_framegeneration_dx12.dll";
static const wchar_t* FFX_UPSCALER_DLL = L"amd_fidelityfx_upscaler_dx12.dll";

FFXLoader::FFXLoader() = default;

FFXLoader::~FFXLoader() {
    Unload();
}

FFXLoader& FFXLoader::Instance() {
    static FFXLoader instance;
    return instance;
}

bool FFXLoader::IsAvailable() {
    // Quick check if the main frame generation DLL exists
    HMODULE hTest = LoadLibraryW(FFX_FRAMEGEN_DLL);
    if (hTest) {
        FreeLibrary(hTest);
        return true;
    }
    return false;
}

bool FFXLoader::Load() {
    if (m_loaded) {
        return true;
    }

    // Load the loader DLL first (required for API)
    if (!LoadDll(FFX_LOADER_DLL, m_hLoader, m_loaderPath)) {
        return false;
    }

    // Load frame generation DLL
    if (!LoadDll(FFX_FRAMEGEN_DLL, m_hFrameGen, m_frameGenPath)) {
        Unload();
        return false;
    }

    // Load upscaler DLL (optional, but usually needed)
    LoadDll(FFX_UPSCALER_DLL, m_hUpscaler, m_upscalerPath);

    // Load function pointers from the loader DLL
    if (!LoadFunctions()) {
        Unload();
        return false;
    }

    m_loaded = true;
    return true;
}

void FFXLoader::Unload() {
    CreateContext = nullptr;
    DestroyContext = nullptr;
    Configure = nullptr;
    Query = nullptr;
    Dispatch = nullptr;

    if (m_hUpscaler) {
        FreeLibrary(m_hUpscaler);
        m_hUpscaler = nullptr;
    }

    if (m_hFrameGen) {
        FreeLibrary(m_hFrameGen);
        m_hFrameGen = nullptr;
    }

    if (m_hLoader) {
        FreeLibrary(m_hLoader);
        m_hLoader = nullptr;
    }

    m_loaderPath.clear();
    m_frameGenPath.clear();
    m_upscalerPath.clear();

    m_loaded = false;
}

bool FFXLoader::LoadDll(const wchar_t* name, HMODULE& hModule, std::wstring& path) {
    hModule = LoadLibraryW(name);
    if (!hModule) {
        DWORD errorCode = ::GetLastError();

        // Convert wide string name to narrow for error message
        char narrowName[256];
        WideCharToMultiByte(CP_UTF8, 0, name, -1, narrowName, sizeof(narrowName), nullptr, nullptr);

        char errorMsg[512];
        sprintf_s(errorMsg, "Failed to load %s (error %lu)", narrowName, errorCode);
        m_lastError = errorMsg;

        return false;
    }

    // Get the full path
    wchar_t fullPath[MAX_PATH];
    GetModuleFileNameW(hModule, fullPath, MAX_PATH);
    path = fullPath;

    return true;
}

bool FFXLoader::LoadFunctions() {
    // The FFX API functions are exported from the loader DLL
    // Function names based on ffx_api.h exports

    CreateContext = reinterpret_cast<PfnFfxCreateContext>(
        GetProcAddress(m_hLoader, "ffxCreateContext"));

    DestroyContext = reinterpret_cast<PfnFfxDestroyContext>(
        GetProcAddress(m_hLoader, "ffxDestroyContext"));

    Configure = reinterpret_cast<PfnFfxConfigure>(
        GetProcAddress(m_hLoader, "ffxConfigure"));

    Query = reinterpret_cast<PfnFfxQuery>(
        GetProcAddress(m_hLoader, "ffxQuery"));

    Dispatch = reinterpret_cast<PfnFfxDispatch>(
        GetProcAddress(m_hLoader, "ffxDispatch"));

    // Verify all required functions were loaded
    if (!CreateContext || !DestroyContext || !Configure || !Query || !Dispatch) {
        m_lastError = "Failed to load FFX API functions from loader DLL";
        return false;
    }

    return true;
}

} // namespace OSFG
