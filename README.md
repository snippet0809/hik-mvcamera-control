# hik-mvcamera-control

围绕海康机器人 **机器视觉 SDK** 的本地封装与工程骨架：仓库中已包含 **读码器（MvCodeReader）** 的头文件与静态库，当前 `src` 下的实现是一套 C++ 封装（设备枚举、开关流、网络与参数设置等），并带有基于 GoogleTest 的 CMake 测试目标。对外提供 **稳定 C ABI**（`include/hik_code_reader/c_api.h` + `c_api.cpp`），便于 **Python（ctypes）** 与 **Go（cgo）** 等语言加载 `hik_code_reader` 共享库调用。`include/lib` 下同时提供了 **工业相机（MvCamera）** 相关 SDK 文件，便于后续扩展相机侧逻辑。

## 功能概览（读码器 C++ 封装）

- **枚举设备**：`enumDevice()`，返回序列号与 GigE 导出 IP 等信息（见 `src/code_reader/device_info.cpp`）。
- **运行控制**：`startDevice` / `stopDevice`（内部完成打开设备、注册图像回调、起停取流等；按序列号操作）。
- **读码回调**：`registerImageCallback`（BCR 结果列表）；**软触发**：`triggerDevice`（仅当设备处于取流 Grabbing）。
- **改参前置**：`openDeviceForParameters` 将设备置为 **Open**（已 OpenDevice、未取流）。正在取流时须先 `stopDevice`，否则会抛 `std::logic_error`。
- **GigE 网络**：`setIp` 改 IP/掩码/网关（与 GenICam 写参不同类；成功后常 `destroyDevice` 释放本地缓存）。
- **GenICam 写参**：`setIntValue` / `setFloatValue` / `setBoolValue` / `setStringValue` / `setEnumValue` / `setEnumValueByString`（均须先处于 Open，约定同 `openDeviceForParameters`）。
- **C API / FFI**：C 函数前缀 `hik_cr_*`，返回 `HikCrResult`，错误信息用 `hik_cr_last_error_copy` 按线程读取。正式发布用 **`python/hik_code_reader`**（wheel 内嵌 DLL）；`ffi/python` 为同逻辑参考副本。Go 见 **`ffi/go`**。

## 在 GitHub 上托管分发（维护者）

GitHub Packages **没有**与 PyPI 对等的 Python 包仓，也**没有**替代 `go get` 的独立 Go Registry。本仓库采用 **「Releases + GitHub Pages（PEP 503）+ Git 标签」**，全部留在 GitHub 上完成托管。

### 原理简述

| 对象 | 托管位置 | 作用 |
|------|----------|------|
| Python wheel | **GitHub Releases** 附件 | 真实安装包；`pip` 最终下载的文件 |
| `pip search` 式索引 | **GitHub Pages**（`gh-pages` 分支的 `simple/`） | 符合 **PEP 503**，让开发者能用 `pip install 包名==版本 --index-url ...` |
| Go 源码模块 | **本仓库 Git** + 标签 **`ffi/go/vX.Y.Z`** | `go get` 经官方模块代理从 GitHub 拉取 |

### 维护者一次性配置

1. 保证 **`ffi/go/go.mod`** 第一行与你的 GitHub 路径一致，例如仓库 `https://github.com/you/hik-mvcamera-control` 时应为：  
   `module github.com/you/hik-mvcamera-control/ffi/go`  
   （本仓库示例为 `github.com/snipp/hik-mvcamera-control/ffi/go`，若你的用户名或组织不同，请改这一行并提交。）
2. 在 GitHub 打开本仓库：**Settings → Pages**  
   - **Build and deployment**：Source 选 **Deploy from a branch**  
   - Branch 选 **`gh-pages`**，文件夹选 **`/(root)`**  
   - 保存。首次需等 **`release.yml` 成功跑过一次** 后才有 `gh-pages` 分支。

### 维护者发版步骤

1. 在默认分支上确认代码与 `python/pyproject.toml` 已就绪。  
2. 创建并推送 **语义化标签**（必须以 `v` 开头）：  
   `git tag v0.1.0 && git push origin v0.1.0`  
3. **GitHub Actions** 中 **`Release`** 工作流（`release.yml`）会自动：  
   - 将 **`python/pyproject.toml` 里的 `version`** 改成与标签一致（去掉 `v`，如 `v0.1.0` → `0.1.0`），再构建 **Windows x64 wheel**（内含 `hik_code_reader.dll`）；  
   - 创建/更新 **GitHub Release**，并上传 wheel、Go/cgo 用 zip、`dll`、`lib`；  
   - 生成 **PEP 503** 页面并推送到 **`gh-pages`**（与已有索引合并，保留历史版本链接）；  
   - 在同一提交上自动创建 **`ffi/go/v0.1.0`** 标签（若不存在），供 `go get` 使用。

### 发版后维护者自检

- **Releases** 页面是否出现新版本，且附件中有 `.whl`。  
- **Actions** 里 `Release` 是否全部绿色。  
- **Settings → Pages** 是否显示站点地址（约 `https://<用户>.github.io/<仓库名>/`）。  
- 浏览器打开 `https://<用户>.github.io/<仓库名>/simple/hik-code-reader/`，应能看到指向 Release 的链接。

---

## 开发者使用指南

以下假设仓库为 `github.com/you/hik-mvcamera-control`，请把 `you` / 仓库名换成实际路径。

### Python 开发者

**环境**：当前 wheel 为 **Windows x64**，需在对应环境安装。

