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

/** 读码成功帧的元数据（不含图像数据；数据经回调的 buffer 传递）。 */
export interface ReaderFrameInfo {
  width: number;
  height: number;
  /** MvCodeReaderGvspPixelType；读码器帧通常已是 JPEG。 */
  pixelType: number;
  frameLen: number;
  frameNum: number;
}

/** 最近一次 BCR 成功帧的图像副本。 */
export interface BcrImage {
  width: number;
  height: number;
  /** MvCodeReaderGvspPixelType；读码器帧通常已是 JPEG。 */
  pixelType: number;
  buffer: Buffer;
}

export type ReaderFrameCallback = (serial: string, info: ReaderFrameInfo, buffer: Buffer) => void;

export interface ReaderFrameCallbackOptions {
  /** 读码成功帧回调：(serial, frameInfo, buffer) => void。 */
  onFrame?: ReaderFrameCallback | null;
  /** 清除该序列号已登记的帧回调（与 onFrame 互斥）。 */
  clearFrame?: boolean;
}

export class HikCodeReader {
  constructor();
  /** 枚举设备；无读码器时返回 []。 */
  enumDevices(): DeviceInfo[];
  /** 起流；已在取流时忽略 params，仅按 onBcr/clearBcr 更新 BCR。 */
  startDevice(sn: string, opts?: ReaderStartDeviceOptions): void;
  /** 停流；已登记的 BCR 回调保留。 */
  stopDevice(sn: string): void;
  /** 停流但保留连接（不 CloseDevice）；下次 startDevice 省掉重建句柄 + OpenDevice。 */
  stopGrabbing(sn: string): void;
  /** 软触发（须已 startDevice 且处于取流）。 */
  triggerDevice(sn: string): void;
  /** 按 GenICam 节点名写参数；value 支持 number/boolean/string。设备须已 startDevice。 */
  setParam(sn: string, name: string, value: number | boolean | string): void;
  /** 按 GenICam 节点名读参数 → number | boolean | string。设备须已 startDevice。 */
  getParam(sn: string, name: string): number | boolean | string;
  /** 执行 GenICam 命令节点（如 'TriggerSoftware'、'UserSetLoad'）。设备须已 startDevice。 */
  runCommand(sn: string, name: string): void;
  /** 取最近一次 BCR 成功帧的图像副本；该序列号尚未读到过条码时返回 null。 */
  getBcrImage(sn: string): BcrImage | null;
  /** 登记 / 清除读码成功帧回调（独立于 BCR，可在已取流时热替换）。 */
  setFrameCallback(sn: string, opts?: ReaderFrameCallbackOptions): void;
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
  /** >0 时起流前写 Width。 */
  width?: number;
  /** >0 时起流前写 Height（线阵相机：每帧行数）。 */
  height?: number;
}

export class CameraOpenParams implements CameraOpenParamsLike {
  trigger_mode?: string;
  trigger_source?: string;
  /** 0=不设置（SDK 默认驱动模式）; 1=驱动; 2=socket（免 GigE 过滤驱动）。 */
  net_trans_mode?: number;
  /** >0 时起流前写 Width。 */
  width?: number;
  /** >0 时起流前写 Height（线阵相机：每帧行数）。 */
  height?: number;
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
  /**
   * 起流。
   *
   * 已在取流时：忽略 `params` 并直接返回。
   *
   * **但取流中不能登记/更换图像回调** —— 海康要求 `MV_CC_RegisterImageCallBackEx` 在
   * `StartGrabbing` **之前**调用（见 `MvCameraControl.h` 的 @remarks），取流中改会被 SDK
   * 拒为 `MV_E_CALLORDER`。这是**厂商的时序约束**，不是可绕过的缺陷。故已在取流时若传了
   * `onFrame`/`clearFrame`，本方法抛 `logic_error`；要换回调请先 `stopDevice` 再 `startDevice`。
   */
  startDevice(sn: string, opts?: CameraStartDeviceOptions): void;
  /** 停流；已登记的图像回调保留。 */
  stopDevice(sn: string): void;
  /** 软触发（须已 startDevice、处于取流且 TriggerMode=On）。 */
  triggerDevice(sn: string): void;
  /** 按 GenICam 节点名写参数；value 支持 number/boolean/string。 */
  setParam(sn: string, name: string, value: number | boolean | string): void;
  /** 按 GenICam 节点名读参数 → number | boolean | string。 */
  getParam(sn: string, name: string): number | boolean | string;
  /** 执行 GenICam 命令节点（如 'TriggerSoftware'、'UserSetLoad'）。设备须已 startDevice。 */
  runCommand(sn: string, name: string): void;
  /**
   * 把 onFrame 拿到的原始帧编码为 JPEG 字节 → Buffer（不落盘）。
   * @param quality JPEG 质量 (50,99]；省略或越界按 80
   * @param method  Bayer 插值 0-快速 1-均衡 2-最优 3-最优+；省略或越界按 1
   */
  encodeJpeg(sn: string, frameInfo: FrameInfo, frameBuffer: Buffer, quality?: number, method?: number): Buffer;
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
export const HIK_CR_FRAME_KEEP: number;
export const HIK_CR_FRAME_SET: number;
export const HIK_CR_FRAME_CLEAR: number;
/** 读码器字符串参数读取的建议缓冲长度。 */
export const HIK_CR_PARAM_STRING_MAX: number;

export const HIK_CV_OK: number;
export const HIK_CV_ERR_UNKNOWN: number;
export const HIK_CV_ERR_LOGIC: number;
export const HIK_CV_ERR_RUNTIME: number;
export const HIK_CV_ERR_INVALID_ARG: number;
export const HIK_CV_ERR_NO_MEMORY: number;
export const HIK_CV_FRAME_KEEP: number;
export const HIK_CV_FRAME_SET: number;
export const HIK_CV_FRAME_CLEAR: number;
