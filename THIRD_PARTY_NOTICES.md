# 第三方组件与素材声明

以下按仓库中实际保留的源码、许可文件和依赖声明整理。本文件仅列第三方权利与出处，不为 bazaarBot Mobile 自有代码指定许可证，也不把第三方许可证扩展到游戏本体、图片、字体或数据。

## 随仓原生组件

| 组件 | 权利人与许可 | 随仓依据 |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | Copyright (c) 2014–2026 Omar Cornut；MIT | [LICENSE.txt](third_party/imgui/LICENSE.txt) |
| [ShadowHook](https://github.com/bytedance/android-inline-hook) | Copyright (c) 2021–2026 ByteDance Inc.；MIT | [LICENSE](third_party/shadowhook/LICENSE) |
| ShadowHook 内含 xdl | Copyright (c) 2020–2024 HexHacking Team；MIT | [xdl/LICENSE](third_party/shadowhook/shadowhook/src/main/cpp/third_party/xdl/LICENSE) |
| ShadowHook 内含 Linux Syscall Support（lss） | Copyright (c) 2005–2011 Google Inc.；BSD 三条款 | [lss/LICENSE](third_party/shadowhook/shadowhook/src/main/cpp/third_party/lss/LICENSE) |
| ShadowHook 内含 BSD `queue.h` | Copyright (c) 1991, 1993 The Regents of the University of California；BSD 三条款 | [queue.h 文件头](third_party/shadowhook/shadowhook/src/main/cpp/third_party/bsd/queue.h) |
| ShadowHook 内含 BSD `tree.h` | Copyright 2002 Niels Provos；BSD 两条款 | [tree.h 文件头](third_party/shadowhook/shadowhook/src/main/cpp/third_party/bsd/tree.h) |

Dear ImGui 内含 `imstb_rectpack.h`、`imstb_textedit.h`、`imstb_truetype.h`，其文件尾提供 MIT / Public Domain 双重许可，MIT 部分为 Copyright (c) 2017 Sean Barrett。随仓保留各文件中的完整条款。`imgui_draw.cpp` 还包含 MIT 许可的 ProggyClean 字体（Copyright (c) 2004, 2005 Tristan Grimmer）及 ProggyForever 最小字体（Copyright (c) 2026 Disco Hello；Copyright (c) 2019, 2023 Tristan Grimmer）。日本字形范围表的资料来源与 CC BY 4.0 说明保留在同一文件的 `GetGlyphRangesJapanese()` 注释中。

## 可选调试与构建依赖

调试 MCP 的直接依赖是 `@modelcontextprotocol/sdk`（MIT，Copyright (c) 2024 Anthropic, PBC）和 `zod`（MIT，Copyright (c) 2025 Colin McDonnell）。版本与传递依赖由 [package.json](mobilebpp/debug-mcp/package.json) 和 [package-lock.json](mobilebpp/debug-mcp/package-lock.json) 记录；`node_modules` 不随仓提供，执行 `npm ci` 时由包管理器取得，各包自带许可继续适用。

Python 工具依赖列于 [requirements.txt](mobilebpp/tools/requirements.txt)，由使用者单独安装。Android SDK / NDK、JDK、Python、Node.js、Il2CppDumper 等外部工具不随源码提供，其许可随各自发行物保留。原生构建静态链接 NDK C++ 运行时；自行再分发二进制时，还需保留所用 NDK 的运行时许可与 NOTICE，不能以本文件替代整个工具链的声明。

## 游戏素材与社区数据

The Bazaar 游戏程序、商标、卡牌插画、界面素材、字体和原始数据属于各自权利人。`mobilebpp/web` 中的加工素材及生成的数据表不因此获得 MIT 或 BSD 授权。原游戏 APK、SDK、签名密钥、账号会话和设备验收资料不属于公开源码包。

阵容查询及其内容来自 [巴扎丘Bot](https://bazaarqiubot.com/)，相关上游包括 [BazaarDB](https://bazaardb.gg/)；离线阵容数据的既有来源包括 [BazaarPlusPlus](https://bazaarplusplus.com/)。这些名称用于说明来源，不代表上述项目授权、背书或承诺本项目的投稿能力。第三方站点内容、接口与数据遵循各自条款；本项目当前暂停社区上传。

## MIT 许可文本

本节适用于上文明确标为 MIT 的组件，版权声明按各组件对应权利人保留；各原始文件中的完整许可仍随源码保留。

```text
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## BSD 许可文本

### Linux Syscall Support（lss）

```text
Copyright (c) 2005-2011, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
   Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
   Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### BSD queue.h

```text
Copyright (c) 1991, 1993
     The Regents of the University of California. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of the University nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

### BSD tree.h

```text
Copyright 2002 Niels Provos <provos@citi.umich.edu>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
