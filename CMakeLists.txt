# OSFG - Open Source Frame Generation
# CMake Build Configuration

cmake_minimum_required(VERSION 3.20)
project(OSFG VERSION 0.1.0 LANGUAGES CXX)

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
# Test Applications
# ============================================================================

# DXGI Capture Test
add_executable(test_dxgi_capture
    tests/test_dxgi_capture.cpp
)

target_link_libraries(test_dxgi_capture PRIVATE
    osfg_capture
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

# ============================================================================
# Installation
# ============================================================================
install(TARGETS osfg_capture osfg_simple_opticalflow osfg_interop osfg_interpolation osfg_presentation test_dxgi_capture test_simple_opticalflow test_frame_generation osfg_demo
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# ============================================================================
# Print Configuration Summary
# ============================================================================
message(STATUS "")
message(STATUS "=== OSFG Build Configuration ===")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output Directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  Phase 1: Simple Optical Flow (block matching)")
message(STATUS "")
