# hik-mvcamera-control (Python)

海康 MVS **相机 + 读码器** 统一 Python 封装。wheel 的 `_native` 内含：

- `hik_code_reader.dll` — 读码器（`HikCodeReader`）
- `hik_mvcamera.dll` — 工业相机（`HikCamera`）
- `*.lib` — 海康 SDK 导入库（供在本机链其它原生代码）

运行时依赖海康 MVS 安装（`MvCodeReaderCtrl.dll` / `MvCameraControl.dll`）；加载前自动探测并预置搜索路径。

```python
from hik_mvcamera_control import HikCamera, HikCodeReader

cam = HikCamera()
for d in cam.enum_devices():
    print(d.serial_number, d.model_name, d.net_export_ip)

cr = HikCodeReader()
print(cr.enum_devices())
```

诊断：

```python
from hik_mvcamera_control import diagnose_camera_runtime_context, diagnose_camera_windows_native_load
print(diagnose_camera_runtime_context())
print(diagnose_camera_windows_native_load())
```

环境变量覆盖：`HIK_MVCAMERA_DLL` / `HIK_CODE_READER_DLL` 指定各自 DLL 路径。

## 本地开发安装

```bash
cd python/hik_mvcamera_control
pip install .
```

需先把 `build/Release/hik_mvcamera.dll`（与 `hik_code_reader.dll`）拷入 `hik_mvcamera_control/_native/`。
