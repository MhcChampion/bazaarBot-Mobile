# bazaarBot Mobile（大巴扎手机版）

> **2026-10-23 · v0.0.1-r2 版本号修正**：Android versionName / versionCode 自动继承官方底包，当前为 `1.0.12825-production-android-d9601ded` / `12825`；插件版本仍为 `0.0.1`。独立包名和签名保持原样；不因此获得商店官方更新资格。见 [VERSIONING.md](docs/VERSIONING.md)。

> **2026-10-23 · 当前独立包 v0.0.1**：社区接入按用户要求搁置，十胜自动记录默认关闭，手机旧包中的对应开关也已关闭；关注和悬浮窗继续默认开启。应用名为 **The Bazaar - bazaarBot Mobile**，包名 `com.tempogames.thebazaar.bazaarbotmobile`，与原版应用 ID 分开。已安装并验证新旧包不同 UID 共存、新包启动及重启、设置和插件导航；14 项 Python、23 项 Node/MCP 与 release 检查通过。新包需独立登录，登录、购买与真实对局尚未验收；旧槽位目前仍是历史同包 Mod，未卸载或清数据。见 [独立包说明](docs/COEXISTENCE.md) 与 `out/delivery-audit-v0.0.1.json`。

> **2026-10-23 · 历史版本 v0.101**：关注阵容在十胜推荐顶部独立置顶并高亮，分页、筛选、加载失败仍可取消；列表内点关注后立即定位到置顶项，取消恢复原顺序。本机十胜记录改为有效 PNG 完成后去重，失败限次重试，避免截图漏存。20 项 Node/MCP、10 项离线原生记录场景、release 与真机置顶／取消检查通过。真实十胜自动触发仍待验收，社区上传尚未接通。见 `docs/RECORDING.md`、`out/delivery-audit-v0.101.json`。

社区接入方案（用户要求搁置）：[加加自动上传与 BazaarDB 投稿](docs/COMMUNITY-UPLOAD-PLAN.md)。先验证真实结算与最终战档案，再适配加加 V5 上传；BazaarDB 首版通过系统浏览器由用户登录、选图和投稿。协议依据见 [上传条件核查](docs/COMMUNITY-UPLOAD-REQUIREMENTS.md)。

> **2026-10-23 · 历史版本 v0.100**：关注提醒改为游戏原始铜色尖尾挂签与收藏图标，替换黄色文字矩形；挂在实际卡牌顶边并向卡面内垂下，随卡牌投影尺寸缩放。已覆盖安装并核对真实商店目标牌，16 项 Node/MCP 与 release 检查通过。来源和定位见 `docs/FOLLOW-TAG-v0.100.md`，真机证据见 `out/delivery-audit-v0.100.json`。

> **2026-10-23 · 历史版本 v0.99**：修复导航及展开／收起按钮绘制缩放与点击位置脱节，隐藏无附魔默认值 -1，保留独立内容滚动裁切。统一游戏风格边签与拖回区，悬浮地毯加阴影；技能等原生窗口确认高度后显示，避免短暂截断。16 项 Node/MCP、原生 release 及最终 APK 真机界面复查通过；证据见 `out/delivery-audit-v0.99.json`，排查见 `docs/UI-REGRESSION-v0.98.md`。关注扫描与倍速恢复的场内验收单独记录；社区上传仍未接通。

> **2026-10-23 · 历史版本 v0.92**：界面改用游戏原始地毯、菜单边框、按钮、勾选框、标题字体与五档技能框。设置可选当前 APK 内 94 款地毯，卡面与列表更紧凑；悬浮窗轻点只切换下方技能，拖回提示改为游戏返回图标。13 项 Node／MCP、6 项 Python 与 release 检查通过。来源见 `mobilebpp/docs/GAME-THEME.md`，最终真机结果见 `out/delivery-audit-v0.92.json`。商店／战斗关注标签按用户要求延后测试，社区上传仍未接通。

> **2026-10-22 · 历史版本 v0.91**：新增十胜阵容详情、技能与原局截图、70% 透明卡图悬浮窗、原生长按拖拽及左侧停靠、阵容关注和十胜本地档案；标题改为 bazaarBot Mobile。已覆盖安装，详情、短点与返回、精确拖动和停靠及 PixelCopy 截图通过真机验证，18 项自动检查与 release 通过。实际游戏关注贴标和十胜自动触发仍待场景验收，社区上传尚未接通。详见 `mobilebpp/docs/DELIVERY.md`、`mobilebpp/docs/COMPANION.md` 和 `out/delivery-audit-v0.91.json`。

