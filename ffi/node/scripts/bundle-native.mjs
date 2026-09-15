/**
 * @file bundle-native.mjs
 * @brief 把**本项目自己的** wrapper DLL/导入库拷入 `ffi/node/_native/`。
 *
 * 默认只拷根 CMake 的产物：`hik_code_reader.dll/.lib` 与 `hik_mvcamera.dll/.lib`。
 *
 * **厂商运行时（`MvCameraControl.dll` / `MvCodeReaderCtrl.dll` 及其依赖）默认不随包分发** ——
 * 与 `runtime/VERSION` 的 Windows 口径、以及 Go 绑定（`hikcr/hikcr.go` 的注释）保持一致：
 * 由使用方在目标机上安装 MVS/IDMVS，安装器写机器级 PATH，wrapper 的 PE 导入表在那里解析。
 * 实测（2026-09-15）：`_native/` 只留两个 wrapper（480KB）时枚举与取流均正常，
 * 厂商 DLL 全部从 `C:\Program Files (x86)\Common Files\MVS\...\Win64_x64\` 与
 * `D:\IDMVS\...\plugins\mvsidcamctrl\` 解析。
 *
 * 若确有离线/免安装需求，可设 `HIK_BUNDLE_VENDOR_RUNTIME=1` 走旧的"全捆绑"路径：
 *   1. 定位海康 **MVS 相机运行时**（含 `MvCameraControl.dll`），先整目录拷贝（force，胜出）；
 *   2. 定位海康 **读码器运行时**（含 `MvCodeReaderCtrl.dll`），后拷（跳过已存在的相对路径，补齐读码器专属文件）。
 *   去重策略：相机（MVS）运行时为官方基座，对共享同名 DLL 一律以 MVS 版本为准——Windows 进程内
 *   只加载一份 `MvCameraControl.dll`，读码器 SDK 本就设计为依赖 MVS 基座；去重键为完整相对路径。
 *   ⚠️ 这条路径会把整棵 MVS 树拷进来（实测 `_native/` 176MB、tarball 72.5MB），
 *   且该体积会随 tarball 提交进使用方仓库——默认关闭正是为了避免这件事。
 *
 * 用法：`npm run bundle`（或 `node scripts/bundle-native.mjs`）。
 */

