# hik-code-reader (PyPI)

Windows x64 **wheel** 在 `hik_code_reader/_native/` 内含 **`hik_code_reader.dll`**（运行 ctypes）以及海康 **`MvCodeReaderCtrl.lib`** / **`turbojpeg.lib`**（与仓库 `lib/MvCodeReader/win64` 一致，供本机再链其它原生代码；再分发须遵守海康许可）。用法见仓库根目录 `README.md`。

本地打 wheel 前请把上述 DLL（由 CMake 编出）与 `lib/MvCodeReader/win64/*.lib` 拷入 `hik_code_reader/_native/`，或与 CI 中「Stage native artifacts」步骤一致。

```python
from hik_code_reader import HikCodeReader
cr = HikCodeReader()
print(cr.enum_devices())
```
