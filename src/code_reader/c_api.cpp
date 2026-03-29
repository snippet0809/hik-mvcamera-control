#include "code_reader.h"
#include <cstring>
#include <vector>

extern "C" {
typedef struct {
    char *serialNumber;
    char *netExportIp;
} CCodeReaderInfo;

bool c_startDevice(const char *sn) {
    try {
        startDevice(sn);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_stopDevice(const char *sn) {
    try {
        stopDevice(sn);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_triggerDevice(const char *sn) {
    try {
        triggerDevice(sn);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_setIp(const char *sn, const char *ip, const char *mask, const char *gateway) {
    try {
        setIp(sn, ip, mask, gateway);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_setIntValue(const char *sn, const char *key, int value) {
    try {
        setIntValue(sn, key, value);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_setStringValue(const char *sn, const char *key, const char *value) {
    try {
        setStringValue(sn, key, value);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_setBoolValue(const char *sn, const char *key, bool value) {
    try {
        setBoolValue(sn, key, value);
        return true;
    } catch (...) {
        return false;
    }
}

bool c_setFloatValue(const char *sn, const char *key, float value) {
    try {
        setFloatValue(sn, key, value);
        return true;
    } catch (...) {
        return false;
    }
}
}