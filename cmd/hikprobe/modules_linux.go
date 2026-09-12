//go:build cgo && linux && amd64

package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// loadedModules 从 /proc/self/maps 取本进程映射的共享库路径（去重、保序）。
func loadedModules() []string {
	f, err := os.Open("/proc/self/maps")
	if err != nil {
		return nil
	}
	defer f.Close()

	seen := make(map[string]struct{})
	var out []string
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		i := strings.IndexByte(line, '/')
		if i < 0 {
			continue
		}
		path := strings.TrimSpace(line[i:])
		if !strings.HasSuffix(path, ".so") && !strings.Contains(path, ".so.") {
			continue
		}
		if _, ok := seen[path]; ok {
			continue
		}
		seen[path] = struct{}{}
		out = append(out, path)
	}
	return out
}

// dumpModules 打印所有既不在 exe 目录、也不在系统库目录下的已加载 .so。
//
// 期望结果：海康相关模块**全部**落在 exe 旁边（DT_RPATH=$ORIGIN 解析到的位置）。
// 只要有一个来自 /opt/GenICam_v3_0、或运行机器上另行安装的 MVS，验收就不成立。
func dumpModules(exeDir string) {
	mods := loadedModules()
	if len(mods) == 0 {
		fmt.Println("\n（无法读取 /proc/self/maps，跳过这一步）")
		return
	}

	exeAbs, _ := filepath.Abs(exeDir)
	sysPrefixes := []string{"/lib/", "/lib64/", "/usr/lib/", "/usr/local/lib/"}

	fmt.Printf("\n=== 已加载共享库（共 %d，仅列非系统） ===\n", len(mods))
	var leaked []string
	for _, m := range mods {
		sys := false
		for _, p := range sysPrefixes {
			if strings.HasPrefix(m, p) {
				sys = true
				break
			}
		}
		if sys {
			continue
		}
		// /proc/self/maps 里的路径可能是软链或带 " (deleted)" 后缀。
		clean := strings.TrimSuffix(m, " (deleted)")
		if resolved, err := filepath.EvalSymlinks(clean); err == nil {
			clean = resolved
		}
		mark := "ok  "
		if !strings.HasPrefix(clean, exeAbs) {
			mark = "!!  "
			leaked = append(leaked, m)
		}
		fmt.Printf("  %s%s\n", mark, m)
	}

	fmt.Println()
	if len(leaked) == 0 {
		fmt.Println("结果：exe 目录之外没有加载任何非系统库 —— 免安装验收通过。")
		return
	}
	fmt.Printf("结果：有 %d 个库来自 exe 目录之外 —— 免安装验收不成立：\n", len(leaked))
	for _, m := range leaked {
		fmt.Printf("  %s\n", m)
	}
	os.Exit(1)
}
