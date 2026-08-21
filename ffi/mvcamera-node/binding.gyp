{
  "targets": [
    {
      "target_name": "hik_mvcamera",
      "sources": ["src/addon.cc"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(module_root_dir)/../../include"
      ],
      "defines": ["NAPI_CPP_EXCEPTIONS", "HIK_CV_USE_DLL"],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1
        }
      },
      "libraries": ["<(module_root_dir)/_native/hik_mvcamera.lib"]
    }
  ]
}
