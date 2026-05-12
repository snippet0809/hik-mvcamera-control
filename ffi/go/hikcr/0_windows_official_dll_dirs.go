//go:build windows

package hikcr

import (
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"unsafe"
)

// 在 cgo 加载 hik_code_reader.dll 及其海康依赖之前，按与 python/hik_code_reader 相同规则注册 DLL 搜索目录
// （GENICAM_GENTL64_PATH / GENICAM_GENTL32_PATH、MVCAM_GENICAM_CLPROTOCOL、Path 启发式、HIK_CODE_READER_DLL 所在目录）。
// 文件名 0_ 前缀使本文件 init 在包内按字典序早于 hikcr.go、早于 _cgo_ 生成代码的 init。
func init() {
	kernel32 := syscall.NewLazyDLL("kernel32.dll")
	procAddDllDirectory := kernel32.NewProc("AddDllDirectory")
	for _, dir := range windowsOfficialHikDllDirs() {
		addDllDirectory(procAddDllDirectory, dir)
	}
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
