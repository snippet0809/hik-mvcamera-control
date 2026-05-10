# hik-code-reader (PyPI)

Windows x64 **wheel** 内含 `hik_code_reader.dll`（包内 `hik_code_reader/_native/`）。用法见仓库根目录 `README.md`。

```python
from hik_code_reader import HikCodeReader
cr = HikCodeReader()
print(cr.enum_devices())
```
