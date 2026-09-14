/**
 * @file api.test.mjs
 * @brief 冒烟测试：捆绑文件、加载诊断、无设备下的 API 行为与参数校验。
 *
 * 无读码器/相机设备时也应通过；BCR/图像回调与软触发的联机路径需真机（见 README）。
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
    t.skip(`addon 未加载（${loadError?.message ?? loadError}）；请先 npm run bundle 并确认海康读码器/相机运行时`);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

test('package 导出常量与类', (t) => {
  if (skipIfNotLoaded(t)) return;
  assert.equal(hik.HIK_CR_OK, 0);
  assert.equal(hik.HIK_CR_BCR_KEEP, 0);
  assert.equal(hik.HIK_CR_BCR_SET, 1);
  assert.equal(hik.HIK_CR_BCR_CLEAR, 2);
  assert.equal(hik.HIK_CR_FRAME_KEEP, 0);
  assert.equal(hik.HIK_CR_FRAME_SET, 1);
  assert.equal(hik.HIK_CR_FRAME_CLEAR, 2);
  assert.equal(hik.HIK_CR_PARAM_STRING_MAX, 256, '字符串参数缓冲长度应与相机侧 HIK_CV_STRING_MAX 一致');
  assert.equal(hik.HIK_CV_OK, 0);
  assert.equal(hik.HIK_CV_FRAME_KEEP, 0);
  assert.equal(hik.HIK_CV_FRAME_SET, 1);
  assert.equal(hik.HIK_CV_FRAME_CLEAR, 2);
  assert.equal(typeof hik.HikCodeReader, 'function');
  assert.equal(typeof hik.HikCamera, 'function');
  assert.equal(typeof hik.ReaderOpenParams, 'function');
  assert.equal(typeof hik.CameraOpenParams, 'function');
});

test('捆绑文件存在（_native 内 DLL）', (t) => {
  if (skipIfNotLoaded(t)) return;
  const dllCr = path.join(pkgRoot, '_native', 'hik_code_reader.dll');
  const dllCv = path.join(pkgRoot, '_native', 'hik_mvcamera.dll');
  const ctrlCr = path.join(pkgRoot, '_native', 'MvCodeReaderCtrl.dll');
  const ctrlCv = path.join(pkgRoot, '_native', 'MvCameraControl.dll');
  assert.ok(existsSync(dllCr), `缺少 ${dllCr}（npm run bundle）`);
  assert.ok(existsSync(dllCv), `缺少 ${dllCv}（npm run bundle）`);
  assert.ok(existsSync(ctrlCr), `缺少 ${ctrlCr}（海康读码器运行时，npm run bundle）`);
  assert.ok(existsSync(ctrlCv), `缺少 ${ctrlCv}（海康相机运行时，npm run bundle）`);
});

test('diagnoseNativeLoad 报告 addon 已加载', (t) => {
  if (skipIfNotLoaded(t)) return;
  const diag = hik.diagnoseNativeLoad();
  assert.ok(Array.isArray(diag.bundledDlls));
  assert.ok(Array.isArray(diag.searchPathPrefix));
  assert.equal(diag.addonLoaded, true);
});

test('enumDevices 无设备时返回数组', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();
  const devs = cr.enumDevices();
  assert.ok(Array.isArray(devs));

  const cam = new hik.HikCamera();
  const cams = cam.enumDevices();
  assert.ok(Array.isArray(cams));
});

test('读码器 startDevice 参数校验', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();

  assert.throws(() => cr.startDevice(''), TypeError, '空序列号应抛 TypeError');
  assert.throws(() => cr.startDevice('sn', { onBcr: () => {}, clearBcr: true }), /不可同时指定/, 'onBcr 与 clearBcr 互斥');
  assert.throws(() => cr.startDevice('sn', { onBcr: 'not-a-function' }), TypeError, 'onBcr 须为函数');

  // 无设备时起流应失败并带 hik_cr error 前缀（C ABI 错误映射）
  assert.throws(() => cr.startDevice('no-such-device'), /hik_cr error/, '未知设备起流应抛错');
});

test('相机 startDevice 参数校验', (t) => {
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

test('ReaderOpenParams.toNative 归一化', (t) => {
  if (skipIfNotLoaded(t)) return;
  const p = new hik.ReaderOpenParams({ trigger_mode: 'On', code128: true });
  const o = p.toNative();
  assert.equal(o.trigger_mode, 'On');
  assert.equal(o.code128, true);
  assert.equal(o.trigger_source, undefined);
  assert.equal(o.qrcode, undefined);
});

test('CameraOpenParams.toNative 归一化', (t) => {
  if (skipIfNotLoaded(t)) return;
  const p = new hik.CameraOpenParams({ trigger_mode: 'On', trigger_source: 'Software', net_trans_mode: 2 });
  const o = p.toNative();
  assert.equal(o.trigger_mode, 'On');
  assert.equal(o.trigger_source, 'Software');
  assert.equal(o.net_trans_mode, 2);
});

test('CameraOpenParams 支持 width/height', (t) => {
  if (skipIfNotLoaded(t)) return;
  const p = new hik.CameraOpenParams({ width: 2448, height: 2048 });
  const o = p.toNative();
  assert.equal(o.width, 2448);
  assert.equal(o.height, 2048);

  // 未填字段不应出现在 native 载荷里（C 侧按 0 处理 = 不修改）
  const empty = new hik.CameraOpenParams({}).toNative();
  assert.equal(empty.width, undefined);
  assert.equal(empty.height, undefined);
});

test('读码器 setFrameCallback 参数校验', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();

  assert.throws(() => cr.setFrameCallback(''), TypeError, '空序列号应抛 TypeError');
  assert.throws(
    () => cr.setFrameCallback('sn', { onFrame: () => {}, clearFrame: true }),
    /不可同时指定/,
    'onFrame 与 clearFrame 互斥'
  );
  assert.throws(() => cr.setFrameCallback('sn', { onFrame: 'not-a-function' }), TypeError, 'onFrame 须为函数');
});

// 帧回调注册表（INSERT / KEEP / CLEAR + releaseFrame）无需硬件即可驱动：
// hik_cr_set_frame_callback → cppFrame → registerFrameCallbackForSerial 只写注册表，
// findDevice() 返回 null 时不会去碰 SDK，故无设备也应正常返回。
test('读码器 setFrameCallback 注册表在有/无设备时均可驱动', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();

  assert.doesNotThrow(() => cr.setFrameCallback('no-such-reader', { onFrame: () => {} }), 'SET 应只写注册表');
  assert.doesNotThrow(() => cr.setFrameCallback('no-such-reader'), 'KEEP 应不动注册表');
  assert.doesNotThrow(() => cr.setFrameCallback('no-such-reader', { clearFrame: true }), 'CLEAR 应释放注册表条目');
  // 重复 CLEAR（条目已不存在）不应因重复 Release 而崩溃
  assert.doesNotThrow(() => cr.setFrameCallback('no-such-reader', { clearFrame: true }), '重复 CLEAR 应幂等');
});

test('读码器 setParam / runCommand 未起流设备应报错', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();

  // paramDevice 在设备未 startDevice 时抛 logic_error → HIK_CR_ERR_LOGIC → "hik_cr error 2: ..."
  assert.throws(() => cr.setParam('no-such-reader', 'ExposureTime', 3000), /hik_cr error/, '未起流写参数应抛错');
  assert.throws(() => cr.getParam('no-such-reader', 'ExposureTime'), /hik_cr error/, '未起流读参数应抛错');
  assert.throws(() => cr.runCommand('no-such-reader', 'TriggerSoftware'), /hik_cr error/, '未起流执行命令应抛错');
});

test('读码器 getBcrImage 未读到条码时返回 null', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cr = new hik.HikCodeReader();
  // 该序列号既未 startDevice 也未解码过，属正常空态而非异常
  assert.equal(cr.getBcrImage('no-such-reader'), null);
});

test('相机 encodeJpeg 参数校验与未起流报错', (t) => {
  if (skipIfNotLoaded(t)) return;
  const cam = new hik.HikCamera();

  assert.throws(() => cam.encodeJpeg('', {}, Buffer.alloc(0)), TypeError, '空序列号应抛 TypeError');
  assert.throws(() => cam.encodeJpeg('sn', null, Buffer.alloc(0)), TypeError, 'frameInfo 须为对象');
  assert.throws(() => cam.encodeJpeg('sn', {}, 'not-a-buffer'), TypeError, 'frameBuffer 须为 Buffer');

  // 帧元数据大于实际缓冲：必须在「输入下界校验」处被挡住（HIK_CV_ERR_INVALID_ARG = 4），
  // 否则会把 10 字节的缓冲按 5000×5000 交给 SDK，变成越界读。
  // 该校验发生在 findCamera 之前，因此无需真机即可验证。
  const bogus = { width: 5000, height: 5000, pixelType: 0x01080001, frameLen: 5000 * 5000, frameNum: 1 };
  assert.throws(
    () => cam.encodeJpeg('no-such-camera', bogus, Buffer.alloc(10)),
    /hik_cv error 4/,
    '帧元数据与实际缓冲不匹配应判为参数非法'
  );

  // 未 startDevice：几何校验通过后抛 logic_error → HIK_CV_ERR_LOGIC = 2
  const info = { width: 640, height: 480, pixelType: 0x01080001, frameLen: 640 * 480, frameNum: 1 };
  assert.throws(
    () => cam.encodeJpeg('no-such-camera', info, Buffer.alloc(640 * 480)),
    /hik_cv error 2/,
    '未起流设备编码应抛 logic 错误'
  );
  // 注：编码成功路径（真实 JPEG 字节）无相机无法验证，需真机。
});

test('无 IDMVS/MVS 的 PATH 下仍能加载（全捆绑离线可用）', (t) => {
  if (skipIfNotLoaded(t)) return;
  const ctrlCv = path.join(pkgRoot, '_native', 'MvCameraControl.dll');
  if (!existsSync(ctrlCv)) {
    t.skip('_native 无海康相机运行时，跳过离线加载验证');
    return;
  }
  // 构造剔除 MVS/IDMVS 项的 PATH，子进程 require 包并同时调读码器 + 相机
  const filteredPath = (process.env.PATH || '')
    .split(path.delimiter)
    .filter((p) => !/mvs|idmvs|mvcode|hikrobot|mvvision/i.test(p))
    .join(path.delimiter);
  const script =
    "const h=require('" + pkgRoot.replace(/\\/g, '\\\\') + "');" +
    'const cr=new h.HikCodeReader();const cam=new h.HikCamera();' +
    'process.stdout.write(JSON.stringify({d:cr.enumDevices(),c:cam.enumDevices()}));';
  const out = execFileSync(process.execPath, ['-e', script], {
    env: { ...process.env, PATH: filteredPath },
    encoding: 'utf8',
  });
  const parsed = JSON.parse(out);
  assert.ok(Array.isArray(parsed.d), '离线 PATH 下应能加载并枚举读码器');
  assert.ok(Array.isArray(parsed.c), '离线 PATH 下应能加载并枚举相机');
});
