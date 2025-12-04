# OSFG - Open Source Frame Generation
# CMake Build Configuration

cmake_minimum_required(VERSION 3.20)
project(OSFG VERSION 0.2.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# vcpkg DirectX Headers
# ============================================================================
set(VCPKG_ROOT "C:/vcpkg")
set(DIRECTX_HEADERS_INCLUDE ${VCPKG_ROOT}/installed/x64-windows/include)

# ============================================================================
# FidelityFX SDK Paths
# ============================================================================
set(FFX_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/FidelityFX-SDK")
set(FFX_INCLUDE_DIRS
    ${FFX_SDK_ROOT}/Kits/FidelityFX/api/include
    ${FFX_SDK_ROOT}/Kits/FidelityFX/api/internal
    ${FFX_SDK_ROOT}/Kits/FidelityFX/backend/dx12
    ${FFX_SDK_ROOT}/Kits/FidelityFX/framegeneration/fsr3/include
)
set(FFX_LIB_DIR ${FFX_SDK_ROOT}/Kits/FidelityFX/signedbin)
set(FFX_BIN_DIR ${FFX_SDK_ROOT}/Kits/FidelityFX/signedbin)

# ============================================================================
# DXGI Capture Library
# ============================================================================
add_library(osfg_capture STATIC
    src/capture/dxgi_capture.cpp
    src/capture/dxgi_capture.h
)

target_include_directories(osfg_capture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link Windows libraries for DXGI capture
if(WIN32)
    target_link_libraries(osfg_capture PUBLIC
        d3d11
        dxgi
    )
endif()

# ============================================================================
# Simple Optical Flow Library (Phase 1 - uses D3DCompile for runtime shader)
# ============================================================================
add_library(osfg_simple_opticalflow STATIC
    src/opticalflow/simple_opticalflow.cpp
    src/opticalflow/simple_opticalflow.h
)

target_include_directories(osfg_simple_opticalflow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_simple_opticalflow PUBLIC
    d3d12
    dxgi
    dxguid
    d3dcompiler
)

# ============================================================================
# FSR 3 Optical Flow Library (Stub - requires FidelityFX SDK build)
# Note: Full integration requires building FidelityFX-SDK from source.
# This stub allows the code to compile while documenting the integration path.
# ============================================================================
add_library(osfg_fsr_opticalflow STATIC
    src/opticalflow/fsr_opticalflow.cpp
    src/opticalflow/fsr_opticalflow.h
)

target_include_directories(osfg_fsr_opticalflow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_fsr_opticalflow PUBLIC
    d3d12
    dxgi
)

# ============================================================================
# FidelityFX Dynamic Loader Library
# ============================================================================
add_library(osfg_ffx_loader STATIC
    src/ffx/ffx_loader.cpp
    src/ffx/ffx_loader.h
)

target_include_directories(osfg_ffx_loader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
    ${FFX_INCLUDE_DIRS}
)

target_link_libraries(osfg_ffx_loader PUBLIC
    d3d12
    dxgi
)

# ============================================================================
# FidelityFX Frame Generation Wrapper Library
# ============================================================================
add_library(osfg_ffx_framegen STATIC
    src/ffx/ffx_framegen.cpp
    src/ffx/ffx_framegen.h
)

target_include_directories(osfg_ffx_framegen PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
    ${FFX_INCLUDE_DIRS}
)

target_link_libraries(osfg_ffx_framegen PUBLIC
    osfg_ffx_loader
    d3d12
    dxgi
)

# ============================================================================
# D3D11-D3D12 Interop Library
# ============================================================================
add_library(osfg_interop STATIC
    src/interop/d3d11_d3d12_interop.cpp
    src/interop/d3d11_d3d12_interop.h
)

target_include_directories(osfg_interop PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_interop PUBLIC
    d3d11
    d3d12
    dxgi
)

# ============================================================================
# Frame Interpolation Library
# ============================================================================
add_library(osfg_interpolation STATIC
    src/interpolation/frame_interpolation.cpp
    src/interpolation/frame_interpolation.h
)

target_include_directories(osfg_interpolation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_interpolation PUBLIC
    d3d12
    dxgi
    dxguid
    d3dcompiler
)

# ============================================================================
# Presentation Library
# ============================================================================
add_library(osfg_presentation STATIC
    src/presentation/simple_presenter.cpp
    src/presentation/simple_presenter.h
)

target_include_directories(osfg_presentation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_presentation PUBLIC
    d3d12
    dxgi
)

# ============================================================================
# GPU Transfer Library (Phase 2 - Dual-GPU support)
# ============================================================================
add_library(osfg_transfer STATIC
    src/transfer/gpu_transfer.cpp
    src/transfer/gpu_transfer.h
)

target_include_directories(osfg_transfer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_transfer PUBLIC
    d3d12
    dxgi
)

# ============================================================================
# Application Layer Library
# ============================================================================
add_library(osfg_app STATIC
    src/app/config_manager.cpp
    src/app/config_manager.h
    src/app/hotkey_handler.cpp
    src/app/hotkey_handler.h
    src/app/stats_overlay.cpp
    src/app/stats_overlay.h
)

target_include_directories(osfg_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_app PUBLIC
    d3d11
    d2d1
    dwrite
    Shell32
)

# ============================================================================
# Dual-GPU Pipeline Library (Phase 2)
# ============================================================================
add_library(osfg_pipeline STATIC
    src/pipeline/dual_gpu_pipeline.cpp
    src/pipeline/dual_gpu_pipeline.h
)

target_include_directories(osfg_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${DIRECTX_HEADERS_INCLUDE}
    ${FFX_INCLUDE_DIRS}
)

target_link_libraries(osfg_pipeline PUBLIC
    osfg_capture
    osfg_transfer
    osfg_simple_opticalflow
    osfg_interpolation
    osfg_presentation
    osfg_ffx_framegen
    d3d12
    dxgi
)

# ============================================================================
# Test Applications
# ============================================================================

# DXGI Capture Test
add_executable(test_dxgi_capture
    tests/test_dxgi_capture.cpp
)

target_link_libraries(test_dxgi_capture PRIVATE
    osfg_capture
)

# Capture Display Test (minimal test for debugging)
add_executable(test_capture_display
    tests/test_capture_display.cpp
)

target_link_libraries(test_capture_display PRIVATE
    osfg_capture
    d3d12.lib
    dxgi.lib
)

# Interop Display Test (tests interop + presenter)
add_executable(test_interop_display
    tests/test_interop_display.cpp
)

target_link_libraries(test_interop_display PRIVATE
    osfg_capture
    osfg_interop
    osfg_presentation
    d3d12.lib
    dxgi.lib
)

# Simple Optical Flow Test (Phase 1)
add_executable(test_simple_opticalflow
    tests/test_simple_opticalflow.cpp
)

target_include_directories(test_simple_opticalflow PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(test_simple_opticalflow PRIVATE
    osfg_capture
    osfg_simple_opticalflow
    osfg_interop
)

# Full Frame Generation Pipeline Test
add_executable(test_frame_generation
    tests/test_frame_generation.cpp
)

target_include_directories(test_frame_generation PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(test_frame_generation PRIVATE
    osfg_capture
    osfg_simple_opticalflow
    osfg_interop
    osfg_interpolation
)

# OSFG Demo Application (Phase 1 - Complete visual demo)
add_executable(osfg_demo WIN32
    demos/osfg_demo.cpp
)

target_include_directories(osfg_demo PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(osfg_demo PRIVATE
    osfg_capture
    osfg_simple_opticalflow
    osfg_interop
    osfg_interpolation
    osfg_presentation
)

# Simple Demo (no frame generation - for debugging)
add_executable(osfg_demo_simple WIN32
    demos/osfg_demo_simple.cpp
)

target_link_libraries(osfg_demo_simple PRIVATE
    osfg_capture
    osfg_interop
    osfg_presentation
)

# Dual-GPU Pipeline Test (Phase 2)
add_executable(test_dual_gpu_pipeline
    tests/test_dual_gpu_pipeline.cpp
)

target_include_directories(test_dual_gpu_pipeline PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(test_dual_gpu_pipeline PRIVATE
    osfg_pipeline
    osfg_app
)

# FSR 3 Optical Flow Test
add_executable(test_fsr_opticalflow
    tests/test_fsr_opticalflow.cpp
)

target_include_directories(test_fsr_opticalflow PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
    ${FFX_INCLUDE_DIRS}
)

target_link_libraries(test_fsr_opticalflow PRIVATE
    osfg_capture
    osfg_fsr_opticalflow
    osfg_interop
)

# FFX Loader Test
add_executable(test_ffx_loader
    tests/test_ffx_loader.cpp
)

target_include_directories(test_ffx_loader PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
    ${FFX_INCLUDE_DIRS}
)

target_link_libraries(test_ffx_loader PRIVATE
    osfg_ffx_loader
)

# FFX Frame Generation Test
add_executable(test_ffx_framegen
    tests/test_ffx_framegen.cpp
)

target_include_directories(test_ffx_framegen PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
    ${FFX_INCLUDE_DIRS}
)

target_link_libraries(test_ffx_framegen PRIVATE
    osfg_ffx_framegen
)

# Init Only Test (diagnoses if initialization breaks display)
add_executable(test_init_only
    tests/test_init_only.cpp
)

target_include_directories(test_init_only PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(test_init_only PRIVATE
    osfg_capture
    osfg_interop
    osfg_simple_opticalflow
    osfg_interpolation
    osfg_presentation
)

# Optical Flow Only Test (diagnoses if optical flow dispatch causes freeze)
add_executable(test_opticalflow_only
    tests/test_opticalflow_only.cpp
)

target_include_directories(test_opticalflow_only PRIVATE
    ${DIRECTX_HEADERS_INCLUDE}
)

target_link_libraries(test_opticalflow_only PRIVATE
    osfg_capture
    osfg_interop
    osfg_simple_opticalflow
    osfg_presentation
)

# ============================================================================
# Installation
# ============================================================================
install(TARGETS
    osfg_capture
    osfg_simple_opticalflow
    osfg_fsr_opticalflow
    osfg_ffx_loader
    osfg_ffx_framegen
    osfg_interop
    osfg_interpolation
    osfg_presentation
    osfg_transfer
    osfg_app
    osfg_pipeline
    test_dxgi_capture
    test_simple_opticalflow
    test_fsr_opticalflow
    test_ffx_loader
    test_ffx_framegen
    test_frame_generation
    test_dual_gpu_pipeline
    osfg_demo
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# ============================================================================
# Copy FidelityFX DLLs to output directory (if they exist)
# ============================================================================
set(FFX_DLLS
    ${FFX_BIN_DIR}/amd_fidelityfx_framegeneration_dx12.dll
    ${FFX_BIN_DIR}/amd_fidelityfx_loader_dx12.dll
    ${FFX_BIN_DIR}/amd_fidelityfx_upscaler_dx12.dll
)

# Copy DLLs after building test_fsr_opticalflow
add_custom_command(TARGET test_fsr_opticalflow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>"
    COMMENT "Checking for FidelityFX DLLs..."
)

foreach(DLL ${FFX_DLLS})
    if(EXISTS ${DLL})
        add_custom_command(TARGET test_fsr_opticalflow POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/"
            COMMENT "Copying ${DLL}"
        )
    endif()
endforeach()

# Installation (copies DLLs if they exist)
foreach(DLL ${FFX_DLLS})
    if(EXISTS ${DLL})
        install(FILES ${DLL} DESTINATION bin)
    endif()
endforeach()

# ============================================================================
# Print Configuration Summary
# ============================================================================
message(STATUS "")
message(STATUS "=== OSFG Build Configuration ===")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output Directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Phase 1: Single-GPU frame generation")
message(STATUS "  Phase 2: Dual-GPU pipeline")
message(STATUS "")