> **2026-10-22 · 历史版本 v0.90**：十胜推荐已切换至巴扎丘Bot当前赛季、最近 7 天的公开对局接口，支持英雄自动读取、点牌筛选、分页和刷新；记录保留 ID、日期、附魔与赛季信息。卡面使用游戏原始三档尺寸／五档品质卡框和完整插画，按原始比例缩放。已覆盖安装并验证真机查询、选牌与取消；8 项 Node／MCP、6 项 Python、release 检查通过。详见 `mobilebpp/docs/DELIVERY.md`、`evidence/qiubot-source.json` 和 `out/delivery-audit-v0.90.json`。下方旧版本内容为历史记录。

> **2026-10-22 · 历史版本 v0.89**：纠正官网数据源对接。线上官网使用 V5，原 V6 源与官网数据不同；现已同步官网 V5 联网、离线及原生阵容库，卡诺克 595 套、总计 5689 套，数据截至 2026-10-05。v0.88“卡诺克无样本需等待”的结论已撤销。详见 `evidence/bpp-builds.source.json` 和 `mobilebpp/docs/DELIVERY.md`。以下旧版本说明仅保留历史。

> **2026-10-22 · 历史版本 v0.88**：已覆盖安装；框外点击收起、面板边缘按钮、紧凑棋盘卡面、隐藏仓库／技能、插画 Alpha 遮罩修复、按英雄显示十胜阵容及选牌筛选已交付。当前数据源卡诺克无样本；卡面未复制动态附魔／实时数字。详见 `mobilebpp/docs/TODO.md`、`mobilebpp/docs/DELIVERY.md` 与 `out/delivery-audit-v0.88.json`。下方旧版本说明为历史记录。

> 历史交付：**v0.87（2026-10-21）**。入口是默认收起的 WebView 面板；已接入中文搜索、3260 条图片、分组阵容、推荐、速度与双把手设置，以及本地调试 MCP。新增资源缺口查询、进程 ID 核对和键盘关闭。交付与验证见 [DELIVERY.md](docs/DELIVERY.md)，开发见 [DEVELOPMENT.md](docs/DEVELOPMENT.md)，剩余事项见 [TODO.md](docs/TODO.md)，MCP 见 [debug-mcp/README.md](debug-mcp/README.md)。下文早期版本记录保留作历史参考。

面向《The Bazaar》Android 版（Unity 6000.3.11f1 / IL2CPP / arm64-v8a）的原生信息增强 mod。
**只读展示，不修改任何游戏数值。**

---

## 1. 目录结构

```
mobilebpp/
├─ native/                 C++ mod 源码（编译为 libbpp.so）
│  ├─ main.cpp             引导线程、每帧 hook、EGL hook
│  ├─ il2cpp_api.*         IL2CPP 导出 API 封装（按名字解析，无硬编码偏移）
│  ├─ game_api.*           游戏数据读取（卡牌图鉴 / 场上卡牌 / 时间缩放）
│  ├─ catalog.*            十胜阵容库匹配（BPP metrics 数据）
│  ├─ catalog_data.*       由 tools/gen_catalog.py 生成
│  ├─ ui.*                 ImGui + EGL 渲染与触摸注入
│  └─ features.cpp         面板 UI（图鉴 / 阵容 / 设置）与输入拦截
├─ tools/
│  ├─ gen_catalog.py       从 BPP 阵容库 JSON 生成 C++ 数据
│  ├─ inject_needed.py     给 libil2cpp.so 加 DT_NEEDED=libbpp.so（LIEF）
│  ├─ patch_apk.py         重打包 APK（替换 libil2cpp + 加入 libbpp）
│  └─ sign_apk.ps1         zipalign + apksigner
├─ build.ps1               用 NDK clang 编译 libbpp.so
└─ docs/

第三方：
- [ShadowHook](https://github.com/bytedance/android-inline-hook)（Apache-2.0）内联 hook
- [Dear ImGui](https://github.com/ocornut/imgui)（MIT）界面
```

## 1.5 交付状态与验证

- 当前版本与产物清单：[docs/STATUS.md](docs/STATUS.md)
- 一次跑完全部静态校验：`pwsh -File tools\verify_all.ps1`
- 首次实机测试：[docs/TESTING.md](docs/TESTING.md)

