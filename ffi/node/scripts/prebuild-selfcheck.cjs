/**
 * @file prebuild-selfcheck.cjs
 * @brief node-gyp-build 的**安装期**自检入口（由 package.json 的 `prebuild.test` 指向）。
 *
 * 文件名刻意不叫 `*-test.cjs`：`node --test` 会按 `**\/*-test.{js,mjs,cjs}` 自动收集测试文件，
 * 那样它会被当成用例跑；在没有对应预编译的平台上它会直接失败，把 `npm test` 整体带崩。
 *
 * 为什么需要这个文件：
 *   `npm install` 时本包的 install 脚本是 `node-gyp-build`，它会先跑 `node-gyp-build-test`
 *   把预编译 .node **裸 require** 一次；成功就跳过，失败就回退到 `node-gyp rebuild`。
 *
 *   但本插件的 PE 导入表里静态依赖 `hik_code_reader.dll` / `hik_mvcamera.dll`（见
 *   binding.gyp 的 libraries），Windows 加载器在 require 的那一刻就必须解析它们。解析的前提
 *   是把捆绑目录 `_native/` 前置到 PATH —— 那一步在 `lib/index.js` 里。裸 require 不经过
 *   `lib/index.js`，于是**必然**报 "The specified module could not be found"，node-gyp-build
 *   随即尝试本地编译；而工控机/使用方机器上没有 Visual Studio，安装直接失败。
 *
 *   实测：在未装 VS 的机器上 `npm install` 会以
 *   `node-gyp rebuild → Could not find any Visual Studio installation to use` 告终。
 *
 * 对策：把自检改成加载 `lib/index.js`（自带 PATH 准备与详尽的失败提示）。自检通过后
 *   node-gyp-build 不会再尝试编译；真·不支持的平台仍旧会回退到源码编译，行为不变。
 *
 * @see https://github.com/prebuild/node-gyp-build — build-test.js 中 `pkg.prebuild.test` 分支
 */

'use strict';

require('../lib/index.js');
