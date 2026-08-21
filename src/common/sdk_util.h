#pragma once

/** 两个 C++ 包装（src/code_reader、src/mvcamera）共用的工具：hex/IP 串、字节转串、SDK 调用检查。
 *
 * 原先这些工具在每个包装内各有一份（甚至实现不一），统一收拢到这里后：
 *   - `toHexStr` / `intToIp` / `bytesToStr` 为 inline 自由函数，随头文件展开到各 TU；
 *   - `checkSdk<kOk>` 为模板，`kOk` 传各自 SDK 的成功哨兵（MV_OK / MV_CODEREADER_OK）。
 */

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

/** 把 SDK 返回的 int 错误码格式化为 "0xABCDEF01"。 */
inline std::string toHexStr(int value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(value));
    return buf;
}

/** 32 位 IP（大端字段：最高字节在前）→ "a.b.c.d"。 */
inline std::string intToIp(unsigned int ip) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF,
                  ip & 0xFF);
    return buf;
}

/** 定长字节缓冲 → 字符串（截断到首个 NUL 或 len；buf 可为空）。 */
inline std::string bytesToStr(const unsigned char* buf, std::size_t len) {
    if (!buf) {
        return {};
    }
    const char* p = reinterpret_cast<const char*>(buf);
    return std::string(p, strnlen(p, len));
}

/** 检查 SDK 返回值，非 kOk 时抛 runtime_error（消息统一 "<api> error: 0x…"）。 */
template <int kOk>
void checkSdk(int ok, const char* api) {
    if (ok != kOk) {
        throw std::runtime_error(std::string(api) + " error: " + toHexStr(ok));
    }
}
