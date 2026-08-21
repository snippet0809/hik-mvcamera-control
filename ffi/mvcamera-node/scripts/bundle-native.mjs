/**
 * @file bundle-native.mjs
 * @brief 把运行所需 DLL 拷入 `ffi/mvcamera-node/_native/`，实现"全捆绑"（工控机免装 MVS、免联网）。
 *
 * 步骤：
 *   1. 从根 CMake 构建产物拷 `hik_mvcamera.dll` / `hik_mvcamera.lib`；
 *   2. 定位海康 MVS 相机运行时目录（含 `MvCameraControl.dll`），整目录拷贝。
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
// 1) hik_mvcamera.dll / .lib（来自根 CMake 构建）
// ---------------------------------------------------------------------------

const releaseDir = join(repoRoot, 'build', 'Release');
const fromCmake = ['hik_mvcamera.dll', 'hik_mvcamera.lib'];
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
// 2) 海康 MVS 相机运行时目录（含 MvCameraControl.dll）—— 整目录拷贝
// ---------------------------------------------------------------------------

const RUNTIME_SEARCH_ROOTS = [
  'C:\\Program Files (x86)\\Common Files\\MVS\\Runtime\\Win64_x64',
  'C:\\Program Files (x86)\\Common Files\\MVS',
  'C:\\Program Files\\Common Files\\MVS',
  'C:\\Program Files\\MVS',
  'C:\\Program Files (x86)\\MVS',
  'C:\\Program Files\\Hikrobot',
  'C:\\Program Files (x86)\\Hikrobot',
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

function findRuntimeDirs() {
  const found = [];
  const push = (d) => {
    if (!d || !existsSync(d)) return;
    const r = resolve(d);
    if (!found.includes(r)) found.push(r);
  };

  if (process.platform === 'win32') {
    const r = spawnSync('where.exe', ['MvCameraControl.dll'], { encoding: 'utf8', windowsHide: true });
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
        if (f.toLowerCase().endsWith('\\mvcameracontrol.dll')) push(dirname(f));
      });
    } catch {
      /* ignore */
    }
  }

  // 优先 64 位运行时（PATH 里可能同时有 Win32_i86 / Win64_x64，必须选 x64）
  found.sort((a, b) => {
    const rank = (p) => (/x64|win64/i.test(p) ? 0 : /i86|win32/i.test(p) ? 2 : 1);
    return rank(a) - rank(b);
  });
  return found;
}

const runtimeDirs = findRuntimeDirs();
if (runtimeDirs.length === 0) {
  console.warn('[bundle] 未找到海康 MVS 相机运行时（MvCameraControl.dll）。');
  console.warn('[bundle] 请安装 MVS，或把相机运行时目录放入 _native/ 后重试。');
  console.warn('[bundle] 已捆绑：' + (copiedFromCmake.join(', ') || '（无）'));
  process.exit(0);
}

const runtimeDir = runtimeDirs[0];
if (resolve(runtimeDir).toLowerCase() !== resolve(nativeDir).toLowerCase()) {
  cpSync(runtimeDir, nativeDir, { recursive: true, force: true });
  console.log(`[bundle] 海康 MVS 相机运行时 <- ${runtimeDir}`);
}

// ---------------------------------------------------------------------------
// 汇总
// ---------------------------------------------------------------------------

const dllCount = readdirSync(nativeDir).filter((f) => f.toLowerCase().endsWith('.dll')).length;
const missing = ['hik_mvcamera.dll', 'MvCameraControl.dll'].filter((f) => !existsSync(join(nativeDir, f)));
console.log(`[bundle] _native/ 现有 ${dllCount} 个 DLL。`);
if (missing.length === 0) {
  console.log('[bundle] 捆绑完整：hik_mvcamera.dll + 海康 MVS 相机运行时均已就位。');
} else {
  console.warn('[bundle] 仍缺失：' + missing.join(', '));
}
