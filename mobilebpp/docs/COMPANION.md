# bazaarBot Mobile 阵容助手（v0.92）

## 手机交互

- 点十胜记录打开详情；先显示已经读到的物品与技能，再以同一记录 ID 更新详情。原局截图和局数等信息折叠显示，避免信息占满屏幕。
- 每条记录的“悬浮”切换为独立的小尺寸 WebView；v0.92 使用可自选的游戏原始地毯，替换 v0.91 的黑色半透明圆角底，窗外触摸由游戏继续接收。
- 轻点悬浮窗只切换下方技能，不打开完整详情；技能使用五档品质的原始模型框，不显示文字。完整详情从列表或参考阵容入口进入。设置可从 94 款当前 APK 地毯中选择，详见 GAME-THEME.md。
- Android 原生 OnTouchListener 在长按 350ms 后，以 MotionEvent.getRawX/getRawY 的屏幕坐标移动，屏幕边界内限位；浏览器预览才使用 PointerEvent。拖到左侧中央并松手恢复原入口，拖拽时只出现游戏圆形返回图标，替换旧黄色长条；原地长按不会误收回。悬浮窗没有关闭按钮。
- 一次关注一个阵容，可重复点击取消；目标包括物品和技能。关注开关与悬浮窗开关默认开启。
- 选择的阵容、关注目标、地毯和拖拽位置保存在 WebView 本地存储；功能开关保存在原生设置文件中。启动时仍保持收起，浮窗由用户主动选择显示。拖回入口后，主面板的“参考阵容”保留查看和恢复悬浮操作。

## 卡图与刷新

界面先显示 APK 内本地图。远程完整插画由 `Image` 预取并完成解码后覆盖，下载期间本地图保持可见；失败继续使用本地图片。解码缓存限制 160 个 URL。相同推荐内容不重建列表 DOM；正常游戏状态刷新不会重复清空卡图。首次远程下载仍需要网络时间，但不再用空卡框等待。

## 数据和原生命令

- 详情：`GET https://bazaarqiubot.com/api/runs/<id>`，独立排队，与列表查询共用线程和至少 1100ms 的查询启动间隔。界面按 ID 拒绝过期详情回复。
- `watch`：JSON GUID 数组，最多 80 个，原生严格校验；关闭关注时传空数组。
- `float`：宽、高、x、y，单位为 WebView CSS 像素／Android dp；原生按屏幕密度换算并限位。`float_move`、`float_drag`、`dock` 分别移动、显示停靠区和恢复入口。
- 游戏标记：Unity 主线程读取活动 `CardController.CardData.TemplateId`，结合商店选择或战斗对手的物品／技能；通过实际相机投影 BoxCollider 世界包围盒。Android 标签显示在投影卡框上方，关闭点击与焦点。没有使用按屏幕比例猜测的固定牌位坐标。
- 状态包含 `watch_count`、`markers`、`marker_status`，供真实场景验收和将来的 Tooltip 定位使用。缺少有效坐标时不显示标签。

## 十胜记录和社区接入边界

原生只读 `Data.Run.Victories/Losses`。开启记录后，到十胜且离开战斗时保存当前棋盘／技能／仓库、品质／位置／附魔、胜负和截图。PixelCopy 从 Unity 的 SurfaceView 获取图像，不把辅助面板拍进游戏截图；PNG 压缩写盘在后台线程。当前进程内及同一已保存指纹防止重复记录，下一局重置。

输出在 `/sdcard/Android/media/com.tempogames.thebazaar/`：`bazaarBot-tenwin-<timestamp>-<hero>.json/.png`。JSON 使用 `bazaarbot-mobile-local-v1` 并明确标记 `community_upload=not_connected`。

社区上传没有完成：丘Bot当前 `/api/ingest` 需要运营者的 `X-Ingest-Token`，并非公开截图投稿；README 旧例子和当前 app.py 的鉴权及请求体不同，应以源码为准。大巴扎加加 `/bundles` 接收有版本、摘要、清单和战斗投影的专用包，普通截图和 GUID 列表不能直接冒充。没有发送测试对局或虚构资料到生产社区。设置会明确显示“当前仅保存本机”。来源核对见 `evidence/community-upload-v0.91.json`。

只读截图测试命令为 `touch /sdcard/Android/media/com.tempogames.thebazaar/bpp_capture`，输出 `bazaarBot-check-*.png`；该命令不伪造十胜、不改变游戏数值，也不触发上传。测试截图不能作为十胜触发流程的验收。
