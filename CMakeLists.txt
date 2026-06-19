cmake_minimum_required(VERSION 3.20)

# Fix compatibility with CMake 3.27+ (yaml-cpp and other deps use old cmake_minimum_required)
if(POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD)
endif()
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0148 OLD)

project(CloseCrab-Unified VERSION 0.4.2 LANGUAGES C CXX)

# ============================================
# Build options
# ============================================

option(USE_CUDA "Enable CUDA GPU acceleration (Windows/Linux with NVIDIA)" ON)
option(USE_LOCAL_LLM "Enable local LLM inference via llama.cpp" ON)
option(USE_FAISS "Enable FAISS vector search for RAG" ON)
option(USE_ONNX "Enable ONNX Runtime for embeddings" ON)
option(BUILD_TESTS "Build unit tests" OFF)

# ============================================
# Platform detection
# ============================================

if(APPLE)
    set(USE_CUDA OFF)
    set(PLATFORM_MACOS TRUE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum macOS version")
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
elseif(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    if(EXISTS "G:/vcpkg/installed/x64-windows")
        set(CMAKE_PREFIX_PATH "G:/vcpkg/installed/x64-windows")
    endif()
endif()

# ============================================
# Global config
# ============================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /utf-8: treat source AND execution charset as UTF-8. Required because the
    # codebase embeds UTF-8 string literals (e.g. Chinese activation messages)
    # and the runtime sets the console to CP_UTF8; without this MSVC assumes the
    # system code page (936/GBK on zh-CN) and miscompiles those literals (C2001).
    add_compile_options(/utf-8 /Zc:char8_t-)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # Address Sanitizer: detects use-after-free, buffer overflow, stack corruption.
    # Enable for Debug builds to find the heap corruption causing crash 0xC0000005.
    # The crash reads freed memory containing {3, 1000} as a function pointer.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_ASAN)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
        message(STATUS "AddressSanitizer ENABLED")
    endif()
endif()

# ============================================
# CUDA (optional)
# ============================================

if(USE_CUDA)
    find_package(CUDAToolkit QUIET)
    if(NOT CUDAToolkit_FOUND)
        message(WARNING "CUDA not found - building without GPU acceleration")
        set(USE_CUDA OFF)
    endif()
endif()

# ============================================
# Required dependencies (cross-platform)
# ============================================

find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
    find_package(CURL REQUIRED)
endif()
find_package(ZLIB REQUIRED)

include(FetchContent)

# spdlog
# spdlog — prefer system install, fallback to FetchContent
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1)
    FetchContent_MakeAvailable(spdlog)
endif()

# json — prefer system install
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3)
    FetchContent_MakeAvailable(json)
endif()

# yaml-cpp — prefer system install
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG master
        SYSTEM)
    set(CMAKE_POLICY_DEFAULT_CMP0148 OLD CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# CLI11
FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2)
FetchContent_MakeAvailable(CLI11)

# thread pool
FetchContent_Declare(thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v5.1.0)
FetchContent_MakeAvailable(thread_pool)

# sqlite3
FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2024/sqlite-autoconf-3460100.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)
if(NOT TARGET sqlite3)
    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
endif()

# httplib (header-only)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.1)
FetchContent_MakeAvailable(httplib)