## 1.6 代码级静态审查

见 [docs/CODE-REVIEW.md](docs/CODE-REVIEW.md)：hook 的 ABI 对照表、反射调用约定、
硬编码假设清单、已修复缺陷时间线，以及哪些问题只能靠真机回答。

配套检查器（`release.ps1` 会依次执行，任一失败即中断）：

```powershell
pwsh -File tools\release.ps1 -ChecksOnly
```

## 2. 构建

前置：NDK r27d、JDK 21、Android build-tools（本仓库路径见 `build.ps1` / `sign_apk.ps1`）。

**一键出包（推荐）**：

```powershell
cd mobilebpp
pwsh -File tools\release.ps1 -Version vX              # 编译→注入 libil2cpp→重打包→对齐签名→校验
pwsh -File tools\release.ps1 -Version vX -UseLibmain  # 备用注入点
```

它会依次跑：`build.ps1` → `inject_needed.py` → `verify_elf.py` → `patch_apk.py` → `sign_apk.ps1`
→ `verify_apk.py`，任何一步失败直接中断，最后打印体积与 SHA256。

**手动分步**：

```powershell
# 1) 编译 libbpp.so
pwsh -File build.ps1

# 2) 给官方 libil2cpp.so 加依赖（只需做一次，可复用）
python tools/inject_needed.py `
  --elf C:\bppwork\libil2cpp.so --lib libbpp.so --out C:\bppwork\libil2cpp.bpp.so

# 3) 重打包 + 签名
python tools/patch_apk.py --apk ..\TheBazaar.apk --lib build\libbpp.so `
  --il2cpp-patched C:\bppwork\libil2cpp.bpp.so --out ..\out\TheBazaar-bpp-unsigned.apk
pwsh -File tools\sign_apk.ps1 -In ..\out\TheBazaar-bpp-unsigned.apk -Out ..\out\TheBazaar-bpp.apk
```

> LIEF 在 Windows 上无法处理含中文的路径，`inject_needed.py` 会自动经 `C:\bppwork` 中转。

## 2.5 首次实机测试

完整清单见 [docs/TESTING.md](docs/TESTING.md)。最快路径：

```bash
adb uninstall com.tempogames.thebazaar
adb install -r out/TheBazaar-bpp-v0.17.apk
# 启动、等 10 秒，注意左下角有没有洋红色条
D=/sdcard/Android/data/com.tempogames.thebazaar/files
adb shell touch $D/bpp_dump; sleep 3
adb pull $D/bpp.log; adb pull $D/bpp_dump.txt
```

一键收集：`pwsh -File tools\collect_report.ps1`（打包日志/数据/设备信息到 `out/bpp-report-*.zip`）。

## 3. 安装（测试）

签名与原版不同，**必须先卸载官方版本**（账号进度在服务端，不会丢；但 Google Play 登录会因签名变化失效，
必要时改用邮箱/其他方式登录）。

```bash
adb uninstall com.tempogames.thebazaar
adb install -r out\TheBazaar-bpp-v0.7.apk
adb logcat -c && adb logcat -s BPP:V > bpp.log
```

### 两个注入变体

| 文件 | 注入点 | 说明 |
| --- | --- | --- |
| `TheBazaar-bpp-v0.7.apk` | `libil2cpp.so` 加 `DT_NEEDED` | 默认版本 |
| `TheBazaar-bpp-v0.7-libmain.apk` | `libmain.so` 加 `DT_NEEDED` | 完全不碰 148 MB 的 libil2cpp，体积小 106 MB；若默认版启动即崩，先试这个 |

重新生成任一变体：

```powershell
pwsh -File tools\release.ps1 -Version vX              # libil2cpp
pwsh -File tools\release.ps1 -Version vX -UseLibmain  # libmain 备用
```

### 安全模式（崩溃逃生）

如果装好后启动异常，可**不重装 APK** 直接禁用 mod：

```bash
adb shell mkdir -p /sdcard/Android/data/com.tempogames.thebazaar/files
adb shell touch /sdcard/Android/data/com.tempogames.thebazaar/files/bpp_safe_mode
```

下次启动时 libbpp 只写一行日志就退出，游戏按原版运行。删掉该文件即恢复。

## 4. 使用

