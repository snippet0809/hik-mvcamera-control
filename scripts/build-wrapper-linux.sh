#!/usr/bin/env bash
# 重建 Linux 版 wrapper：runtime/linux-x86_64/lib/libhik_mvcamera.so
#
# 参数与随仓库提交的那份逐字节一致（-O2，不加 -g/-fno-omit-frame-pointer）：
# 2026-11-19 那份 .so 用本命令在 Debian 12 / gcc 12.2 上重编，SHA256 可与提交产物对上。
# 改动编译器或优化级别会让产物不可比对，因此**不要**随手加 -O3：CMake Release 默认就是 -O3，
# 与本命令不是同一份产物。
#
# 用法（Linux 或 Debian 12 容器内，在仓库根执行）：
#   bash scripts/build-wrapper-linux.sh                # 编译并覆盖 runtime/
#   bash scripts/build-wrapper-linux.sh /tmp/out       # 只编译到指定目录，不动 runtime/
#
# 产物的 DT_RPATH 必须是 $ORIGIN（-Wl,--disable-new-dtags 让它落成 DT_RPATH 而非 RUNPATH）：
# wrapper 与厂商 .so 扁放在同一目录，靠它找到邻居；DT_RUNPATH 不被传递依赖继承。
# 编完记得重算 MANIFEST 并跑 verify-runtime.sh（脚本末尾会打印命令）。
set -euo pipefail

cd "$(dirname "$0")/.."

OUT_DIR="${1:-runtime/linux-x86_64/lib}"
mkdir -p "$OUT_DIR"

SRCS=(
    src/mvcamera/c_api.cpp
    src/mvcamera/device_control.cpp
    src/mvcamera/device_info.cpp
    src/mvcamera/image_callback.cpp
    src/mvcamera/image_encode.cpp
    src/mvcamera/net_stats.cpp
    src/mvcamera/param.cpp
)

g++ -std=c++17 -O2 -DNDEBUG -fPIC -fvisibility=hidden -DHIK_CV_COMPILE_DLL -shared \
    -I include -I include/MvCamera -I src/mvcamera \
    "${SRCS[@]}" \
    -o "$OUT_DIR/libhik_mvcamera.so" \
    -L runtime/linux-x86_64/lib -lMvCameraControl \
    -Wl,--disable-new-dtags -Wl,-Bsymbolic -Wl,--no-undefined -Wl,-rpath,'$ORIGIN'

echo "[build-linux] $OUT_DIR/libhik_mvcamera.so"
readelf -d "$OUT_DIR/libhik_mvcamera.so" | grep -E 'RPATH|RUNPATH' || true
nm -D --defined-only "$OUT_DIR/libhik_mvcamera.so" | awk '$2=="T" && $3 ~ /^hik_cv_/{print "  " $3}'

if [ "$OUT_DIR" = "runtime/linux-x86_64/lib" ]; then
    echo "[build-linux] 下一步：bash scripts/update-manifest.sh && bash scripts/verify-runtime.sh"
fi