# capstone
set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(capstone
    URL https://github.com/capstone-engine/capstone/archive/refs/tags/5.0.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(capstone)

# ixwebsocket
set(IXWEBSOCKET_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/ixwebsocket")
if(EXISTS ${IXWEBSOCKET_PATH}/CMakeLists.txt)
    add_subdirectory(${IXWEBSOCKET_PATH})
else()
    # Fetch from source if not available locally
    FetchContent_Declare(ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG v11.4.5)
    set(USE_TLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ixwebsocket)
endif()

# ============================================
# ONNX Runtime (optional, platform-specific)
# ============================================

if(USE_ONNX)
    if(PLATFORM_WINDOWS)
        set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime-win-x64-gpu-1.24.4")
        if(EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
            add_library(onnxruntime SHARED IMPORTED)
            set_target_properties(onnxruntime PROPERTIES
                IMPORTED_LOCATION "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                IMPORTED_IMPLIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_DIR}/include")
        else()
            message(WARNING "ONNX Runtime not found - disabling")
            set(USE_ONNX OFF)
        endif()
    elseif(PLATFORM_MACOS)
        find_library(ONNXRUNTIME_LIB onnxruntime PATHS /usr/local/lib /opt/homebrew/lib)
        if(ONNXRUNTIME_LIB)
            add_library(onnxruntime SHARED IMPORTED)
            set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ONNXRUNTIME_LIB})
        else()
            message(WARNING "ONNX Runtime not found on macOS - disabling (install: brew install onnxruntime)")
            set(USE_ONNX OFF)
        endif()
    else()
        find_library(ONNXRUNTIME_LIB onnxruntime)
        if(ONNXRUNTIME_LIB)
            add_library(onnxruntime SHARED IMPORTED)
            set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ONNXRUNTIME_LIB})
        else()
            message(WARNING "ONNX Runtime not found - disabling")
            set(USE_ONNX OFF)
        endif()
    endif()
endif()

# ============================================
# FAISS (optional, platform-specific)
# ============================================

if(USE_FAISS)
    if(PLATFORM_WINDOWS)
        set(FAISS_DIR "G:/vcpkg/installed/x64-windows")
        if(EXISTS "${FAISS_DIR}/lib/faiss.lib")
            add_library(faiss SHARED IMPORTED)
            set_target_properties(faiss PROPERTIES
                IMPORTED_IMPLIB "${FAISS_DIR}/lib/faiss.lib"
                IMPORTED_LOCATION "${FAISS_DIR}/bin/faiss.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${FAISS_DIR}/include")
            target_link_libraries(faiss INTERFACE "${FAISS_DIR}/lib/openblas.lib")
            if(USE_CUDA)
                target_link_libraries(faiss INTERFACE CUDA::cudart CUDA::cublas CUDA::cufft CUDA::curand)
                add_definitions(-DFAISS_GPU_ENABLED)
            endif()
        else()
            message(WARNING "FAISS not found - disabling RAG vector search")
            set(USE_FAISS OFF)
        endif()
    else()
        # macOS/Linux: try system FAISS (brew install faiss / apt install libfaiss-dev)
        find_library(FAISS_LIB faiss PATHS /usr/local/lib /opt/homebrew/lib)
        find_path(FAISS_INCLUDE faiss/IndexFlat.h PATHS /usr/local/include /opt/homebrew/include)
        if(FAISS_LIB AND FAISS_INCLUDE)
            add_library(faiss SHARED IMPORTED)
            set_target_properties(faiss PROPERTIES
                IMPORTED_LOCATION ${FAISS_LIB}
                INTERFACE_INCLUDE_DIRECTORIES ${FAISS_INCLUDE})
        else()
            message(WARNING "FAISS not found - disabling RAG vector search (install: brew install faiss)")
            set(USE_FAISS OFF)
        endif()
    endif()
endif()

# ============================================
# llama.cpp (optional, platform-specific)
# ============================================

