{
  "targets": [
    {
      "target_name": "hik_mvcamera_control",
      "sources": ["src/addon.cc", "src/reader_addon.cc", "src/camera_addon.cc"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(module_root_dir)/../../include"
      ],
      "defines": ["NAPI_CPP_EXCEPTIONS", "HIK_CR_USE_DLL", "HIK_CV_USE_DLL"],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1
        }
      },
      "libraries": [
        "<(module_root_dir)/_native/hik_code_reader.lib",
        "<(module_root_dir)/_native/hik_mvcamera.lib"
      ]
    }
  ]
}