- **三指点击屏幕**：打开 / 关闭面板
- 面板分四页：图鉴 / 阵容 / 设置 / 诊断
- 设置页可切 0.5× / 1× / 2× / 4×（修改 `Time.timeScale`）
- 面板打开时会禁用游戏 UI 的射线检测（复用已有 CanvasGroup，blocksRaycasts=false），关闭后恢复

## 5. 运行时行为（排障用）

`logcat` 关键行：

| 日志 | 含义 |
| --- | --- |
| `libbpp constructor` | 注入成功（最重要的一行，没有它说明 linker 没加载我们） |
| `libbpp.so` 加载后 `safe mode marker found` | 命中了安全模式 |
| `il2cpp api bound (handle=...)` | 反射层就绪 |
| `il2cpp domain ready` | 游戏已初始化 |
| `assemblies: N` + 列表 | 程序集枚举成功 |
| `tick hook installed: EventSystem::Update @...` | 每帧 hook 成功 |
| `eglSwapBuffers hooked` | 渲染 hook 成功 |
| `imgui ready 1080x2400 scale=...` | ImGui 初始化 |
| `touchCount=N boxedClass=System.Int32` | 验证 runtime_invoke 装箱约定 |
| `catalog built: N cards` + `sample card[0..4]` | 图鉴数据读取成功 |
| `catalog not ready (IsManagerCreated=…, GetStatic=…)` | 静态数据还没就绪（每 15s 一条） |
| `boardSection N -> 名字 (card=… go=…)` | 棋盘分区归因 |
| `gl canary drawn WxH` | GL 探针画出来了（EGL hook + 上下文 OK） |

### 首次测试判定表

按顺序看四个信号，基本能一步定位问题层：

| 现象 | 结论 | 下一步 |
| --- | --- | --- |
| 连 `bpp.log` 都没有 | 注入没生效（linker 没加载 libbpp） | 换 `-libmain` 版重试 |
| 有日志，但**看不到洋红色带**（启动 45s 内） | EGL hook 没命中（swap 变体/时机） | 发日志，我按 `eglSwapBuffers hooked` / `eglGetProcAddress hooked` 判断 |
| 有洋红色带，但**三指呼不出面板** | ImGui/输入问题 | 发日志 + `bpp_dump.txt`；可用 `bpp_open` 强制开面板验证 |
| 面板出来了，但**图鉴为空** | 数据层问题 | `bpp_dump` 看 `catalog_count` 与 `IsManagerCreated` |

洋红色带是**故意**加的诊断色块，只在前 45 秒出现；确认链路后会在下一版默认关闭。

如果启动即闪退，优先看 `BPP` 之外的系统日志：
`adb logcat -d | findstr /i "bpp DEBUG crash tombstone"`。

首次运行后还会写一份完整日志：

```
/sdcard/Android/data/com.tempogames.thebazaar/files/bpp.log
adb pull /sdcard/Android/data/com.tempogames.thebazaar/files/bpp.log
```

面板里的「诊断」页可以看到 hook 状态、图鉴/棋盘数量、最后一条错误。

## 6. 版本记录

### v0.34

- **图鉴构建移出主线程**：`GetCardMap()` 是整表 SQLite + JSON 反序列化，之前在 Unity 主线程
  每秒尝试一次。真机上表现为加载到 75%（静态数据初始化）时闪退；现在改由后台线程取字典，
  主线程只做轻量枚举（几千次属性读取）。
- 对应日志：`catalog dictionary fetched on worker thread` / `catalog built: N cards`。

### v0.31 – v0.33（模拟器根因定位）

- **v0.31** 进程内数据库探针（`bpp_dbprobe` 开关）：在游戏进程里复现 `sqlite3_open_v2`
- **v0.32** 探针扩展：`fstat` / `fcntl` 锁 / 只读 / 显式 vfs / 内部目录全部尝试
- **v0.33** 探针默认关闭，出正式包（含 libmain 备用版）

结论：MuMu 里游戏自带 SQLite 在 houdini 下初始化失败——`open()`/`fstat()`/`fcntl()` 全部成功，
但 `sqlite3_open_v2` 对任何路径都返回 `CANTOPEN`。官方原版 APK 同样无法通过，属模拟器限制。

### v0.22 – v0.30（真机/模拟器联调）

