# 项目状态（交付收口）

> **2026-10-23 · 当前独立包 v0.0.1**：社区接入按用户要求搁置，十胜自动记录默认关闭，手机旧包中的对应开关也已关闭；关注和悬浮窗继续默认开启。应用名为 **The Bazaar - bazaarBot Mobile**，包名 `com.tempogames.thebazaar.bazaarbotmobile`，与原版应用 ID 分开。已安装并验证新旧包不同 UID 共存、新包启动及重启、设置和插件导航；14 项 Python、23 项 Node/MCP 与 release 检查通过。新包需独立登录，登录、购买与真实对局尚未验收；旧槽位目前仍是历史同包 Mod，未卸载或清数据。见 [独立包说明](COEXISTENCE.md) 与 `out/delivery-audit-v0.0.1.json`。

> **2026-10-23 · 历史版本 v0.101**：关注阵容在十胜推荐顶部独立置顶并高亮，分页、筛选、加载失败仍可取消；列表内点关注后立即定位到置顶项，取消恢复原顺序。本机十胜记录改为有效 PNG 完成后去重，失败限次重试，避免截图漏存。20 项 Node/MCP、10 项离线原生记录场景、release 与真机置顶／取消检查通过。真实十胜自动触发仍待验收，社区上传尚未接通。见 `docs/RECORDING.md`、`out/delivery-audit-v0.101.json`。

> **2026-10-23 · 历史版本 v0.100**：关注提醒改为游戏原始铜色尖尾挂签与收藏图标，替换黄色文字矩形；挂在实际卡牌顶边并向卡面内垂下，随卡牌投影尺寸缩放。已覆盖安装并核对真实商店目标牌，16 项 Node/MCP 与 release 检查通过。来源和定位见 `docs/FOLLOW-TAG-v0.100.md`，真机证据见 `out/delivery-audit-v0.100.json`。

> **2026-10-23 · 历史版本 v0.99**：修复导航及展开／收起按钮绘制缩放与点击位置脱节，隐藏无附魔默认值 -1，保留独立内容滚动裁切。统一游戏风格边签与拖回区，悬浮地毯加阴影；技能等原生窗口确认高度后显示，避免短暂截断。16 项 Node/MCP、原生 release 及最终 APK 真机界面复查通过；证据见 `out/delivery-audit-v0.99.json`，排查见 `docs/UI-REGRESSION-v0.98.md`。关注扫描与倍速恢复的场内验收单独记录；社区上传仍未接通。

> **2026-10-23 · 历史版本 v0.92**：界面改用游戏原始地毯、菜单边框、按钮、勾选框、标题字体与五档技能框。设置可选当前 APK 内 94 款地毯，卡面与列表更紧凑；悬浮窗轻点只切换下方技能，拖回提示改为游戏返回图标。13 项 Node／MCP、6 项 Python 与 release 检查通过。来源见 `mobilebpp/docs/GAME-THEME.md`，最终真机结果见 `out/delivery-audit-v0.92.json`。商店／战斗关注标签按用户要求延后测试，社区上传仍未接通。

> **2026-10-22 · 历史版本 v0.91**：新增十胜阵容详情、技能与原局截图、70% 透明卡图悬浮窗、原生长按拖拽及左侧停靠、阵容关注和十胜本地档案；标题改为 bazaarBot Mobile。已覆盖安装，详情、短点与返回、精确拖动和停靠及 PixelCopy 截图通过真机验证，18 项自动检查与 release 通过。实际游戏关注贴标和十胜自动触发仍待场景验收，社区上传尚未接通。详见 `mobilebpp/docs/DELIVERY.md`、`mobilebpp/docs/COMPANION.md` 和 `out/delivery-audit-v0.91.json`。

> **2026-10-22 · 历史版本 v0.90**：十胜推荐已切换至巴扎丘Bot当前赛季、最近 7 天的公开对局接口，支持英雄自动读取、点牌筛选、分页和刷新；记录保留 ID、日期、附魔与赛季信息。卡面使用游戏原始三档尺寸／五档品质卡框和完整插画，按原始比例缩放。已覆盖安装并验证真机查询、选牌与取消；8 项 Node／MCP、6 项 Python、release 检查通过。详见 `mobilebpp/docs/DELIVERY.md`、`evidence/qiubot-source.json` 和 `out/delivery-audit-v0.90.json`。下方旧版本内容为历史记录。

> **2026-10-22 · 历史版本 v0.89**：纠正官网数据源对接。线上官网使用 V5，原 V6 源与官网数据不同；现已同步官网 V5 联网、离线及原生阵容库，卡诺克 595 套、总计 5689 套，数据截至 2026-10-05。v0.88“卡诺克无样本需等待”的结论已撤销。详见 `evidence/bpp-builds.source.json` 和 `mobilebpp/docs/DELIVERY.md`。以下旧版本说明仅保留历史。

