// Package hikcr 通过 cgo 调用 hik_code_reader C API。
// import 路径形如：github.com/<你的GitHub>/<仓库名>/ffi/go/hikcr（与 ffi/go/go.mod 的 module 行一致）。
//
// 构建示例（按实际路径修改）：
//
//	# PowerShell（cgo 的 CC 请用 MinGW gcc，勿设 cl；见 https://go.dev/issue/20982）
//	$env:CGO_CFLAGS="-I$PWD/include"
//	$env:CGO_LDFLAGS="-L$PWD/build -lhik_code_reader"
//	go build ./ffi/go/hikcr
package hikcr

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include

#include <stdlib.h>
#include "hik_code_reader/c_api.h"
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"
)

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

// DeviceInfo 对应 HikCrDeviceInfo。
type DeviceInfo struct {
	SerialNumber string
	NetExportIP  string
}

// EnumDevices 枚举 GigE 读码器。
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
		})
	}
	return out, nil
}

func StartDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_start_device(cs))
}

func StopDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_stop_device(cs))
}

func OpenDeviceForParameters(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_open_device_for_parameters(cs))
}

func SetIP(serial, ip, mask, gateway string) error {
	cs := C.CString(serial)
	cip := C.CString(ip)
	cm := C.CString(mask)
	cg := C.CString(gateway)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(cip))
	defer C.free(unsafe.Pointer(cm))
	defer C.free(unsafe.Pointer(cg))
	return check(C.hik_cr_set_ip(cs, cip, cm, cg))
}

func TriggerDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_trigger_device(cs))
}

var (
	bcrMu sync.Mutex
	bcrFn func([]string)
)

//export hikcrGoBcrShim
func hikcrGoBcrShim(codes **C.char, count C.int, _ unsafe.Pointer) {
	bcrMu.Lock()
	fn := bcrFn
	bcrMu.Unlock()
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

// RegisterBcrCallback 注册 BCR 回调（在 SDK 线程调用，勿长时间阻塞）。
// 传 nil 可清除回调。
func RegisterBcrCallback(fn func([]string)) error {
	bcrMu.Lock()
	bcrFn = fn
	bcrMu.Unlock()
	var cb C.HikCrBcrCallback
	if fn != nil {
		cb = C.hikcrGoBcrShim
	}
	return check(C.hik_cr_register_bcr_callback(cb, nil))
}

func SetIntValue(serial, key string, value int32) error {
	cs := C.CString(serial)
	ck := C.CString(key)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(ck))
	return check(C.hik_cr_set_int_value(cs, ck, C.int32_t(value)))
}

func SetStringValue(serial, key, value string) error {
	cs := C.CString(serial)
	ck := C.CString(key)
	cv := C.CString(value)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(ck))
	defer C.free(unsafe.Pointer(cv))
	return check(C.hik_cr_set_string_value(cs, ck, cv))
}

func SetBoolValue(serial, key string, v bool) error {
	cs := C.CString(serial)
	ck := C.CString(key)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(ck))
	nv := C.int32_t(0)
	if v {
		nv = 1
	}
	return check(C.hik_cr_set_bool_value(cs, ck, nv))
}

func SetFloatValue(serial, key string, value float32) error {
	cs := C.CString(serial)
	ck := C.CString(key)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(ck))
	return check(C.hik_cr_set_float_value(cs, ck, C.float(value)))
}

func SetEnumValue(serial, key string, value uint32) error {
	cs := C.CString(serial)
	ck := C.CString(key)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(ck))
	return check(C.hik_cr_set_enum_value(cs, ck, C.uint32_t(value)))
}

func SetEnumValueByString(serial, key, symbolic string) error {
	cs := C.CString(serial)
	ck := C.CString(key)
	cs2 := C.CString(symbolic)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(ck))
	defer C.free(unsafe.Pointer(cs2))
	return check(C.hik_cr_set_enum_value_by_string(cs, ck, cs2))
}
