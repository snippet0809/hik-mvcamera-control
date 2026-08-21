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
    URL_HASH SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff
    # 解压时间戳策略，避免新版 CMake 对 CMP0135 的警告
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# 下载、add_subdirectory、定义目标 GTest::gtest_main 等，供后面 target_link_libraries 使用。
FetchContent_MakeAvailable(googletest)

# -----------------------------------------------------------------------------
# 统一「包装库」目标：static + shared（DLL），共享同一份源与 include/链接配置。
# -----------------------------------------------------------------------------
# 用法：hik_wrapper_library(<target> <ABI 前缀> <src 目录> <SDK 头目录> <SDK 库> <源文件...>)
#   - {target}          静态库 {target}_static.lib（供测试/其它可执行文件链接）
#   - {target}_shared   共享库 {target}.dll，定义 {prefix}_COMPILE_DLL 导出 C ABI
function(hik_wrapper_library target prefix src_dir sdk_inc sdk_lib)
    # 静态库
    add_library(${target} STATIC ${ARGN})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${target}_static)
    target_include_directories(${target}
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${sdk_inc} ${src_dir}
    )
    target_link_libraries(${target} PRIVATE ${sdk_lib})

    # 共享库（与静态库共用 .cpp；DLL 基础名不带上 _static 后缀，避免与静态库同名冲突）
    add_library(${target}_shared SHARED ${ARGN})
    target_compile_definitions(${target}_shared PRIVATE ${prefix}_COMPILE_DLL)
    target_include_directories(${target}_shared
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${sdk_inc} ${src_dir}
    )
    target_link_libraries(${target}_shared PRIVATE ${sdk_lib})
    set_target_properties(${target}_shared PROPERTIES OUTPUT_NAME ${target})
endfunction()

# SDK 导入/静态库按目标架构选择 win64/win32（仓库两者均预置）。
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(HIK_SDK_ARCH win32)
else()
    set(HIK_SDK_ARCH win64)
endif()

# -----------------------------------------------------------------------------
# 读码器封装（hik_cr_* C ABI → hik_code_reader.dll，链 MvCodeReaderCtrl.lib）
# -----------------------------------------------------------------------------
set(HIK_CODE_READER_SOURCES
    src/code_reader/c_api.cpp
    src/code_reader/device_control.cpp
    src/code_reader/device_info.cpp
    src/code_reader/device_trigger.cpp
    src/code_reader/param.cpp
)

hik_wrapper_library(hik_code_reader HIK_CR src/code_reader
    ${CMAKE_CURRENT_SOURCE_DIR}/include/MvCodeReader
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/MvCodeReader/${HIK_SDK_ARCH}/MvCodeReaderCtrl.lib
    ${HIK_CODE_READER_SOURCES})

# -----------------------------------------------------------------------------
# 相机封装（hik_cv_* C ABI → hik_mvcamera.dll，链 MvCameraControl.lib）
# -----------------------------------------------------------------------------
set(HIK_MVCAMERA_SOURCES
    src/mvcamera/c_api.cpp
    src/mvcamera/device_control.cpp
    src/mvcamera/device_info.cpp
    src/mvcamera/image_callback.cpp
    src/mvcamera/param.cpp
)

hik_wrapper_library(hik_mvcamera HIK_CV src/mvcamera
    ${CMAKE_CURRENT_SOURCE_DIR}/include/MvCamera
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/MvCamera/${HIK_SDK_ARCH}/MvCameraControl.lib
    ${HIK_MVCAMERA_SOURCES})

# -----------------------------------------------------------------------------
# 测试可执行文件 all_tests
# -----------------------------------------------------------------------------
# 收集 tests 目录下所有 *_test.cpp；CONFIGURE_DEPENDS 让新增测试文件自动触发重新配置。
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "tests/*_test.cpp")

add_executable(all_tests ${TEST_SOURCES})
target_include_directories(all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mvcamera
)
# 链接：本工程静态库 + GoogleTest（提供 main 与断言宏）。
target_link_libraries(all_tests PRIVATE
    hik_code_reader
    hik_mvcamera
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
list(APPEND _HIK_CLANGD_CPP ${HIK_MVCAMERA_SOURCES})
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
    set(_HIK_CMD "clang++ -std=c++17 -Wno-unused-command-line-argument -I${_HIK_CLANGD_ROOT_JSON}/include -I${_HIK_CLANGD_ROOT_JSON}/include/MvCodeReader -I${_HIK_CLANGD_ROOT_JSON}/src/code_reader -I${_HIK_CLANGD_ROOT_JSON}/include/MvCamera -I${_HIK_CLANGD_ROOT_JSON}/src/mvcamera")
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
