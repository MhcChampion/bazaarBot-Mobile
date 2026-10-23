# 首次实机测试清单

> 2026-10-21：当前 v0.87 的开发与设备验证入口见 [DEVELOPMENT.md](DEVELOPMENT.md)、[TODO.md](TODO.md) 和 [MCP 说明](../debug-mcp/README.md)。日志目录已迁到 Android/media，默认界面为 WebView。下文为早期版本排障记录，不代表当前功能完成状态；普通升级不卸载游戏。

> 目标：一次就能判断"卡在哪一层"，避免来回猜。整份流程约 10 分钟。

## 0. 前置

- arm64 安卓设备，Android 8+（minSdk 25），已开 USB 调试
- 电脑已装 `adb`（platform-tools），`adb devices` 能看到设备
- APK：`out/TheBazaar-bpp-v0.17.apk`（默认）或 `out/TheBazaar-bpp-v0.17-libmain.apk`（备用）

> 签名与原版不同，**必须先卸载官方版本**。账号进度在服务端，不随卸载丢失；
> 但 Google Play 登录会因签名变化失效，必要时改用邮箱登录。

## 1. 安装

```bash
adb uninstall com.tempogames.thebazaar
adb install -r out/TheBazaar-bpp-v0.17.apk
adb logcat -c
```

## 2. 启动并观察

游戏启动后**等 10 秒**，注意两件事：

1. 屏幕**左下角有没有洋红色条**（前 45 秒内出现）
2. 游戏是否能正常进入主界面

然后导出数据：

```bash
D=/sdcard/Android/data/com.tempogames.thebazaar/files
adb shell touch $D/bpp_dump
sleep 3
adb pull $D/bpp_dump.txt
adb pull $D/bpp.log
```

如果 `bpp.log` 不存在，说明 libbpp 没被加载 → 换 `-libmain` 变体，重复本节。

## 3. 一键收集（推荐）

```powershell
cd mobilebpp
pwsh -File tools\collect_report.ps1
```

会打包设备信息、`bpp.log`、`bpp_dump.txt`、`logcat` 到 `out/bpp-report-<时间>.zip`，
并直接打印日志的头尾，便于先自己看一眼。

## 4. 判定表

| 现象 | 含义 | 下一步 |
| --- | --- | --- |
| 无 `bpp.log` | 注入失败（linker 没加载 libbpp） | 换 `-libmain` 版；两份都不行则改注入方案 |
| 有日志，`libbpp constructor` 后无 `il2cpp domain ready` | 反射层拿不到 runtime | 发日志，多为加载时序问题 |
| 有日志，`tick hook installed` 缺失 | 每帧 hook 没装上 | 发日志，看 `no per-frame hook candidate` |
| `gfx: egl_swaps=0 vulkan_loaded=1` | 游戏走 Vulkan | GL 覆盖层不可用；改用 `bpp_unityui` 探针 |
| 无洋红条、`egl_swaps>0` | hook 命中但没画出来 | 发日志（GL 状态问题） |
| `catalog built: 0 cards` | 静态数据没就绪或解析失败 | 用 `bpp_dump` 看 `IsManagerCreated` |
| `FATAL signal=… hook_depth=1 in=EventSystem.Update` | 崩在 mod 的 tick 里 | 发日志，按 `pc` 定位 |
| 三指无反应，但 `bpp_open` 能开 | 触摸读取有问题 | 发日志里的 `touchCount=… boxedClass=…` |

## 5. 可选探针

```bash
D=/sdcard/Android/data/com.tempogames.thebazaar/files

# Unity 原生文本探针（Vulkan 也能显示，用来验证"反射建 UI"这条路）
adb shell touch $D/bpp_unityui

# 打开面板（触摸失效时）
adb shell touch $D/bpp_open

# 关闭面板
adb shell touch $D/bpp_close
```

## 6. 回退

任何异常都可以不重装直接停用 mod：

```bash
adb shell touch /sdcard/Android/data/com.tempogames.thebazaar/files/bpp_safe_mode
```

重启后 mod 只写一行日志就退出。删文件即恢复。

彻底回退：卸载后安装官方 APK 即可。

## 7. 需要回传的东西

最小集合：

1. `bpp.log`（最重要，前 50 行 + 最后 30 行）
2. `bpp_dump.txt`（如果生成过）
3. 一句观察：**有没有洋红条**、面板能否打开、tooltip 是否出现 `[BPP]` 段落

有这四项，基本可以一次定位到具体层（注入 / hook / 渲染 / 数据）。
