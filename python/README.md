# hik-code-reader (PyPI)

Windows x64 **wheel** 在 `hik_code_reader/_native/` 内含 **`hik_code_reader.dll`**（ctypes）与海康 **`MvCodeReaderCtrl.lib`** / **`turbojpeg.lib`**（再链用）。**海康运行时 `*.dll` 不由公共 CI 打入 wheel**；运行端需安装 MVS/IDMVS Runtime，或由你方专用打包流程随应用下发（须遵守海康许可）。用法见仓库根目录 `README.md`。

本地打 wheel 与 CI「Stage native artifacts」一致：仅拷 `hik_code_reader.dll` 与 `lib/MvCodeReader/win64/*.lib`。

**自检（本机已装海康 Runtime）**：`pip install -e .` 后执行 `python -m hik_code_reader`；短会话联机可设 `HIK_CR_SELFTEST_LIVE=1`。

```python
from hik_code_reader import HikCodeReader
cr = HikCodeReader()
print(cr.enum_devices())
```
