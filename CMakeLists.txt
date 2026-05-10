cmake_minimum_required(VERSION 4.0)
project(hik-mvcamera-control)

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

# 自动收集所有源文件和测试文件
file(GLOB_RECURSE SRC_SOURCES "src/**/*.cpp")
file(GLOB_RECURSE TEST_SOURCES "tests/*_test.cpp")

# 创建测试可执行文件
add_executable(all_tests ${SRC_SOURCES} ${TEST_SOURCES})

# include目录
target_include_directories(all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/MvCodeReader
    ${CMAKE_CURRENT_SOURCE_DIR}/src/code_reader
)

# 链接库
target_link_libraries(all_tests PRIVATE
    GTest::gtest_main
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/MvCodeReader/win64/MvCodeReaderCtrl.lib
    fmt::fmt
)

# 注册测试
include(GoogleTest)
gtest_discover_tests(all_tests)