{
  "targets": [
    {
      "target_name": "hik_code_reader",
      "sources": ["src/addon.cc"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(module_root_dir)/../../include"
      ],
      "defines": ["NAPI_CPP_EXCEPTIONS"],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1
        }
      },
      "libraries": ["<(module_root_dir)/_native/hik_code_reader.lib"]
    }
  ]
}