> **2026-10-22 · 历史版本 v0.88**：已覆盖安装；框外点击收起、面板边缘按钮、紧凑棋盘卡面、隐藏仓库／技能、插画 Alpha 遮罩修复、按英雄显示十胜阵容及选牌筛选已交付。当前数据源卡诺克无样本；卡面未复制动态附魔／实时数字。详见 `mobilebpp/docs/TODO.md`、`mobilebpp/docs/DELIVERY.md` 与 `out/delivery-audit-v0.88.json`。下方旧版本说明为历史记录。

> **历史版本 v0.87，2026-10-21。** WebView 主流程、本地 MCP、3260 条图片覆盖、联网 JSON 统计与软键盘收起已完成本轮开发；构建、APK、Python／Node 和 Vulkan 真机 MCP 集成检查通过。引擎实际 2x/3x/暂停/1x、键盘关闭、基本后台恢复已验证，完整战斗和其他机型仍需覆盖。详见 [DELIVERY.md](DELIVERY.md)、[TODO.md](TODO.md)、[DEVELOPMENT.md](DEVELOPMENT.md)。下面是 v0.21 起的历史状态，不能作为当前验收清单。

> 生成时间：2026-10-20 · 对应 APK：`v0.21`

## 1. 交付物

| 文件 | 大小 | SHA256 |
| --- | --- | --- |
| `out/TheBazaar-bpp-v0.21.apk` | 2804.6 MB | `20A2E152…D37E0B` |
| `out/TheBazaar-bpp-v0.21-libmain.apk` | 2698.5 MB | `B363C461…E9B2FC` |
| 源码 | `mobilebpp/native/` | C++17，`-Wall -Wextra` 0 warning |
| 构建 | `mobilebpp/build.ps1`、`tools/release.ps1` | 一条命令出包 |
| 文档 | `README.md`、`docs/CODE-REVIEW.md`、`docs/TESTING.md`、`docs/game-api-map.md` | — |

默认版改 `libil2cpp.so`（DT_NEEDED），备用版改 `libmain.so`；两者功能相同。

## 2. 已实现功能

| 功能 | 实现方式 | 图形 API 依赖 |
| --- | --- | --- |
| 图鉴 | `Data.GetStatic()` → `JsonGameDataManager.GetCardMap()`，ImGui 列表 + 搜索 | GLES（ImGui） |
| 阵容读取 | `Data.Run` → `Player.Hand/Stash/Skills`、`Run.Opponent`、`RunState.GetCardsInSelectionSet()` | GLES / 文本 HUD |
| 阵容库 | 内置 BPP 十胜阵容快照，按棋盘/仓库/商店加权匹配 | 无 |
| tooltip 增强 | hook `CardTooltipData::RenderTooltip`，`String.Concat` 追加静态信息 | **无** |
| 倍速 / 暂停 | `GameServiceManager.PauseOrUnpauseGame` + `Time.timeScale` | **无** |
| 数据导出 | `files/bpp_dump` → `bpp_dump.txt` | **无** |
| 文件命令 | `bpp_open` / `bpp_close` / `bpp_dump` / `bpp_unityui` / `bpp_safe_mode` | **无** |

## 3. 验证状态

### 已通过（离线可验证）

`pwsh -File tools/verify_all.ps1` → `all checks passed`

| 检查 | 覆盖 |
| --- | --- |
| `check_lookups.py` | 源码中 17 处类查找的命名空间 + 类名 |
| `check_offsets.py` | 9 个偏移常量 + 7 个字段与 dump 一致 |
| `check_hooks.py` | 2 个 IL2CPP hook 的签名（参数个数/返回类型）+ 3 个 EGL hook |
| `selfcheck.py` | 32 个游戏符号存在性 |
| `verify_elf.py` | libbpp 与改写后 libil2cpp 的 linker 不变量 |
| `verify_apk.py` | 包内含 libbpp、DT_NEEDED 生效、`.so` 16 KiB 对齐 |
| 编译器 | `-Wall -Wextra` 0 warning |
| 签名 | apksigner v2 + v3 |

### 未验证（必须真机）

1. 注入是否被 bionic 接受
2. `il2cpp_runtime_invoke` 的装箱约定
3. 每帧 hook 是否命中（`EventSystem.Update`）
4. 游戏渲染走 GLES 还是 Vulkan
5. `Data.Run` / `GetItems()` 的实际返回结构
6. ImGui/Unity-UI 两条渲染路径的真实表现

## 4. 真机测试入口

```bash
adb uninstall com.tempogames.thebazaar
adb install -r out/TheBazaar-bpp-v0.21.apk
# 启动后等 10 秒，注意左下角洋红色条
D=/sdcard/Android/data/com.tempogames.thebazaar/files
adb shell touch $D/bpp_dump; sleep 3
adb pull $D/bpp.log; adb pull $D/bpp_dump.txt
```

