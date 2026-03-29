#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <stdexcept>
#include <string>
#include <thread>

void CodeReader::open() {
    if (this->status == CodeReaderStatus::Open) {
        return;
    }
    if (this->status == CodeReaderStatus::Connected) {
        int ok = MV_CODEREADER_OpenDevice(this->handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        int ok = MV_CODEREADER_StopGrabbing(this->handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StopGrabbing error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
}

void CodeReader::close() {
    if (this->status == CodeReaderStatus::Connected) {
        return;
    }
    if (this->status == CodeReaderStatus::Grabbing) {
        int ok = MV_CODEREADER_StopGrabbing(this->handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StopGrabbing error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        int ok = MV_CODEREADER_CloseDevice(this->handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_CloseDevice error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Connected;
    }
}

void CodeReader::grabbing() {
    if (this->status == CodeReaderStatus::Grabbing) {
        return;
    }
    if (this->status == CodeReaderStatus::Connected) {
        int ok = MV_CODEREADER_OpenDevice(this->handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_OpenDevice error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Open;
    }
    if (this->status == CodeReaderStatus::Open) {
        int ok = MV_CODEREADER_StartGrabbing(this->handle);
        if (ok != MV_CODEREADER_OK) {
            throw std::runtime_error("MV_CODEREADER_StartGrabbing error: " + toHexStr(ok));
        }
        this->status = CodeReaderStatus::Grabbing;
    }
}

void stopDevice(std::string sn) {
    CodeReader *cr = getDevice(sn, false);
    if (cr != nullptr) {
        cr->close();
    }
}

void startDevice(std::string sn) {
    CodeReader *cr = getDevice(sn, true);
    cr->grabbing();
}
