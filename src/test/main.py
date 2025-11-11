import asyncio
import os
import sys
from time import sleep

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))


import hik_mvcamera_control.api.api as api

if __name__ == "__main__":
    serial_list = api.get_camara_list()
    print(serial_list)
    api.open_camera()
    api.start_grabbing()
    for i in range(10):
        asyncio.run(api.trigger_camera())
        sleep(8)
    api.stop_grabbing()
    api.close_camera()
