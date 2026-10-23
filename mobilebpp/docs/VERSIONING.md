# 游戏版本与插件版本

2026-10-23，v0.0.1-r2 修正了首版独立包将 Android 应用版本误设为插件版本的问题。

| 字段 | 当前值 |
| --- | --- |
| Android versionName | `1.0.12825-production-android-d9601ded` |
| Android versionCode | `12825` |
| 插件状态版本 | `0.0.1` |
| 安装包发行修订 | `v0.0.1-r2` |

release 默认保留输入官方 APK 的 versionName、versionCode 和 versionCodeMajor，并用独立 aapt 检查输出版本与底包一致。`release.ps1 -Version` 只标记插件安装包文件名；独立应用 ID、显示名称和签名继续保留，以便与官方版共存。

同版本号不等于同一应用。Android 的官方覆盖更新仍涉及应用 ID 和签名，本插件包不能直接接收商店给原包的安装更新。官方版自身的商店更新不受此包的版本号影响。依据：[应用版本](https://developer.android.com/studio/publish/versioning)、[应用签名](https://developer.android.com/studio/publish/app-signing)。

游戏包含 Addressables 资源目录更新和依赖下载，也有服务器客户端版本检查；当前插件没有 Hook 或绕过这些流程。局部只读核查发现客户端检查读取 Unity Application.version，但尚未确认该构建的 getter 是否由 Manifest versionName 决定，不能把此次显示修正视为未来更新场景已通过验收。

未来游戏客户端二进制升级后，仍需使用新版底包重新检查符号、偏移和兼容性，再发布新版插件安装包。首版 v0.0.1 的源码标签和附件保留，r2 作为单独修正版发布，不移动既有公共标签。
