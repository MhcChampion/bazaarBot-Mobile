# v0.100 卡牌关注挂签

用户参考图要求把黄色“关注”文字矩形改为游戏风格的小挂签，从卡牌顶边向内垂下。

## 素材与样式

- 挂签使用当前游戏 APK 的 `PlayerProfile_Leaderboard_ItemBanner_TUI`，对应新增 `theme/follow-banner.webp`；顺时针转 90°，使尖尾朝下。
- 中央复用 `Icon_Menu_Favorite_TUI` 收藏图标（已有资源别名 `star-icon.webp`，实际图形为心形）。不再绘制“关注”文字。
- 在 Android Canvas 中组合一次后缓存 Bitmap；每个标记使用独立 Drawable，避免多个标记共享绘制边界。
- 新素材加入 `tools/extract_theme.py` 的提取表，`theme-report.json` 保留原始 Sprite、资产文件、像素尺寸与边框信息。

## 挂载位置

`ProjectCard` 投影真实包围盒的 8 个角点，同时返回卡牌顶边中心与显示宽高。挂签宽度随卡牌缩放，受显示宽度的 40%、高度的 25% 和 10～20 dp 范围约束；按原始素材 198:256 的旋转比例计算高度。挂签外框上方留 2 dp 的压边位置，主体垂到卡面内。

侧栏展开时隐藏标记，标记保持不可点击。图标继续依赖实际关注命中结果，不向场景中添加固定牌位。

## 验证

16 项 Node/MCP 回归通过，日志 `out/tests-v0.100.log`；原生、静态、ELF、签名、APK 对齐检查通过，日志 `mobilebpp/build/release-v0.100.log`。新增 WebP 实际解码通过，APK 内资源字节与本地来源一致。

真机结果与截图记录在 `out/delivery-audit-v0.100.json`；未获取实际场景前不把挂签位置记为验收通过。
