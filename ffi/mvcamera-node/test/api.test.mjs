/**
 * @file api.test.mjs
 * @brief 冒烟测试：捆绑文件、加载诊断、无相机下的 API 行为与参数校验。
 *
 * 无相机设备时也应通过；图像回调与软触发的联机路径需真实相机（见 README）。
 */

import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const pkgRoot = path.join(here, '..');
const require = createRequire(import.meta.url);

let hik = null;
let loadError = null;
try {
  hik = require(path.join(pkgRoot, 'index.js'));
} catch (err) {
  loadError = err;
}

function skipIfNotLoaded(t) {
  if (!hik) {
    t.skip(`addon 未加载（${loadError?.message ?? loadError}）；请先 npm run bundle 并确认 MVS 运行时`);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

test('package 导出常量', (t) => {
  if (skipIfNotLoaded(t)) return;
  assert.equal(hik.HIK_CV_OK, 0);
  assert.equal(hik.HIK_CV_FRAME_KEEP, 0);
  assert.equal(hik.HIK_CV_FRAME_SET, 1);
  assert.equal(hik.HIK_CV_FRAME_CLEAR, 2);
  assert.equal(typeof hik.HikCamera, 'function');
  assert.equal(typeof hik.OpenParams, 'function');
});

test('捆绑文件存在（_native 内 DLL）', (t) => {
  if (skipIfNotLoaded(t)) return;
  const dll = path.join(pkgRoot, '_native', 'hik_mvcamera.dll');
  const ctrl = path.join(pkgRoot, '_native', 'MvCameraControl.dll');
  assert.ok(existsSync(dll), `缺少 ${dll}（npm run bundle）`);
  assert.ok(existsSync(ctrl), `缺少 ${ctrl}（海康 MVS 相机运行时，npm run bundle）`);
});

test('diagnoseNativeLoad 报告 addon 已加载', (t) => {
  if (skipIfNotLoaded(t)) return;
  const diag = hik.diagnoseNativeLoad();
  assert.ok(Array.isArray(diag.bundledDlls));
  assert.ok(Array.isArray(diag.searchPathPrefix));
  assert.equal(diag.addonLoaded, true);
});

test('enumDevices 返回数组', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cam = new hik.HikCamera();
  const devs = cam.enumDevices();
  assert.ok(Array.isArray(devs));
});

test('startDevice 参数校验', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cam = new hik.HikCamera();

  assert.throws(() => cam.startDevice(''), TypeError, '空序列号应抛 TypeError');
  assert.throws(
    () => cam.startDevice('sn', { onFrame: () => {}, clearFrame: true }),
    /不可同时指定/,
    'onFrame 与 clearFrame 互斥'
  );
  assert.throws(() => cam.startDevice('sn', { onFrame: 'not-a-function' }), TypeError, 'onFrame 须为函数');

  // 未知相机起流应失败并带 hik_cv error 前缀（C ABI 错误映射）
  assert.throws(() => cam.startDevice('no-such-camera'), /hik_cv error/, '未知相机起流应抛错');
});

test('OpenParams.toNative 归一化', (t) => {
  if (skipIfNotLoaded(t)) return;
  const p = new hik.OpenParams({ trigger_mode: 'On', trigger_source: 'Software' });
  const o = p.toNative();
  assert.equal(o.trigger_mode, 'On');
  assert.equal(o.trigger_source, 'Software');
});

test('无 MVS/IDMVS 的 PATH 下仍能加载（全捆绑离线可用）', (t) => {
  if (skipIfNotLoaded(t)) return;
  const ctrl = path.join(pkgRoot, '_native', 'MvCameraControl.dll');
  if (!existsSync(ctrl)) {
    t.skip('_native 无 MVS 相机运行时，跳过离线加载验证');
    return;
  }
  const filteredPath = (process.env.PATH || '')
    .split(path.delimiter)
    .filter((p) => !/mvs|idmvs|mvcode|hikrobot|mvvision/i.test(p))
    .join(path.delimiter);
  const script =
    "const h=require('" + pkgRoot.replace(/\\/g, '\\\\') + "');" +
    'const cam=new h.HikCamera(); process.stdout.write(JSON.stringify({d:cam.enumDevices()}));';
  const out = execFileSync(process.execPath, ['-e', script], {
    env: { ...process.env, PATH: filteredPath },
    encoding: 'utf8',
  });
  assert.ok(JSON.parse(out).d, '离线 PATH 下应能加载并枚举');
});
