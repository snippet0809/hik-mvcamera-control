//go:build cgo && (windows || linux) && amd64

// Command hikprobe 是设备验收探针。
//
// 它做两件事：
//  1. 枚举读码器与相机；找到 GigE 相机就起流、软触发、收帧。
//  2. 列出本进程已加载的非系统模块，标出各自来自 exe 目录还是别处。
//
// 第 2 步只做报告、不做判定——来源是否"正确"取决于部署方式：Windows 走
// 「使用方自装 MVS/IDMVS」，海康模块来自已装目录就是预期结果；Linux 走随包分发，
// 海康模块应落在 exe 旁边。两种情况下看一眼来源就知道实际用的是哪一套。
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/snippet0809/hik-mvcamera-control/hikcr"
	"github.com/snippet0809/hik-mvcamera-control/hikcv"
)

// netMode 选 GigE 传输方式：auto（SDK 默认，驱动）/ driver / socket（免过滤驱动）。
//
// 默认 auto：Windows 的既定部署方式是使用方自装 MVS，装了就带 GigE 过滤驱动，
// 走 SDK 默认的驱动模式才是现场实际会用的那条路。`socket` 留作诊断——
// 诊断「拿不到过滤驱动时到底能不能取流」时用得上（枚举成功不代表收得到帧）。
var netMode = flag.String("net", "auto", "GigE 传输模式: auto | driver | socket")

func netTransMode() (int, string) {
	switch *netMode {
	case "auto":
		return hikcv.NetTransAuto, "auto(SDK默认/驱动)"
	case "driver":
		return hikcv.NetTransDriver, "driver"
	case "socket":
		return hikcv.NetTransSocket, "socket(免过滤驱动)"
	default:
		fmt.Fprintf(os.Stderr, "未知 -net=%s\n", *netMode)
		os.Exit(2)
		return 0, ""
	}
}

func main() {
	flag.Parse()
	exe, _ := os.Executable()
	fmt.Printf("exe      : %s\n", exe)
	fmt.Printf("exe dir  : %s\n", filepath.Dir(exe))

	readers, err := hikcr.EnumDevices()
	if err != nil {
		fmt.Printf("读码器枚举失败: %v\n", err)
	}
	fmt.Printf("\n读码器 (%d):\n", len(readers))
	for _, d := range readers {
		fmt.Printf("  sn=%-16s ip=%-16s model=%s\n", d.SerialNumber, d.NetExportIP, d.ModelName)
	}

	cameras, err := hikcv.EnumDevices()
	if err != nil {
		fmt.Printf("相机枚举失败: %v\n", err)
	}
	fmt.Printf("\n相机 (%d):\n", len(cameras))
	for _, d := range cameras {
		fmt.Printf("  sn=%-16s ip=%-16s model=%s\n", d.SerialNumber, d.NetExportIP, d.ModelName)
	}

	if len(cameras) > 0 {
		probeCamera(cameras[0].SerialNumber)
	}

	dumpModules(filepath.Dir(exe))
}

// probeCamera 起流并软触发取几帧。传输模式由 -net 决定。
func probeCamera(serial string) {
	mode, label := netTransMode()
	fmt.Printf("\n=== 起流 sn=%s net=%s ===\n", serial, label)

	tm, ts := "On", "Software"
	var (
		mu     sync.Mutex
		frames int
	)
	start := time.Now()

	err := hikcv.StartDevice(serial, &hikcv.OpenParams{
		TriggerMode:   &tm,
		TriggerSource: &ts,
		NetTransMode:  mode,
	}, hikcv.FrameSet, func(fi hikcv.FrameInfo, data []byte) {
		// 回调来自 SDK 的抓图线程，主 goroutine 要读这个计数：必须加锁。
		// （曾是无同步的自增，Go 内存模型下主协程可能看不到最后一次自增。）
		mu.Lock()
		frames++
		n := frames
		mu.Unlock()
		if n <= 3 {
			fmt.Printf("  帧 #%d %dx%d pixel=0x%08x len=%d\n",
				fi.FrameNum, fi.Width, fi.Height, fi.PixelType, fi.FrameLen)
		}
	})
	if err != nil {
		fmt.Printf("  起流失败: %v\n", err)
		return
	}
	defer hikcv.StopDevice(serial)

	// 软触发间隔必须长于相机的采集周期：快于实际帧率的触发会被相机**直接丢弃**，
	// 于是「触发 3 次收到 2 帧」看着像丢帧，其实一切正常（实测：曝光 500ms →
	// ResultingFrameRate 1.656fps → 604ms/帧，300ms 间隔触发 10 次只出 5 帧，
	// 间隔 800ms 则 10/10）。所以这里按实际帧率算间隔，并把它打印出来。
	const triggers = 3
	interval, known := triggerInterval(serial)
	fmt.Printf("  采集节奏: %s；软触发间隔取 %v\n", cadence(serial), interval.Round(time.Millisecond))
	if !known {
		fmt.Println("  （读不到 ResultingFrameRate，按 300ms 兜底：若少帧，先怀疑触发快过采集）")
	}

	for i := 0; i < triggers; i++ {
		if err := hikcv.TriggerDevice(serial); err != nil {
			fmt.Printf("  触发失败: %v\n", err)
			return
		}
		time.Sleep(interval)
	}
	time.Sleep(interval)

	mu.Lock()
	got := frames
	mu.Unlock()

	fmt.Printf("  共收到 %d 帧（软触发 %d 次），用时 %v\n", got, triggers, time.Since(start).Round(time.Millisecond))
	if got < triggers {
		fmt.Printf("  注意：%d/%d。间隔已按实际帧率放宽，仍少帧才需要考虑链路问题。\n", got, triggers)
	}
}

// triggerInterval 依据相机的实际帧率算软触发间隔，留 30% 余量。
func triggerInterval(serial string) (time.Duration, bool) {
	const fallback = 300 * time.Millisecond
	if _, v, err := hikcv.GetParam(serial, "ResultingFrameRate"); err == nil {
		if fps, ok := v.(float64); ok && fps > 0 {
			d := time.Duration(float64(time.Second) / fps * 1.3)
			if d < fallback {
				return fallback, true
			}
			return d, true
		}
	}
	return fallback, false
}

// cadence 描述「相机最快多久能出一帧」，让人一眼看出触发间隔是怎么来的。
func cadence(serial string) string {
	var exposure any = "?"
	if _, v, err := hikcv.GetParam(serial, "ExposureTime"); err == nil {
		exposure = v
	}
	if _, v, err := hikcv.GetParam(serial, "ResultingFrameRate"); err == nil {
		return fmt.Sprintf("曝光 %v us，实际帧率 %v fps", exposure, v)
	}
	return fmt.Sprintf("曝光 %v us（实际帧率读不到）", exposure)
}
