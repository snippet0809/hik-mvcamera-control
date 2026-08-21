/**
 * @file lib/index.js
 * @brief hik-code-reader JS 封装：DLL 搜索路径准备 + 加载 N-API 插件 + 高层 API。
 *
 * 与 Python 包 `hik_code_reader` 同名同构：
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
      /* 忽略不可解析路径 */
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

/**
 * 收集需要前置到 PATH 的目录（捆绑 `_native/`、开发期 build/Release、预编译 prebuilds/<platform>-<arch>/），
 * 并把已存在的目录按序前置，保证 `hik_code_reader.dll` 及其海康依赖可解析。
 * @returns {string[]} 实际前置的目录
 */
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
      `加载 hik_code_reader 原生插件失败：${why}`,
      `_native/ 中已捆绑 DLL：${dlls.length ? dlls.join(', ') : '（无）'}`,
      '请运行 `npm run bundle` 以捆绑 hik_code_reader.dll 与海康读码器运行时 DLL；',
      '开发期也可先安装 IDMVS/MVS 使 MvCodeReaderCtrl.dll 可解析。',
    ].join('\n');
    if (err instanceof Error) {
      err.message = err.message ? `${err.message}\n${hint}` : hint;
    }
    throw err;
  }
}

/** 自助诊断：捆绑文件、PATH 前置目录、addon 是否可加载（供排障）。 */
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

// 模块加载即解析 DLL 并加载插件（失败时抛错并附诊断）
const native = _loadAddon();

// ---------------------------------------------------------------------------
// 常量
// ---------------------------------------------------------------------------

const HIK_CR_OK = native.HIK_CR_OK;
const HIK_CR_ERR_UNKNOWN = native.HIK_CR_ERR_UNKNOWN;
const HIK_CR_ERR_LOGIC = native.HIK_CR_ERR_LOGIC;
const HIK_CR_ERR_RUNTIME = native.HIK_CR_ERR_RUNTIME;
const HIK_CR_ERR_INVALID_ARG = native.HIK_CR_ERR_INVALID_ARG;
const HIK_CR_ERR_NO_MEMORY = native.HIK_CR_ERR_NO_MEMORY;
const HIK_CR_BCR_KEEP = native.HIK_CR_BCR_KEEP;
const HIK_CR_BCR_SET = native.HIK_CR_BCR_SET;
const HIK_CR_BCR_CLEAR = native.HIK_CR_BCR_CLEAR;

// ---------------------------------------------------------------------------
// 高层 API
// ---------------------------------------------------------------------------

/** 起流前 GenICam 项（未填字段走 C++ 默认）。 */
class OpenParams {
  constructor({ trigger_mode = undefined, trigger_source = undefined, code128 = undefined, qrcode = undefined } = {}) {
    this.trigger_mode = trigger_mode;
    this.trigger_source = trigger_source;
    this.code128 = code128;
    this.qrcode = qrcode;
  }

  /** 转成 addon 期望的 {key:value}（布尔转 bool，省略 undefined/空串）。 */
  toNative() {
    const o = {};
    if (this.trigger_mode) o.trigger_mode = String(this.trigger_mode);
    if (this.trigger_source) o.trigger_source = String(this.trigger_source);
    if (this.code128 != null) o.code128 = !!this.code128;
    if (this.qrcode != null) o.qrcode = !!this.qrcode;
    return o;
  }
}

class HikCodeReader {
  constructor() {
    /** 保活已登记的 BCR 回调（与 Python `_bcr_keepalive` 同构）。 */
    this._bcrKeepalive = new Map();
  }

  /** 枚举设备 → [{serialNumber, netExportIp}]。无读码器时返回 []。 */
  enumDevices() {
    return native.enumDevices();
  }

  /**
   * 起流。
   * @param {string} sn 序列号
   * @param {object} [opts]
   * @param {object|OpenParams} [opts.params] 起流前 GenICam 项（trigger_mode/trigger_source/code128/qrcode）
   * @param {Function} [opts.onBcr] 注册 BCR 回调：(serial, codes[]) => void
   * @param {boolean} [opts.clearBcr] 清除该序列号已登记的 BCR 回调
   */
  startDevice(sn, opts = {}) {
    if (typeof sn !== 'string' || sn.length === 0) {
      throw new TypeError('serial must be a non-empty string');
    }
    const { params = null, onBcr = null, clearBcr = false } = opts || {};
    if (clearBcr && onBcr != null) {
      throw new Error('clearBcr 与 onBcr 不可同时指定');
    }
    const nativeParams = params && typeof params.toNative === 'function' ? params.toNative() : params;

    if (clearBcr) {
      native.startDevice(sn, null, HIK_CR_BCR_CLEAR, null);
      this._bcrKeepalive.delete(sn);
      return;
    }
    if (onBcr != null) {
      if (typeof onBcr !== 'function') {
        throw new TypeError('onBcr must be a function');
      }
      this._bcrKeepalive.set(sn, onBcr);
      native.startDevice(sn, nativeParams, HIK_CR_BCR_SET, onBcr);
      return;
    }
    native.startDevice(sn, nativeParams, HIK_CR_BCR_KEEP, null);
  }

  /** 停流；已登记的 BCR 回调保留（与 Python/Go 行为一致）。 */
  stopDevice(sn) {
    native.stopDevice(sn);
  }

  /** 软触发（须已 startDevice 且处于取流）。 */
  triggerDevice(sn) {
    native.triggerDevice(sn);
  }

  /** 最近一次错误的线程局部信息。 */
  lastError() {
    return native.lastError();
  }
}

module.exports = {
  HikCodeReader,
  OpenParams,
  diagnoseNativeLoad,
  HIK_CR_OK,
  HIK_CR_ERR_UNKNOWN,
  HIK_CR_ERR_LOGIC,
  HIK_CR_ERR_RUNTIME,
  HIK_CR_ERR_INVALID_ARG,
  HIK_CR_ERR_NO_MEMORY,
  HIK_CR_BCR_KEEP,
  HIK_CR_BCR_SET,
  HIK_CR_BCR_CLEAR,
};