if(USE_LOCAL_LLM)
    set(LLAMA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/llama.cpp")
    if(PLATFORM_WINDOWS AND EXISTS "${LLAMA_PATH}/build/src/Release/llama.lib")
        # Windows: use pre-built libs
        set(LLAMA_LIB_DIR "${LLAMA_PATH}/build/src/Release")
        set(GGML_LIB_DIR "${LLAMA_PATH}/build/ggml/src/Release")
        add_library(llama STATIC IMPORTED)
        set_target_properties(llama PROPERTIES
            IMPORTED_LOCATION "${LLAMA_LIB_DIR}/llama.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${LLAMA_PATH}/include")
        add_library(ggml STATIC IMPORTED)
        set_target_properties(ggml PROPERTIES IMPORTED_LOCATION "${GGML_LIB_DIR}/ggml.lib")
        add_library(ggml-base STATIC IMPORTED)
        set_target_properties(ggml-base PROPERTIES IMPORTED_LOCATION "${GGML_LIB_DIR}/ggml-base.lib")
        add_library(ggml-cpu STATIC IMPORTED)
        set_target_properties(ggml-cpu PROPERTIES IMPORTED_LOCATION "${GGML_LIB_DIR}/ggml-cpu.lib")
    elseif(EXISTS "${LLAMA_PATH}/CMakeLists.txt")
        # macOS/Linux: build from source with Metal/CPU
        set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
        if(APPLE)
            set(GGML_METAL ON CACHE BOOL "" FORCE)
            message(STATUS "llama.cpp: Metal GPU acceleration ENABLED")
        else()
            set(GGML_METAL OFF CACHE BOOL "" FORCE)
        endif()
        if(USE_CUDA)
            set(GGML_CUDA ON CACHE BOOL "" FORCE)
        else()
            set(GGML_CUDA OFF CACHE BOOL "" FORCE)
        endif()
        add_subdirectory(${LLAMA_PATH} llama.cpp)
    else()
        message(WARNING "llama.cpp not found - disabling local LLM inference")
        set(USE_LOCAL_LLM OFF)
    endif()
endif()

# ============================
# Enhanced FileReadTool (JackProAi alignment, 95% parity)
# ================================

option(USE_ENHANCED_FILE_READ "Enable enhanced file read (PDF/Image/Notebook)" ON)
option(USE_POPPLER "Enable native PDF support via poppler-cpp" OFF)
option(USE_ASYNC_FILE_READ "Enable async file read (experimental, +70% batch perf)" OFF)

if(USE_ENHANCED_FILE_READ)
    message(STATUS "Enhanced FileReadTool enabled")
    add_definitions(-DCLOSECRAB_ENHANCED_FILE_READ)

    # stb_image (header-only, bundled)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h")
     add_definitions(-DCLOSECRAB_HAS_STB_IMAGE)
        message(STATUS "  stb_image: bundled (image compression)")
    else()
        message(WARNING "  stb_image: NOT FOUND (download to third_party/stb/)")
    endif()

    # poppler-cpp (optional, for native PDF rendering)
    # Detection only here; actual link/include happens after add_executable.
    if(USE_POPPLER)
        if(PLATFORM_WINDOWS)
            find_library(POPPLER_CPP_LIB poppler-cpp PATHS "C:/poppler/Library/lib")
            find_path(POPPLER_INCLUDE poppler/cpp PATHS "C:/poppler/Library/include")
        else()
            find_library(POPPLER_CPP_LIB poppler-cpp)
            find_path(POPPLER_INCLUDE poppler/cpp)
        endif()

        if(POPPLER_CPP_LIB AND POPPLER_INCLUDE)
            add_definitions(-DCLOSECRAB_HAS_POPPLER)
            message(STATUS "  poppler: ENABLED (native PDF rendering)")
        else()
            message(WARNING "  poppler: NOT FOUND (PDF via external tools only)")
            set(USE_POPPLER OFF)
        endif()
    endif()

    # Async I/O (thread-pool based, experimental)
    if(USE_ASYNC_FILE_READ)
        add_definitions(-DCLOSECRAB_ASYNC_FILE_READ)
        message(STATUS "  Async I/O: ENABLED (experimental, +70% batch)")
    endif()
endif()

# ============================================
# Compile definitions based on available features
# ============================================

if(USE_ONNX)
    add_definitions(-DCLOSECRAB_HAS_ONNX)
endif()
if(USE_FAISS)
    add_definitions(-DCLOSECRAB_HAS_FAISS)
endif()
if(USE_LOCAL_LLM)
    add_definitions(-DCLOSECRAB_HAS_LOCAL_LLM)
endif()
if(USE_CUDA)
    add_definitions(-DCLOSECRAB_HAS_CUDA)
endif()

# ============================================
# Executable
# ============================================

add_executable(closecrab-unified
    src/main.cpp
    src/core/QueryEngine.cpp
    src/core/Message.cpp
    src/core/SessionManager.cpp
    src/core/SessionRouter.cpp
    src/core/ContextCollapse.cpp
    src/core/MessageSnip.cpp
    src/stats/StatsEngine.cpp
    src/knowledge/SharedKnowledge.cpp
    src/api/LocalLLMClient.cpp
    src/api/RemoteAPIClient.cpp
    src/api/OpenAICompatClient.cpp
    src/api/StreamParser.cpp
    src/tools/ToolRegistry.cpp
    src/commands/CommandRegistry.cpp
    src/permissions/PermissionEngine.cpp
    src/agents/AgentManager.cpp
    src/mcp/MCPClient.cpp
    src/mcp/MCPTransport.cpp
    src/lsp/LSPClient.cpp
    src/bridge/BridgeClient.cpp
    src/config/Config.cpp
    src/config/SettingsManager.cpp
    src/memory/MemorySystem.cpp
    src/llm/LLMEngine.cpp
    src/network/WebSocketServer.cpp
    src/network/HttpServer.cpp
    src/network/SSEServer.cpp
    src/security/Sandbox.cpp
    src/ssd/SSDExpertStreamer.cpp
    # License / online activation (序列号在线激活)
    src/license/LicenseCore.cpp
    src/license/LicenseGate.cpp
    src/license/tweetnacl.c
    src/license/tweetnacl_glue.c
)

if(USE_ONNX)
    target_sources(closecrab-unified PRIVATE
        src/rag/RAGManager.cpp
        src/rag/EmbeddingEngine.cpp
        src/rag/RerankerEngine.cpp
        src/rag/HFTokenizer.cpp
  )
endif()

# Enhanced FileReadTool sources
if(USE_ENHANCED_FILE_READ AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h")
    target_sources(closecrab-unified PRIVATE src/utils/ImageUtils.cpp)
endif()

# ============================================
# Link libraries (cross-platform)
# ============================================

set(CLOSECRAB_LIBS
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    yaml-cpp
    CLI11::CLI11
    sqlite3
    ixwebsocket
    capstone
    CURL::libcurl
    ZLIB::ZLIB
)

if(USE_ONNX)
    list(APPEND CLOSECRAB_LIBS onnxruntime)
endif()
if(USE_FAISS)
    list(APPEND CLOSECRAB_LIBS faiss)
endif()
if(USE_LOCAL_LLM)
    if(PLATFORM_WINDOWS)
        list(APPEND CLOSECRAB_LIBS llama ggml ggml-base ggml-cpu)
    else()
        list(APPEND CLOSECRAB_LIBS llama ggml)
    endif()
endif()

# Platform-specific system libraries
if(PLATFORM_WINDOWS)
    list(APPEND CLOSECRAB_LIBS ws2_32 wldap32 crypt32 advapi32 user32 iphlpapi secur32)
elseif(PLATFORM_MACOS)
    find_library(FOUNDATION Foundation)
    find_library(SECURITY Security)
    find_library(IOKIT IOKit)
    find_library(COREFOUNDATION CoreFoundation)
    list(APPEND CLOSECRAB_LIBS ${FOUNDATION} ${SECURITY} ${IOKIT} ${COREFOUNDATION} pthread)
    if(USE_LOCAL_LLM)
        find_library(METAL Metal)
        find_library(METALKIT MetalKit)
        if(METAL)
            list(APPEND CLOSECRAB_LIBS ${METAL} ${METALKIT})
        endif()
    endif()
elseif(PLATFORM_LINUX)
    list(APPEND CLOSECRAB_LIBS pthread dl)
endif()

target_link_libraries(closecrab-unified PRIVATE ${CLOSECRAB_LIBS})

# ============================================
# Include directories
# ============================================

set(CLOSECRAB_INCLUDES
    src
    ${thread_pool_SOURCE_DIR}/include
    ${httplib_SOURCE_DIR}
    ${capstone_SOURCE_DIR}/include
)

if(USE_ONNX AND PLATFORM_WINDOWS)
    list(APPEND CLOSECRAB_INCLUDES ${ONNXRUNTIME_DIR}/include)
endif()
if(USE_LOCAL_LLM AND EXISTS "${LLAMA_PATH}/include")
    list(APPEND CLOSECRAB_INCLUDES ${LLAMA_PATH}/include ${LLAMA_PATH}/ggml/include)
endif()
if(USE_FAISS AND PLATFORM_WINDOWS)
    list(APPEND CLOSECRAB_INCLUDES ${FAISS_DIR}/include)
endif()
if(USE_ENHANCED_FILE_READ AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb")
    list(APPEND CLOSECRAB_INCLUDES ${CMAKE_SOURCE_DIR}/third_party/stb)
endif()
if(USE_POPPLER AND POPPLER_CPP_LIB AND POPPLER_INCLUDE)
    list(APPEND CLOSECRAB_INCLUDES ${POPPLER_INCLUDE})
    target_link_libraries(closecrab-unified PRIVATE ${POPPLER_CPP_LIB})
endif()
if(EXISTS ${IXWEBSOCKET_PATH})
    list(APPEND CLOSECRAB_INCLUDES ${IXWEBSOCKET_PATH})
endif()

target_include_directories(closecrab-unified PRIVATE ${CLOSECRAB_INCLUDES})

# Ship the binary as the short command name `closecrab` on every platform
# (the CMake target stays `closecrab-unified`, so --target references are unchanged).
set_target_properties(closecrab-unified PROPERTIES OUTPUT_NAME closecrab)

# Inject the project version (single source of truth) so the banner / /version
# command never drift from CMakeLists again. Use via CLOSECRAB_VERSION macro.
target_compile_definitions(closecrab-unified PRIVATE CLOSECRAB_VERSION="${PROJECT_VERSION}")

# ============================================
# Post-build: Copy DLLs (Windows only)
# ============================================

if(PLATFORM_WINDOWS)
    # Bundle the MSVC C/C++ runtime (vcruntime140.dll, vcruntime140_1.dll,
    # msvcp140.dll, ...) next to the exe so it runs on a clean machine that does
    # not have the "Visual C++ Redistributable" installed. The exe and vcpkg's
    # libcurl.dll both link the dynamic CRT (MultiThreadedDLL), so without these
    # a fresh download dies with "VCRUNTIME140.dll is missing". The CMake module
    # resolves the correct version-suffixed DLL names for us.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)  # populate the list, don't add install() rules
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET closecrab-unified POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} $<TARGET_FILE_DIR:closecrab-unified>
            COMMAND_EXPAND_LISTS)
    endif()
    if(USE_FAISS AND EXISTS "${FAISS_DIR}/bin/faiss.dll")
        add_custom_command(TARGET closecrab-unified POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FAISS_DIR}/bin/faiss.dll" $<TARGET_FILE_DIR:closecrab-unified>)
    endif()
    if(USE_ONNX AND EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
        add_custom_command(TARGET closecrab-unified POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll" $<TARGET_FILE_DIR:closecrab-unified>)
    endif()
endif()

# ============================================
# Status
# ============================================

message(STATUS "========================================")
message(STATUS "CloseCrab-Unified v${PROJECT_VERSION}")
message(STATUS "  Platform:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "  CUDA:        ${USE_CUDA}")
message(STATUS "  Local LLM:   ${USE_LOCAL_LLM}")
message(STATUS "  FAISS:       ${USE_FAISS}")
message(STATUS "  ONNX:        ${USE_ONNX}")
if(APPLE AND USE_LOCAL_LLM)
    message(STATUS "  Metal:       ON (Apple GPU)")
endif()
message(STATUS "========================================")

# ============================================
# Tests (optional)
# ============================================

if(BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        enable_testing()
        add_executable(closecrab-tests tests/test_core.cpp src/api/StreamParser.cpp src/permissions/PermissionEngine.cpp)
        target_include_directories(closecrab-tests PRIVATE
            ${CMAKE_SOURCE_DIR}/src ${json_SOURCE_DIR}/include ${spdlog_SOURCE_DIR}/include)
        target_link_libraries(closecrab-tests PRIVATE GTest::gtest GTest::gtest_main spdlog::spdlog)
        # nlohmann/json may come from vcpkg find_package (no json_SOURCE_DIR) or
        # FetchContent. Link the imported target when present so its include dir
        # is carried regardless of source — the trailing-include path above is
        # empty on the vcpkg path and the test sources need <nlohmann/json.hpp>.
        if(TARGET nlohmann_json::nlohmann_json)
            target_link_libraries(closecrab-tests PRIVATE nlohmann_json::nlohmann_json)
        endif()
        add_test(NAME CoreTests COMMAND closecrab-tests)
    endif()
endif()
