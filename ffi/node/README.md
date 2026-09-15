# hik-mvcamera-control (Node.js / N-API)

海康**读码器（MvCodeReader）** 与**工业相机（MvCamera）** 的 **N-API 原生插件** + JS 封装，合并为**一个包**。
链接同一个 `hik_code_reader.dll`（`hik_cr_*` C ABI）与 `hik_mvcamera.dll`（`hik_cv_*` C ABI），
通过 **预编译 `.node`**（N-API，跨 Node 版本）与**随包的两个 wrapper DLL**，
让使用方免装 MSVC、免联网。

**厂商运行时（MVS/IDMVS）不随包分发**，需在目标机安装 —— 与 `runtime/VERSION` 的 Windows 口径、
以及 Go 绑定（`hikcr/hikcr.go`）一致。理由与实测见下方「运行时来源」。

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
npm run bundle                  # 把两个 wrapper DLL/导入库拷进 _native/（厂商运行时不拷，见下）
npm run build:addon             # node-gyp rebuild → build/Release/hik_mvcamera_control.node
npm run prebuild                # prebuildify --napi → prebuilds/win32-x64/hik-mvcamera-control.node
npm test                        # 冒烟测试（无设备也应通过）
```

一键：`npm run build`（上面 4 步按序执行）。

> 使用方**不需要**执行以上任何步骤：预编译 `.node` 与两个 wrapper DLL 随包分发，加载走
> `node-gyp-build`（纯选文件，不编译）。但**目标机必须已安装 MVS/IDMVS**。

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

cr.setParam(sn, 'ExposureTime', 3000);    // 读码器同样支持按 GenICam 节点名读写参数
const et = cr.getParam(sn, 'ExposureTime');

// 取最近一次 BCR 成功帧的图像：与解码结果同一帧，不会错配
const img = cr.getBcrImage(sn);   // { width, height, pixelType, buffer } | null
if (img) {
  // 读码器帧通常已是 JPEG（buffer 可直接写文件或上传）
  require('node:fs').writeFileSync('bcr.jpg', img.buffer);
}

cr.stopGrabbing(sn);             // 仅停流、保留连接（下次 startDevice 更快）
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
    // 直接编码为 JPEG（不落盘），省掉「先写文件再读回」的往返：
    const jpeg = cam.encodeJpeg(sn, info, buffer, 80);
    require('node:fs').writeFileSync('frame.jpg', jpeg);
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
| 读码器 | `HikCodeReader.stopDevice(sn)` / `stopGrabbing(sn)` / `triggerDevice(sn)` | 停流（BCR 回调保留）/ 仅停流保留连接 / 软触发 |
| 读码器 | `HikCodeReader.setParam(sn, name, value)` / `getParam(sn, name)` | 按 GenICam 节点名读写任意参数 |
| 读码器 | `HikCodeReader.getBcrImage(sn)` | 最近一次 BCR 成功帧的图像副本 → `{width, height, pixelType, buffer}`；未读到过条码 → `null` |
| 读码器 | `HikCodeReader.setFrameCallback(sn, {onFrame?, clearFrame?})` | 登记/清除读码成功帧回调（独立于 BCR，可热替换） |
| 读码器 | `HikCodeReader.lastError()` | 最近一次错误的线程局部信息 |
| 相机 | `HikCamera.enumDevices()` | 枚举相机（GigE + USB）→ `[{serialNumber, netExportIp, modelName}]` |
| 相机 | `HikCamera.startDevice(sn, {params?, onFrame?, clearFrame?})` | 起流（含起流参数与图像回调登记/清除） |
| 相机 | `HikCamera.stopDevice(sn)` / `triggerDevice(sn)` | 停流（图像回调保留）/ 软触发 |
| 相机 | `HikCamera.setParam(sn, name, value)` / `getParam(sn, name)` | 按 GenICam 节点名读写任意参数 |
| 相机 | `HikCamera.encodeJpeg(sn, frameInfo, frameBuffer, quality?, method?)` | 把 `onFrame` 的原始帧编码为 JPEG `Buffer`（不落盘） |
| 相机 | `HikCamera.forceIp(sn, ip, subnetMask?, gateway?)` | 临时强制 GigE 相机 IP（重启恢复） |
| 相机 | `HikCamera.lastError()` | 最近一次错误的线程局部信息 |

参数类：`ReaderOpenParams`（trigger_mode/trigger_source/code128/qrcode）、
`CameraOpenParams`（trigger_mode/trigger_source/net_trans_mode/width/height）。

常量：`HIK_CR_OK`、`HIK_CR_ERR_*`、`HIK_CR_BCR_KEEP/SET/CLEAR`、`HIK_CR_FRAME_KEEP/SET/CLEAR`、
`HIK_CR_PARAM_STRING_MAX`、`HIK_CV_OK`、`HIK_CV_ERR_*`、`HIK_CV_FRAME_KEEP/SET/CLEAR`。

### 读码器图像为什么用「拉」而不是「推」

读码器的图像有两条获取路径，按需选择：

- **`getBcrImage(sn)`（拉，推荐）**：读码器**解码成功那一帧**的图像被常驻在 C++ 层，读码成功后再拉取。
  图与码来自同一帧，**不会错配**，适合「先拿码、再决定要不要存图」的流程。
- **`setFrameCallback`（推）**：每次读码成功都回调一帧，适合要连续留图/实时预览的场景；
  数据在 SDK 抓图线程产生，插件已同步拷出后再排到 JS 主线程。

> 读码器帧通常是 **JPEG**（`pixelType` 为 `Gvsp_Jpeg`），`getBcrImage` 的 `buffer` 可直接当 JPEG 字节用。
> 相机则相反：`onFrame` 给的是**原始像素**（Bayer/Mono），需要 `encodeJpeg` 转成 JPEG。

**⚠️ 前提：`getBcrImage` 要求起流时登记过 `onBcr` 或帧回调。** 海康 SDK 的图像回调只有在
「BCR 回调 或 读码成功帧回调」至少登记一个时才会绑定到本库的桥函数；两者都没登记时桥函数被解绑，
读码帧图不会被留档，`getBcrImage` 将**始终返回 `null`**。

```js
// 正确：登记 onBcr（产线通常本来就需要码），再按需 getBcrImage
cr.startDevice(sn, { onBcr: (serial, codes) => { /* 拿到 codes */ } });
cr.triggerDevice(sn);
const img = cr.getBcrImage(sn);   // 解码成功后才有值；此时返回 { width, height, pixelType, buffer }

