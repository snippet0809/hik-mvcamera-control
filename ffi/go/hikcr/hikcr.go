// Package hikcr：cgo 调用 hik_code_reader C API（与 C++ code_reader.h 对齐）。
package hikcr

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include
#include <stdlib.h>
#include "hik_code_reader/c_api.h"

extern void hikcrGoBcrShim(char *serial_utf8, char **codes, int code_count, void *user_data);

static HikCrBcrCallback hikcr_wrap_bcr_shim(void) {
	return (HikCrBcrCallback)hikcrGoBcrShim;
}
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"
)

const (
	BcrKeep  = int(C.HIK_CR_BCR_KEEP)
	BcrSet   = int(C.HIK_CR_BCR_SET)
	BcrClear = int(C.HIK_CR_BCR_CLEAR)
)

// OpenParams 对应 HikCrOpenParams；字符串指针在 StartDevice 调用期间须保持有效（由本包拷贝到 C 栈上）。
type OpenParams struct {
	TriggerMode, TriggerSource *string
	Code128, QRCode             *bool // nil → 使用默认 true
}

func lastError() string {
	n := C.hik_cr_last_error_copy(nil, 0)
	if n <= 1 {
		return ""
	}
	buf := make([]byte, n)
	C.hik_cr_last_error_copy((*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)))
	for i, b := range buf {
		if b == 0 {
			buf = buf[:i]
			break
		}
	}
	return string(buf)
}

func check(r C.HikCrResult) error {
	if r == C.HIK_CR_OK {
		return nil
	}
	return fmt.Errorf("hik_cr %d: %s", int(r), lastError())
}

type DeviceInfo struct {
	SerialNumber string
	NetExportIP  string
	ModelName    string // 设备型号（MV-IDB*=读码器、MV-CU*=相机），用于区分读码器/相机
}

func EnumDevices() ([]DeviceInfo, error) {
	var arr *C.HikCrDeviceInfo
	var n C.int
	if err := check(C.hik_cr_enum_devices(&arr, &n)); err != nil {
		return nil, err
	}
	if arr == nil || n == 0 {
		return nil, nil
	}
	defer C.hik_cr_free_device_list(arr)
	raw := unsafe.Slice(arr, int(n))
	out := make([]DeviceInfo, 0, int(n))
	for _, d := range raw {
		out = append(out, DeviceInfo{
			SerialNumber: C.GoString((*C.char)(unsafe.Pointer(&d.serial_number[0]))),
			NetExportIP:  C.GoString((*C.char)(unsafe.Pointer(&d.net_export_ip[0]))),
			ModelName:    C.GoString((*C.char)(unsafe.Pointer(&d.model_name[0]))),
		})
	}
	return out, nil
}

// bcrBySerial：StartDevice（调用方 goroutine）写、hikcrGoBcrShim（SDK 抓图线程）读，
// 须用 bcrMu 保护，否则并发读写会 fatal panic。
var (
	bcrBySerial = map[string]func([]string){}
	bcrMu       sync.RWMutex
)

// StartDevice 起流。open==nil 表示全默认；bcrAction 为 BcrKeep/BcrSet/BcrClear；仅在 BcrSet 时需提供 bcrFn。
func StartDevice(serial string, open *OpenParams, bcrAction int, bcrFn func([]string)) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	var copen C.HikCrOpenParams
	var copenPtr *C.HikCrOpenParams
	var tm, ts *C.char
	if open != nil {
		copen.code128 = -1
		copen.qrcode = -1
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
		if open.Code128 != nil {
			v := C.int(0)
			if *open.Code128 {
				v = 1
			}
			copen.code128 = v
		}
		if open.QRCode != nil {
			v := C.int(0)
			if *open.QRCode {
				v = 1
			}
			copen.qrcode = v
		}
		copenPtr = &copen
	}
	var cb C.HikCrBcrCallback
	switch bcrAction {
	case BcrSet:
		if bcrFn == nil {
			return fmt.Errorf("BcrSet requires bcrFn")
		}
		bcrMu.Lock()
		bcrBySerial[serial] = bcrFn
		bcrMu.Unlock()
		cb = C.hikcr_wrap_bcr_shim()
	case BcrClear:
		bcrMu.Lock()
		delete(bcrBySerial, serial)
		bcrMu.Unlock()
	}
	return check(C.hik_cr_start_device(cs, copenPtr, C.int(bcrAction), cb, nil))
}

