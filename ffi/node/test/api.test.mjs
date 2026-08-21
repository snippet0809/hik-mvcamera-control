/**
 * @file api.test.mjs
 * @brief 冒烟测试：捆绑文件、加载诊断、无设备下的 API 行为与参数校验。
 *
 * 无读码器设备时也应通过；BCR 回调与软触发的联机路径需真机（见 README）。
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
    t.skip(`addon 未加载（${loadError?.message ?? loadError}）；请先 npm run bundle 并确认海康运行时`);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

test('package 导出常量', (t) => {
  if (skipIfNotLoaded(t)) return;
  assert.equal(hik.HIK_CR_OK, 0);
  assert.equal(hik.HIK_CR_BCR_KEEP, 0);
  assert.equal(hik.HIK_CR_BCR_SET, 1);
  assert.equal(hik.HIK_CR_BCR_CLEAR, 2);
  assert.equal(typeof hik.HikCodeReader, 'function');
  assert.equal(typeof hik.OpenParams, 'function');
});

test('捆绑文件存在（_native 内 DLL）', (t) => {
  if (skipIfNotLoaded(t)) return;
  const dll = path.join(pkgRoot, '_native', 'hik_code_reader.dll');
  const ctrl = path.join(pkgRoot, '_native', 'MvCodeReaderCtrl.dll');
  assert.ok(existsSync(dll), `缺少 ${dll}（npm run bundle）`);
  assert.ok(existsSync(ctrl), `缺少 ${ctrl}（海康读码器运行时，npm run bundle）`);
});

test('diagnoseNativeLoad 报告 addon 已加载', (t) => {
  if (skipIfNotLoaded(t)) return;
  const diag = hik.diagnoseNativeLoad();
  assert.ok(Array.isArray(diag.bundledDlls));
  assert.ok(Array.isArray(diag.searchPathPrefix));
  assert.equal(diag.addonLoaded, true);
});

test('enumDevices 无设备时返回 []', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();
  const devs = cr.enumDevices();
  assert.ok(Array.isArray(devs));
});

test('startDevice 参数校验', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();

  assert.throws(() => cr.startDevice(''), TypeError, '空序列号应抛 TypeError');
  assert.throws(() => cr.startDevice('sn', { onBcr: () => {}, clearBcr: true }), /不可同时指定/, 'onBcr 与 clearBcr 互斥');
  assert.throws(() => cr.startDevice('sn', { onBcr: 'not-a-function' }), TypeError, 'onBcr 须为函数');

  // 无设备时起流应失败并带 hik_cr error 前缀（C ABI 错误映射）
  assert.throws(() => cr.startDevice('no-such-device'), /hik_cr error/, '未知设备起流应抛错');
});

test('OpenParams.toNative 归一化', (t) => {
  if (skipIfNotLoaded(t)) return;
  const p = new hik.OpenParams({ trigger_mode: 'On', code128: true });
  const o = p.toNative();
  assert.equal(o.trigger_mode, 'On');
  assert.equal(o.code128, true);
  assert.equal(o.trigger_source, undefined);
  assert.equal(o.qrcode, undefined);
});

test('无 IDMVS/MVS 的 PATH 下仍能加载（全捆绑离线可用）', (t) => {
  if (skipIfNotLoaded(t)) return;
  const ctrl = path.join(pkgRoot, '_native', 'MvCodeReaderCtrl.dll');
  if (!existsSync(ctrl)) {
    t.skip('_native 无海康运行时，跳过离线加载验证');
    return;
  }
  // 构造剔除 MVS/IDMVS 项的 PATH，子进程 require 包
  const filteredPath = (process.env.PATH || '')
    .split(path.delimiter)
    .filter((p) => !/mvs|idmvs|mvcode|hikrobot|mvvision/i.test(p))
    .join(path.delimiter);
  const script = "const h=require('" + pkgRoot.replace(/\\/g, '\\\\') + "');" +
    'const cr=new h.HikCodeReader(); process.stdout.write(JSON.stringify({d:cr.enumDevices()}));';
  const out = execFileSync(process.execPath, ['-e', script], {
    env: { ...process.env, PATH: filteredPath },
    encoding: 'utf8',
  });
  assert.ok(JSON.parse(out).d, '离线 PATH 下应能加载并枚举');
});
