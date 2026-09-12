//go:build windows

// Package hikdll：Windows 下为海康运行时登记 DLL 搜索目录。
//
// hikcr 与 hikcv 都空导入本包。Go 规定「被导入包的 init 先于导入者」，
// 而 cgo 生成的 _cgo_*.go 的 init 属于导入者包，所以本包的 init 必然先跑——
// 早先靠文件名 `0_` 前缀在包内排字典序的做法因此不再需要。
package hikdll

import (
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

// 先说清楚这个 init 能做什么、不能做什么：
//
//	cgo 把 hik_code_reader.dll / hik_mvcamera.dll 写进可执行文件的 PE 导入表，
//	ntdll 在**任何 Go 代码运行之前**就解析完了它们。所以这里**无法**帮助定位
//	wrapper DLL 自身，也管不到它导入表里的那一层依赖。
//
// 真正有用的是**海康 SDK 自己迟加载**的那一层：MVGigEVisionSDK.dll、
//	MvFGProducer*.cti、MvCamLVision.dll 等由 SDK 运行时 LoadLibrary/dlopen。
//
// 结论（也是分发模型）：**所有 DLL 必须与可执行文件同目录**，或在该文件启动前
// 已在 PATH 上。runtime/windows-x86_64/bin/ 的整个内容就是按这个前提设计的——
// 整体拷到 exe 旁边即可，不需要安装 MVS/IDMVS。
//
// 顺序：exe 自身目录 → GENICAM_GENTL* / MVCAM_GENICAM_CLPROTOCOL（装了 MVS 才有，
// 作兜底）→ Path 里含 mvs/idmvs 的项 → HIK_CODE_READER_DLL 所在目录。
func init() {
	dirs := officialDllDirs()
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	procAddDllDirectory := kernel32.NewProc("AddDllDirectory")
	for _, dir := range dirs {
		addDllDirectory(procAddDllDirectory, dir)
	}
	// Python 侧（python/hik_code_reader/_dll_utils.py）是 AddDllDirectory 与 PATH 两者都做；
	// 这里补齐 PATH，否则仅靠 PATH 搜索的加载路径看不到这些目录。
	prependPath(dirs)
}

func addDllDirectory(proc *syscall.LazyProc, dir string) {
	abs, err := filepath.Abs(dir)
	if err != nil {
		abs = dir
	}
	p, err := syscall.UTF16PtrFromString(abs)
	if err != nil {
		return
	}
	proc.Call(uintptr(unsafe.Pointer(p)))
}

func prependPath(dirs []string) {
	if len(dirs) == 0 {
		return
	}
	joined := strings.Join(dirs, string(os.PathListSeparator))
	if cur := os.Getenv("PATH"); cur != "" {
		joined += string(os.PathListSeparator) + cur
	}
	_ = os.Setenv("PATH", joined)
}

func is64BitArch() bool { return unsafe.Sizeof(uintptr(0)) == 8 }

// exeDirIfAny 返回可执行文件所在目录——分发模型里最要紧的那个位置。
func exeDirIfAny() []string {
	exe, err := os.Executable()
	if err != nil {
		return nil
	}
	return []string{filepath.Dir(exe)}
}

func envDirIfExists(name string) []string {
	v := strings.TrimSpace(os.Getenv(name))
	if v == "" {
		return nil
	}
	if st, err := os.Stat(v); err != nil || !st.IsDir() {
		return nil
	}
	return []string{v}
}

// pathEntriesHikMvs 挑出 PATH 里看着像海康安装目录的项，作为「装了 MVS/IDMVS」时的兜底。
func pathEntriesHikMvs() []string {
	p := os.Getenv("Path")
	if p == "" {
		p = os.Getenv("PATH")
	}
	var out []string
	for _, part := range strings.Split(p, string(os.PathListSeparator)) {
		part = strings.TrimSpace(strings.Trim(part, `"`))
		if part == "" {
			continue
		}
		low := strings.ToLower(part)
		if strings.Contains(low, `\mvs`) ||
			strings.Contains(low, `/mvs/`) ||
			strings.Contains(low, "idmvs") ||
			strings.Contains(low, "mvsdk") ||
			strings.Contains(low, "mvcode") {
			if st, err := os.Stat(part); err == nil && st.IsDir() {
				out = append(out, part)
			}
		}
	}
	return out
}

func officialDllDirs() []string {
	seen := make(map[string]struct{})
	var ordered []string
	push := func(s string) {
		abs, err := filepath.Abs(s)
		if err != nil {
			abs = s
		}
		if _, ok := seen[abs]; ok {
			return
		}
		seen[abs] = struct{}{}
		ordered = append(ordered, abs)
	}

	for _, d := range exeDirIfAny() {
		push(d)
	}
	if is64BitArch() {
		for _, d := range envDirIfExists("GENICAM_GENTL64_PATH") {
			push(d)
		}
	} else {
		for _, d := range envDirIfExists("GENICAM_GENTL32_PATH") {
			push(d)
		}
	}
	for _, d := range envDirIfExists("MVCAM_GENICAM_CLPROTOCOL") {
		push(d)
	}
	for _, d := range pathEntriesHikMvs() {
		push(d)
	}
	// 与 Python 一致：wrapper DLL 所在目录在官方目录与 Path 启发式之后追加。
	// 读码器与相机两个环境变量都认，任一存在即可（两包共用本文件）。
	for _, env := range []string{"HIK_CODE_READER_DLL", "HIK_MVCAMERA_DLL"} {
		if v := strings.TrimSpace(os.Getenv(env)); v != "" {
			if d := filepath.Dir(v); d != "." && d != "" {
				push(d)
			}
		}
	}
	return ordered
}
