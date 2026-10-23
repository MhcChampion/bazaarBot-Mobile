# 开发与调试

## 修改与重建

- 原生逻辑：`native/`，C++17，Android arm64 API 26。
- 界面：编辑 `web/panel.html`、`panel.css`、`panel.js`。构建时自动生成 `native/bpp_web_assets.h`。
- Java 命令桥：编辑 `java/bpp/Ui.java`。构建自动执行 javac → d8 → `native/bpp_dex.h`，编译用注解 stub 不会打包入 DEX。
- WebView 操作通过 Android UI 线程执行；JS 指令进入队列，由 Unity 的 `EventSystem.Update` 消费后才可调用游戏 API。
- 日志、状态与设置：`/sdcard/Android/media/com.tempogames.thebazaar/`。命令和安全模式同时兼容旧 `Android/data/.../files` 目录。

```powershell
pwsh -File mobilebpp/build.ps1
pwsh -File mobilebpp/tools/release.ps1 -Version v0.92
pwsh -File mobilebpp/tools/verify_all.ps1 -Apk out/TheBazaar-bpp-v0.92.apk
npm test --prefix mobilebpp/debug-mcp
```

签名仍使用已有 `mobilebpp/build/bpp.keystore`，清理对象文件不会删除密钥。不要用清空游戏数据或卸载来做普通覆盖升级。

## 卡牌数据与图片

v0.92 的游戏界面素材可用 `python mobilebpp/tools/extract_theme.py` 重建。它从 APK 提取 94 款地毯、五品质技能模型框和菜单 Sprite，使用原始 UV、几何与九宫格边距；来源及设计说明见 [GAME-THEME.md](GAME-THEME.md)。`web/theme-report.json` 随页面嵌入，`web/theme/` 随 APK 打包。修改 CSS 后应检查 Android 真机；透明 WebView 的背景绘制与桌面浏览器存在差异，Android 使用同一原生 View 绘制底板与 NinePatch 游戏边框，并隐藏浏览器预览的 CSS 边框层。原生底板只在主面板展开时显示，悬浮和收起时隐藏。

生成输入：游戏缓存 `work/GameData.db` 和官方中文翻译 SQLite `work/zh-CN.bytes`；从开发设备的 `files/prod/cache/` 获取。读取 SQLite 时使用只读模式。

```powershell
# 使用支持 UnityPy、Pillow、lief、pyelftools 的 Python（也可设置 BPP_PYTHON）
python -m pip install -r mobilebpp/tools/requirements.txt
python mobilebpp/tools/gen_card_table.py
python mobilebpp/tools/extract_art.py
python mobilebpp/tools/gen_card_table.py
```

第一条生成静态元数据与中文搜索表。图片提取沿 `CardData.cardGUID → cardMaterial → CF_*_D` 引用和技能图标精确名字映射进行，再运行生成器更新图片 URL。默认从本地 `TheBazaar.apk` 读取，不上传游戏资产。

漫反射贴图的 Alpha 是游戏特效遮罩，不能用作插画透明度。v0.88 在缩小前转 RGB，避免透明区域中的刀身等 RGB 被重采样抹掉。升级旧图片运行 `extract_art.py --refresh-card-art`；补充图标／肖像仍保留各自真实透明度。

当前 3375 条记录中 2993 条获得官方中文名，3260 条有可提取图片。其余 115 条按无 ArtKey、目录可查但尚未导出、当前 APK 尚未解析三种原因逐项记录于 `web/art-report.json`。`web/translation-report.json` 区分 376 条官方缺译和 6 条官方保留英文名称；未伪造中文译名或图片来源。

提取还覆盖 ArtKey 在目录中对应的别名卡牌、奖励图片和事件／商人／战斗肖像。`extract_art.py --supplemental-only`（兼容别名 `--icons-only`）只补共享图标与肖像；`--bundle card_karnok_assets_all.bundle` 可单独调试一个卡牌包。导出的 WebP 在打包时写到 `assets/bpp/art/`，WebView 通过 Android asset URL 加载。

## MCP 与状态

