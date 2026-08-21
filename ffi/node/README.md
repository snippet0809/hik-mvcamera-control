# hik-mvcamera-control (Node.js / N-API)

海康**读码器（MvCodeReader）** 与**工业相机（MvCamera）** 的 **N-API 原生插件** + JS 封装，合并为**一个包**。
链接同一个 `hik_code_reader.dll`（`hik_cr_*` C ABI）与 `hik_mvcamera.dll`（`hik_cv_*` C ABI），
通过 **预编译 `.node`**（N-API，跨 Node 版本）与**海康读码器/相机运行时全捆绑**，
让使用方免装 MSVC、免装 MVS/IDMVS、免联网。

```js
const { HikCodeReader, HikCamera } = require('hik-mvcamera-control');
```

## 前提（仅构建时需要）

- **Windows x64**（与项目其余部分一致）
- **Node.js ≥ 18**，**npm**
- **MSVC**（VS 2022/2026 Community 或 Build Tools，含 C++ 桌面开发工作负载）+ **Python 3.x**（node-gyp 依赖）
- 根 CMake 工程已构建出 `build/Release/hik_code_reader.dll` / `.lib` 与 `hik_mvcamera.dll` / `.lib`（`npm run build:native` 会触发）

## 构建

```powershell
npm install                     # 拉取 node-addon-api / prebuildify / node-gyp-build
npm run build:native            # 构建根 CMake 工程 → build/Release/（两个 DLL）
npm run bundle                  # 合并捆绑两个 DLL + 读码器/相机运行时到 _native/
npm run build:addon             # node-gyp rebuild → build/Release/hik_mvcamera_control.node
npm run prebuild                # prebuildify --napi → prebuilds/win32-x64/hik-mvcamera-control.node
npm test                        # 冒烟测试（无设备也应通过）
```

一键：`npm run build`（上面 4 步按序执行）。

> 使用方**不需要**执行以上任何步骤：预编译 `.node` 与捆绑 DLL 随包分发，加载走
> `node-gyp-build`（纯选文件，不编译）。

## 使用

### 读码器

```js
const { HikCodeReader, ReaderOpenParams } = require('hik-mvcamera-control');

const cr = new HikCodeReader();
console.log(cr.enumDevices());   // [{ serialNumber, netExportIp }, ...]；无设备 → []

cr.startDevice(sn, {
  params: new ReaderOpenParams({ trigger_mode: 'On', trigger_source: 'Software', code128: true, qrcode: true }),
  onBcr: (serial, codes) => console.log(`[BCR] ${serial}: ${codes.join(', ')}`),
});
cr.triggerDevice(sn);            // 软触发
// ...
cr.stopDevice(sn);
```

### 相机

```js
const { HikCamera, CameraOpenParams } = require('hik-mvcamera-control');

const cam = new HikCamera();
console.log(cam.enumDevices());   // [{ serialNumber, netExportIp, modelName }, ...]；无相机 → []

cam.startDevice(sn, {
  params: new CameraOpenParams({ trigger_mode: 'On', trigger_source: 'Software' }),
  onFrame: (serial, info, buffer) => {
    console.log(`[frame] ${info.width}x${info.height} len=${info.frameLen} pixelType=${info.pixelType}`);
    // buffer：Node Buffer，含原始图像数据
  },
});
cam.triggerDevice(sn);            // 软触发（须 TriggerMode=On）
cam.setParam(sn, 'ExposureTime', 1000);   // 通用按名写参数（Int/Float/Bool/枚举 symbolic/字符串）
const et = cam.getParam(sn, 'ExposureTime');
// ...
cam.stopDevice(sn);
```

## API

