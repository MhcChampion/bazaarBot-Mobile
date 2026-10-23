# BPP Mobile 调试 MCP

v0.91：`bpp_state.json` 增加 `victories/losses`、`preferences`、`mini`、`watch_count`、`markers`、`marker_status`、`capture_status`，卡牌含 slot/enchantment，并包含 opponent_skills。`watch_count` 只说明已关注 GUID 数，只有真实 markers 和截图核对才能证明游戏贴标成功。MCP 固定文件枚举不支持任意 `bazaarBot-tenwin-*.json/.png`；这些本机档案需通过明确的 ADB 路径读取。只读截图测试命令与上传限制见 `docs/COMPANION.md`。

v0.90：可直接读取 `bpp_runs.json`，内容是 QiuBot 当前查询的真实对局、查询 key、卡图元数据和抓取时间。兼容文件名 `bpp_builds.json` 按 QiuBot → V5 → 旧缓存顺序读取；缓存是否新鲜应核对其 key、`fetched_at` 和设备状态。服务代码更新后需重启 MCP 服务，旧进程不会自动加载新模块／参数枚举。

本地 stdio MCP，使用官方 `@modelcontextprotocol/sdk`。不开放网络端口。

```powershell
npm ci --prefix mobilebpp/debug-mcp
pwsh -File mobilebpp/tools/install_mcp.ps1
npm test --prefix mobilebpp/debug-mcp
```

安装脚本写入本项目 `.codex/config.toml`。重新加载 MCP 连接或重新打开聊天后可使用 `bpp_mobile_debug`。迁移路径后重跑安装脚本。

需要 Node.js 20+ 和 adb；可用 `BPP_ADB` 指定 adb 路径，多台设备时传 `serial`。自动发现 Android SDK 或本磁盘 MuMu 的 adb。

工具：`list_devices`、`get_device_status`、`read_debug_file`、`read_logcat`、`search_cards`、`asset_coverage`、`project_status`、`request_snapshot`、`set_panel`、`hide_keyboard`、`set_speed`、`set_launcher`、`set_safe_mode`。资源：`bpp://project/todo`。

推荐调试顺序：列设备 → 看状态／日志 → 请求快照 → 读 `bpp_state.json` 和 `bpp_dump.txt`。写命令返回 `queued`，必须以快照的 `last_command`、`updated_at` 或日志确认消费。游戏未启动时文件可能是上次运行留下的记录。

当前聊天尚未重新加载 MCP 时，也能用真实 MCP 客户端调试：

```powershell
node mobilebpp/debug-mcp/call.mjs list_devices
node mobilebpp/debug-mcp/call.mjs read_debug_file '{"file":"bpp_state.json"}'
node mobilebpp/debug-mcp/call.mjs search_cards '{"query":"猎鹰"}'
node mobilebpp/debug-mcp/call.mjs asset_coverage '{"kind":"missing_art","query":"Item"}'
```

`asset_coverage` 读取逐条资源来源报告，可以区分游戏未提供 ArtKey、当前 APK 资源尚未解析、官方译文缺失及官方保留英文名称。`get_device_status` 使用设备时钟判断新鲜度，v0.86+ 还会核对快照进程 ID，避免重启后把上次运行的数据当成当前状态。

v0.87 的 `hide_keyboard` 可以关闭搜索软键盘，`set_panel(open=false)` 也会主动关闭键盘。它们调用 Android 输入法管理器，不发送游戏的返回键。

显式设备回归：`node mobilebpp/debug-mcp/smoke_device.mjs <serial>`。需运行中的 v0.85+，会短暂切换面板／把手模式和 2x／3x／暂停／1x，读取引擎实际 `Time.timeScale`，最后恢复原设置；不会操作对局选项。结果写入 `out/mcp-device-smoke.json`。

只读取固定 Mod 文件，只允许固定命令；不提供任意 shell、任意路径、卸载或删除游戏数据。安全模式下次启动生效，不会自动重启游戏。状态 JSON、速度与显示模式命令需要 v0.84+，旧版本仍能读取日志和文本导出。

官方参考：[MCP server](https://modelcontextprotocol.io/docs/develop/build-server)、[Codex MCP 配置](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)。