import { cpSync, existsSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const pkgRoot = resolve(here, '..');
const nativeDir = join(pkgRoot, '_native');
const repoRoot = resolve(pkgRoot, '../..');

mkdirSync(nativeDir, { recursive: true });

// ---------------------------------------------------------------------------
// 1) 两个项目 DLL / 导入库（来自根 CMake 构建）
// ---------------------------------------------------------------------------

const releaseDir = join(repoRoot, 'build', 'Release');
const fromCmake = ['hik_code_reader.dll', 'hik_code_reader.lib', 'hik_mvcamera.dll', 'hik_mvcamera.lib'];
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
// 2) 运行时目录定位 + 合并
// ---------------------------------------------------------------------------

const READER_SEARCH_ROOTS = [
  // IDMVS 的读码器运行时在 plugins\mvsidcamctrl 下。曾经这里写的是 plugins\MvSDK
  // ——那个目录**在真机上是根本不存在的**（IDMVS 安装器注册的 PATH 项还留着这个名字，
  // 所以它看着像真的）。本脚本靠下面的 where.exe 兜住了，但别再把错的名字抄回来。
  'D:\\IDMVS\\Applications\\Win64\\plugins\\mvsidcamctrl',
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

const CAMERA_SEARCH_ROOTS = [
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

/**
 * 找含 markerDll 的运行时目录。
 * @param {string} markerDll 如 MvCodeReaderCtrl.dll / MvCameraControl.dll
 * @param {string[]} searchRoots 已知安装根
 * @param {boolean} preferX64 命中多个时优先 64 位（相机运行时用）
 * @returns {string[]} 去重后的绝对目录
 */
function findRuntimeDirs(markerDll, searchRoots, preferX64) {
  const found = [];
  const push = (d) => {
    if (!d || !existsSync(d)) return;
    const r = resolve(d);
    if (!found.includes(r)) found.push(r);
  };

  if (process.platform === 'win32') {
    const r = spawnSync('where.exe', [markerDll], { encoding: 'utf8', windowsHide: true });
    if (r.status === 0 && r.stdout) {
      for (const line of r.stdout.split(/\r?\n/)) {
        const p = line.trim();
        if (p) push(dirname(p));
      }
    }
  }

  for (const root of searchRoots) {
    try {
      walk(root, (f) => {
        if (f.toLowerCase().endsWith(`\\${markerDll.toLowerCase()}`)) push(dirname(f));
      });
    } catch {
      /* 不可访问的根忽略 */
    }
  }

  if (preferX64) {
    // PATH 里可能同时有 Win32_i86 / Win64_x64，必须选 x64
    found.sort((a, b) => {
      const rank = (p) => (/x64|win64/i.test(p) ? 0 : /i86|win32/i.test(p) ? 2 : 1);
      return rank(a) - rank(b);
    });
  }
  return found;
}

/**
 * 把 srcDir 递归拷入 destDir（保留相对路径）。
 * @param {boolean} skipExisting true 时跳过已存在的相对路径（后拷方用，实现去重）
 */
function copyTreeMerge(srcDir, destDir, skipExisting) {
  let copied = 0;
  let skipped = 0;
  walk(srcDir, (f) => {
    const rel = relative(srcDir, f);
    if (!rel || rel.startsWith('..')) return;
    const dest = join(destDir, rel);
    if (skipExisting && existsSync(dest)) {
      skipped++;
      return;
    }
    mkdirSync(dirname(dest), { recursive: true });
    cpSync(f, dest, { force: true });
    copied++;
  });
  return { copied, skipped };
}

if (process.env.HIK_BUNDLE_VENDOR_RUNTIME !== '1') {
  console.log('[bundle] 厂商运行时按设计不随包分发：目标机需安装 MVS/IDMVS（与 Go 绑定口径一致）。');
  console.log('[bundle] 如需离线全捆绑，设 HIK_BUNDLE_VENDOR_RUNTIME=1 重跑（_native/ 会涨到约 176MB）。');
} else {
  const cameraDirs = findRuntimeDirs('MvCameraControl.dll', CAMERA_SEARCH_ROOTS, true);
  const readerDirs = findRuntimeDirs('MvCodeReaderCtrl.dll', READER_SEARCH_ROOTS, false);

  if (cameraDirs.length === 0 && readerDirs.length === 0) {
    console.warn('[bundle] 未找到海康相机（MvCameraControl.dll）与读码器（MvCodeReaderCtrl.dll）运行时。');
    console.warn('[bundle] 请安装 IDMVS/MVS，或把运行时目录放入 _native/ 后重试。');
    console.warn('[bundle] 已捆绑：' + (copiedFromCmake.join(', ') || '（无）'));
    process.exit(0);
  }

  // 相机（MVS）运行时先拷（force，胜出），读码器运行时后拷（跳过已存在相对路径，补齐专属文件）
  if (cameraDirs.length > 0) {
    const dir = cameraDirs[0];
    if (resolve(dir).toLowerCase() !== resolve(nativeDir).toLowerCase()) {
      const { copied } = copyTreeMerge(dir, nativeDir, false);
      console.log(`[bundle] 海康 MVS 相机运行时 <- ${dir}（${copied} 文件）`);
    }
  } else {
    console.warn('[bundle] 未找到相机运行时（MvCameraControl.dll），仅捆绑读码器部分。');
  }

  if (readerDirs.length > 0) {
    const dir = readerDirs[0];
    if (resolve(dir).toLowerCase() !== resolve(nativeDir).toLowerCase()) {
      const { copied, skipped } = copyTreeMerge(dir, nativeDir, true);
      console.log(`[bundle] 海康读码器运行时 <- ${dir}（拷 ${copied}，跳过 ${skipped}）`);
    }
  } else {
    console.warn('[bundle] 未找到读码器运行时（MvCodeReaderCtrl.dll），仅捆绑相机部分。');
  }
}

// ---------------------------------------------------------------------------
// 汇总
// ---------------------------------------------------------------------------

function countDlls(dir) {
  let n = 0;
  walk(dir, (f) => {
    if (f.toLowerCase().endsWith('.dll')) n++;
  });
  return n;
}

const dllCount = countDlls(nativeDir);
// 只校验**本项目自己的** wrapper：厂商 DLL 默认不在包内，缺了要由目标机安装 MVS/IDMVS 提供，不在这里报。
const missing = ['hik_code_reader.dll', 'hik_mvcamera.dll'].filter((f) => !existsSync(join(nativeDir, f)));
console.log(`[bundle] _native/ 现有 ${dllCount} 个 DLL。`);
if (missing.length === 0) {
  console.log('[bundle] 本项目 wrapper 已就位：hik_code_reader.dll + hik_mvcamera.dll。');
  console.log('[bundle] 厂商运行时（MvCameraControl.dll / MvCodeReaderCtrl.dll）由目标机安装 MVS/IDMVS 提供。');
} else {
  console.warn('[bundle] 仍缺失本项目 wrapper：' + missing.join(', '));
}
