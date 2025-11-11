class CameraError(Exception):
    def __init__(self, err_msg: str, err_code: str | None = None):
        self.err_msg = err_msg
        self.err_code = err_code
        super().__init__(self.err_msg)
