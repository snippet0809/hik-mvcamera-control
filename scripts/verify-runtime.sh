#!/usr/bin/env bash
# 校验 runtime/ 的完整性 —— 哈希 + ABI 三重守卫。
#
# 两道检查：
#   1. MANIFEST.sha256 全量哈希比对（任何文件被换掉/改写都会发现）。
#   2. 「头文件声明的符号集 == 二进制导出的符号集」，两库 × 各平台。
#      这才是真正防住「头/Go 新、DLL 旧」的检查 —— 消费方
#      yusen-cylinder-inspect/desktop/scripts/sync-hik-sdk.ps1 记的正是这个伤
#      （线阵 Height 不生效、读码器枚举 IP 错误）。
#
# 用法：bash scripts/verify-runtime.sh      （在仓库根执行；Git Bash 与 Linux CI 均可）

set -u

cd "$(dirname "$0")/.." || exit 1
fail=0

note() { printf '%s\n' "$*"; }
bad()  { printf 'FAIL: %s\n' "$*"; fail=1; }

# --------------------------------------------------------------------------
# 1. 哈希
# --------------------------------------------------------------------------
if [ ! -f runtime/MANIFEST.sha256 ]; then
    bad "runtime/MANIFEST.sha256 不存在"
elif sha256sum -c runtime/MANIFEST.sha256 --quiet 2>/dev/null; then
    note "OK   哈希：$(grep -c . runtime/MANIFEST.sha256) 个文件全部匹配"
else
    sha256sum -c runtime/MANIFEST.sha256 2>&1 | grep -v ': OK$' | sed 's/^/      /'
    bad "哈希不匹配（上方为差异行）"
fi

# --------------------------------------------------------------------------
# 2. ABI 三重守卫
# --------------------------------------------------------------------------
# 从 C ABI 头抽取「声明了的」函数名（约定前缀 HIK_CR_API / HIK_CV_API）。
declared() { grep -oE "$2_[a-z0-9_]+\(" "$1" 2>/dev/null | tr -d '(' | sort -u; }

# 从共享库抽取「导出了的」函数名。
#   PE  -> objdump -p 的导出表；ELF -> nm -D --defined-only 的 T 符号
exported() {
    case "$1" in
        *.dll)
            command -v objdump >/dev/null 2>&1 || return 1
            objdump -p "$1" 2>/dev/null | awk '/\[ *[0-9]+\]/{print $NF}' ;;
        *.so|*.so.*)
            command -v nm >/dev/null 2>&1 || return 1
            nm -D --defined-only "$1" 2>/dev/null | awk '$2=="T"{print $3}' ;;
        *) return 1 ;;
    esac | grep "^$2_" | sort -u
}

check_abi() { # $1=标签 $2=头 $3=库 $4=符号前缀
    if [ ! -f "$2" ] || [ ! -f "$3" ]; then
        note "SKIP $1（$3 不存在）"
        return
    fi
    local d e
    d=$(declared "$2" "$4")
    e=$(exported "$3" "$4")
    if [ -z "$e" ]; then
        note "SKIP $1（读不到 $3 的导出表，缺 objdump/nm？）"
        return
    fi
    if [ "$d" = "$e" ]; then
        note "OK   $1：头声明 == 导出（$(printf '%s\n' "$d" | grep -c .) 个符号）"
    else
        diff <(printf '%s\n' "$d") <(printf '%s\n' "$e") | sed 's/^/      /'
        bad "$1：头声明与导出符号集不一致（< 仅头有，> 仅库有）"
    fi
}

HDR_CR=runtime/include/hik_code_reader/c_api.h
HDR_CV=runtime/include/hik_mvcamera/c_api.h

# Windows：wrapper 与厂商 DLL 同放 bin/；导入库在 lib/（不参与符号比对）。
if [ -d runtime/windows-x86_64/bin ]; then
    check_abi "windows-x86_64 hik_code_reader" "$HDR_CR" runtime/windows-x86_64/bin/hik_code_reader.dll hik_cr
    check_abi "windows-x86_64 hik_mvcamera"    "$HDR_CV" runtime/windows-x86_64/bin/hik_mvcamera.dll    hik_cv
fi

# Linux：wrapper 是 libhik_*.so，与厂商 .so 一起扁放在 lib/（照厂商布局）。
if [ -d runtime/linux-x86_64/lib ]; then
    check_abi "linux-x86_64 hik_code_reader" "$HDR_CR" runtime/linux-x86_64/lib/libhik_code_reader.so hik_cr
    check_abi "linux-x86_64 hik_mvcamera"    "$HDR_CV" runtime/linux-x86_64/lib/libhik_mvcamera.so    hik_cv
fi

exit $fail
