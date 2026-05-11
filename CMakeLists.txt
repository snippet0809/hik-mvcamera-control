# =============================================================================
# hik-mvcamera-control — CMake 构建说明（读码器 C++ 封装 + 测试 + 共享库）
# =============================================================================
# 典型流程（本机）：
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#   ctest --test-dir build -C Release
# 配置阶段会在仓库根目录生成 compile_commands.json（已 .gitignore），供 clangd / Cursor 找到 MvCodeReader 等头文件。
# 当前工程面向 Windows x64，链接海康 MvCodeReader 的 win64 静态库。
# =============================================================================

# 要求 CMake 最低版本；低于此版本的 cmake 会直接报错退出，避免用到不支持的语法。
cmake_minimum_required(VERSION 4.0)

# 定义工程名；同时会设置 PROJECT_SOURCE_DIR 等变量，供下面路径引用。
project(hik-mvcamera-control)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# 工具链 / IDE：生成 compile_commands.json
# -----------------------------------------------------------------------------
# 开启后，在构建目录里生成 compile_commands.json，供 clangd、VS Code C++ 插件等
# 做「跳转、补全」；不参与编译结果本身，可关可开。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# 第三方库：GoogleTest（测试）
# -----------------------------------------------------------------------------
# FetchContent：在配置阶段从网络下载依赖源码并加入当前工程（无需系统预装）。
# 使用 zip URL 而非 git clone：不依赖本机安装 Git，部分网络环境也更省事。
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    # 解压时间戳策略，避免新版 CMake 对 CMP0135 的警告
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# 下载、add_subdirectory、定义目标 GTest::gtest_main 等，供后面 target_link_libraries 使用。
FetchContent_MakeAvailable(googletest)

# -----------------------------------------------------------------------------
# 读码器封装：源文件列表（与海康 SDK 头文件 + MvCodeReaderCtrl.lib 一起编进库）
# -----------------------------------------------------------------------------
set(HIK_CODE_READER_SOURCES
    src/code_reader/c_api.cpp
    src/code_reader/device_control.cpp
    src/code_reader/device_info.cpp
    src/code_reader/device_set_param.cpp
    src/code_reader/device_trigger.cpp
)

# 海康读码器 SDK 在仓库中的 Windows x64 导入/静态库路径（与 CI、发版一致）。
set(HIK_MVCR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/lib/MvCodeReader/win64/MvCodeReaderCtrl.lib)

# -----------------------------------------------------------------------------
# 目标 1：静态库 hik_code_reader_static（.lib）
# -----------------------------------------------------------------------------
# 给单元测试、其它可执行文件链接用；不会导出 DLL。
add_library(hik_code_reader STATIC ${HIK_CODE_READER_SOURCES})

# 产物磁盘上的基础名设为 hik_code_reader_static，避免与下面 DLL 同名冲突。
set_target_properties(hik_code_reader PROPERTIES OUTPUT_NAME hik_code_reader_static)

# include 目录：
#   PUBLIC  — 链接本库的目标也会自动带上这些头文件路径（如对外 ABI 头 hik_code_reader/）。
#   PRIVATE — 仅编译本库源文件时使用（MvCodeReader 内部头、实现目录）。
target_include_directories(hik_code_reader
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/MvCodeReader
        ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
)

# 链接海康提供的 MvCodeReader 库（Windows 下为 .lib）。
target_link_libraries(hik_code_reader PRIVATE ${HIK_MVCR_LIB})

# -----------------------------------------------------------------------------
# 目标 2：共享库 hik_code_reader.dll（Python ctypes / Go cgo 等 FFI）
# -----------------------------------------------------------------------------
# 与静态库共用同一套 .cpp，但编译为 DLL，并定义 HIK_CR_COMPILE_DLL 以导出 C API。
add_library(hik_code_reader_shared SHARED ${HIK_CODE_READER_SOURCES})
target_compile_definitions(hik_code_reader_shared PRIVATE HIK_CR_COMPILE_DLL)
target_include_directories(hik_code_reader_shared
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/MvCodeReader
        ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
)
target_link_libraries(hik_code_reader_shared PRIVATE ${HIK_MVCR_LIB})

# 输出文件名为 hik_code_reader（Windows 上即 hik_code_reader.dll），与静态库的 hik_code_reader_static 区分，
# 否则 MSVC 可能把导入库和静态库文件名搞混、互相覆盖。
set_target_properties(hik_code_reader_shared PROPERTIES OUTPUT_NAME hik_code_reader)

