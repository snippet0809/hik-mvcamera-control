#!/usr/bin/env bash
# 重算 runtime/MANIFEST.sha256（verify-runtime.sh 的哈希守卫）。
#
# 规则：runtime/ 下**除 MANIFEST.sha256 与 VERSION 之外**的全部文件都进清单。
#   - MANIFEST 不能把自己算进去（自指）；
#   - VERSION 记录来源 commit/日期，会随每次同步变化，算进去只会制造无意义的差异。
# 输出格式与历史一致：`<sha256> *<相对仓库根的路径>`（-b 二进制模式标记）。
#
# 用法（仓库根或任意目录）：bash scripts/update-manifest.sh
set -euo pipefail

cd "$(dirname "$0")/.."

find runtime -type f ! -name MANIFEST.sha256 ! -name VERSION | LC_ALL=C sort | xargs sha256sum -b \
    > runtime/MANIFEST.sha256

echo "[manifest] runtime/MANIFEST.sha256：$(grep -c . runtime/MANIFEST.sha256) 个文件"
