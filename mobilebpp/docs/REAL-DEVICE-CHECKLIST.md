# 真机验证一页纸

> **v0.87 当前入口**：普通升级使用 `adb install -r out/TheBazaar-bpp-v0.87.apk`，保留游戏数据。默认显示收起把手；四页为“阵容／加速／设置／诊断”。日志和文件命令在 `/sdcard/Android/media/com.tempogames.thebazaar/`，也可通过 `mobilebpp/debug-mcp` 读取和执行命令。自动设备检查运行 `node mobilebpp/debug-mcp/smoke_device.mjs <serial>`，其范围与剩余验收见 [TODO.md](TODO.md)。**下面的 v0.29 安装、页签和 tooltip 步骤仅是历史记录，请勿按历史步骤卸载当前游戏。**

> 目标：在真实手机上把四项功能一次验完。约 10 分钟。

## 1. 安装

```bash
adb uninstall com.tempogames.thebazaar        # 必须：签名与官方不同
adb install -r out/TheBazaar-bpp-v0.29.apk
```

装完先别急着开游戏，清一下日志缓冲：

```bash
adb logcat -c
```

## 2. 启动，看到主界面后

```bash
D=/sdcard/Android/data/com.tempogames.thebazaar/files

# 1) 导出数据（图鉴 + 阵容）
adb shell touch $D/bpp_dump

# 2) 打开面板（三指轻点屏幕若无效，用这条）
adb shell touch $D/bpp_open

# 3) 切到诊断页看状态
adb shell "echo 3 > $D/bpp_tab"
```

面板应该出现在屏幕左上区域，四个页签：图鉴 / 阵容 / 设置 / 诊断。

## 3. 逐项验收

| 功能 | 怎么看 | 期望 |
| --- | --- | --- |
| 图鉴 | 「图鉴」页 | 有卡片列表；搜索框能过滤；点一项看详情 |
| 阵容 | 「阵容」页 | Day / Hero / 棋盘 / 仓库 / 对手 / 商店 / 技能 都有内容 |
| 阵容库 | 「阵容」页底部 | 显示匹配到的 BPP 十胜阵容与命中次数 |
| tooltip | 把手指按在任意卡上 | 原提示文本末尾多出 `[BPP] …` 一段 |
| 倍速 | 「设置」页或 `bpp_settings.txt` | 调到 2x/3x 后战斗明显加速；暂停按钮可用 |

## 4. 回传

```powershell
cd mobilebpp
pwsh -File tools\collect_report.ps1
```

生成 `out/bpp-report-*.zip`（含 `bpp.log`、`bpp_dump.txt`、设备信息、logcat）。

**最小集合**：`bpp_dump.txt` + `bpp.log` + 一句话说明哪项不对。

## 5. 出问题时

| 现象 | 处理 |
| --- | --- |
| 面板不出现 | 先确认 `bpp.log` 里有 `gl canary drawn`；没有就是 EGL hook 没装上 |
| 三指无效 | 用 `bpp_open`/`bpp_tab` 文件命令，触摸交互是已知待修项 |
| 图鉴为空 | 看 `bpp_dump.txt` 里 `catalog_count`；为 0 说明静态数据没就绪 |
| 崩溃 | 日志里会有 `*** FATAL signal=… in=…`，直接回传 |
| 想临时停用 mod | `adb shell touch $D/bpp_safe_mode`，重启后 mod 只写一行日志 |

## 6. 已知在模拟器上不成立的部分

MuMu 12（x86_64 + houdini 转译）里游戏自身会因
`SQLiteException: Could not open database file ... (CannotOpen)` 停在加载页，
**官方原版 APK 同样如此**，与 mod 无关。真机没有这个限制。
