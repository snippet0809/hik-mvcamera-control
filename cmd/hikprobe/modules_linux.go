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

// dumpModules 列出非系统共享库并标出来源：exe 目录 / 别处。
//
// 只报告、不判定。Linux 侧的厂商 .so 随仓库分发，由链接期写死的 DT_RPATH=$ORIGIN
// 解析，所以正常情况下都会落在 exe 目录之内（含 runtime/linux-x86_64/lib 子目录）；
// 若出现来自 /opt/GenICam_v3_0 或系统另装的 MVS，说明这台机器上另有一套抢先了。
func dumpModules(exeDir string) {
	mods := loadedModules()
	if len(mods) == 0 {
		fmt.Println("\n（无法读取 /proc/self/maps，跳过这一步）")
		return
	}

	exeAbs, _ := filepath.Abs(exeDir)
	sysPrefixes := []string{"/lib/", "/lib64/", "/usr/lib/", "/usr/local/lib/"}

	fmt.Printf("\n=== 已加载共享库来源（共 %d，仅列非系统） ===\n", len(mods))
	var inExe, elsewhere int
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
		src := "其他位置"
		if strings.HasPrefix(clean, exeAbs) {
			src = "exe 目录"
			inExe++
		} else {
			elsewhere++
		}
		fmt.Printf("  %-10s %s\n", src, m)
	}

	fmt.Printf("\n  exe 目录：%d 个；其他位置：%d 个\n", inExe, elsewhere)
	if elsewhere > 0 {
		fmt.Println("  （其余位置出现海康的库，说明本机另装了一套运行时并抢先被解析）")
	}
}
