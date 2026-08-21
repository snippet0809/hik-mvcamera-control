/** TypeScript 类型声明：镜像 JS API（lib/index.js）。统一包同时导出读码器（HikCodeReader）与相机（HikCamera）。 */

// ---------------------------------------------------------------------------
// 读码器（hik_cr_*）
// ---------------------------------------------------------------------------

/** 枚举到的读码器设备。 */
export interface DeviceInfo {
  serialNumber: string;
  netExportIp: string;
}

/** 读码器起流前 GenICam 项；未填字段走 C++ 默认。 */
export interface ReaderOpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  code128?: boolean;
  qrcode?: boolean;
}

export class ReaderOpenParams implements ReaderOpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  code128?: boolean;
  qrcode?: boolean;
  constructor(opts?: ReaderOpenParamsLike);
  toNative(): ReaderOpenParamsLike;
}

export type BcrCallback = (serial: string, codes: string[]) => void;

export interface ReaderStartDeviceOptions {
  /** 起流前 GenICam 项。 */
  params?: ReaderOpenParamsLike | ReaderOpenParams | null;
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
  startDevice(sn: string, opts?: ReaderStartDeviceOptions): void;
  /** 停流；已登记的 BCR 回调保留。 */
  stopDevice(sn: string): void;
  /** 软触发（须已 startDevice 且处于取流）。 */
  triggerDevice(sn: string): void;
  /** 最近一次错误的线程局部信息。 */
  lastError(): string;
}

// ---------------------------------------------------------------------------
// 相机（hik_cv_*）
// ---------------------------------------------------------------------------

/** 枚举到的相机设备。 */
export interface CameraDeviceInfo {
  serialNumber: string;
  netExportIp: string;
  modelName: string;
}

/** 相机起流前 GenICam 项；未填字段不修改。 */
export interface CameraOpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  /** 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）。 */
  net_trans_mode?: number;
}

export class CameraOpenParams implements CameraOpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  /** 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）。 */
  net_trans_mode?: number;
  constructor(opts?: CameraOpenParamsLike);
  toNative(): CameraOpenParamsLike;
}

/** 单帧元数据（配合 onFrame 的 buffer）。 */
export interface FrameInfo {
  width: number;
  height: number;
  pixelType: number;
  frameLen: number;
  frameNum: number;
  hostTimestamp: number;
}

export type FrameCallback = (serial: string, info: FrameInfo, buffer: Buffer) => void;

export interface CameraStartDeviceOptions {
  /** 起流前 GenICam 项。 */
  params?: CameraOpenParamsLike | CameraOpenParams | null;
  /** 图像回调：(serial, frameInfo, buffer) => void。 */
  onFrame?: FrameCallback | null;
  /** 清除该序列号已登记的图像回调（与 onFrame 互斥）。 */
  clearFrame?: boolean;
}

export class HikCamera {
  constructor();
  /** 枚举相机（GigE + USB）；无相机时返回 []。 */
  enumDevices(): CameraDeviceInfo[];
  /** 起流；已在取流时忽略 params，仅按 onFrame/clearFrame 更新图像回调。 */
  startDevice(sn: string, opts?: CameraStartDeviceOptions): void;
  /** 停流；已登记的图像回调保留。 */
  stopDevice(sn: string): void;
  /** 软触发（须已 startDevice、处于取流且 TriggerMode=On）。 */
  triggerDevice(sn: string): void;
  /** 按 GenICam 节点名写参数；value 支持 number/boolean/string。 */
  setParam(sn: string, name: string, value: number | boolean | string): void;
  /** 按 GenICam 节点名读参数 → number | boolean | string。 */
  getParam(sn: string, name: string): number | boolean | string;
  /** 临时强制 GigE 相机 IP（重启恢复，不改持久配置）。 */
  forceIp(sn: string, ip: string, subnetMask?: string, gateway?: string): void;
  /** 最近一次错误的线程局部信息。 */
  lastError(): string;
}

// ---------------------------------------------------------------------------
// 通用
// ---------------------------------------------------------------------------

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

export const HIK_CV_OK: number;
export const HIK_CV_ERR_UNKNOWN: number;
export const HIK_CV_ERR_LOGIC: number;
export const HIK_CV_ERR_RUNTIME: number;
export const HIK_CV_ERR_INVALID_ARG: number;
export const HIK_CV_ERR_NO_MEMORY: number;
export const HIK_CV_FRAME_KEEP: number;
export const HIK_CV_FRAME_SET: number;
export const HIK_CV_FRAME_CLEAR: number;