| 模块 | 类 / 类方法 | 说明 |
|------|------|------|
| 读码器 | `HikCodeReader.enumDevices()` | 枚举设备 → `[{serialNumber, netExportIp}]` |
| 读码器 | `HikCodeReader.startDevice(sn, {params?, onBcr?, clearBcr?})` | 起流（含起流参数与 BCR 登记/清除） |
| 读码器 | `HikCodeReader.stopDevice(sn)` / `triggerDevice(sn)` | 停流（BCR 回调保留）/ 软触发 |
| 读码器 | `HikCodeReader.lastError()` | 最近一次错误的线程局部信息 |
| 相机 | `HikCamera.enumDevices()` | 枚举相机（GigE + USB）→ `[{serialNumber, netExportIp, modelName}]` |
| 相机 | `HikCamera.startDevice(sn, {params?, onFrame?, clearFrame?})` | 起流（含起流参数与图像回调登记/清除） |
| 相机 | `HikCamera.stopDevice(sn)` / `triggerDevice(sn)` | 停流（图像回调保留）/ 软触发 |
| 相机 | `HikCamera.setParam(sn, name, value)` / `getParam(sn, name)` | 按 GenICam 节点名读写任意参数 |
| 相机 | `HikCamera.forceIp(sn, ip, subnetMask?, gateway?)` | 临时强制 GigE 相机 IP（重启恢复） |
| 相机 | `HikCamera.lastError()` | 最近一次错误的线程局部信息 |

参数类：`ReaderOpenParams`（trigger_mode/trigger_source/code128/qrcode）、
`CameraOpenParams`（trigger_mode/trigger_source/net_trans_mode）。

常量：`HIK_CR_OK`、`HIK_CR_ERR_*`、`HIK_CR_BCR_KEEP/SET/CLEAR`、`HIK_CV_OK`、`HIK_CV_ERR_*`、`HIK_CV_FRAME_KEEP/SET/CLEAR`。

> **改名说明**：原独立的 `hik-code-reader` / `hik-mvcamera` 两个包合并为 `hik-mvcamera-control`；
> 原各自的 `OpenParams` 相应改名为 `ReaderOpenParams` / `CameraOpenParams`。

### 回调线程

海康 SDK 在**抓图线程**调用 C 回调；插件把载荷**排到 JS 主线程**后再调用你的回调
（BCR：`(serial, codes[])`；图像：`(serial, frameInfo, buffer)`，`buffer` 为每次新建的 Node Buffer），
因此回调内可安全使用 Node 主线程 API。`napi_threadsafe_function` 已 `Unref`，回调不阻止进程退出。

## 全捆绑说明

`npm run bundle`（`scripts/bundle-native.mjs`）会：

1. 从根 CMake 构建拷 `hik_code_reader.dll/.lib` 与 `hik_mvcamera.dll/.lib`；
2. 定位本机海康 **MVS 相机运行时**（`MvCameraControl.dll` 所在目录）整目录拷入（胜出）；
3. 定位本机海康 **读码器运行时**（`MvCodeReaderCtrl.dll` 所在目录）补齐拷入（跳过已存在文件）。

共享同名 DLL（`MvCameraControl.dll`、GenICam、CL-serial 等）以**较新的 MVS 版本为准**——
Windows 进程内只加载一份，读码器 SDK 本就依赖 MVS 基座。去重按完整相对路径（`ThirdParty/` 子目录保留）。

因此目标工控机**不需要**安装 MVS/IDMVS，也不需要运行时在 PATH 上——加载时插件会把
`_native/` 前置到 `PATH`（镜像 Python 包的 DLL 解析逻辑）。

> 打包分发前请确认海康运行时 DLL 的许可/分发政策符合你方要求。

## 结构

```
ffi/node/
├── package.json / binding.gyp
├── index.js / index.d.ts        # 入口 + TS 类型
├── lib/index.js                 # JS 封装（DLL 路径 + 加载 + 读码器/相机 API）
├── src/addon.cc                 # N-API 插件入口（唯一 NODE_API_MODULE，注册 reader + camera）
├── src/reader_addon.cc          # 读码器部分（BCR 回调桥 + HIK_CR_*）
├── src/camera_addon.cc          # 相机部分（图像回调桥 + HIK_CV_*）
├── scripts/bundle-native.mjs    # 全捆绑脚本（合并去重）
├── _native/                     # 捆绑 DLL（构建产物，不入库）
├── prebuilds/                   # 预编译 .node（构建产物，不入库）
└── test/api.test.mjs            # node:test 冒烟测试
```

## 后续（不在本仓库当前范围）

分发接入（npm publish / GitHub Releases / release.yml 集成）、CI 预编译矩阵、32 位支持、图像格式转换（Bayer→RGB 等）。
