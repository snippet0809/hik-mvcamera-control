/**
 * @file lib/index.js
 * @brief hik-mvcamera JS 封装：DLL 搜索路径准备 + 加载 N-API 插件 + 高层 API。
 *
 * 与 ffi/node（读码器包）同构：
 *   - 加载前把捆绑目录 `_native/` 与 addon 所在目录前置到 PATH（镜像 Python `_windows_prepend_path`）；
 *   - 加载预编译 .node（node-gyp-build 选 prebuilds 或 build/Release，纯选文件不编译）。
 */

'use strict';

const fs = require('node:fs');
const path = require('node:path');

const PKG_ROOT = path.join(__dirname, '..');
const NATIVE_DIR = path.join(PKG_ROOT, '_native');

// ---------------------------------------------------------------------------
// DLL 搜索路径准备
// ---------------------------------------------------------------------------

function _existingDirs(dirs) {
  const seen = new Set();
  const out = [];
  for (const d of dirs) {
    if (!d) continue;
    try {
      const r = path.resolve(d);
      if (fs.existsSync(r) && fs.statSync(r).isDirectory()) {
        const key = r.toLowerCase();
        if (!seen.has(key)) {
          seen.add(key);
          out.push(r);
        }
      }
    } catch {
      /* ignore */
    }
  }
  return out;
}

function _prependPath(dirs) {
  const parts = dirs.join(path.delimiter);
  if (!parts) return;
  const cur = process.env.PATH || '';
  process.env.PATH = parts + path.delimiter + cur;
}

function _bundledNativeFiles() {
  try {
    return fs.readdirSync(NATIVE_DIR).filter((f) => /\.dll$/i.test(f));
  } catch {
    return [];
  }
}

function _prepareNativeSearch() {
  const dirs = [NATIVE_DIR];
  dirs.push(path.join(PKG_ROOT, 'build', 'Release'));
  const prebuildsDir = path.join(PKG_ROOT, 'prebuilds');
  if (fs.existsSync(prebuildsDir)) {
    try {
      for (const sub of fs.readdirSync(prebuildsDir)) {
        dirs.push(path.join(prebuildsDir, sub));
      }
    } catch {
      /* ignore */
    }
  }
  const ordered = _existingDirs(dirs);
  _prependPath(ordered);
  return ordered;
}

let _addon = null;

function _loadAddon() {
  if (_addon) return _addon;
  _prepareNativeSearch();
  try {
    const gypBuild = require('node-gyp-build');
    _addon = gypBuild(PKG_ROOT);
    return _addon;
  } catch (err) {
    const dlls = _bundledNativeFiles();
    const why = err && err.code ? err.code : err && err.message ? err.message : String(err);
    const hint = [
      `加载 hik_mvcamera 原生插件失败：${why}`,
      `_native/ 中已捆绑 DLL：${dlls.length ? dlls.join(', ') : '（无）'}`,
      '请运行 `npm run bundle` 以捆绑 hik_mvcamera.dll 与海康 MVS 相机运行时；',
      '开发期也可先安装 MVS 使 MvCameraControl.dll 可解析。',
    ].join('\n');
    if (err instanceof Error) {
      err.message = err.message ? `${err.message}\n${hint}` : hint;
    }
    throw err;
  }
}

function diagnoseNativeLoad() {
  const dirs = _prepareNativeSearch();
  let addonLoaded = false;
  try {
    _loadAddon();
    addonLoaded = true;
  } catch {
    addonLoaded = false;
  }
  return {
    nativeDir: NATIVE_DIR,
    bundledDlls: _bundledNativeFiles(),
    searchPathPrefix: dirs,
    addonLoaded,
  };
}

const native = _loadAddon();

// ---------------------------------------------------------------------------
// 常量
// ---------------------------------------------------------------------------

const HIK_CV_OK = native.HIK_CV_OK;
const HIK_CV_ERR_UNKNOWN = native.HIK_CV_ERR_UNKNOWN;
const HIK_CV_ERR_LOGIC = native.HIK_CV_ERR_LOGIC;
const HIK_CV_ERR_RUNTIME = native.HIK_CV_ERR_RUNTIME;
const HIK_CV_ERR_INVALID_ARG = native.HIK_CV_ERR_INVALID_ARG;
const HIK_CV_ERR_NO_MEMORY = native.HIK_CV_ERR_NO_MEMORY;
const HIK_CV_FRAME_KEEP = native.HIK_CV_FRAME_KEEP;
const HIK_CV_FRAME_SET = native.HIK_CV_FRAME_SET;
const HIK_CV_FRAME_CLEAR = native.HIK_CV_FRAME_CLEAR;

