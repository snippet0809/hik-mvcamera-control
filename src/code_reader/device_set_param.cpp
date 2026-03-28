#include "MvCodeReaderCtrl.h"
#include "code_reader.h"
#include <sstream>
#include <string>
#include <vector>



/**
 * 设置IP，请注意一下两点
 *
 * 1、设置IP时设备必须处于关闭状态
 * 2、设置IP后设备会自动重启
 *
 * @param sn 设备序列号
 * @param ip IP
 * @param mask 子网掩码
 * @param gateway 网关
 */
void setIp(std::string sn, std::string ip, std::string mask, std::string gateway) {
    // 先关闭设备，再获取句柄，这样无论句柄是否是新建的，设备总处于关闭状态
    stopDevice(sn);
    void *handle = getHandle(sn, true);
    // 设置IP
    int ok = MV_CODEREADER_GIGE_ForceIp(handle, ipToInt(ip), ipToInt(mask), ipToInt(gateway));
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("MV_CODEREADER_GIGE_ForceIp error: " + toHexStr(ok));
    }
    // 因为设备会自动重启，所以要把句柄销毁
    destroyHandle(sn);
}
/**
 * 设置int类型的参数
 *
 * 注意：此时相机必须处于【已打开+未取流】状态
 */
void setIntValue(std::string sn, std::string key, int value) {


    int ok = MV_CODEREADER_SetIntValue(handle, key.c_str(), value);
    if (ok != MV_CODEREADER_OK) {
        throw std::runtime_error("")
    }
}

void setFloatValue(std::string sn, std::string key)
