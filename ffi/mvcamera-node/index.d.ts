/** TypeScript 类型声明：镜像 JS API（lib/index.js）。 */

/** 枚举到的相机设备。 */
export interface CameraDeviceInfo {
  serialNumber: string;
  netExportIp: string;
  modelName: string;
}

/** 起流前 GenICam 项；未填字段不修改。 */
export interface OpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  /** 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）。 */
  net_trans_mode?: number;
}

export class OpenParams implements OpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  constructor(opts?: OpenParamsLike);
  toNative(): OpenParamsLike;
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

export interface StartDeviceOptions {
  /** 起流前 GenICam 项。 */
  params?: OpenParamsLike | OpenParams | null;
  /** 图像回调：(serial, frameInfo, buffer) => void。 */
  onFrame?: FrameCallback | null;
  /** 清除该序列号已登记的图像回调（与 onFrame 互斥）。 */
  clearFrame?: boolean;
}

export class HikCamera {
  constructor();
  /** 枚举相机；无相机时返回 []。 */
  enumDevices(): CameraDeviceInfo[];
  /** 起流；已在取流时忽略 params，仅按 onFrame/clearFrame 更新图像回调。 */
  startDevice(sn: string, opts?: StartDeviceOptions): void;
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

export interface NativeLoadDiagnostics {
  nativeDir: string;
  bundledDlls: string[];
  searchPathPrefix: string[];
  addonLoaded: boolean;
}

export function diagnoseNativeLoad(): NativeLoadDiagnostics;

export const HIK_CV_OK: number;
export const HIK_CV_ERR_UNKNOWN: number;
export const HIK_CV_ERR_LOGIC: number;
export const HIK_CV_ERR_RUNTIME: number;
export const HIK_CV_ERR_INVALID_ARG: number;
export const HIK_CV_ERR_NO_MEMORY: number;
export const HIK_CV_FRAME_KEEP: number;
export const HIK_CV_FRAME_SET: number;
export const HIK_CV_FRAME_CLEAR: number;
