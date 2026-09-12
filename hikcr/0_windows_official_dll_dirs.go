//go:build windows

package hikcr

import (
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

// 注册 DLL 搜索目录，并前置 PATH。
//
// 必须先说清楚这个 init 能做什么、不能做什么：
//
//	cgo 把 hik_code_reader.dll 写进可执行文件的 PE 导入表，ntdll 在**任何 Go 代码
//	运行之前**（早于 _start 之后的全部 init）就解析完了它。所以本文件**无法**帮助
//	定位 hik_code_reader.dll 本身，也管不到它导入表里的那一层依赖。
//
// 它真正有用的是**海康 SDK 自己迟加载**的那一层：MVGigEVisionSDK.dll、
// MvFGProducer*.cti、MvCamLVision.dll 等由 SDK 运行时 LoadLibrary/dlopen，
// 那时这些目录已经在搜索路径里了。
//
// 结论（也是分发模型）：**所有 DLL 必须与可执行文件同目录**，或在该文件启动前
// 已在 PATH 上。runtime/windows-x86_64/bin/ 的整个内容就是按这个前提设计的——
// 把它整体拷到 exe 旁边即可，不需要安装 MVS/IDMVS。
//
// 顺序：exe 自身目录 → GENICAM_GENTL* / MVCAM_GENICAM_CLPROTOCOL（装了 MVS 才有，
// 作为兜底）→ Path 里含 mvs/idmvs 的项 → HIK_CODE_READER_DLL 所在目录。
//
// 文件名 0_ 前缀使本文件 init 在包内按字典序早于 hikcr.go、早于 _cgo_ 生成代码的 init。
func init() {
	dirs := windowsOfficialHikDllDirs()
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	procAddDllDirectory := kernel32.NewProc("AddDllDirectory")
	for _, dir := range dirs {
		addDllDirectory(procAddDllDirectory, dir)
	}
	// Python 侧（python/hik_code_reader/_dll_utils.py）是 AddDllDirectory 与 PATH 两者都做；
	// 这里补齐 PATH，否则通过 PATH 搜索的加载路径看不到这些目录。
	prependPath(dirs)
}

func prependPath(dirs []string) {
	if len(dirs) == 0 {
		return
	}
	cur := os.Getenv("PATH")
	joined := strings.Join(dirs, string(os.PathListSeparator))
	if cur != "" {
		joined += string(os.PathListSeparator) + cur
	}
	_ = os.Setenv("PATH", joined)
}

// exeDirIfAny 返回可执行文件所在目录——分发模型里最要紧的那个目录。
func exeDirIfAny() []string {
	exe, err := os.Executable()
	if err != nil {
		return nil
	}
	return []string{filepath.Dir(exe)}
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
	r, _, _ := proc.Call(uintptr(unsafe.Pointer(p)))
	if r == 0 {
		return
	}
}

func is64BitArch() bool {
	return unsafe.Sizeof(uintptr(0)) == 8
}

func envDirIfExists(name string) []string {
	v := strings.TrimSpace(os.Getenv(name))
	if v == "" {
		return nil
	}
	st, err := os.Stat(v)
	if err != nil || !st.IsDir() {
		return nil
	}
	return []string{v}
}

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

func windowsOfficialHikDllDirs() []string {
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

	// exe 自身目录在最前：随包运行时（runtime/windows-x86_64/bin/）就是拷到这里，
	// 也是唯一不依赖使用者装过任何东西的位置。
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
	// 与 Python 一致：`hik_code_reader.dll` 所在目录在官方目录与 Path 启发式之后追加
	if v := strings.TrimSpace(os.Getenv("HIK_CODE_READER_DLL")); v != "" {
		if d := filepath.Dir(v); d != "." && d != "" {
			push(d)
		}
	}
	return ordered
}
