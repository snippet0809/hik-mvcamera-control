/** 状态迁移、startDevice/stopDevice、起流前四项 GenICam 写参 */

#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include "code_reader_detail.h"
#include <cstdio>
#include <stdexcept>

namespace {

void openIfConnected(CodeReader *d) {
    if (d->status != CodeReaderStatus::Connected) {
        return;
    }
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_OpenDevice(d->handle), "MV_CODEREADER_OpenDevice");
    d->status = CodeReaderStatus::Open;
}

void applyOpenParams(CodeReader *d, const CodeReaderOpenParams &p) {
    if (d->status != CodeReaderStatus::Open) {
        throw std::logic_error("applyOpenParams: expect Open");
    }
    void *h = d->handle;
    // TriggerMode/TriggerSource 决定软触发是否可用，属关键项：失败即报错阻断起流。
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetEnumValueByString(h, "TriggerMode", p.triggerMode.c_str()), "SetEnum(TriggerMode)");
    checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_SetEnumValueByString(h, "TriggerSource", p.triggerSource.c_str()), "SetEnum(TriggerSource)");
    // CODE128 / QRCode 解码使能开关按型号可选：部分读码器固件无此节点或当前不可写
    // （如 DA3578913 报 SetBool(CODE128) 0x80020100），失败不应阻断起流，否则该型号无法 startDevice。
    // 读码器沿用自身当前配置即可，必要时再用 hik_cr_set_param 单独调整。
    if (MV_CODEREADER_SetBoolValue(h, "CODE128", p.code128) != MV_CODEREADER_OK) {
        std::fprintf(stderr, "[hik_code_reader] SetBool(CODE128) 不可写，忽略（沿用当前配置）\n");
    }
    if (MV_CODEREADER_SetBoolValue(h, "QRCode", p.qrcode) != MV_CODEREADER_OK) {
        std::fprintf(stderr, "[hik_code_reader] SetBool(QRCode) 不可写，忽略（沿用当前配置）\n");
    }
}

} // namespace

void CodeReader::open() {
    if (status == CodeReaderStatus::Open) {
        return;
    }
    if (status == CodeReaderStatus::Grabbing) {
        throw std::logic_error("CodeReader::open: Grabbing 下请先 stopDevice");
    }
    openIfConnected(this);
}

void CodeReader::grabbing() {
    if (status == CodeReaderStatus::Grabbing) {
        return;
    }
    openIfConnected(this);
    if (status == CodeReaderStatus::Open) {
        codeReaderInternalBindImageCallbackBeforeGrabbing(this);
        checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_StartGrabbing(handle), "MV_CODEREADER_StartGrabbing");
        status = CodeReaderStatus::Grabbing;
    }
}

void CodeReader::close() {
    // 调用方须已持有 g_device_mutex。
    if (status == CodeReaderStatus::Connected) {
        return;
    }
    if (status == CodeReaderStatus::Grabbing) {
        checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_StopGrabbing(handle), "MV_CODEREADER_StopGrabbing");
        status = CodeReaderStatus::Open;
    }
    if (status == CodeReaderStatus::Open) {
        checkSdk<MV_CODEREADER_OK>(MV_CODEREADER_CloseDevice(handle), "MV_CODEREADER_CloseDevice");
        status = CodeReaderStatus::Connected;
        // CloseDevice 之后 SDK 不允许拿该句柄再次 OpenDevice（返回 0x80020000），须先重建。
        handleStale = true;
    }
}

void stopDevice(const std::string &sn) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *cr = findDevice(sn);
    if (cr) {
        cr->close();
    }
}

void startDevice(const std::string &sn, const CodeReaderOpenParams &params,
                 const std::optional<CodeReaderBcrCallback> &onBcrCodes) {
    std::lock_guard<std::mutex> lock(g_device_mutex);
    CodeReader *cr = getOrCreateDevice(sn);
    const auto reg = [&] {
        if (onBcrCodes) {
            registerImageCallbackForSerial(sn, *onBcrCodes);
        }
    };
    reg();
    if (cr->status == CodeReaderStatus::Grabbing) {
        return;
    }
    if (cr->status == CodeReaderStatus::Connected) {
        if (cr->handleStale) {
            // 修复：stop 后再 start 时旧句柄已 CloseDevice，重建后 OpenDevice 才能成功。
            cr->recreateHandle();
        }
        cr->open();
    }
    applyOpenParams(cr, params);
    cr->grabbing();
}
