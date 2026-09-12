//go:build cgo && (windows || linux) && amd64

// Command hikprobe 是「免装 MVS/IDMVS」的验收探针。
//
// 它做两件事：
//  1. 用随包的 runtime/ 枚举读码器与相机；找到 GigE 相机就 socket 模式起流、软触发、收帧。
//  2. 枚举本进程已加载的模块，打印任何既不在可执行文件目录、也不在系统目录下的模块。
//     第 2 步是「没有偷偷用到已安装的 MVS/IDMVS」的硬证据——只看第 1 步成功说明不了问题，
//     因为一台装了 MVS 的机器上怎么跑都会成功。
//
// 配合一个被清空 PATH 与海康环境变量的 shell 运行，才算真的验证。见 README「免安装验证」。
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/snippet0809/hik-mvcamera-control/hikcr"
	"github.com/snippet0809/hik-mvcamera-control/hikcv"
)

// netMode 选 GigE 传输方式：auto（SDK 默认，驱动）/ driver / socket（免过滤驱动）。
// 诊断「socket 模式到底能不能取流」时用得上——枚举成功不代表收得到帧。
var netMode = flag.String("net", "socket", "GigE 传输模式: auto | driver | socket")

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
	var frames int
	start := time.Now()

	err := hikcv.StartDevice(serial, &hikcv.OpenParams{
		TriggerMode:   &tm,
		TriggerSource: &ts,
		NetTransMode:  mode,
	}, hikcv.FrameSet, func(fi hikcv.FrameInfo, data []byte) {
		frames++
		if frames <= 3 {
			fmt.Printf("  帧 #%d %dx%d pixel=0x%08x len=%d\n",
				fi.FrameNum, fi.Width, fi.Height, fi.PixelType, fi.FrameLen)
		}
	})
	if err != nil {
		fmt.Printf("  起流失败: %v\n", err)
		return
	}
	defer hikcv.StopDevice(serial)

	for i := 0; i < 3; i++ {
		if err := hikcv.TriggerDevice(serial); err != nil {
			fmt.Printf("  触发失败: %v\n", err)
			return
		}
		time.Sleep(300 * time.Millisecond)
	}
	time.Sleep(time.Second)
	fmt.Printf("  共收到 %d 帧，用时 %v\n", frames, time.Since(start).Round(time.Millisecond))
}
