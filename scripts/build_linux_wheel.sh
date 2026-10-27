#!/usr/bin/env bash
# 在 Linux x86_64 上从源码构建 hik-mvcamera-control 的 Python wheel。
#
# 前置（Debian 12）：
#   sudo apt-get install -y build-essential cmake ninja-build python3-venv python3-pip
#
# 产出：dist/hik_mvcamera_control-<版本>-py3-none-linux_x86_64.whl
#
# 与 Windows 发版的差别：Windows 用 `python -m build`（setup.cfg 的 plat_name=win_amd64）；
# 这里走 scripts/assemble_linux_wheel.py 自己组装，因为同一个 setup.cfg 没法产出 linux 标签。
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$root/build-linux}"
out="${OUT_DIR:-$root/dist}"

if command -v ninja >/dev/null 2>&1; then
  gen=(-G Ninja)
else
  gen=()
fi

echo "==> 配置 & 编译 wrapper（$build）"
cmake -S "$root" -B "$build" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target hik_code_reader_shared hik_mvcamera_shared -j "$(nproc)"

# 断言：链接期必须落地 DT_RPATH=$ORIGIN（DT_RUNPATH 不被传递依赖继承，管不到
# wrapper → 厂商 .so 那一跳，会表现为 import 时 libMvCameraControl.so 找不到）。
echo "==> 校验 RPATH / 导出符号"
for f in "$build/libhik_code_reader.so" "$build/libhik_mvcamera.so"; do
  readelf -d "$f" | grep -qE 'RPATH.*\$ORIGIN' || {
    echo "错误：$f 缺少 DT_RPATH=\$ORIGIN（检查 CMake 的 --disable-new-dtags / INSTALL_RPATH）" >&2
    exit 1
  }
done
nm -D --defined-only "$build/libhik_code_reader.so" | grep -q ' T hik_cr_enum_devices'
nm -D --defined-only "$build/libhik_mvcamera.so" | grep -q ' T hik_cv_enum_devices'

echo "==> 组装 wheel"
python3 "$root/scripts/assemble_linux_wheel.py" --built-dir "$build" --out "$out"

echo "==> 冒烟：把 wheel 装进临时 venv，import 并跑加载诊断"
tmp="$(mktemp -d)"
python3 -m venv "$tmp/venv"
"$tmp/venv/bin/pip" install --quiet --no-index --find-links "$out" hik-mvcamera-control
"$tmp/venv/bin/python" - <<'PY'
import hik_mvcamera_control as m

ctx = m.diagnose_runtime_search_context()
print("platform      :", ctx["platform"])
print("native_lib    :", ctx["native_lib"])
print("bundled_exists:", ctx["bundled_exists"])
assert ctx["bundled_exists"], "wheel 里缺少 _native/*.so"
assert ctx["native_lib"].endswith(".so"), ctx["native_lib"]
pre = ctx["preloaded"]
bad = [x for x in pre if not x["ok"]]
print(f"preloaded     : {len(pre) - len(bad)}/{len(pre)} 成功")
for x in bad:
    print("  FAILED", x["name"], x["error"])
print("（无相机/读码器时枚举失败是预期的；到这里说明 _native 与 dlopen 链路正常）")
PY
rm -rf "$tmp"

echo "==> 完成：$out"
