#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <string>
#include <thread>

void CodeReader::open() {
    if (this->status == CodeReaderStatus::Open) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        throw std::runtime_error("Device is grabbing");
    }
    if (this->status == CodeReaderStatus::Connected) {
        int ok = MV_CODEREADER_OpenDevice(handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
}

void CodeReader::close() {
    if (this->status == CodeReaderStatus::Connected) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        int ok = MV_CODEREADER_StopGrabbing(handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StopGrabbing error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        MV_CODEREADER_CloseDevice(handle);
    }
}

void CodeReader::grabbing() {
    if (this->status == CodeReaderStatus::Grabbing) {
        return;
    }
    if (this->status != CodeReaderStatus::Connected) {
        int ok = MV_CODEREADER_OpenDevice(handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        int ok = MV_CODEREADER_StartGrabbing(handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StartGrabbing error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Grabbing;
    }
}

void CodeReader::stopGrabbing() {
    if (this->status == CodeReaderStatus::Open ) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        int ok = MV_CODEREADER_StopGrabbing(handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StopGrabbing error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
}

/**
 * 停止设备，会执行以下操作：
 * 1.停止取流
 * 2.关闭设备
 * 3.销毁句柄
 */
void stopDevice(std::string sn) {
    void *handle = getHandle(sn, false);
    if (handle != nullptr) {
        MV_CODEREADER_StopGrabbing(handle);
        MV_CODEREADER_CloseDevice(handle);
    }
}

/**
 * 启动设备，会执行以下操作：
 * 1.创建句柄
 * 2.打开设备
 * 3.启动取流
 */
void startDevice(std::string sn) {
    // 停止设备并销毁句柄
    stopDevice(sn);
    destroyHandle(sn);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 创建句柄
    void *handle = getHandle(sn, true);
    // 打开设备
    int ok = MV_CODEREADER_OpenDevice(handle);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
    }
    // 启动取流
    ok = MV_CODEREADER_StartGrabbing(handle);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_StartGrabbing error: " + toHexStr(ok));
    }
}

void openDevice(std::string sn) {
    void *handle = getHandle(sn, false);
    if (handle != nullptr) {
        stopDevice(sn);
    } else {
    }

    int ok = MV_CODEREADER_OpenDevice(handle);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
    }
}
