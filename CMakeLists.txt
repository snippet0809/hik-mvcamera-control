cmake_minimum_required(VERSION 4.0)
project(hik-mvcamera-control)

# 供支持 compile_commands 的工具生成 compile_commands.json（可选）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 通过 FetchContent 获取 GoogleTest 与 fmt
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 12.1.0
)
FetchContent_MakeAvailable(googletest fmt)

set(HIK_CODE_READER_SOURCES
    src/code_reader/c_api.cpp
    src/code_reader/device_control.cpp
    src/code_reader/device_info.cpp
    src/code_reader/device_set_param.cpp
    src/code_reader/device_trigger.cpp
)

set(HIK_MVCR_LIB ${CMAKE_CURRENT_SOURCE_DIR}/lib/MvCodeReader/win64/MvCodeReaderCtrl.lib)

add_library(hik_code_reader STATIC ${HIK_CODE_READER_SOURCES})
set_target_properties(hik_code_reader PROPERTIES OUTPUT_NAME hik_code_reader_static)
target_include_directories(hik_code_reader
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/MvCodeReader
        ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
)
target_link_libraries(hik_code_reader PRIVATE ${HIK_MVCR_LIB})

# Python ctypes / Go cgo 等 FFI：共享库产物为 hik_code_reader.dll（导入库与静态库文件名区分开，避免 MSVC 覆盖）
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
set_target_properties(hik_code_reader_shared PROPERTIES OUTPUT_NAME hik_code_reader)

file(GLOB_RECURSE TEST_SOURCES "tests/*_test.cpp")

add_executable(all_tests ${TEST_SOURCES})
target_include_directories(all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
)
target_link_libraries(all_tests PRIVATE
    hik_code_reader
    GTest::gtest_main
    fmt::fmt
)

include(GoogleTest)
gtest_discover_tests(all_tests)