// 错误：什么都没登记 —— img 永远是 null
cr.startDevice(sn);
cr.triggerDevice(sn);
const img2 = cr.getBcrImage(sn);  // 始终 null
```

### 「软触发 → 等新一帧」惯用法

相机图像是**推**的（`onFrame` 每帧都回调），而产线流程通常需要「触发一次、拿到这一帧就返回」。
本包不提供阻塞式取帧接口，用帧号比对 + 超时在 JS 侧组装即可（`frameNum` 单调递增）：

```js
/** 软触发一次并等待一幅新帧（含 JPEG），帧号不推进则超时。 */
function triggerAndGrabJpeg(cam, sn, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    let lastFrameNum = -1;
    const timer = setTimeout(() => {
      cam.startDevice(sn, { clearFrame: true });   // 摘掉回调，避免泄漏
      reject(new Error(`triggerAndGrabJpeg: ${sn} 等待新帧超时（${timeoutMs}ms）`));
    }, timeoutMs);

    cam.startDevice(sn, {
      onFrame: (_serial, info, buffer) => {
        if (info.frameNum === lastFrameNum) return;  // 同一帧的重发，忽略
        lastFrameNum = info.frameNum;
        clearTimeout(timer);
        cam.startDevice(sn, { clearFrame: true });
        try {
          resolve({ info, jpeg: cam.encodeJpeg(sn, info, buffer, 80) });
        } catch (err) {
          reject(err);
        }
      },
    });
    cam.triggerDevice(sn);
  });
}
```

> 必须在 `onFrame` 里**同步**调用 `encodeJpeg`：传给回调的 `buffer` 是插件从 SDK 抓图线程
> 拷出的独立副本，可以安全持有；但 `info` 描述的是那一刻的帧，晚拿到的帧不会改变它。
> 若同一序列号要连续触发多轮，注意 `startDevice` 的 `clearFrame` 与重新登记之间存在窗口，
> 高频场景下更稳妥的做法是**登记一次回调**、用队列把帧分发给各轮等待者。

> **改名说明**：原独立的 `hik-code-reader` / `hik-mvcamera` 两个包合并为 `hik-mvcamera-control`；
> 原各自的 `OpenParams` 相应改名为 `ReaderOpenParams` / `CameraOpenParams`。

### 回调线程

海康 SDK 在**抓图线程**调用 C 回调；插件把载荷**排到 JS 主线程**后再调用你的回调
（BCR：`(serial, codes[])`；图像：`(serial, frameInfo, buffer)`，`buffer` 为每次新建的 Node Buffer），
因此回调内可安全使用 Node 主线程 API。`napi_threadsafe_function` 已 `Unref`，回调不阻止进程退出。

### 参数持久性（相机断电/重启）

海康相机的参数（`ExposureTime`、`Gain` 等）是**易失的**：存于相机内存，**断电即恢复出厂默认**。
`setParam` 的值在**不断电**的前提下跨 `stopDevice`/`startDevice`、跨进程重启都保持；但**相机一断电就丢**。

因此若应用要求相机重启后参数仍为期望值，**每次 `startDevice` 后重新 `setParam` 即可**（实测可用）：

```js
// 把要恢复的参数封装成一个函数，每次 startDevice 后调用
function applyParams(sn) {
  cam.setParam(sn, 'ExposureTime', 6000);
  cam.setParam(sn, 'Gain', 3);
  // ... 其它期望值
}

