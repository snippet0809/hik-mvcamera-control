#include "code_reader.h"
#include "MvCodeReaderCtrl.h"
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

MV_CODEREADER_DEVICE_INFO_LIST g_stDevList{};

inline std::string toHexStr(int value) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << value;
    return ss.str();
}

std::vector<std::string> enumCodeReader() {
    int ok = MV_CODEREADER_EnumCodeReader(&g_stDevList);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_EnumCodeReader error: " + toHexStr(ok));
    }
    std::vector<std::string> sns;
    for (unsigned int i = 0; i < g_stDevList.nDeviceNum; i++) {
        sns.push_back((char *)g_stDevList.pDeviceInfo[i]->SpecialInfo.stGigEInfo.chSerialNumber);
    }
    return sns;
}
