//go:build !cgo || !(windows || linux) || !amd64

// Package hikcr 在不支持的平台上为空包。
//
// 不能只给 hikcr.go 加 build tag 就完事：那样本包会变成「没有任何可编译文件」，
// `go build ./hikcr` 会直接报 "build constraints exclude all Go files"——那是错误，
// 不是跳过。留一个同名的空包，调用方拿到的是自己调用点上清晰的
// `undefined: hikcr.EnumDevices`，而不是一屏 cgo 报错。
//
// 支持的平台：windows/amd64、linux/amd64（runtime/ 下的厂商二进制只有这两个）。
package hikcr
