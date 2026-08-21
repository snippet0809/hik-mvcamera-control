/** TypeScript 类型声明：镜像 JS API（lib/index.js）。 */

/** 枚举到的读码器设备。 */
export interface DeviceInfo {
  serialNumber: string;
  netExportIp: string;
}

/** 起流前 GenICam 项；未填字段走 C++ 默认。 */
export interface OpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  code128?: boolean;
  qrcode?: boolean;
}

export class OpenParams implements OpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  code128?: boolean;
  qrcode?: boolean;
  constructor(opts?: OpenParamsLike);
  toNative(): OpenParamsLike;
}

export type BcrCallback = (serial: string, codes: string[]) => void;

export interface StartDeviceOptions {
  /** 起流前 GenICam 项。 */
  params?: OpenParamsLike | OpenParams | null;
  /** 注册 BCR 回调：(serial, codes[]) => void。 */
  onBcr?: BcrCallback | null;
  /** 清除该序列号已登记的 BCR 回调（与 onBcr 互斥）。 */
  clearBcr?: boolean;
}

export class HikCodeReader {
  constructor();
  /** 枚举设备；无读码器时返回 []。 */
  enumDevices(): DeviceInfo[];
  /** 起流；已在取流时忽略 params，仅按 onBcr/clearBcr 更新 BCR。 */
  startDevice(sn: string, opts?: StartDeviceOptions): void;
  /** 停流；已登记的 BCR 回调保留。 */
  stopDevice(sn: string): void;
  /** 软触发（须已 startDevice 且处于取流）。 */
  triggerDevice(sn: string): void;
  /** 最近一次错误的线程局部信息。 */
  lastError(): string;
}

export interface NativeLoadDiagnostics {
  nativeDir: string;
  bundledDlls: string[];
  searchPathPrefix: string[];
  addonLoaded: boolean;
}

export function diagnoseNativeLoad(): NativeLoadDiagnostics;

export const HIK_CR_OK: number;
export const HIK_CR_ERR_UNKNOWN: number;
export const HIK_CR_ERR_LOGIC: number;
export const HIK_CR_ERR_RUNTIME: number;
export const HIK_CR_ERR_INVALID_ARG: number;
export const HIK_CR_ERR_NO_MEMORY: number;
export const HIK_CR_BCR_KEEP: number;
export const HIK_CR_BCR_SET: number;
export const HIK_CR_BCR_CLEAR: number;