- **v0.22** `il2cpp_init` 门控：不再在 IL2CPP 初始化前调用 `il2cpp_domain_get`（曾崩在 `DomainReady()+24`）
- **v0.23** ShadowHook 失败原因可视（`errno`/`errmsg`）；Unity API 调用全部移回主线程
- **v0.24** 允许 dlopen 监控失败（`hook_func_addr` 不依赖它）→ hook 全部恢复
- **v0.25** `UnityEngine.*` 查找改用 `KlassAny`；类查找告警限流；枚举异常时放弃本次图鉴构建
- **v0.26** 初始化宽限期缩短到 8 秒
- **v0.27** 新增 `bpp_tab N` 文件命令（不依赖触摸切页）；输入诊断
- **v0.28** 摸清 `Touchscreen.current` 可读
- **v0.29** `ReadValue()` 泛型重载适配（Vector2Control 只有 0 参版本）
- **v0.30** 输入改为「触摸优先、鼠标兜底」，`input tap` 可直接操作面板

### v0.21

- **Unity 文本 HUD 升级为可用的阵容面板**：启用 `bpp_unityui` 后不再只显示一行计数，而是把
  棋盘 / 仓库 / 对手 / 商店 / 技能 摘要直接写进游戏自己的 TMP 标签：

  ```
  BPP  Day 4  Hour 11  Vanessa  [战斗]
  棋盘: 火拳, 毒刃, 石像鬼, …
  仓库: 药水, 铁砧
  对手: 巨蛇, 火把
  商店: 神秘卷轴
  技能: 连击
  图鉴 512 张 · 三指/文件控制
  ```

  这条路**不依赖图形 API**，所以 Vulkan 设备上也有可见的阵容面板（只是不能用触摸交互，
  刷新间隔 1.5 秒）。标签尺寸调成 760×520、字号 22 以容纳多行。

### v0.20

- **编译告警清零**：`build.ps1` 对 `native/` 打开 `-Wall -Wextra`，当前 0 warning。
- 修掉一个真实缺陷：HUD 文本用 `%d` 接 `size_t`（`board.size()`），在 AArch64 可变参数下属于
  未定义行为；同时那行每秒还会**完整拷贝一次图鉴**（含数百个 std::string）——
  已改为新的 `game::CatalogSize()`（只取 size）。
- 清理死代码/未用符号（`StaticFieldValuePtr`、`g_tooltip_card`、`g_tab`、`g_set_go`、
  `M_set_timeScale`(features)、`M_get_gameObject`、局部 `canvas`）。

### v0.19

阵容面板补全到 BPP 的粒度：

- **商店/选择集**：`Data.CurrentState` → `RunState.GetCardsInSelectionSet()`（商人货架、奖励选择）
- **技能列**：`Player.Skills`（`List<SkillCard>`）
- **战斗状态**：`Data.IsInCombat` → 面板标题显示 `[战斗中]`
- `bpp_dump.txt` 的 `run` 行增加 `in_combat/day/hour/shop/skills` 计数，并追加对应卡片行

面板现在五栏：棋盘 / 仓库 / 对手 / 商店·选择 / 技能。

### v0.18

数据层按 `docs/game-api-map.md`（子代理整理的 API 地图）改成**逻辑模型优先**：

- **阵容改读 `Data.Run`**：`Data.Run` → `Player`(0x40) → `Hand`(0x28) / `Stash`(0x30)
  → `IPlayerInventory.GetItems()`；同时拿到 `Day`(0x30)、`Hour`(0x2C)、
  `Player.Hero`(0x20) 与 `Run.Opponent`(0x48) 的手牌。
  读取顺序：`Data.Run` → `ClientBoardModel` → UI 扫描，日志打 `board source: …`。
- **暂停走游戏自己的接口**：`GameServiceManager.PauseOrUnpauseGame(bool)`（找不到时退回 `Time.timeScale`），
  避免用全局 timeScale 冻结整个引擎。
- **静态数据兜底**：`GetStatic()` 若返回 `Task<JsonGameDataManager>` 则检查 `IsCompleted` 后取 `.Result`；
  再不行直接读 `Data.manager` 静态字段。

### v0.17

