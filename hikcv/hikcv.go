//go:build cgo && (windows || linux) && amd64

// Package hikcv：cgo 调用 hik_mvcamera C API（与 C++ camera.h 对齐）。
//
// 头文件与库都来自随仓库提交的 runtime/（来源见 runtime/VERSION），使用方
// 无需安装 MVS/IDMVS。平台仅支持 windows/amd64 与 linux/amd64。
//
// 与 hikcr 分开成两个包，是为了不让只用读码器的人被迫一起链接 libhik_mvcamera。
package hikcv

/*
#cgo CFLAGS: -I${SRCDIR}/../runtime/include

// Windows：wrapper 与厂商 DLL 同放 bin/；-l 命中 lib/hik_mvcamera.lib（MSVC 导入库）。
#cgo windows LDFLAGS: -L${SRCDIR}/../runtime/windows-x86_64/lib -lhik_mvcamera

// Linux：wrapper 是 libhik_mvcamera.so，与厂商 .so 扁放在 lib/。
// --disable-new-dtags 强制 DT_RPATH（会被传递依赖继承，且优先于 LD_LIBRARY_PATH），
// 细节见 runtime 与 hikcr 的同名说明。
#cgo linux LDFLAGS: -L${SRCDIR}/../runtime/linux-x86_64/lib -lhik_mvcamera -Wl,-rpath,$$ORIGIN/runtime/linux-x86_64/lib -Wl,--disable-new-dtags

#include <stdlib.h>
#include "hik_mvcamera/c_api.h"

extern void hikcvGoFrameShim(char *serial_utf8, HikCvFrameInfo *info,
                             unsigned char *data, size_t len, void *user_data);

static HikCvFrameCallback hikcv_wrap_frame_shim(void) {
	return (HikCvFrameCallback)hikcvGoFrameShim;
}

// HikCvParamValue 的两个成员 cgo 都碰不到：`type` 是 Go 保留字（语法错误），
// i/f/b/e 在匿名 union 里（cgo 不支持）。用这组薄存取器绕开。
static void hikcv_pv_set_type(HikCvParamValue *v, int t)      { v->type = (HikCvParamType)t; }
static void hikcv_pv_set_int(HikCvParamValue *v, long long x) { v->i = (int64_t)x; }
static void hikcv_pv_set_float(HikCvParamValue *v, double x)  { v->f = x; }
static void hikcv_pv_set_bool(HikCvParamValue *v, int x)      { v->b = x; }
static void hikcv_pv_set_enum(HikCvParamValue *v, unsigned x) { v->e = (uint32_t)x; }

static int          hikcv_pv_get_type(const HikCvParamValue *v)  { return (int)v->type; }
static long long    hikcv_pv_get_int(const HikCvParamValue *v)   { return (long long)v->i; }
static double       hikcv_pv_get_float(const HikCvParamValue *v) { return v->f; }
static int          hikcv_pv_get_bool(const HikCvParamValue *v)  { return v->b; }
static unsigned int hikcv_pv_get_enum(const HikCvParamValue *v)  { return (unsigned int)v->e; }
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"

	// Windows 下先于本包 init 登记厂商 DLL 搜索目录（细节见该包文档）。
	_ "github.com/snippet0809/hik-mvcamera-control/internal/hikdll"
)

const (
	FrameKeep  = int(C.HIK_CV_FRAME_KEEP)
	FrameSet   = int(C.HIK_CV_FRAME_SET)
	FrameClear = int(C.HIK_CV_FRAME_CLEAR)
)

// GigE 传输模式（对应 HikCvOpenParams.net_trans_mode）。
const (
	// NetTransAuto 不设置，随 SDK 默认（驱动模式）。需要装 MVS 的 GigE 过滤驱动。
	NetTransAuto = 0
	// NetTransDriver 强制走过滤驱动。
	NetTransDriver = 1
	// NetTransSocket 走 socket，**免 GigE 过滤驱动**——随包分发时的默认选择。
	NetTransSocket = 2
)

// ParamKind 对应 HikCvParamType。
type ParamKind int

const (
	ParamInt     ParamKind = ParamKind(C.HIK_CV_PARAM_INT)
	ParamFloat   ParamKind = ParamKind(C.HIK_CV_PARAM_FLOAT)
	ParamBool    ParamKind = ParamKind(C.HIK_CV_PARAM_BOOL)
	ParamEnum    ParamKind = ParamKind(C.HIK_CV_PARAM_ENUM)
	ParamString  ParamKind = ParamKind(C.HIK_CV_PARAM_STRING)
	ParamCommand ParamKind = ParamKind(C.HIK_CV_PARAM_COMMAND)
)

// OpenParams 对应 HikCvOpenParams。字段指针为 nil 表示不修改该 GenICam 节点。
//
// 想免装 MVS，请把 NetTransMode 设为 NetTransSocket；零值 NetTransAuto 会沿用
// SDK 默认（驱动模式），在没有过滤驱动的机器上可能取不到流。
type OpenParams struct {
	TriggerMode, TriggerSource *string
	NetTransMode               int
	Width, Height              int // >0 时起流前写入；线阵相机 Height 是每帧行数
}

type DeviceInfo struct {
	SerialNumber string
	NetExportIP  string // 仅 GigE 有；USB 为空
	ModelName    string
}

// FrameInfo 对应 HikCvFrameInfo。
type FrameInfo struct {
	Width         uint32
	Height        uint32
	PixelType     uint32
	FrameLen      uint32
	FrameNum      uint32
	HostTimestamp uint64
}

// FrameCallback 由 SDK 抓图线程调用。data 指向 SDK 缓冲，**仅回调期内有效**，
// 需要留存必须同步拷贝。
type FrameCallback func(info FrameInfo, data []byte)

func lastError() string {
	n := C.hik_cv_last_error_copy(nil, 0)
	if n <= 1 {
		return ""
	}
	buf := make([]byte, n)
	C.hik_cv_last_error_copy((*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)))
	for i, b := range buf {
		if b == 0 {
			buf = buf[:i]
			break
		}
	}
	return string(buf)
}

func check(r C.HikCvResult) error {
	if r == C.HIK_CV_OK {
		return nil
	}
	return fmt.Errorf("hik_cv %d: %s", int(r), lastError())
}

func EnumDevices() ([]DeviceInfo, error) {
	var arr *C.HikCvDeviceInfo
	var n C.int
	if err := check(C.hik_cv_enum_devices(&arr, &n)); err != nil {
		return nil, err
	}
	if arr == nil || n == 0 {
		return nil, nil
	}
	defer C.hik_cv_free_device_list(arr)
	out := make([]DeviceInfo, 0, int(n))
	for _, d := range unsafe.Slice(arr, int(n)) {
		out = append(out, DeviceInfo{
			SerialNumber: C.GoString((*C.char)(unsafe.Pointer(&d.serial_number[0]))),
			NetExportIP:  C.GoString((*C.char)(unsafe.Pointer(&d.net_export_ip[0]))),
			ModelName:    C.GoString((*C.char)(unsafe.Pointer(&d.model_name[0]))),
		})
	}
	return out, nil
}

// frameBySerial：StartDevice（调用方 goroutine）写、hikcvGoFrameShim（SDK 抓图线程）读，
// 须用 frameMu 保护，否则并发读写会 fatal panic。
var (
	frameBySerial = map[string]FrameCallback{}
	frameMu       sync.RWMutex
)

// StartDevice 起流。open==nil 表示不写 TriggerMode/TriggerSource；已在取流时忽略 open，
// 仅按 frameAction 更新回调。frameAction 取 FrameKeep/FrameSet/FrameClear，仅在 FrameSet 时需给 fn。
func StartDevice(serial string, open *OpenParams, frameAction int, fn FrameCallback) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))

	var copen C.HikCvOpenParams
	var copenPtr *C.HikCvOpenParams
	var tm, ts *C.char
	if open != nil {
		if open.TriggerMode != nil {
			tm = C.CString(*open.TriggerMode)
			defer C.free(unsafe.Pointer(tm))
			copen.trigger_mode = tm
		}
		if open.TriggerSource != nil {
			ts = C.CString(*open.TriggerSource)
			defer C.free(unsafe.Pointer(ts))
			copen.trigger_source = ts
		}
		copen.net_trans_mode = C.int(open.NetTransMode)
		copen.width = C.int(open.Width)
		copen.height = C.int(open.Height)
		copenPtr = &copen
	}

	// 回调表须在 SDK 可能调用之前登记；FrameSet 时先写表再起流。
	switch frameAction {
	case FrameKeep:
		// 不动
	case FrameSet:
		if fn == nil {
			return fmt.Errorf("FrameSet requires fn")
		}
		frameMu.Lock()
		frameBySerial[serial] = fn
		frameMu.Unlock()
	case FrameClear:
		frameMu.Lock()
		delete(frameBySerial, serial)
		frameMu.Unlock()
	default:
		return fmt.Errorf("StartDevice: invalid frameAction %d", frameAction)
	}

	var cb C.HikCvFrameCallback
	if frameAction == FrameSet {
		cb = C.hikcv_wrap_frame_shim()
	}
	return check(C.hik_cv_start_device(cs, copenPtr, C.int(frameAction), cb, nil))
}

// StopDevice 停流并释放设备（下次起流要重建句柄 + OpenDevice）。
func StopDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cv_stop_device(cs))
}

// TriggerDevice 软触发一次（须 TriggerMode=On、TriggerSource=Software）。
func TriggerDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cv_trigger_device(cs))
}

// ForceIP 临时强制 GigE 相机 IP（重启后恢复，不改持久配置）。
// 相机出厂 IP（常见 192.168.1.10/24）不在网卡子网内、枚举不到时，用它带外补上。
func ForceIP(serial, ip, subnetMask, gateway string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cip := C.CString(ip)
	defer C.free(unsafe.Pointer(cip))
	cmask := C.CString(subnetMask)
	defer C.free(unsafe.Pointer(cmask))
	cgw := C.CString(gateway)
	defer C.free(unsafe.Pointer(cgw))
	return check(C.hik_cv_force_ip(cs, cip, cmask, cgw))
}

// SetParam 写数值/枚举/命令参数；字符串节点请用 SetParamString。
func SetParam(serial, name string, kind ParamKind, value any) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	var cv C.HikCvParamValue
	switch kind {
	case ParamInt:
		C.hikcv_pv_set_type(&cv, C.HIK_CV_PARAM_INT)
		v, ok := value.(int64)
		if !ok {
			return fmt.Errorf("SetParam: ParamInt 需要 int64，得到 %T", value)
		}
		C.hikcv_pv_set_int(&cv, C.longlong(v))
	case ParamFloat:
		C.hikcv_pv_set_type(&cv, C.HIK_CV_PARAM_FLOAT)
		v, ok := value.(float64)
		if !ok {
			return fmt.Errorf("SetParam: ParamFloat 需要 float64，得到 %T", value)
		}
		C.hikcv_pv_set_float(&cv, C.double(v))
	case ParamBool:
		C.hikcv_pv_set_type(&cv, C.HIK_CV_PARAM_BOOL)
		v, ok := value.(bool)
		if !ok {
			return fmt.Errorf("SetParam: ParamBool 需要 bool，得到 %T", value)
		}
		if v {
			C.hikcv_pv_set_bool(&cv, 1)
		}
	case ParamEnum:
		C.hikcv_pv_set_type(&cv, C.HIK_CV_PARAM_ENUM)
		v, ok := value.(uint32)
		if !ok {
			return fmt.Errorf("SetParam: ParamEnum 需要 uint32，得到 %T", value)
		}
		C.hikcv_pv_set_enum(&cv, C.uint(v))
	case ParamCommand:
		C.hikcv_pv_set_type(&cv, C.HIK_CV_PARAM_COMMAND)
	default:
		return fmt.Errorf("SetParam: invalid kind %d", kind)
	}
	return check(C.hik_cv_set_param(cs, cn, &cv))
}

// SetParamString 写字符串参数（含枚举 symbolic 值）。
func SetParamString(serial, name, value string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	cv := C.CString(value)
	defer C.free(unsafe.Pointer(cv))
	return check(C.hik_cv_set_param_string(cs, cn, cv))
}

// GetParam 读数值/枚举参数；字符串节点请用 GetParamString。
func GetParam(serial, name string) (kind ParamKind, value any, err error) {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	var cv C.HikCvParamValue
	if err := check(C.hik_cv_get_param(cs, cn, &cv)); err != nil {
		return 0, nil, err
	}
	switch C.hikcv_pv_get_type(&cv) {
	case C.HIK_CV_PARAM_INT:
		return ParamInt, int64(C.hikcv_pv_get_int(&cv)), nil
	case C.HIK_CV_PARAM_FLOAT:
		return ParamFloat, float64(C.hikcv_pv_get_float(&cv)), nil
	case C.HIK_CV_PARAM_BOOL:
		return ParamBool, C.hikcv_pv_get_bool(&cv) != 0, nil
	case C.HIK_CV_PARAM_ENUM:
		return ParamEnum, uint32(C.hikcv_pv_get_enum(&cv)), nil
	default:
		return ParamString, "", fmt.Errorf("GetParam: string 节点请用 GetParamString")
	}
}

// GetParamString 读字符串参数。
func GetParamString(serial, name string) (string, error) {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	buf := make([]byte, 256)
	if err := check(C.hik_cv_get_param_string(cs, cn, (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)))); err != nil {
		return "", err
	}
	for i, b := range buf {
		if b == 0 {
			return string(buf[:i]), nil
		}
	}
	return string(buf), nil
}

//export hikcvGoFrameShim
func hikcvGoFrameShim(serialUTF8 *C.char, info *C.HikCvFrameInfo, data *C.uchar, length C.size_t, userData unsafe.Pointer) {
	if info == nil {
		return
	}
	frameMu.RLock()
	fn := frameBySerial[C.GoString(serialUTF8)]
	frameMu.RUnlock()
	if fn == nil {
		return
	}
	fi := FrameInfo{
		Width:         uint32(info.width),
		Height:        uint32(info.height),
		PixelType:     uint32(info.pixel_type),
		FrameLen:      uint32(info.frame_len),
		FrameNum:      uint32(info.frame_num),
		HostTimestamp: uint64(info.host_timestamp),
	}
	// data 仅回调期内有效，这里只包一层切片交给调用方；调用方要留存须自行拷贝。
	var buf []byte
	if data != nil && length > 0 {
		buf = unsafe.Slice((*byte)(unsafe.Pointer(data)), int(length))
	}
	fn(fi, buf)
}
