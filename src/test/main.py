import asyncio
from time import sleep

import api.api as api

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
