# hik-mvcamera (Node.js / N-API)

海康工业相机（MvCamera）C API 的 **N-API 原生插件** + JS 封装。链接同一个 `hik_mvcamera.dll`
（C++ 封装 `src/mvcamera/` + C ABI `hik_cv_*`），并通过 **预编译 `.node`**（N-API，跨 Node 版本）
与**海康 MVS 相机运行时全捆绑**，让使用方免装 MSVC、免装 MVS、免联网。

## 前提（仅构建时需要）

- **Windows x64**
- **Node.js ≥ 18**，**npm**
- **MSVC**（VS 2022/2026 Community 或 Build Tools）+ **Python 3.x**（node-gyp 依赖）
- 根 CMake 工程已构建出 `build/Release/hik_mvcamera.dll` / `.lib`

## 构建

```powershell
npm install                     # 拉取 node-addon-api / prebuildify / node-gyp-build
npm run build:native            # 构建根 CMake 工程 → build/Release/（含 hik_mvcamera.dll）
npm run bundle                  # 把 hik_mvcamera.dll + 海康 MVS 相机运行时拷入 _native/
npm run build:addon             # node-gyp rebuild → build/Release/hik_mvcamera.node
npm run prebuild                # prebuildify --napi → prebuilds/win32-x64/
npm test                        # 冒烟测试（无相机也应通过）
```

一键：`npm run build`。

> 使用方**不需要**执行以上任何步骤：预编译 `.node` 与捆绑 DLL 随包分发，加载走
> `node-gyp-build`（纯选文件，不编译）。

## 使用

```js
const { HikCamera, OpenParams } = require('hik-mvcamera');

const cam = new HikCamera();
console.log(cam.enumDevices());   // [{ serialNumber, netExportIp }, ...]；无相机 → []

cam.startDevice(sn, {
  params: new OpenParams({ trigger_mode: 'On', trigger_source: 'Software' }),
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

API 与读码器包 `hik-code-reader`（`ffi/node`）同构：

| 方法 | 说明 |
|------|------|
| `enumDevices()` | 枚举相机（GigE + USB）→ `[{serialNumber, netExportIp}]` |
| `startDevice(sn, {params?, onFrame?, clearFrame?})` | 起流（含起流参数与图像回调登记/清除） |
| `stopDevice(sn)` | 停流（图像回调保留） |
| `triggerDevice(sn)` | 软触发（须处于取流且 TriggerMode=On） |
| `setParam(sn, name, value)` / `getParam(sn, name)` | 按 GenICam 节点名读写任意参数 |
| `lastError()` | 最近一次错误的线程局部信息 |

常量：`HIK_CV_OK`、`HIK_CV_ERR_*`、`HIK_CV_FRAME_KEEP/SET/CLEAR`。

### 图像回调线程

海康 SDK 在**抓图线程**调用图像回调；插件把帧数据同步拷出后经 `napi_threadsafe_function`
**排到 JS 主线程**，再调你的回调 `(serial, frameInfo, buffer)`。`buffer` 是每次新建的 Node Buffer
（原始帧，含 `pixelType` 标识的像素格式；格式转换不在本包范围）。

## 全捆绑说明

`npm run bundle`（`scripts/bundle-native.mjs`）会：

1. 从根 CMake 构建拷 `hik_mvcamera.dll` / `hik_mvcamera.lib`；
2. 定位本机海康 **MVS 相机运行时**（`MvCameraControl.dll` 所在目录，如
   `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64`），**整目录拷贝**到 `_native/`。

因此目标工控机**不需要**安装 MVS，也不需要运行时在 PATH 上——加载时插件会把 `_native/` 前置到
`PATH`（镜像读码器包的 DLL 解析逻辑）。

> 打包分发前请确认海康运行时 DLL 的许可/分发政策符合你方要求。

## 结构

```
ffi/mvcamera-node/
├── package.json / binding.gyp
├── index.js / index.d.ts        # 入口 + TS 类型
├── lib/index.js                 # JS 封装（DLL 路径 + 加载 + HikCamera API）
├── src/addon.cc                 # N-API 插件（图像回调 → Buffer）
├── scripts/bundle-native.mjs    # 全捆绑脚本
├── _native/                     # 捆绑 DLL（构建产物，不入库）
├── prebuilds/                   # 预编译 .node（构建产物，不入库）
└── test/api.test.mjs            # node:test 冒烟测试
```

## 后续（不在本仓库当前范围）

分发接入（npm publish / GitHub Releases / release.yml 集成）、图像格式转换（Bayer→RGB 等）、
相机侧 Python/Go 绑定（C ABI `hik_cv_*` 已就绪，随时可加）。