// ---------------------------------------------------------------------------
// 高层 API
// ---------------------------------------------------------------------------

/** 起流前 GenICam 项（未填字段不修改）。 */
class OpenParams {
  constructor({ trigger_mode = undefined, trigger_source = undefined, net_trans_mode = undefined } = {}) {
    this.trigger_mode = trigger_mode;
    this.trigger_source = trigger_source;
    this.net_trans_mode = net_trans_mode;
  }

  toNative() {
    const o = {};
    if (this.trigger_mode) o.trigger_mode = String(this.trigger_mode);
    if (this.trigger_source) o.trigger_source = String(this.trigger_source);
    if (this.net_trans_mode != null) o.net_trans_mode = Number(this.net_trans_mode);
    return o;
  }
}

class HikCamera {
  constructor() {
    /** 保活已登记的图像回调（与 ffi/node 的 `_bcrKeepalive` 同构）。 */
    this._frameKeepalive = new Map();
  }

  /** 枚举相机 → [{serialNumber, netExportIp}]。无相机时返回 []。 */
  enumDevices() {
    return native.enumDevices();
  }

  /**
   * 起流。
   * @param {string} sn 序列号
   * @param {object} [opts]
   * @param {object|OpenParams} [opts.params] 起流前 GenICam 项（trigger_mode/trigger_source）
   * @param {Function} [opts.onFrame] 图像回调：(serial, frameInfo, buffer) => void
   * @param {boolean} [opts.clearFrame] 清除该序列号已登记的图像回调
   */
  startDevice(sn, opts = {}) {
    if (typeof sn !== 'string' || sn.length === 0) {
      throw new TypeError('serial must be a non-empty string');
    }
    const { params = null, onFrame = null, clearFrame = false } = opts || {};
    if (clearFrame && onFrame != null) {
      throw new Error('clearFrame 与 onFrame 不可同时指定');
    }
    const nativeParams = params && typeof params.toNative === 'function' ? params.toNative() : params;

    if (clearFrame) {
      native.startDevice(sn, null, HIK_CV_FRAME_CLEAR, null);
      this._frameKeepalive.delete(sn);
      return;
    }
    if (onFrame != null) {
      if (typeof onFrame !== 'function') {
        throw new TypeError('onFrame must be a function');
      }
      this._frameKeepalive.set(sn, onFrame);
      native.startDevice(sn, nativeParams, HIK_CV_FRAME_SET, onFrame);
      return;
    }
    native.startDevice(sn, nativeParams, HIK_CV_FRAME_KEEP, null);
  }

  /** 停流；已登记的图像回调保留（与读码器 BCR 行为一致）。 */
  stopDevice(sn) {
    native.stopDevice(sn);
  }

  /** 软触发（须已 startDevice 且处于取流、TriggerMode=On）。 */
  triggerDevice(sn) {
    native.triggerDevice(sn);
  }

  /** 按 GenICam 节点名写参数；value 支持 number / boolean / string。 */
  setParam(sn, name, value) {
    native.setParam(sn, name, value);
  }

  /** 按 GenICam 节点名读参数 → number | boolean | string。 */
  getParam(sn, name) {
    return native.getParam(sn, name);
  }

  /**
   * 临时强制 GigE 相机 IP（MV_GIGE_ForceIpEx；重启后恢复，不改持久配置）。
   * 改完 IP 后需重新 enumDevices 并针对新 IP 起流。
   */
  forceIp(sn, ip, subnetMask = '255.255.255.0', gateway = '0.0.0.0') {
    native.forceIp(sn, ip, subnetMask, gateway);
  }

  /** 最近一次错误的线程局部信息。 */
  lastError() {
    return native.lastError();
  }
}

module.exports = {
  HikCamera,
  OpenParams,
  diagnoseNativeLoad,
  HIK_CV_OK,
  HIK_CV_ERR_UNKNOWN,
  HIK_CV_ERR_LOGIC,
  HIK_CV_ERR_RUNTIME,
  HIK_CV_ERR_INVALID_ARG,
  HIK_CV_ERR_NO_MEMORY,
  HIK_CV_FRAME_KEEP,
  HIK_CV_FRAME_SET,
  HIK_CV_FRAME_CLEAR,
};