**方式 A（推荐，像用「私有 PyPI 源」一样）**  
在维护者已开启 **Pages** 且发过版的前提下：

```bash
pip install "hik-code-reader==0.1.0" \
  --index-url "https://you.github.io/hik-mvcamera-control/simple/" \
  --trusted-host "you.github.io"
```

- 版本号 **`0.1.0`** 与 Git 标签 **`v0.1.0`** 对应（无 `v`）。  
- `--trusted-host` 在部分企业网络下必填；若 pip 仍报错，请检查 HTTPS 与防火墙。

**方式 B（不依赖 Pages，直链 wheel）**  
在 **Releases** 中复制对应版本的 `.whl` 下载地址：

```bash
pip install "https://github.com/you/hik-mvcamera-control/releases/download/v0.1.0/hik_code_reader-0.1.0-py3-none-win_amd64.whl"
```

（文件名随版本变化，以 Release 页为准。）

**代码示例**：

```python
from hik_code_reader import HikCodeReader

cr = HikCodeReader()  # wheel 内已带 DLL，一般无需设 HIK_CODE_READER_DLL
print(cr.enum_devices())
```

### Go 开发者

**模块路径**（须与仓库 `go.mod` 一致）：

```text
github.com/you/hik-mvcamera-control/ffi/go
```

**安装指定版本**（与已发布的 **`vX.Y.Z`** / 自动打的 **`ffi/go/vX.Y.Z`** 一致）：

```bash
go get github.com/you/hik-mvcamera-control/ffi/go@v0.1.0
```

**代码中导入**：

```go
import "github.com/you/hik-mvcamera-control/ffi/go/hikcr"
```

**说明**：

- **cgo**：需本机可链接 `hik_code_reader`（Windows 上通常为 MSVC 与 Release 里的 `.lib` / 运行时 `dll`）；**Release** 附件中的 zip 含 `ffi/go` 与 `include/hik_code_reader`，可与同版 `dll`/`lib` 一起用于集成。  
- **私有仓库**：  
  `go env -w GOPRIVATE=github.com/you/*`  
  必要时配置 Git 使用 SSH 或带 token 的 HTTPS。

## GitHub Actions 摘要

| Workflow | 说明 |
|----------|------|
| **CI**（`.github/workflows/ci.yml`） | `pull_request` / 推送到 `main`、`master`：Windows 上构建 DLL、**wheel**，并做 **Go**（`gofmt`、`go mod tidy`、可选 `go build`）校验（不落库、不上传产物）。 |
| **Release**（`.github/workflows/release.yml`） | 推送 **`v*.*.*`**：**GitHub Release** 附件、**gh-pages**（更新 **pip** 用 `simple/` 与根目录 **README 页**）、自动 **`ffi/go/v*`** 标签。 |
| **Pages (README)**（`.github/workflows/pages-readme.yml`） | 推送到 `main`/`master` 且变更 **`README.md`**（或落地页脚本）时：只重部署 **根 `index.html`**，`keep_files` 保留已有 **`simple/`**，与发版解耦。 |

## 仓库结构

| 路径 | 说明 |
|------|------|
| `src/code_reader/` | 读码器 C++ 封装实现（`code_reader.h`、`c_api.cpp` 等） |
| `include/hik_code_reader/` | **C ABI 头文件**（`c_api.h`），供 Python/Go 等包含 |
| `python/` | **`hik-code-reader`** 包与 `pyproject.toml`（wheel 内含 `_native/hik_code_reader.dll`） |
| `ffi/python/` | ctypes 参考实现（与 `python/hik_code_reader` 保持同步为佳） |
| `ffi/go/` | Go 子模块（`go.mod`）；包目录 `hikcr` |
| `include/MvCamera/`、`include/MvCodeReader/` | 海康 SDK 头文件 |
| `lib/MvCamera/{win32,win64}/`、`lib/MvCodeReader/{win32,win64}/` | 预置静态库（含 `turbojpeg` 等读码器依赖） |
| `tests/` | GTest 用例（需连接真实设备时谨慎运行） |
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

产物说明：

- **`hik_code_reader_static.lib`**：静态库，供 `all_tests` 等链接。
- **`hik_code_reader.dll`**：共享库（目标名 `hik_code_reader_shared`），导出 `hik_cr_*` 供 FFI。Python 可将环境变量 **`HIK_CODE_READER_DLL`** 设为该 DLL 的完整路径，或把 DLL 放到进程当前目录 / `PATH`。
- **发版产物**：见上文 **Release / gh-pages**。

## 运行与部署说明

1. 在目标机器安装海康读码器/视觉设备所需 **驱动与运行库**（版本需与 SDK 匹配）。
2. GigE 设备注意网卡、防火墙与网段；改 IP 前须 `openDeviceForParameters` 使设备处于 **Open**，并阅读 `setIp` 注释（改 IP 后设备常重启，本地句柄会被移除）。
3. 将 SDK 提供的 **DLL**（若静态链仍依赖运行时）放在可执行文件同目录或系统 `PATH` 中，按官方文档为准。

## 许可证与第三方

海康威视 **MvCamera / MvCodeReader** SDK 及其文档的版权与许可归原著作权人所有；本仓库中的封装代码请以你方项目许可证为准。GoogleTest、{fmt} 遵循各自开源协议（由 CMake `FetchContent` 获取）。

## 相关文档

- 读码器开发说明可参考仓库内 `.docs` 下文档及 `include/MvCodeReader` 头文件中的 API 定义。
