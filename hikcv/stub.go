//go:build !cgo || !(windows || linux) || !amd64

// Package hikcv 在不支持的平台上为空包。
//
// 与 hikcr 同理：只给 hikcv.go 加 build tag 会让本包变成「没有任何可编译文件」，
// `go build ./hikcv` 会报 "build constraints exclude all Go files"——那是错误，
// 不是跳过。留一个同名空包，调用方拿到的是自己调用点上清晰的
// `undefined: hikcv.EnumDevices`。
//
// 支持的平台：windows/amd64、linux/amd64（runtime/ 下的厂商二进制只有这两个）。
package hikcv
