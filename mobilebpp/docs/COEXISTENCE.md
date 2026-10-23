# 独立包 v0.0.1

v0.0.1-r2：Android 版本已恢复为底包原始值，插件版本保持 0.0.1。应用商店升级与资源热更新的边界见 [VERSIONING.md](VERSIONING.md)。

2026-10-23：按用户要求搁置社区接入，十胜自动记录开关默认关闭，保留后续手动开启本机记录的能力。关注与阵容悬浮窗维持原有默认值。手机原同包安装中的记录开关也已关闭。

| 项目 | 值 |
| --- | --- |
| 应用名 | The Bazaar - bazaarBot Mobile |
| Android applicationId | `com.tempogames.thebazaar.bazaarbotmobile` |
| Android versionName / versionCode | `1.0.12825-production-android-d9601ded` / `12825` |
| 插件状态版本 | `0.0.1` |
| APK | `out/The Bazaar - bazaarBot Mobile-v0.0.1-r2.apk` |
| 原版 applicationId | `com.tempogames.thebazaar` |

应用 ID、Provider authorities、自定义权限及相关引用采用独立标识；游戏实际 Java 类名和资源保持正确引用。Manifest 应用及启动入口标签追加插件名。构建使用原始官方 APK 为输入，经现有 Mod 密钥签名；没有把官方签名冒充为本项目签名。

独立包会作为另一个应用安装，不覆盖原版。两者使用各自的数据、缓存、插件设置和登录状态；新包首次启动需单独登录。原应用不卸载、不清数据，不自动复制账号会话或存档。

手机原 `com.tempogames.thebazaar` 槽位目前是历史同包 Mod v0.101，它仍不能直接被不同签名的官方包覆盖。此次通过新增独立包解决今后与官方包共存；若用户以后要把旧槽位恢复为官方版，应单独安排数据保留与重装，不在本次操作中删除旧应用。

## 构建与调试

`tools/release.ps1` 默认输出上述独立包与名称，支持显式指定插件发行文件名、应用名和包名；Android 版本字段继承官方输入 APK。`tools/verify_apk.py` 用独立 aapt 解析核对 Manifest 身份与资源包命名空间，并保留原来的库、ELF和对齐检查。`tools/apk_identity.py` 定点编辑二进制 Manifest，同时同步 `resources.arsc` 包头的固定包名字段，保持所有资源 ID 和其他资源内容不变；不全局替换 Dex 中的类名。

第一次候选只改 Manifest，导致 Unity 按当前包名查找启动字符串时得到资源 ID 0 并崩溃。已同步资源包命名空间，并加入原始 APK 资源 ID 保持及独立 aapt 核验，最终交付必须以修复后的 APK 和新进程验收为准。

原生路径从当前进程的包名解析；新包只访问自己的 Android/media、Android/data 和内部探针目录。调试 MCP 默认目标改为新包；仅显式设置 `BPP_ANDROID_PACKAGE=com.tempogames.thebazaar` 时才读旧包，避免两个应用同时存在时误选。

安装后分别核对两个包的 UID、版本、Provider 和目录；验证新包新进程状态为 `0.0.1`、`upload:false`。启动、安装共存和插件功能不代表第三方账号登录、Play购买或签名绑定服务全部兼容；没有为改包名而绕过这些服务的验证。

本轮结果以 `out/delivery-audit-v0.0.1.json` 为准。社区实施方案继续保存在 [COMMUNITY-UPLOAD-PLAN.md](COMMUNITY-UPLOAD-PLAN.md)，状态为搁置。