- **Unity 原生文本回退（opt-in 探针）**：当 GL 覆盖层不可用时（很可能是 Vulkan），
  改用**反射驱动游戏自己的 TMP 文本**：找一个现成的 `TMP_Text` 实例 → `Object.Instantiate`
  克隆 → `Transform.SetParent` 挂到根 Canvas → 设 anchors/anchoredPosition/fontSize → 改 `text`。
  全程不碰图形 API，所以 GLES / Vulkan 都能显示。

  因为无法真机调布局，这一版只作为**探针**：默认关闭，创建标记后启用：

  ```bash
  adb shell touch /sdcard/Android/data/com.tempogames.thebazaar/files/bpp_unityui
  ```

  启用后屏幕左上角会出现一行 `BPP | 图鉴 N | 棋盘 M | 三指开关`，
  同时日志会打印每一步（`unityui: 12 TMP texts, source=…` / `clone=…` / `spawned label=…`），
  任何一步失败都能定位。

### v0.16

- **tooltip 文本直接增强（不依赖图形 API）**：hook `CardTooltipData::RenderTooltip` 的返回值，
  用 `System.String::Concat(string,string)` 把 BPP 信息追加到游戏自己的提示文本后面：

  ```
  …（游戏原文）…

  [BPP] Vanessa  T3  S2
  标签: Weapon/Food
  十胜阵容出现 3 次
  ```

  关键点：**GLES 和 Vulkan 都能用**——它只改字符串，不碰渲染。这绕开了 v0.15 发现的
  "Vulkan 下 GL 覆盖层不可用"的整条风险线。
- 结果字符串按（原文对象, 后缀对象）缓存并持有 GC handle，避免每帧重复分配；
  后缀每 2 秒或换卡时重建。

### v0.15

- **图形后端自检**：`libunity.so` 带完整 Vulkan 支持，而 Unity 6 的 Android 默认是
  Auto Graphics API（优先 Vulkan）。如果游戏真跑 Vulkan，`eglSwapBuffers` 根本不会被调用，
  GL 悬浮层永远不出现——而且**不是 hook 写错了**。
  现在启动 6 秒后打一行：

  ```
  gfx: ticks=360 egl_swaps=356 vulkan_loaded=0
  ```

  - `egl_swaps>0` → GLES 路径，悬浮层可画
  - `egl_swaps=0` → 追加一条警告，说明极可能走了 Vulkan，GL 覆盖层不可用，
    但**数据通道（bpp_open / bpp_dump）仍然可用**

  同一条日志里的 `ticks` 还能验证每帧 hook 是否真的在跑。

### v0.14

- **界面缩放真正生效**：之前那个滑块只改了个没人读的变量；现在接到
  `ImGuiStyle::FontScaleMain`（1.93 已移除 `io.FontGlobalScale`），拖动即时生效。
- **设置持久化**：速度 / tooltip / 缩放 / 悬浮按钮写入
  `files/bpp_settings.txt`，下次启动恢复；保存的 `0x`（暂停）不会被带进新会话。
- **悬浮按钮默认关闭**：那个常驻右上角的按钮点击会穿透到游戏 UI（可能误拖卡牌）。
  默认改为关，用三指手势或 `bpp_open` 打开；需要常驻按钮可在设置里开启。
- **图鉴详情补全**：选中卡片后显示 英雄 / 品质 / 体型 / 标签 / 十胜阵容出现次数 / 描述；
  搜索框同时匹配名称、英雄、标签。

### v0.13

- **崩溃归因**：安装 SIGSEGV/SIGBUS/SIGABRT/SIGILL/SIGFPE 处理器，崩溃时先往 `bpp.log`
  写一行（信号、故障地址、PC、是否在 bpp hook 内、哪个 hook），然后**链回原有处理器**
  （Unity 自带的那个）并重新 raise——只加线索，不吞崩溃。

  ```
  *** FATAL signal=11 addr=0x0 pc=0x7a3c... hook_depth=1 in=EventSystem.Update ***
  ```

  两个 hook 都用 RAII `HookScope` 包住，所以 `hook_depth>0` 基本可以断定崩在 mod 的调用链里；
  `hook_depth=0` 则更像游戏自身或注入层的问题。

### v0.12

- **对象头/数组偏移改为运行时查询**：不再写死 `0x10` / `0x20`，改用游戏导出的
  `il2cpp_object_header_size()`、`il2cpp_offset_of_array_length_in_array_object_header()`、
  `il2cpp_offset_of_array_bounds_in_array_object_header()` 现算，启动日志会打印：

  ```
  layout: object header = 0x10
  layout: array data = 0x20 (length@0x18 bounds@0x8)
  ```

  若某版本 Unity 改了布局，这两行会直接告诉你，而不是让 mod 默默读错。
  `offsets.h` 里的常量保留为兜底值，仍由 `check_offsets.py` 校验。