# -----------------------------------------------------------------------------
# 测试可执行文件 all_tests
# -----------------------------------------------------------------------------
# 收集 tests 目录下所有 *_test.cpp；新增测试文件后若 CMake 未自动刷新，可重新 Configure。
file(GLOB_RECURSE TEST_SOURCES "tests/*_test.cpp")

add_executable(all_tests ${TEST_SOURCES})
target_include_directories(all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
)
# 链接：本工程静态库 + GoogleTest（提供 main 与断言宏）。
target_link_libraries(all_tests PRIVATE
    hik_code_reader
    GTest::gtest_main
)

# 向 CTest 注册每个 TEST() 用例，便于 ctest -R 筛选、IDE 里单独跑某条测试。
# PRE_TEST：在运行 ctest 时再发现用例；若用 POST_BUILD，构建阶段会执行 all_tests.exe，
# 链了海康库时易因缺运行库报 0xc0000135（STATUS_DLL_NOT_FOUND），CI 仅 cmake --build 也会失败。
include(GoogleTest)
gtest_discover_tests(all_tests DISCOVERY_MODE PRE_TEST)

# -----------------------------------------------------------------------------
# clangd / Cursor：仓库根目录 compile_commands.json
# -----------------------------------------------------------------------------
# Visual Studio 等多配置生成器通常不在构建目录写出 compile_commands.json，clangd 会找不到
# MvCodeReaderCtrl.h 等 include。每次 CMake 配置时在源码根生成一份（路径随本机变化，已加入 .gitignore）。
set(_HIK_CLANGD_ROOT "${CMAKE_SOURCE_DIR}")
cmake_path(CONVERT "${_HIK_CLANGD_ROOT}" TO_CMAKE_PATH_LIST _HIK_CLANGD_ROOT NORMALIZE)
string(REPLACE "\\" "/" _HIK_CLANGD_ROOT_JSON "${_HIK_CLANGD_ROOT}")
set(_HIK_CLANGD_CPP ${HIK_CODE_READER_SOURCES})
list(APPEND _HIK_CLANGD_CPP ${TEST_SOURCES})
set(_HIK_CLANGD_JSON "[")
set(_HIK_CLANGD_SEP "")
foreach(_HIK_REL IN LISTS _HIK_CLANGD_CPP)
    cmake_path(IS_ABSOLUTE _HIK_REL _HIK_REL_ABS)
    if(_HIK_REL_ABS)
        file(RELATIVE_PATH _HIK_REL "${CMAKE_SOURCE_DIR}" "${_HIK_REL}")
    endif()
    cmake_path(CONVERT "${_HIK_REL}" TO_CMAKE_PATH_LIST _HIK_REL NORMALIZE)
    string(REPLACE "\\" "/" _HIK_REL_JSON "${_HIK_REL}")
    set(_HIK_FILE "${_HIK_CLANGD_ROOT_JSON}/${_HIK_REL_JSON}")
    set(_HIK_CMD "clang++ -std=c++17 -Wno-unused-command-line-argument -I${_HIK_CLANGD_ROOT_JSON}/include -I${_HIK_CLANGD_ROOT_JSON}/include/MvCodeReader -I${_HIK_CLANGD_ROOT_JSON}/src/code_reader")
    # 测试源文件还依赖 FetchContent 的 gtest 头文件，否则 clangd 会报一整串假错误
    if(_HIK_REL_JSON MATCHES "^tests/")
        cmake_path(CONVERT "${googletest_SOURCE_DIR}/googletest/include" TO_CMAKE_PATH_LIST _HIK_GTEST_INC NORMALIZE)
        string(REPLACE "\\" "/" _HIK_GTEST_INC_JSON "${_HIK_GTEST_INC}")
        string(APPEND _HIK_CMD " -I${_HIK_GTEST_INC_JSON}")
    endif()
    string(APPEND _HIK_CMD " -c ${_HIK_FILE}")
    string(APPEND _HIK_CLANGD_JSON "${_HIK_CLANGD_SEP}\n  {\n    \"directory\": \"${_HIK_CLANGD_ROOT_JSON}\",\n    \"file\": \"${_HIK_FILE}\",\n    \"command\": \"${_HIK_CMD}\"\n  }")
    set(_HIK_CLANGD_SEP ",")
endforeach()
string(APPEND _HIK_CLANGD_JSON "\n]\n")
file(WRITE "${CMAKE_SOURCE_DIR}/compile_commands.json" "${_HIK_CLANGD_JSON}")
