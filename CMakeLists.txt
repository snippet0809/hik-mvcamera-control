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
# MinGW（gcc）：必须定义 WIN32，否则海康结构体布局错位
# -----------------------------------------------------------------------------
# 海康的 PixelType.h 里有：
#     #ifdef WIN32
#         PixelType_Gvsp_Undefined = 0xFFFFFFFF   // 全非负 -> GCC 选 4 字节 unsigned int
#     #else
#         PixelType_Gvsp_Undefined = -1           // 有负数，且另有 0x81200001 > INT_MAX
#     #endif                                      //   -> GCC 只能选 8 字节 long long
# MSVC 无论取值如何一律把枚举当 4 字节 int（甚至静默截断 0x81200001），而 MSVC 会定义
# WIN32；MinGW 只定义 _WIN32、不定义 WIN32，于是走 #else 分支，enum 变成 8 字节，
# 使 MV_FRAME_OUT_INFO_EX 之后所有字段偏移整体错位。
#
# 后果是**静默的数据损坏**而非编译错误：实测 len=0、frameNum 为垃圾值，
# 而 width/height 恰好还对（它们在结构体开头，偏移未受影响）。
# -fshort-enums 无效（值确实超出 32 位有符号）；加 -DWIN32 后偏移与 MSVC 逐项一致。
# 配套还需要 __int64：海康头在 #ifdef WIN32 分支里写的是 MSVC 专有的
#   typedef __int64 int64_t;
# 而 MinGW 的 gcc 不认 __int64（-fms-extensions、gnu++17 都不行），故用宏替成等价类型。
if(MINGW)
    add_compile_definitions(WIN32)
    add_compile_options("-D__int64=long long")
endif()

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
    # 静态链接 CRT（/MT）：这两个 DLL 是随 runtime/ 分发的，若用默认的 /MD 就会让使用方
    # 额外依赖 MSVCP140 / VCRUNTIME140 —— 那是微软 VC++ 可再发行组件包里的文件，
    # 干净工控机上不一定有，会把「免安装」变成「不用装海康的，但要装微软的」。
    #
    # 实测确认这个依赖是本工程自己引入的：runtime/ 里除这两个以外的每一个 DLL
    # （海康的全部在内）都不需要 VC14，它们用的是 MSVCR120 / MSVCP90 等更老的 CRT，
    # 而那些已经随包带上了。所以 /MT 的影响面为零。
    #
    # 安全性：导出的 C ABI 全是 const char* / size_t / POD 结构体 / 函数指针，
    # 没有 C++ 对象跨 DLL 边界，不存在「两套 CRT、两个堆」的经典风险。
    #
    # 只作用于 _shared：{target} 静态库仅测试用、不随包分发，保持默认 /MD，
    # 免得与同样用 /MD 的 GTest 混出 CRT 不匹配的链接错误。
    if(MSVC)
        set_target_properties(${target}_shared PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    # MinGW（gcc）同样支持：GNU ld 能直接读海康那两个 MSVC 导入库，且导出符号
    # 与 MSVC 构建逐字节一致（实测 13/13 未修饰同名）。两处与 MSVC 不同的处理：
    #   1) 默认会加上 lib 前缀，产出 libhik_code_reader.dll，与 cgo / Node / Python
    #      按名字找的 hik_code_reader.dll 对不上——去掉前缀。
    #   2) 默认动态链接 GCC 运行库，会引入 libgcc_s_seh-1 / libstdc++-6 /
    #      libwinpthread-1 三个额外依赖——静态并入，否则只是把「依赖微软的 3 个
    #      DLL」换成「依赖 MinGW 的 3 个 DLL」，随包分发的问题原样保留。
    if(MINGW)
        set_target_properties(${target}_shared PROPERTIES PREFIX "")
        target_link_options(${target}_shared PRIVATE
            -static-libgcc -static-libstdc++ -static)
    endif()
    # 见文件顶部 MINGW 处关于 WIN32 的说明——不定义它会导致海康结构体布局错位。
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