cam.startDevice(sn, { params: new CameraOpenParams({ trigger_mode: 'Off' }), onFrame });
applyParams(sn);   // ← 关键：每次启动后重设
```

注意：
- `startDevice` 的 `CameraOpenParams` 只在**进入取流那一刻**应用（trigger_mode/trigger_source/net_trans_mode），且**已在取流时会被忽略**；其它参数一律用 `setParam` 单独设置。
- 若开启了自动曝光/自动增益，相机自身会覆盖手动 `setParam` 的值，属正常行为。
- 需要"一次保存、每次上电自动恢复"时可考虑相机侧 **UserSet 持久化**（`UserSetSave` 命令节点），但当前包尚未暴露命令节点执行 API（后续可加 `runCommand`）。

## 运行时来源

### 默认：不随包分发厂商运行时

`npm run bundle`（`scripts/bundle-native.mjs`）**只**做一件事：把根 CMake 构建的
`hik_code_reader.dll/.lib` 与 `hik_mvcamera.dll/.lib` 拷进 `_native/`（共约 480KB）。

厂商运行时（`MvCameraControl.dll` / `MvCodeReaderCtrl.dll` 及其依赖）**由目标机安装 MVS/IDMVS 提供**，
安装器写机器级 PATH，wrapper 的 PE 导入表在那里解析。加载时插件把 `_native/` 前置到 `PATH`
（镜像 Python 包的 DLL 解析逻辑）。

这与 `runtime/VERSION` 的 Windows 口径、以及 Go 绑定（`hikcr/hikcr.go` 的注释「厂商读码器运行时
由使用方自行安装 IDMVS 提供」）一致。

**实测（2026-09-15）**：`_native/` 只留两个 wrapper 时，枚举与取流均正常，进程加载的厂商模块
来自 `C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64\` 与
`D:\IDMVS\Applications\Win64\plugins\mvsidcamctrl\`。

### 可选：离线全捆绑（`HIK_BUNDLE_VENDOR_RUNTIME=1`）

确有「工控机免装 MVS/IDMVS、免联网」需求时，设该环境变量重跑 `npm run bundle`，
脚本会整目录合并两套运行时（相机 MVS 先拷胜出，读码器后拷补齐）：
共享同名 DLL 以 MVS 版本为准——Windows 进程内只加载一份 `MvCameraControl.dll`，
读码器 SDK 本就依赖 MVS 基座；去重按完整相对路径（`ThirdParty/` 子目录保留）。

> ⚠️ 代价：`_native/` 会涨到约 **176MB**，tarball 约 **72.5MB**（实测），
> 且该体积会随 tarball 提交进使用方仓库。默认关闭正是为了避免这件事。
>
> ⚠️ 另注意：不装 MVS 就没有 GigE 过滤驱动，取流须走 socket 模式
> （`CameraOpenParams.net_trans_mode = 2`），该模式**尚未在真机验证过**。
>
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
├── scripts/bundle-native.mjs    # 拷 wrapper DLL 进 _native/（厂商运行时默认不拷）
├── _native/                     # wrapper DLL/导入库（构建产物，不入库）
├── prebuilds/                   # 预编译 .node（构建产物，不入库）
└── test/api.test.mjs            # node:test 冒烟测试
```

## 后续（不在本仓库当前范围）

分发接入（npm publish / GitHub Releases / release.yml 集成）、CI 预编译矩阵、32 位支持、图像格式转换（Bayer→RGB 等）。