### v0.11

- **修复数组步长 bug（影响所有数组读取）**：`ArrayAt` 原来用 `il2cpp_class_value_size` 算元素步长，
  对引用类型数组（`CardController[]`、`BazaarCard[]`）是错的——会读到错误地址。
  改用 `il2cpp_array_element_size`，并以"引用类型=8 字节指针"作为兜底。
- **阵容改读权威模型**：不再只扫 UI 上的 `CardController`，优先读
  `ClientBoardModel.bazaarBoardModel`（`Singleton<T> : MonoBehaviour`，场景内可直接 Find）：
  `PlayerCardHand` / `PlayerStorageHand` / `OpponentCardHand` 三条数组。
  好处：面板关着也能读、卡牌被对象池隐藏也不影响、仓库/对手区不再靠猜。
  读不到模型时自动退回原 UI 扫描，日志会打 `board source: ClientBoardModel (...)`。

### v0.10

- **GL 探针**：启动后 45 秒内，用最原始的 `glScissor + glClear`（不经过 ImGui、不需要 shader）
  在屏幕左下角画一条洋红色带。看到它 = EGL hook 与 GL 上下文都是通的；
  看不到 = swap 路径有问题，与 ImGui 无关。日志同时打印 `gl canary drawn WxH`。
- 探针先于 ImGui 初始化绘制，所以即使 ImGui 崩溃也能看到。

### v0.9

- **文件命令通道**：即使悬浮层画不出来、触摸也读不到，仍可通过文件驱动 mod：

  ```bash
  D=/sdcard/Android/data/com.tempogames.thebazaar/files
  adb shell touch $D/bpp_open     # 打开面板
  adb shell touch $D/bpp_close    # 关闭面板
  adb shell touch $D/bpp_dump     # 导出全量图鉴+棋盘到 bpp_dump.txt
  adb pull $D/bpp_dump.txt
  ```

  `bpp_dump.txt` 是制表符分隔，含每张卡的 guid/名称/英雄/品质/体型/标签/阵容库出现次数，
  以及当前棋盘/仓库/对手区。**这条通道把"数据是否正确"和"UI 是否能画"彻底解耦**。
- 诊断页加「导出数据」按钮。

### v0.8

- **拦截 `eglGetProcAddress`**：`libunity.so` 虽然直接 import 了 `eglSwapBuffers`，但也 import 了
  `eglGetProcAddress`——Unity 可能通过它取 `eglSwapBuffersWithDamageKHR/EXT` 并调用，
  那样原来的 hook 永远不会触发、悬浮层什么都不画。现在同时 hook 解析器，把 swap 系列函数
  换成我们自己的包装（带原函数指针回代）。
- 同一帧内若两条路径都触发，用 3 ms 门限去重，避免重复渲染。
- 顺带核实：`libunity.so` 的 `egl*` 导入列表里没有 `WithDamage`，说明它至少是"直连 + 可能运行时解析"混合模式，两条都要堵。

### v0.7

- **安全模式**：`/sdcard/Android/data/com.tempogames.thebazaar/files/bpp_safe_mode` 存在时
  只写一行日志就退出，不装任何 hook——崩溃时不用重装 APK 就能回退。
- **备用注入点**：`libmain.so` 加 `DT_NEEDED`（完全不动 148 MB 的 libil2cpp），
  产物小 106 MB；`tools/release.ps1 -UseLibmain` 一键生成。
- **一键出包脚本** `tools/release.ps1`：编译→注入→ELF 校验→重打包→签名→APK 校验，五步带断言。
- 新增 `tools/verify_apk.py`：检查 APK 内库是否齐全、`DT_NEEDED` 是否生效、
  未压缩 `.so` 是否 16 KiB 对齐。

### v0.6

- **加载顺序更安全**：不再从构造函数里 `dlopen("libil2cpp.so")`（会与 libmain 自己的加载竞争），
  改为等 `dlsym(RTLD_DEFAULT, "il2cpp_domain_get")` 出现后用 `RTLD_NOLOAD` 取句柄。
- **修 CanvasGroup 拦截**：原来直接 `AddComponent<CanvasGroup>`，而 `CanvasGroup` 是
  `[DisallowMultipleComponent]`，重复添加会失败导致"打开面板时游戏仍能点击"。改为先
  `GetComponent` 复用已有的。
