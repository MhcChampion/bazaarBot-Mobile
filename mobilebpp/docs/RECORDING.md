# 十胜记录与上传状态（v0.101）

“十胜自动记录”开关控制本机档案，默认开启；它目前不发送社区上传请求。

## 本机记录

读取真实对局快照，在十胜、非战斗且棋盘非空时，保存 `bazaarBot-tenwin-*.json` 和 `.png` 到 `/sdcard/Android/media/com.tempogames.thebazaar/`。JSON 包含棋盘、技能、品质、附魔和胜负，格式为 `bazaarbot-mobile-local-v1`，`community_upload` 明确为 `not_connected`。截图使用 PixelCopy 读取游戏 SurfaceView，辅助界面不进入截图。

v0.101 修复“JSON 写出后即去重，截图失败不再重试”的问题：JSON 原子写入后只排一个截图请求；收到有效 PNG（签名及 IEND 完整）才提交完成标记。明确失败后间隔三秒、最多尝试三次；只有同一观测对局及完整阵容指纹仍匹配、且非战斗时才继续，离开画面后停止旧档案补拍。JSON 或截图失败会显示未完成状态。

手动截图测试与自动截图互斥。当前 Java 错误回调没有请求编号，因此十五秒未回调时只报告超时并保留占用，直到回调结束，防止迟到回调串到下一请求。对局身份根据已有 `in_run`、英雄、天数及胜场变化观测，尚无已确认的稳定服务端 run ID；完整指纹额外包含时间、胜负、卡牌实例、品质、槽位和附魔。

真正达到十胜后的自动触发仍需真实对局验收；手动截图和离线重试测试不能代替这一验收。

## 社区写入

2026-10-23 只读核对丘Bot公开源码的 commit `a13c04c3f024b2ed5714698ac4a3805f068c7264`：`web_runs/app.py` 的 `POST /api/ingest` 使用运营者配置的 `X-Ingest-Token`，接受采集脚本的上游对局数组及 `screenshotUrl`，并非原始 PNG 上传入口。仓库 README 的鉴权和请求体示例与实现不一致，以固定版本源码为依据。没有向生产接口发送 POST，也没有取得站方写入令牌；不能据只读查询可用推断上传已接通。

进一步核对官方发行清单后确认，加加当前发行是 5.7.0，默认上传 `https://mod-api-v5.bazaarplusplus.com/bundles`；本地和主分支已有 V6 不代表发行客户端已切换。V5/V6 的 POST 上传源码均未要求写入令牌，下载 Bundle 的 GET 鉴权属于另一条路由。手机还需补对局/账号 ID、最终段位和最终战数据、V5 gzip+MessagePack 封包、完整及分段 SHA-256、可选 JPEG/WebP 图片和持久化上传队列。已有本机 JSON/PNG 不能直接提交该接口。

直接向丘Bot写入需要站方授权；另一条上游路径是由用户登录 BazaarDB 网页并验证后提交截图，再等待丘Bot采集。BazaarDB也提供个人Tracker密钥，但第三方手机端的自动写入协议尚未确认。加加不能笼统归为“缺上传 token”。成功接收与进入统计、推荐仍是不同步骤，且当前加加公开快照的数据窗口止于 2026-10-05。完整条件、图片大小限制、上游投稿方式与固定源码见 [COMMUNITY-UPLOAD-REQUIREMENTS.md](COMMUNITY-UPLOAD-REQUIREMENTS.md)。本轮未执行生产 POST。

证据：[丘Bot固定版本源码](https://github.com/CodeHuba/bazaarqiubot/blob/a13c04c3f024b2ed5714698ac4a3805f068c7264/web_runs/app.py)、`evidence/community-upload-v0.101.json`。本轮构建及真机验收以 `out/delivery-audit-v0.101.json` 为准。
