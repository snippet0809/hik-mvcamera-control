from MvImport.MvCameraControl_class import *


def get_device_list():
    result = MvCamera.MV_CC_Initialize()
    if result != MV_OK:
        print("initialize error: %d" % result)


if __name__ == "__main__":
    result = MvCamera.MV_CC_Initialize()
    print(result)
    get_device_list()
