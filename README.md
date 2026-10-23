# bazaarBot Mobile

《The Bazaar》Android 版的非官方辅助 Mod，当前版本 **v0.0.1**，面向 **arm64-v8a / Android 8.0+**。提供卡牌搜索、当前阵容、十胜阵容推荐与筛选、阵容关注、悬浮窗和游戏速度设置。

## 下载与安装

从 [v0.0.1-r2 Releases](https://github.com/MhcChampion/bazaarBot-Mobile/releases/tag/v0.0.1-r2) 下载两个 `.apk.part001` / `.apk.part002` 分卷和 `Merge-APK-v0.0.1-r2.ps1`，放到同一目录。Windows 在该目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Merge-APK-v0.0.1-r2.ps1
```

脚本逐卷校验 SHA-256，再合并出 `The Bazaar - bazaarBot Mobile-v0.0.1-r2.apk`，不会修改签名。把生成的 APK 传到 Android 手机安装。请为合并预留约 3 GB 的额外磁盘空间；单独的分卷无法安装。Linux/macOS 可按编号用 `cat` 合并，然后按 Release 提供的 SHA-256 校验。

| 项目 | 当前值 |
| --- | --- |
| 应用名称 | The Bazaar - bazaarBot Mobile |
| 独立包名 | `com.tempogames.thebazaar.bazaarbotmobile` |
| Android 版本名 / 版本码 | `1.0.12825-production-android-d9601ded` / `12825` |
| 插件版本 / 安装包修订 | `0.0.1` / `r2` |
| APK 文件名 | `The Bazaar - bazaarBot Mobile-v0.0.1-r2.apk` |

独立包与官方应用使用不同应用 ID，可并存安装，不覆盖官方应用。首次启动需要**单独登录**；不会复制另一应用的账号会话、缓存或设置。已验证独立包启动和插件导航；账号登录、购买、签名绑定服务及真实对局仍需进一步验证。

应用商店不会因版本号相同而把独立包识别为官方应用；本包仍需随官方客户端更新重新适配、打包。资源热更新保留游戏原流程，未来实际更新尚未验收。详见 [版本与更新说明](mobilebpp/docs/VERSIONING.md)。

## 当前功能与边界

- 阵容关注和悬浮窗默认开启，可在设置中关闭；面板可从屏幕侧边入口展开。
- 十胜自动记录**默认关闭**，用户可手动开启本机 JSON / PNG 记录；真实十胜完整触发仍待对局验收。
- **社区上传暂缓，当前没有自动投稿功能。** 推荐列表读取社区公开对局，不代表本机对局已上传。

## 从源码构建

当前脚本面向 Windows / PowerShell 7。请自行准备 Python 3.12、Android NDK r27d、JDK 21、Android SDK Build-Tools 35.0.0；调试 MCP 另需 Node.js 20+ 和 ADB。SDK、原游戏 APK 和签名密钥不包含在源码仓库中。

在仓库根目录操作，安装 Python 依赖并指定工具位置，下面的 SDK 路径请替换为自己的安装位置：

```powershell
py -3.12 -m venv .venv
& .\.venv\Scripts\python.exe -m pip install -r mobilebpp\tools\requirements.txt
$env:BPP_PYTHON = (Resolve-Path .\.venv\Scripts\python.exe).Path
$env:BPP_NDK = 'C:\Android\ndk\27.3.13750724'
$env:BPP_JDK = 'C:\Java\jdk-21'
$env:BPP_BUILDTOOLS = 'C:\Android\build-tools\35.0.0'
```

完整出包还需以下本地输入：

1. 将自己取得、与当前 Mod 适配的官方 arm64 APK 放到仓库根目录，命名为 `TheBazaar.apk`。
2. 用 Il2CppDumper 从同一 APK 的 `libil2cpp.so` 与 `global-metadata.dat` 生成 `dump.cs`，放到 `work/il2cpp-dump/dump.cs`。release 的符号、偏移与 Hook 检查依赖它；该游戏生成文件不随源码提供。

```powershell
# 仅编译原生库与内嵌 Java / Web 资源
pwsh -NoProfile -File mobilebpp\build.ps1

# 符号、偏移与 Hook 静态检查（需要上述 dump.cs）
pwsh -NoProfile -File mobilebpp\tools\release.ps1 -ChecksOnly

# 编译、注入、独立包重打包、签名及 APK 检查
pwsh -NoProfile -File mobilebpp\tools\release.ps1 -Version v0.0.1-r2
```

APK 输出到 `out/The Bazaar - bazaarBot Mobile-v0.0.1-r2.apk`。`-Version` 标记插件发行文件名；Android 版本自动继承输入的官方 APK。签名脚本在本地创建 `mobilebpp/build/bpp.keystore`；自行构建应保管自己的密钥，更新安装需要沿用同一签名。仓库不提供发行签名密钥。

配置入口为 [mobilebpp/config.ps1](mobilebpp/config.ps1)：`BPP_ROOT` 可覆盖项目根目录，`BPP_STAGE` 可覆盖临时构建目录；未设置工具变量时，脚本使用项目根目录下的 `tools/` 默认位置。

可选调试 MCP：

```powershell
npm ci --prefix mobilebpp/debug-mcp
npm test --prefix mobilebpp/debug-mcp
```

调试服务和设备连接说明见 [debug-mcp/README.md](mobilebpp/debug-mcp/README.md)。

## 源码与说明

- `mobilebpp/native`、`java`、`web`：原生插件、Android 桥接及面板。
- `mobilebpp/tools`、`tests`、`debug-mcp`：构建、验证与可选调试工具。
- `third_party/imgui`、`third_party/shadowhook`：构建所需第三方源码与许可。
- [独立包说明](mobilebpp/docs/COEXISTENCE.md)、[社区方案（搁置）](mobilebpp/docs/COMMUNITY-UPLOAD-PLAN.md)。

[mobilebpp/README.md](mobilebpp/README.md) 保留开发历史，其中早期包名、安装命令和功能默认值不适用于当前独立包；当前使用说明以本页为准。

本项目未为自有代码统一指定开源许可证。第三方组件、游戏素材及数据的权利边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
