# hik-mvcamera-control

围绕海康机器人 **机器视觉 SDK** 的本地封装与工程骨架：仓库中已包含 **读码器（MvCodeReader）** 的头文件与静态库，当前 `src` 下的实现是一套 C++ 封装（设备枚举、开关流、网络与参数设置等），并带有基于 GoogleTest 的 CMake 测试目标。`include/lib` 下同时提供了 **工业相机（MvCamera）** 相关 SDK 文件，便于后续扩展相机侧逻辑。

## 功能概览（读码器 C++ 封装）

- **枚举设备**：`enumDevice()`，返回序列号与 GigE 导出 IP 等信息（见 `src/code_reader/device_info.cpp`）。
- **设备生命周期**：通过序列号获取或创建 `CodeReader` 实例（`getDevice` / `destroyDevice`），`open` / `close` / `grabbing` 与 SDK 的打开、关闭、起停取流对应。
- **运行控制**：`startDevice` / `stopDevice`（封装为开始取流与关闭到已连接状态）。
- **参数与网络**：`setIp`（GigE 强制 IP，会触发设备重启并销毁本地句柄）、`setIntValue` / `setFloatValue` / `setBoolValue` / `setStringValue`（需在「已打开且未取流」状态下设置，与源码注释一致）。
- **C 接口**：`src/code_reader/c_api.cpp` 中提供 `c_startDevice`、`c_stopDevice`、`c_triggerDevice`、`c_setIp` 及各类型 `c_set*Value` 等，便于给其他语言或动态库做 FFI。
- **占位**：`registerImageCallback`、`triggerDevice` 在 `device_trigger.cpp` 中尚未实现。

## 仓库结构

| 路径 | 说明 |
|------|------|
| `src/code_reader/` | 读码器封装实现与 `c_api.cpp` |
| `include/MvCamera/`、`include/MvCodeReader/` | 海康 SDK 头文件 |
| `lib/MvCamera/{win32,win64}/`、`lib/MvCodeReader/{win32,win64}/` | 预置静态库（含 `turbojpeg` 等读码器依赖） |
| `tests/` | GTest 用例（需连接真实设备时谨慎运行） |
| `python/` | 预留目录（当前无可用 Python 包配置） |
| `.docs/` | 本地文档（如读码器开发指南 CHM） |

## 构建要求

- **CMake** 4.0 及以上（见根目录 `CMakeLists.txt`）。
- 支持 C++17 或项目所用特性的 **MSVC**（当前工程通过 `FetchContent` 拉取 **GoogleTest** 与 **{fmt}**）。
- **Windows**：默认链接 `lib/MvCodeReader/win64/MvCodeReaderCtrl.lib`；若在 32 位环境构建，需自行将 CMake 中的库路径改为 `win32` 对应文件。

## 构建与测试

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

首次配置会从网络下载 GTest 与 fmt；需保证构建环境可访问 GitHub。

**注意**：`tests/code_reader_test.cpp` 中调用了 `enumCodeReader()`，而公共 API 在 `code_reader.h` 中声明为 `enumDevice()`。在运行测试前请将用例改为基于 `enumDevice()` 取得序列号列表，否则无法通过编译。

## 运行与部署说明

1. 在目标机器安装海康读码器/视觉设备所需 **驱动与运行库**（版本需与 SDK 匹配）。
2. GigE 设备注意网卡、防火墙与网段；修改 IP 前阅读 `setIp` 注释（设备须处于合适状态，改 IP 后会重启）。
3. 将 SDK 提供的 **DLL**（若静态链仍依赖运行时）放在可执行文件同目录或系统 `PATH` 中，按官方文档为准。

## 许可证与第三方

海康威视 **MvCamera / MvCodeReader** SDK 及其文档的版权与许可归原著作权人所有；本仓库中的封装代码请以你方项目许可证为准。GoogleTest、{fmt} 遵循各自开源协议（由 CMake `FetchContent` 获取）。

## 相关文档

- 读码器开发说明可参考仓库内 `.docs` 下文档及 `include/MvCodeReader` 头文件中的 API 定义。