// StartDeviceSimple 全默认起流且不改动 BCR（等价 StartDevice(serial, nil, BcrKeep, nil)）。
func StartDeviceSimple(serial string) error {
	return StartDevice(serial, nil, BcrKeep, nil)
}

func StopDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_stop_device(cs))
}

func TriggerDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_trigger_device(cs))
}

// ParamKind 对应 HikCrParamType；ParamCommand 表示命令节点（无值）。
type ParamKind int

const (
	ParamInt     ParamKind = C.HIK_CR_PARAM_INT
	ParamFloat   ParamKind = C.HIK_CR_PARAM_FLOAT
	ParamBool    ParamKind = C.HIK_CR_PARAM_BOOL
	ParamEnum    ParamKind = C.HIK_CR_PARAM_ENUM
	ParamString  ParamKind = C.HIK_CR_PARAM_STRING
	ParamCommand ParamKind = C.HIK_CR_PARAM_COMMAND
)

// SetParam 写数值/枚举/命令参数；kind 为 ParamInt/ParamFloat/ParamBool/ParamEnum 时 value 对应
// int64/float64/bool/uint32；ParamCommand 忽略 value。设备须已 StartDevice。
func SetParam(serial, name string, kind ParamKind, value any) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	var cv C.HikCrParamValue
	switch kind {
	case ParamInt:
		cv.type = C.HIK_CR_PARAM_INT
		cv.i = C.int64_t(value.(int64))
	case ParamFloat:
		cv.type = C.HIK_CR_PARAM_FLOAT
		cv.f = C.double(value.(float64))
	case ParamBool:
		cv.type = C.HIK_CR_PARAM_BOOL
		if value.(bool) {
			cv.b = 1
		}
	case ParamEnum:
		cv.type = C.HIK_CR_PARAM_ENUM
		cv.e = C.uint32_t(value.(uint32))
	case ParamCommand:
		cv.type = C.HIK_CR_PARAM_COMMAND
	default:
		return fmt.Errorf("SetParam: invalid kind %d", kind)
	}
	return check(C.hik_cr_set_param(cs, cn, &cv))
}

// SetParamString 写字符串参数（含枚举 symbolic 值）；设备须已 StartDevice。
func SetParamString(serial, name, value string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	cv := C.CString(value)
	defer C.free(unsafe.Pointer(cv))
	return check(C.hik_cr_set_param_string(cs, cn, cv))
}

// GetParam 读数值/枚举参数，返回其类型与值；字符串节点请用 GetParamString。
func GetParam(serial, name string) (kind ParamKind, value any, err error) {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	cn := C.CString(name)
	defer C.free(unsafe.Pointer(cn))
	var cv C.HikCrParamValue
	if err := check(C.hik_cr_get_param(cs, cn, &cv)); err != nil {
		return 0, nil, err
	}
	switch cv.type {
	case C.HIK_CR_PARAM_INT:
		return ParamInt, int64(cv.i), nil
	case C.HIK_CR_PARAM_FLOAT:
		return ParamFloat, float64(cv.f), nil
	case C.HIK_CR_PARAM_BOOL:
		return ParamBool, cv.b != 0, nil
	case C.HIK_CR_PARAM_ENUM:
		return ParamEnum, uint32(cv.e), nil
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
	if err := check(C.hik_cr_get_param_string(cs, cn, (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)))); err != nil {
		return "", err
	}
	for i, b := range buf {
		if b == 0 {
			buf = buf[:i]
			break
		}
	}
	return string(buf), nil
}

//export hikcrGoBcrShim
func hikcrGoBcrShim(serial *C.char, codes **C.char, count C.int, _ unsafe.Pointer) {
	sn := C.GoString(serial)
	bcrMu.RLock()
	fn := bcrBySerial[sn]
	bcrMu.RUnlock()
	if fn == nil {
		return
	}
	n := int(count)
	if n <= 0 {
		fn(nil)
		return
	}
	ptrs := unsafe.Slice(codes, n)
	out := make([]string, n)
	for i, p := range ptrs {
		out[i] = C.GoString(p)
	}
	fn(out)
}
