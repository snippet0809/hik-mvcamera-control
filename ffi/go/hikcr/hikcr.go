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