或一键收集：`pwsh -File mobilebpp/tools/collect_report.ps1` → `out/bpp-report-*.zip`。

判定表见 [TESTING.md](TESTING.md)。

## 5. 关键设计决策

| 决策 | 原因 |
| --- | --- |
| 反射按名字解析，不写死偏移 | 游戏更新后大概率仍可用；偏移仍有常量 + 校验兜底 |
| 数据走 `Data.Run` 而不是 UI 扫描 | 面板关闭/对象池隐藏时仍能读到 |
| tooltip 改字符串而不是加图层 | 与图形 API 解耦，Vulkan 也可用 |
| 双注入方案 + safe mode | 崩溃时无需重装即可回退/二分 |
| 内置阵容库快照 | 手机端不做网络请求，离线可用 |

## 6. 已知限制

- 图鉴/阵容的 ImGui 面板在 Vulkan 设备上不可用（改走文本 HUD，只读）
- 数值级"升级/附魔预览"未实现：需要游戏 `ValueContext` 上下文，属后续工作
- 阵容库为构建时快照，更新需重跑 `tools/gen_catalog.py`
- 包体 2.7 GB（官方包 2.7 GB），每次出包需重打包

## 7. 真机（MuMu 模拟器）实测记录 — 2026-10-20

环境：MuMu 12，Android 12（API 32），`abilist=x86_64,arm64-v8a,...`，`native.bridge=libnb.so`（ARM 转译）。

### 已跑通

| 环节 | 证据 |
| --- | --- |
| 注入 | `BPP: libbpp constructor, pid=…` |
| 反射 | `il2cpp domain ready` / `assemblies: 145` |
| 每帧 hook | `tick hook installed: EventSystem::Update @…` |
| EGL hook | `eglSwapBuffers hooked @…` + `gl canary drawn 384x27` |
| ImGui | `imgui ready 1920x1080`，面板与中文字体正常渲染 |
| InputSystem | `input source: InputSystem`，`Touchscreen.current` 可读 |
| 数据模型 | `bpp_dump` 输出 `run in_run=1 hero=Common day=1` |
| 文件命令 | `bpp_open` / `bpp_tab` / `bpp_dump` 均生效 |
| 指针输入 | `input tap` 切换页签成功（`pointer down/up at 285,180`） |

### 本轮修复的真实缺陷

1. **`il2cpp_domain_get()` 调用过早** → SIGSEGV 于 `DomainReady()+24`。改为等 `il2cpp_init` hook 或 8 秒宽限期。
2. **ShadowHook `errno=12 Init linker mod failed`** → 所有 hook 失效。打补丁允许 dlopen 监控失败后继续（`hook_func_addr` 不需要它）。
3. **构造期日志无法写入** `/sdcard/Android/data/<pkg>/files`（目录尚未创建）→ 改为每秒重试并补挂崩溃 fd。
4. `UnityEngine.Input` 查错程序集、类查找告警刷屏、枚举 `Collection was modified` 未中止。

### 环境限制（非 mod 问题）

- 模拟器无 Google Play：`PlayCore: Asset Pack Download Error(-11): Play Store app is either not installed`。
- 游戏因此卡在 `Connection Issue / Could not load game data`（42%），`Data.IsManagerCreated()=0`，图鉴/阵容无法用真实数据验证。
- **对照实验**：安装**官方原版 APK**（未做任何改动）得到完全相同的错误界面 → 与 mod 无关。
- 进一步定位：网络是通的（模拟器内 `curl https://data.playthebazaar.com/` 返回 404，游戏自身
  `Ensure Internet` / `Ensure Servers` 均成功）。真正失败点是
  `SQLiteException: Could not open database file ... GameData.db (CannotOpen)`：
  数据库文件本身有效（`SQLite format 3` 头、42,283,008 字节、journal=rollback），
  同一目录下 `sqlite3` 可正常建库读写，说明是游戏内 `libgilzoide-sqlite-net.so`
  经 houdini 转译后打开 FUSE 存储上的数据库失败。官方包同样失败，属模拟器限制。
- 进程内探针（`bpp_dbprobe` 开启）进一步定位：游戏进程里 `open()`/`fstat()`/`fcntl(F_SETLK)` 全部成功，
  但 `sqlite3_open_v2` 对**任何**路径（只读 / 读写 / 显式 vfs=unix / 应用内部目录）都返回
  `rc=14 SQLITE_CANTOPEN`（`unable to open database file`）→ 是游戏自带 SQLite 库在转译层下初始化失败，
  不是文件、权限或网络问题。

### 结论

除「需要游戏进入对局才能产生的数据」外，全部链路已在真实 Android 运行时验证通过。
剩余验证（图鉴、阵容、tooltip、倍速）需要一台装有 Play 商店、能正常进入游戏的设备。
