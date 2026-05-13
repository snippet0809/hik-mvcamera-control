// Package hikcr 通过 cgo 调用 hik_code_reader C API。
// import 路径形如：github.com/snippet0809/hik-mvcamera-control/ffi/go/hikcr（与 ffi/go/go.mod 的 module 行一致）。
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

// Must match cgo-generated //export signature exactly (Go *C.char → char*, not const char* — otherwise gcc conflicts with _cgo_export.c).
extern void hikcrGoBcrShim(char *serial_utf8, char **codes, int code_count, void *user_data);

// Return //export shim as HikCrBcrCallback (Go cannot reference C.hikcrGoBcrShim; go#19837).
HikCrBcrCallback hikcr_wrap_bcr_shim(void) {
	return (HikCrBcrCallback)hikcrGoBcrShim;
}
*/
import "C"

import (
	"fmt"
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

func TriggerDevice(serial string) error {
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	return check(C.hik_cr_trigger_device(cs))
}

var bcrBySerial = map[string]func([]string){}

//export hikcrGoBcrShim
func hikcrGoBcrShim(serial *C.char, codes **C.char, count C.int, _ unsafe.Pointer) {
	sn := C.GoString(serial)
	fn := bcrBySerial[sn]
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

// RegisterBcrCallbackForSerial 为指定序列号注册 BCR 回调（在 SDK 线程调用，勿长时间阻塞）。
// 同一序列号再次注册会覆盖；传 nil 清除该序列号的回调。未注册序列号上的读码结果会被静默丢弃。
func RegisterBcrCallbackForSerial(serial string, fn func([]string)) error {
	if fn == nil {
		delete(bcrBySerial, serial)
	} else {
		bcrBySerial[serial] = fn
	}
	cs := C.CString(serial)
	defer C.free(unsafe.Pointer(cs))
	var cb C.HikCrBcrCallback
	if fn != nil {
		cb = C.hikcr_wrap_bcr_shim()
	}
	return check(C.hik_cr_register_bcr_callback_for_serial(cs, cb, nil))
}
