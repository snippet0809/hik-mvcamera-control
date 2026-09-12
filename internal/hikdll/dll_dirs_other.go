//go:build !windows

// Package hikdll：非 Windows 平台无需登记 DLL 目录。
//
// Linux 侧的库解析靠链接期写死的 DT_RPATH（见 hikcr/hikcv 的 cgo 参数），
// 而不是运行时改搜索路径——ld.so 在 _start 之前就解析完 DT_NEEDED，
// 且搜索路径在 ld.so 启动时只计算一次，init() 里 setenv 是无效的。
package hikdll
