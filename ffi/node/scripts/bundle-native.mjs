/**
 * @file bundle-native.mjs
 * @brief 把运行所需 DLL 拷入 `ffi/node/_native/`，实现"全捆绑"（工控机免装 MVS/IDMVS、免联网）。
 *
 * 步骤：
 *   1. 从根 CMake 构建产物拷 `hik_code_reader.dll` / `hik_code_reader.lib`；
 *   2. 定位海康读码器运行时目录（含 `MvCodeReaderCtrl.dll`），整目录拷贝（含 CodeReaderSdkConfig.ini 与全部依赖 DLL）。
 *
 * 用法：`npm run bundle`（或 `node scripts/bundle-native.mjs`）。
 */

import { cpSync, existsSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const pkgRoot = resolve(here, '..');
const nativeDir = join(pkgRoot, '_native');
const repoRoot = resolve(pkgRoot, '../..');

mkdirSync(nativeDir, { recursive: true });

// ---------------------------------------------------------------------------
// 1) hik_code_reader.dll / .lib（来自根 CMake 构建）
// ---------------------------------------------------------------------------

const releaseDir = join(repoRoot, 'build', 'Release');
const fromCmake = ['hik_code_reader.dll', 'hik_code_reader.lib'];
const copiedFromCmake = [];
for (const f of fromCmake) {
  const src = join(releaseDir, f);
  if (!existsSync(src)) {
    console.warn(`[bundle] 缺少 CMake 产物：${src}（请先 npm run build:native）`);
    continue;
  }
  cpSync(src, join(nativeDir, f), { force: true });
  copiedFromCmake.push(f);
}

// ---------------------------------------------------------------------------
// 2) 海康读码器运行时目录（含 MvCodeReaderCtrl.dll）—— 整目录拷贝
// ---------------------------------------------------------------------------

/** 已知/显式路径（按优先级）。 */
const RUNTIME_SEARCH_ROOTS = [
  'D:\\IDMVS\\Applications\\Win64\\plugins\\MvSDK',
  'C:\\Program Files\\MVS',
  'C:\\Program Files (x86)\\MVS',
  'C:\\Program Files\\IDMVS',
  'C:\\Program Files (x86)\\IDMVS',
  'C:\\Program Files\\Hikrobot',
  'C:\\Program Files (x86)\\Hikrobot',
  'C:\\Program Files\\MvVision',
  'C:\\Program Files (x86)\\MvVision',
  'C:\\Program Files\\Common Files\\MVS',
  'C:\\Program Files (x86)\\Common Files\\MVS',
];

function walk(root, onFile) {
  if (!existsSync(root)) return;
  for (const entry of readdirSync(root)) {
    const p = join(root, entry);
    let st;
    try {
      st = statSync(p);
    } catch {
      continue;
    }
    if (st.isDirectory()) {
      walk(p, onFile);
    } else if (st.isFile()) {
      onFile(p);
    }
  }
}

/** 在候选根下找含 MvCodeReaderCtrl.dll 的目录（含 where.exe 探测）。 */
function findRuntimeDirs() {
  const found = [];
  const push = (d) => {
    if (!d || !existsSync(d)) return;
    const r = resolve(d);
    if (!found.includes(r)) found.push(r);
  };

  if (process.platform === 'win32') {
    const r = spawnSync('where.exe', ['MvCodeReaderCtrl.dll'], { encoding: 'utf8', windowsHide: true });
    if (r.status === 0 && r.stdout) {
      for (const line of r.stdout.split(/\r?\n/)) {
        const p = line.trim();
        if (p) push(dirname(p));
      }
    }
  }

  for (const root of RUNTIME_SEARCH_ROOTS) {
    try {
      walk(root, (f) => {
        if (f.toLowerCase().endsWith('\\mvcodereaderctrl.dll')) push(dirname(f));
      });
    } catch {
      /* 不可访问的根忽略 */
    }
  }
  return found;
}

const runtimeDirs = findRuntimeDirs();
if (runtimeDirs.length === 0) {
  console.warn('[bundle] 未找到海康读码器运行时（MvCodeReaderCtrl.dll）。');
  console.warn('[bundle] 请安装 IDMVS/MVS，或把运行时目录放入 _native/ 后重试。');
  console.warn('[bundle] 已捆绑：' + (copiedFromCmake.join(', ') || '（无）'));
  process.exit(0);
}

// 取第一个命中目录整拷；跳过等于 _native/ 自身的目录（避免自拷）
const runtimeDir = runtimeDirs[0];
if (resolve(runtimeDir).toLowerCase() !== resolve(nativeDir).toLowerCase()) {
  cpSync(runtimeDir, nativeDir, { recursive: true, force: true });
  console.log(`[bundle] 海康读码器运行时 <- ${runtimeDir}`);
}

// ---------------------------------------------------------------------------
// 汇总
// ---------------------------------------------------------------------------

const dllCount = readdirSync(nativeDir).filter((f) => f.toLowerCase().endsWith('.dll')).length;
const missing = ['hik_code_reader.dll', 'MvCodeReaderCtrl.dll'].filter(
  (f) => !existsSync(join(nativeDir, f)),
);
console.log(`[bundle] _native/ 现有 ${dllCount} 个 DLL。`);
if (missing.length === 0) {
  console.log('[bundle] 捆绑完整：hik_code_reader.dll + 海康读码器运行时均已就位。');
} else {
  console.warn('[bundle] 仍缺失：' + missing.join(', '));
}