- 日志增强：图鉴建好后打印前 5 张卡样例；未就绪时每 15 秒打印一次
  `IsManagerCreated / GetStatic / GetCardMap` 状态；棋盘首次出现某分区时打印
  `boardSection=… card=… go=…`（用来判断商店/棋盘卡是否混在同一分区）。

### v0.5

- **字段偏移全部常量化并自动校验**：新增 `native/offsets.h`，把 `CardController.CardData(0x90)`、
  `boardSection(0x98)`、`Card.TemplateId(0x18)/Size(0x40)/Tier(0x50)`、
  `CardTooltipData._cardTemplate(0x18)`、`Touch.m_Position(0x04)/m_Phase(0x24)` 等抽成常量；
  `tools/check_offsets.py` 对着 dump.cs 校验每个常量，游戏更新后偏移漂移会直接报错。
- 触摸读取改用 `m_Phase` 判定按下/抬起（Ended/Canceled 不再算按下），点击判定更准。

### v0.4

- **修复致命的类查找 bug**：`KlassAny("", Name)` 只按"空命名空间"查，带命名空间的类型全部返回 null。
  实测 dump：`Data` 在 `TheBazaar`、`CardTooltipData` 在 `TheBazaar.Tooltips`、
  `JsonGameDataManager` 在 `TheBazaar.DataManagement.Json`、`Input/Screen/Time` 在 `UnityEngine`。
  现在 `KlassAny` 先按给定命名空间快查，再退化为**全程序集按简单名扫描**（`il2cpp_image_get_class`）。
- 新增 `tools/check_lookups.py`：把源码里每一处 `Klass/KlassAny/KlassByName` 的命名空间+类名
  对着 dump.cs 校验，现在全部通过。
- 触摸读取加一次性诊断日志（打印 `il2cpp_runtime_invoke` 返回值类型，验证装箱约定）。

### v0.3

- **修复 tooltip hook 返回值**：`RenderTooltip` 返回 `string`，hook 必须把原返回值放回 `x0`，
  否则游戏拿到野指针（v0.2 的写法是错的，会崩）。
- tooltip 叠加卡新增**标签（ECardTag 名称）**与**十胜阵容出现次数**。
- 图鉴 / 阵容推荐改为 **1 Hz 缓存**，避免每帧拷贝整个图鉴（原来每帧几百次字符串分配）。
- 新增 `tools/verify_elf.py`：按 bionic linker 的不变量检查 ELF（段对齐/不重叠/DT_NEEDED 可解析/
  INIT_ARRAY 在 LOAD 内），用于验证注入产物。

### v0.2

- **物品提示增强**：hook `CardTooltipData::RenderTooltip(TooltipBuilder)`，捕获当前 tooltip 的
  `_cardTemplate`（偏移 0x18），在屏幕左下角叠加静态信息卡（名称/英雄/品质/体型/描述）。
  可在设置页开关。**数值级升级/附魔预览尚未实现**（需要逐方法还原 BPP 的
  `RenderStylizedAttributeValue` 补丁，留待实机验证后做）。
- **文件日志** + **诊断页**，便于无 adb 时抓日志。
- 棋盘分区改正：`BazaarBoard.ESections` = Opponent(0) / Player(1) / Storage(2)，
  之前按 2/3 猜的分组是错的。

### v0.1

- 注入、反射层、每帧 hook、ImGui 面板、图鉴、阵容、倍速、三指唤出。

## 7. 已知限制

- 图鉴数据依赖 `Data.IsManagerCreated()` 为真（进入对局/主界面后才有），冷启动时面板可能为空
- 棋盘只统计带 `CardController` 的卡牌；商店卡牌未单独识别
- 倍速只改 `Time.timeScale`，未像 PC 版那样逐方法控速；联机对战是否允许待验证
- tooltip 只做静态信息叠加，未做数值预览
- 阵容库为构建时快照，更新需重跑 `tools/gen_catalog.py`

## 8. 数据来源与许可

- 阵容库：`https://bpp-metrics-v6.bazaarplusplus.com/builds/latest.json`（BazaarPlusPlus 公开数据，MIT）
- 游戏类型/偏移：`work/il2cpp-dump/`（Il2CppDumper v6.7.46-lxraa.1）
- 本 mod 不含任何游戏资源文件