v0.90 推荐数据使用 [巴扎丘Bot](https://bazaarqiubot.com/runs)公开查询接口，参考源码 [CodeHuba/bazaarqiubot](https://github.com/CodeHuba/bazaarqiubot) 的 `web_runs/app.py` 和 `plugins/bazaar_plugin/runs_query.py`。生产请求：

```text
GET https://bazaarqiubot.com/api/runs?hero=Karnok&days=7&min_wins=10&page=1
GET https://bazaarqiubot.com/api/runs?hero=Karnok&days=7&min_wins=10&page=1&cards=Silver%20Stake
```

每页 5 条，响应含 `runs/total/page/pages/scope`；服务端限定当前赛季／阶段并按日期倒序。多张卡使用英文名以 `+` 连接，再整体 URL 编码；服务端要求同时包含。巨龙英雄的 API 名称为 `The Dragons`，界面归一为 `TheDragons`。数据日期和真实胜负在界面展示，十胜记录数不等于总体胜率。

`QueueQiuRuns` 在独立网络线程处理，快速点选合并 300ms，查询启动至少间隔 1100ms，以遵守公开端点每 IP 每秒一次的限制。序号变化使旧请求回复作废，前端也核对完整查询 key。`bpp_runs.json` 保存查询 key、原始返回、棋盘卡图、抓取时间；联网失败仅回退同 key 缓存，标记 `cached:true`。页面每 5 分钟在展开状态刷新。旧 V5/V6 库不再参与 WebView 推荐。

记录 ID 与完整原始数据保留在缓存和卡片 DOM 的 `data-run-id`。v0.91 已接入 `GET /api/runs/<id>`，详情与列表共用网络线程及查询启动间隔，按 ID 拒绝过时回复。交互、关注坐标、原生悬浮拖拽、PixelCopy 与社区接入边界见 [COMPANION.md](COMPANION.md)。

卡图来自 `/api/card-preview/<guid>` 和对局中的 `img`。只接受 `https://s.bazaardb.gg/` 卡图地址；Small／Medium／Large 使用完整的 `@400S`／`@400`／`@400L` WebP，画布分别 200×400、400×400、600×400。不要猜测 `@200S` 地址（实际 404）。v0.91 先显示 APK 本地图，远程图下载并解码后再覆盖；相同推荐不重新创建 DOM。原局截图仅接受 usercontent.bzdb.network 与 s.bazaardb.gg 的 HTTPS 地址。

原始游戏框执行 `python mobilebpp/tools/extract_frames.py`，输入为 APK 的 `assets/aa/Android/cardframes_assets_all.bundle`，从 `CardFrame_<quality>_<S/M/L>` mesh 的 `_0` 子网格按 X/Z 投影并使用原 UV／漫反射贴图生成 15 个 WebP。其他子网格属不同材质，不混入。`web/frame-report.json` 记录来源与真实比例，青铜 S/M/L 约为 0.499152／0.991844／1.487993。构建嵌入几何报告并将框写入 `assets/bpp/frames/`；JS 根据各品质具体比例设置宽度，完整插画以 `contain` 显示。辅助面板未复制游戏实时数字或动态附魔特效。

见 [debug-mcp/README.md](../debug-mcp/README.md)。MCP 可查询离线卡表、读取日志和快照、发出固定调试命令；不提供任意 shell 或任意文件路径。状态 JSON 包含请求速度 `speed` 和引擎实际速度 `time_scale`，渲染判断使用真实 EGL 帧数与 Vulkan 库状态。

v0.86 快照增加 `process_id`，MCP 会和当前设备进程核对。联网摘要通过 Android [JSONObject](https://developer.android.com/reference/org/json/JSONObject) 结构解析，不再按引号／冒号猜测卡数和英雄数；无效结构保留原缓存。

v0.87 搜索框的“完成”按钮、搜索确认和面板收起会在 Android UI 线程调用输入法管理器隐藏软键盘；MCP `hide_keyboard` 使用相同路径。它不模拟会被 Unity 处理的系统返回键。

本地页面预览：`python mobilebpp/tools/preview_ui.py` → `http://127.0.0.1:8765`，只监听本机；它不连接游戏，不能用来证明原生命令或设备渲染通过。

当前 `preview_ui.py` 使用 `evidence/qiubot-karnok*.sample.json` 与 `qiubot-detail.sample.json` 的真实 API 样本，支持筛选、分页、详情与悬浮预览，不发线上查询或操作手机。展开后的原生 WebView 覆盖全窗，HTML 面板占左侧 56%，右侧透明区域消费首次点击并收起；普通收起恢复小入口，阵容悬浮则缩为独立的小尺寸视图。

真机自动化长按需至少 350ms。该设备系统 long_press_timeout 是 300ms，adb draganddrop 的默认停留不足；使用 motionevent DOWN → 等待 600ms → MOVE → UP 验证，不修改设备长按设置。所有触屏先读取当前 XML／截图并核对辅助窗状态，不使用旧坐标点击游戏。

## 尚需设备验证

以 [TODO.md](TODO.md) 为准。静态校验只证明符号／偏移／ABI 和包结构；游戏战斗中的速度体验、长期稳定性与不同手机兼容性需单独记录。Tooltip 仍按原要求暂缓，默认不安装其 hook。
