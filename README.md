# hik-mvcamera-control

围绕海康机器人 **机器视觉 SDK** 的本地封装与工程骨架：仓库中已包含 **读码器（MvCodeReader）** 的头文件与静态库，当前 `src` 下的实现是一套 C++ 封装（设备枚举、开关流、网络与参数设置等），并带有基于 GoogleTest 的 CMake 测试目标。`include/lib` 下同时提供了 **工业相机（MvCamera）** 相关 SDK 文件，便于后续扩展相机侧逻辑。

## 功能概览（读码器 C++ 封装）

- **枚举设备**：`enumDevice()`，返回序列号与 GigE 导出 IP 等信息（见 `src/code_reader/device_info.cpp`）。
- **运行控制**：`startDevice` / `stopDevice`（内部完成打开设备、注册图像回调、起停取流等；按序列号操作）。
- **读码回调**：`registerImageCallback`（BCR 结果列表）；**软触发**：`triggerDevice`（仅当设备处于取流 Grabbing）。
- **改参前置**：`openDeviceForParameters` 将设备置为 **Open**（已 OpenDevice、未取流）。正在取流时须先 `stopDevice`，否则会抛 `std::logic_error`。
- **GigE 网络**：`setIp` 改 IP/掩码/网关（与 GenICam 写参不同类；成功后常 `destroyDevice` 释放本地缓存）。
- **GenICam 写参**：`setIntValue` / `setFloatValue` / `setBoolValue` / `setStringValue` / `setEnumValue` / `setEnumValueByString`（均须先处于 Open，约定同 `openDeviceForParameters`）。

## 仓库结构

| 路径 | 说明 |
|------|------|
| `src/code_reader/` | 读码器 C++ 封装实现（`code_reader.h` 及对应 `.cpp`） |
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

## 运行与部署说明

1. 在目标机器安装海康读码器/视觉设备所需 **驱动与运行库**（版本需与 SDK 匹配）。
2. GigE 设备注意网卡、防火墙与网段；改 IP 前须 `openDeviceForParameters` 使设备处于 **Open**，并阅读 `setIp` 注释（改 IP 后设备常重启，本地句柄会被移除）。
3. 将 SDK 提供的 **DLL**（若静态链仍依赖运行时）放在可执行文件同目录或系统 `PATH` 中，按官方文档为准。

## 许可证与第三方

海康威视 **MvCamera / MvCodeReader** SDK 及其文档的版权与许可归原著作权人所有；本仓库中的封装代码请以你方项目许可证为准。GoogleTest、{fmt} 遵循各自开源协议（由 CMake `FetchContent` 获取）。

## 相关文档

- 读码器开发说明可参考仓库内 `.docs` 下文档及 `include/MvCodeReader` 头文件中的 API 定义。
