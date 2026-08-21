# hik-code-reader (Node.js / N-API)

海康读码器（MvCodeReader）C API 的 **N-API 原生插件** + JS 封装。链接同一个 `hik_code_reader.dll`
（与 Python `hik_code_reader` / Go `ffi/go` 共享同一份 DLL），并通过 **预编译 `.node`**（N-API，
跨 Node 版本）与**海康运行时 DLL 全捆绑**，让使用方免装 MSVC、免装 MVS/IDMVS、免联网。

## 前提（仅构建时需要）

- **Windows x64**（与项目其余部分一致）
- **Node.js ≥ 18**，**npm**
- **MSVC**（VS 2022/2026 Community 或 Build Tools，含 C++ 桌面开发工作负载）+ **Python 3.x**（node-gyp 依赖）
- 根 CMake 工程已构建出 `build/Release/hik_code_reader.dll` / `.lib`（`npm run build:native` 会触发）

## 构建

```powershell
npm install                     # 拉取 node-addon-api / prebuildify / node-gyp-build
npm run build:native            # 构建根 CMake 工程 → build/Release/
npm run bundle                  # 把 hik_code_reader.dll + 海康读码器运行时拷入 _native/
npm run build:addon             # node-gyp rebuild → build/Release/hik_code_reader.node
npm run prebuild                # prebuildify --napi → prebuilds/win32-x64/node.napi.node
npm test                        # 冒烟测试（无设备也应通过）
```

一键：`npm run build`（上面 4 步按序执行）。

> 使用方**不需要**执行以上任何步骤：预编译 `.node` 与捆绑 DLL 随包分发，加载走
> `node-gyp-build`（纯选文件，不编译）。

## 使用

```js
const { HikCodeReader, OpenParams } = require('hik-code-reader');

const cr = new HikCodeReader();
console.log(cr.enumDevices());   // [{ serialNumber, netExportIp }, ...]；无设备 → []

cr.startDevice(sn, {
  params: new OpenParams({ trigger_mode: 'On', trigger_source: 'Software', code128: true, qrcode: true }),
  onBcr: (serial, codes) => console.log(`[BCR] ${serial}: ${codes.join(', ')}`),
});
cr.triggerDevice(sn);            // 软触发
// ...
cr.stopDevice(sn);
```

API 与 Python 包 `hik_code_reader` 同构：

| 方法 | 说明 |
|------|------|
| `enumDevices()` | 枚举设备 → `[{serialNumber, netExportIp}]` |
| `startDevice(sn, {params?, onBcr?, clearBcr?})` | 起流（含起流参数与 BCR 登记/清除） |
| `stopDevice(sn)` | 停流（BCR 回调保留） |
| `triggerDevice(sn)` | 软触发（须处于取流） |
| `lastError()` | 最近一次错误的线程局部信息 |

常量：`HIK_CR_OK`、`HIK_CR_ERR_*`、`HIK_CR_BCR_KEEP/SET/CLEAR`。

### BCR 回调线程

海康 SDK 在**抓图线程**调用 C 回调；插件用 `napi_threadsafe_function` 把载荷**排到 JS 主线程**
后再调用你的回调 `(serial, codes[])`，因此回调内可安全使用 Node 主线程 API。

## 全捆绑说明

`npm run bundle`（`scripts/bundle-native.mjs`）会：

1. 从根 CMake 构建拷 `hik_code_reader.dll` / `hik_code_reader.lib`；
2. 定位本机海康读码器运行时（`MvCodeReaderCtrl.dll` 所在目录，含 IDMVS MvSDK），**整目录拷贝**
   到 `_native/`（含 `CodeReaderSdkConfig.ini` 与全部依赖 DLL）。

因此目标工控机**不需要**安装 MVS/IDMVS，也不需要海康运行时在 PATH 上——加载时插件会把
`_native/` 前置到 `PATH`（镜像 Python 包的 DLL 解析逻辑）。

> 打包分发前请确认海康运行时 DLL 的许可/分发政策符合你方要求。

## 结构

```
ffi/node/
├── package.json / binding.gyp
├── index.js / index.d.ts        # 入口 + TS 类型
├── lib/index.js                 # JS 封装（DLL 路径 + 加载 + API）
├── src/addon.cc                 # N-API 插件
├── scripts/bundle-native.mjs    # 全捆绑脚本
├── _native/                     # 捆绑 DLL（构建产物，不入库）
├── prebuilds/                   # 预编译 .node（构建产物，不入库）
└── test/api.test.mjs            # node:test 冒烟测试
```

## 后续（不在本仓库当前范围）

分发接入（npm publish / GitHub Releases / release.yml 集成）、CI 预编译矩阵、32 位支持。
