//go:build cgo && windows && amd64

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

const (
	th32csSnapModule   = 0x00000008
	th32csSnapModule32 = 0x00000010
	invalidHandleValue = ^uintptr(0)
)

// moduleEntry32 对应 Windows 的 MODULEENTRY32。
// 5 个 DWORD 之后是 modBaseAddr（指针），Go 会自动插入 4 字节对齐填充，
// 与 x64 下的 C 布局一致——这是能直接喂给 kernel32 的前提。
type moduleEntry32 struct {
	Size         uint32
	ModuleID     uint32
	ProcessID    uint32
	GlblcntUsage uint32
	ProccntUsage uint32
	ModBaseAddr  uintptr
	ModBaseSize  uint32
	ModuleHandle uintptr
	SzModule     [256]uint16
	SzExePath    [260]uint16
}

func utf16ToString(p []uint16) string {
	for i, c := range p {
		if c == 0 {
			return syscall.UTF16ToString(p[:i])
		}
	}
	return syscall.UTF16ToString(p)
}

// loadedModules 返回本进程已加载的全部模块路径。
func loadedModules() []string {
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	procCreate := kernel32.NewProc("CreateToolhelp32Snapshot")
	procFirst := kernel32.NewProc("Module32FirstW")
	procNext := kernel32.NewProc("Module32NextW")

	snap, _, _ := procCreate.Call(uintptr(th32csSnapModule|th32csSnapModule32), 0)
	if snap == invalidHandleValue || snap == 0 {
		return nil
	}
	defer syscall.CloseHandle(syscall.Handle(snap))

	var me moduleEntry32
	me.Size = uint32(unsafe.Sizeof(me))
	r, _, _ := procFirst.Call(snap, uintptr(unsafe.Pointer(&me)))
	if r == 0 {
		return nil
	}
	var out []string
	for {
		out = append(out, utf16ToString(me.SzExePath[:]))
		r, _, _ = procNext.Call(snap, uintptr(unsafe.Pointer(&me)))
		if r == 0 {
			break
		}
	}
	return out
}

// dumpModules 列出非系统模块并标出来源：exe 目录 / 别处。
//
// 只报告、不判定。Windows 现在走的部署方式是「使用方自行安装 MVS/IDMVS」，
// 海康模块来自 C:\Program Files (x86)\Common Files\MVS 或 ...\IDMVS\... 是**预期结果**；
// 若它们出现在 exe 目录里，说明用的是随包分发那份。两种都能跑，看一眼来源即可。
func dumpModules(exeDir string) {
	mods := loadedModules()
	if len(mods) == 0 {
		fmt.Println("\n（无法枚举已加载模块，跳过这一步）")
		return
	}

	sysRoot := strings.ToLower(os.Getenv("SystemRoot"))
	if sysRoot == "" {
		sysRoot = `c:\windows`
	}
	exeAbs, _ := filepath.Abs(exeDir)

	fmt.Printf("\n=== 已加载模块来源（共 %d，仅列非系统） ===\n", len(mods))
	var inExe, elsewhere int
	for _, m := range mods {
		low := strings.ToLower(m)
		if strings.HasPrefix(low, sysRoot) {
			continue // 系统目录：UCRT、kernel32、以及 System32 里的 VC++ 运行库
		}
		abs, _ := filepath.Abs(m)
		src := "其他位置"
		if strings.HasPrefix(strings.ToLower(abs), strings.ToLower(exeAbs)) {
			src = "exe 目录"
			inExe++
		} else {
			elsewhere++
		}
		fmt.Printf("  %-10s %s\n", src, m)
	}

	fmt.Printf("\n  exe 目录：%d 个；其他位置：%d 个\n", inExe, elsewhere)
	if elsewhere > 0 {
		fmt.Println("  （其他位置里的海康模块来自已安装的 MVS/IDMVS —— 这是 Windows 既定部署方式下的正常结果）")
	}
}
