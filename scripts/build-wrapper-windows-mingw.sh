#!/usr/bin/env bash
# 重建 Windows 版 wrapper：runtime/windows-x86_64/bin/hik_mvcamera.dll + lib/hik_mvcamera.lib
#
# 用 MinGW-w64 **交叉编译**，因此不需要 Windows/Visual Studio；这是本仓库维护者没有
# Windows 机器时唯一能保证「头 == 二进制」的路径（verify-runtime.sh 的 ABI 守卫按
# 头文件声明集的符号比对，加了新 API 就必须重建 DLL，否则 Windows 侧校验直接失败）。
#
# 与 MSVC 产物的差别只在编译器与静态运行库（MinGW 静态链入 libgcc/libstdc++/winpthread，
# 因此除了 KERNEL32/msvcrt 与厂商 MVCameraControl.dll 之外没有额外依赖）；
# 导出的 C ABI 完全一致。**首次提交时仍应在真实 Windows 上跑一次相机用例**。
#
# 依赖：mingw-w64 的 **posix 线程模型**（Debian：update-alternatives 选 *-posix；
#       MSYS2：x86_64-w64-mingw32-g++ 默认即 posix）。win32 模型没有 std::mutex，
#       编译会以 "std::mutex is not a member of std" 报错。
#       导入库优选 llvm-dlltool（产出 MS COFF 格式，MSVC 与 MinGW 都能链）；
#       没有 llvm-dlltool 时退回 dlltool。
#
# 用法（在仓库根执行）：
#   bash scripts/build-wrapper-windows-mingw.sh              # 编译并覆盖 runtime/
#   bash scripts/build-wrapper-windows-mingw.sh /tmp/winout  # 只编译到指定目录
set -euo pipefail

cd "$(dirname "$0")/.."

OUT_DIR="${1:-runtime/windows-x86_64/bin}"
mkdir -p "$OUT_DIR"

CXX="${CXX:-x86_64-w64-mingw32-g++}"
command -v "$CXX" >/dev/null 2>&1 || {
    echo "[build-win] 找不到 $CXX（apt install g++-mingw-w64-x86-64）" >&2
    exit 1
}
case "$("$CXX" --version | head -1)" in
    *posix*) ;;
    *) echo "[build-win] 警告：$CXX 不是 posix 线程模型，std::mutex 会编译失败" >&2 ;;
esac

SRCS=(
    src/mvcamera/c_api.cpp
    src/mvcamera/device_control.cpp
    src/mvcamera/device_info.cpp
    src/mvcamera/image_callback.cpp
    src/mvcamera/image_encode.cpp
    src/mvcamera/net_stats.cpp
    src/mvcamera/param.cpp
)

# WIN32 必须定义：海康 PixelType.h 在非 WIN32 下把枚举当 8 字节 long long，
# MV_FRAME_OUT_INFO_EX 之后所有字段偏移整体错位（见 CMakeLists 里的长注释）。
# __int64 是 MSVC 专有关键字，MinGW 的 gcc 不认，用等价的 long long 替代。
"$CXX" -std=c++17 -O2 -DNDEBUG \
    -DWIN32 -D__int64="long long" -DHIK_CV_COMPILE_DLL \
    -I include -I include/MvCamera -I src/mvcamera \
    "${SRCS[@]}" \
    -shared -o "$OUT_DIR/hik_mvcamera.dll" \
    lib/MvCamera/win64/MvCameraControl.lib \
    -static-libgcc -static-libstdc++ -static -Wl,--no-undefined

# 导出 → .def → 导入库。名字直接从 DLL 的导出表取，避免与头文件手工对不上。
{
    echo "LIBRARY hik_mvcamera.dll"
    echo "EXPORTS"
    x86_64-w64-mingw32-objdump -p "$OUT_DIR/hik_mvcamera.dll" \
        | grep -oE "hik_cv_[a-z0-9_]+" | LC_ALL=C sort -u | sed 's/^/  /'
} > "$OUT_DIR/hik_mvcamera.def"

if command -v llvm-dlltool >/dev/null 2>&1; then
    llvm-dlltool -m i386:x86-64 -d "$OUT_DIR/hik_mvcamera.def" -l "$OUT_DIR/hik_mvcamera.lib"
else
    x86_64-w64-mingw32-dlltool -m i386:x86-64 -d "$OUT_DIR/hik_mvcamera.def" -l "$OUT_DIR/hik_mvcamera.lib"
fi

# 布局与 runtime/ 保持一致：dll 在 bin/、lib 在 lib/
if [ "$OUT_DIR" = "runtime/windows-x86_64/bin" ]; then
    mv "$OUT_DIR/hik_mvcamera.lib" runtime/windows-x86_64/lib/hik_mvcamera.lib
    rm -f "$OUT_DIR/hik_mvcamera.def"
fi

echo "[build-win] $OUT_DIR/hik_mvcamera.dll"
x86_64-w64-mingw32-objdump -p "$OUT_DIR/hik_mvcamera.dll" | grep "DLL Name" | LC_ALL=C sort -u
x86_64-w64-mingw32-objdump -p "$OUT_DIR/hik_mvcamera.dll" | grep -oE "hik_cv_[a-z0-9_]+" | LC_ALL=C sort -u | sed 's/^/  /'

if [ "$OUT_DIR" = "runtime/windows-x86_64/bin" ]; then
    echo "[build-win] 下一步：bash scripts/update-manifest.sh && bash scripts/verify-runtime.sh"
fi
