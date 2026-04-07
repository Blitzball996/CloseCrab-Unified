cmake_minimum_required(VERSION 3.20)

project(CloseCrab-Unified VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_PREFIX_PATH "G:/vcpkg/installed/x64-windows")

# ============================================
# Global config
# ============================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Zc:char8_t-)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ============================================
# CUDA
# ============================================

find_package(CUDAToolkit REQUIRED)

# ============================================
# vcpkg dependencies
# ============================================

find_package(CURL CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# ============================================
# FetchContent
# ============================================

include(FetchContent)

# ============================================
# ONNX Runtime
# ============================================

set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime-win-x64-gpu-1.24.4")
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
    IMPORTED_IMPLIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_DIR}/include"
)

if(USE_ONNX_GPU AND EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_cuda.dll")
    add_definitions(-DUSE_ONNX_GPU)
    add_library(onnxruntime_cuda SHARED IMPORTED)
    set_target_properties(onnxruntime_cuda PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_cuda.dll"
    )
    set(ONNXRUNTIME_EXTRA_LIBS onnxruntime_cuda)
else()
    set(ONNXRUNTIME_EXTRA_LIBS "")
endif()

# spdlog
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1)
FetchContent_MakeAvailable(spdlog)

# json
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3)
FetchContent_MakeAvailable(json)

# yaml-cpp
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0)
FetchContent_MakeAvailable(yaml-cpp)

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
    URL https://www.sqlite.org/2024/sqlite-autoconf-3460100.tar.gz)
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

# ============================================
# ixwebsocket (local)
# ============================================

set(IXWEBSOCKET_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/ixwebsocket")
if(EXISTS ${IXWEBSOCKET_PATH}/CMakeLists.txt)
    add_subdirectory(${IXWEBSOCKET_PATH})
endif()

# ============================================
# FAISS (vcpkg)
# ============================================

set(FAISS_DIR "G:/vcpkg/installed/x64-windows")
add_library(faiss SHARED IMPORTED)
set_target_properties(faiss PROPERTIES
    IMPORTED_IMPLIB "${FAISS_DIR}/lib/faiss.lib"
    IMPORTED_LOCATION "${FAISS_DIR}/bin/faiss.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${FAISS_DIR}/include"
)
target_link_libraries(faiss INTERFACE
    "${FAISS_DIR}/lib/openblas.lib"
    CUDA::cudart CUDA::cublas CUDA::cufft CUDA::curand
)

# ============================================
# llama.cpp (pre-built)
# ============================================

set(LLAMA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/llama.cpp")
set(LLAMA_LIB_DIR "${LLAMA_PATH}/build/src/Release")
set(GGML_LIB_DIR "${LLAMA_PATH}/build/ggml/src/Release")

add_library(llama STATIC IMPORTED)
set_target_properties(llama PROPERTIES
    IMPORTED_LOCATION "${LLAMA_LIB_DIR}/llama.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${LLAMA_PATH}/include"
)

add_library(ggml STATIC IMPORTED)
set_target_properties(ggml PROPERTIES IMPORTED_LOCATION "${GGML_LIB_DIR}/ggml.lib")

add_library(ggml-base STATIC IMPORTED)
set_target_properties(ggml-base PROPERTIES IMPORTED_LOCATION "${GGML_LIB_DIR}/ggml-base.lib")

add_library(ggml-cpu STATIC IMPORTED)
set_target_properties(ggml-cpu PROPERTIES IMPORTED_LOCATION "${GGML_LIB_DIR}/ggml-cpu.lib")

# ============================================
# Executable: closecrab-unified
# ============================================

add_definitions(-DFAISS_GPU_ENABLED)

add_executable(closecrab-unified
    # Main entry
    src/main.cpp

    # Core
    src/core/QueryEngine.cpp
    src/core/Message.cpp
    src/core/SessionManager.cpp

    # API clients
    src/api/LocalLLMClient.cpp
    src/api/RemoteAPIClient.cpp
    src/api/OpenAICompatClient.cpp
    src/api/StreamParser.cpp

    # Tools
    src/tools/ToolRegistry.cpp

    # Commands
    src/commands/CommandRegistry.cpp

    # Permissions
    src/permissions/PermissionEngine.cpp

    # Agents
    src/agents/AgentManager.cpp

    # MCP
    src/mcp/MCPClient.cpp

    # LSP
    src/lsp/LSPClient.cpp

    # Bridge
    src/bridge/BridgeClient.cpp

    # Config
    src/config/Config.cpp
    src/config/SettingsManager.cpp

    # Memory
    src/memory/MemorySystem.cpp

    # LLM
    src/llm/LLMEngine.cpp

    # Network
    src/network/WebSocketServer.cpp
    src/network/HttpServer.cpp
    src/network/SSEServer.cpp

    # Security
    src/security/Sandbox.cpp

    # RAG
    src/rag/RAGManager.cpp
    src/rag/EmbeddingEngine.cpp
    src/rag/RerankerEngine.cpp
    src/rag/HFTokenizer.cpp

    # SSD Expert Streaming
    src/ssd/SSDExpertStreamer.cpp
)

if(USE_CUDA_EXPERT_CACHE)
    target_compile_definitions(closecrab-unified PRIVATE USE_CUDA_EXPERT_CACHE)
endif()

# ============================================
# Link libraries
# ============================================

target_link_libraries(closecrab-unified PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    yaml-cpp
    CLI11::CLI11
    sqlite3

    onnxruntime
    ${ONNXRUNTIME_EXTRA_LIBS}
    faiss
    llama ggml ggml-base ggml-cpu

    ixwebsocket

    CURL::libcurl
    ZLIB::ZLIB

    ws2_32 wldap32 crypt32 advapi32 user32 iphlpapi secur32
)

# ============================================
# Include directories
# ============================================

target_include_directories(closecrab-unified PRIVATE
    src
    ${thread_pool_SOURCE_DIR}/include
    ${ONNXRUNTIME_DIR}/include
    ${LLAMA_PATH}/include
    ${LLAMA_PATH}/ggml/include
    ${FAISS_DIR}/include
    ${IXWEBSOCKET_PATH}
    ${httplib_SOURCE_DIR}
)

# ============================================
# Copy DLLs
# ============================================

add_custom_command(TARGET closecrab-unified POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FAISS_DIR}/bin/faiss.dll" $<TARGET_FILE_DIR:closecrab-unified>
)

add_custom_command(TARGET closecrab-unified POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll" $<TARGET_FILE_DIR:closecrab-unified>
)

if(ONNXRUNTIME_EXTRA_LIBS)
    add_custom_command(TARGET closecrab-unified POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_cuda.dll"
            $<TARGET_FILE_DIR:closecrab-unified>
    )
endif()

# ============================================
# Status
# ============================================

message(STATUS "========================================")
message(STATUS "CloseCrab-Unified v${PROJECT_VERSION}")
message(STATUS "MSVC Runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
message(STATUS "========================================")

# ============================================
# Tests (optional, requires GTest)
# ============================================

option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        enable_testing()
        add_executable(closecrab-tests
            tests/test_core.cpp
            src/api/StreamParser.cpp
        )
        target_include_directories(closecrab-tests PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${json_SOURCE_DIR}/include
            ${spdlog_SOURCE_DIR}/include
        )
        target_link_libraries(closecrab-tests PRIVATE GTest::gtest GTest::gtest_main spdlog::spdlog)
        add_test(NAME CoreTests COMMAND closecrab-tests)
        message(STATUS "Tests: ENABLED")
    else()
        message(STATUS "Tests: GTest not found, skipping")
    endif()
endif()
