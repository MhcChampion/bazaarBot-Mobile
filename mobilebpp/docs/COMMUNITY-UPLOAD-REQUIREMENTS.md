# 两站社区上传条件

核查日期：2026-10-23。上传目标为 `bazaarqiubot.com` 与 `bazaarplusplus.com`。本轮只读核对公开源码和发行信息，没有向社区提交对局。

## 巴扎丘Bot

写入入口是 `POST https://bazaarqiubot.com/api/ingest`，与公开查询接口分开。请求需要 `X-Ingest-Token`，由服务端 `BAZAAR_INGEST_TOKEN` 校验；没有公开的普通用户自行领取写入令牌流程。该入口在源码中标为采集脚本专用。

请求体是非空 JSON 数组，应用全局请求体上限为 2 MiB。代码会跳过非对象条目；单条读取 `id`，并要求 `statWins` 为数值且至少为 1。它不强制十胜，也不强制截图；这些只是代码可见的最少处理条件，不能据此保证任意稀疏对象都能在实际数据库成功插入，或作为完整阵容展示。

有意义的阵容数据还需英雄、日期、胜负、物品与技能的 GUID、品质、附魔及位置，按站方原有字段结构传入；段位、评分及战斗快照按实际数据提供，不能补造。现有采集器从 BazaarDB 的 `/api/run` 取得对局对象后转发，手机本地快照尚未映射成该结构，也未建立与上游 ID 的关系。

`screenshotUrl` 是可选字段；此接口不接收 PNG 文件或 multipart 图片。战斗快照任务只补充结构化战斗数据，不从卡牌列表生成原局截图。站内另有反馈图片上传，但只关联反馈帖，不能作为对局图片入口。不能把本地图片路径当作可访问的截图 URL。

接通条件是站方提供适合移动端的写入授权，并确认手机投稿的字段、ID 和去重规则。若提供的是共享的运营者令牌，适合由服务端持有并代转，或者由站方提供专用客户端凭证，不应直接作为所有 APK 用户共用的内置令牌。

源码：[app.py](https://github.com/CodeHuba/bazaarqiubot/blob/a13c04c3f024b2ed5714698ac4a3805f068c7264/web_runs/app.py)、[采集器](https://github.com/CodeHuba/bazaarqiubot/blob/a13c04c3f024b2ed5714698ac4a3805f068c7264/tools/bazaardb_runs_collector.py)。README 的 Bearer 鉴权及 `{runs:[...]}` 示例与该版本实现不一致，以固定版本实现为准。细节见 `evidence/qiubot-upload-requirements-2026-10-23.json`。

## 大巴扎加加

线上官方发行清单当前为 5.7.0，发行客户端默认使用 `POST https://mod-api-v5.bazaarplusplus.com/bundles`，`Content-Type: application/x-bpp-bundle-v5`。仓库主分支和本地源码已有 V6，不能据此认定当前发行客户端已经切换 V6。

V5 和 V6 源码的 `POST /bundles` 均未要求上传令牌；另一个下载 Bundle 的 GET 路由有服务鉴权，不能把下载凭证要求套给上传。这是公开源码和官方客户端所示行为；本轮没有 POST，未验证线上是否还有源码以外的网络限制。

必须构造该版本的二进制 Bundle，按约定提供 manifest、Run 段、长度及 SHA-256 摘要并通过校验。HTTP 还要提供准确的 `Content-Length` 和 `Content-Digest`。完整包最多 8,388,607 字节，压缩 Run 最多 2,097,151 字节。Run 段采用该版本的 gzip 与 MessagePack 数字键数组结构。

Screenshot 段可选，存在时图片放在同一个 Bundle 内；格式只能是 JPEG 或 WebP，最多 1 MiB，还需尺寸、品质、时间及摘要，当前手机 PNG 要转码。不需要为此单独搭图片 URL 托管。Replay 不是入口接收的硬要求。

上传接收与进入十胜推荐是两层条件。入口主要校验包结构，不会把 Run 内容全部解码并作阵容分析。下游分析器另要求 Run 与账号 ID 对应、有效英雄和最终段位；十胜统计筛选 `status=completed`、十胜及非负负场。构建推荐还需唯一最终战、有效的最终玩家棋盘，并按卡牌尺寸与槽位完整覆盖十格，再经每英雄 Top 500 和未覆盖卡补位选择。缺截图或缺 Replay 不等于不能进入该推荐；缺最终段位或最终棋盘等内容则会影响后续分析。

官方客户端只给已完成的 Ranked、非 PTR 对局封包；不能把客户端这条策略说成入口再次强制检查的条件。上传也不限定十胜。逐战胜负数量与总胜负不一致只记质量 WARN，不单独淘汰整局；它不能作为补造缺失战斗记录的理由。

当前手机有英雄、胜负、棋盘与技能的 GUID、品质、尺寸、槽位、附魔及 PNG；尚未导出稳定对局 ID、账号 ID、最终段位、明确的完成状态与最终战记录，也未实现 V5 Bundle 序列化、摘要和网络上传队列。需要补这些采集与协议适配，不能把现有本机状态 JSON 直接发给 `/bundles`。

截至核查时，官网仍读 V5 公开统计快照，英雄与阵容快照分别生成于 2026-10-06 16:13:48/49 UTC，数据窗口止于 2026-10-05。因此不能承诺上传成功后立即在网站看见；本轮未核实分析发布任务的实际运行情况。

固定发行源码：[上传路由](https://github.com/BazaarPlusPlus/BazaarPlusPlus/blob/bd88473e29eca6b514a5834d0b82ed88c5e119f2/bazaarplusplus-server/src/http/routes.ts)、[V5 Bundle 契约](https://github.com/BazaarPlusPlus/BazaarPlusPlus/blob/bd88473e29eca6b514a5834d0b82ed88c5e119f2/bazaarplusplus-server/contracts/v5/bundle-v5.md)、[分析准入](https://github.com/BazaarPlusPlus/BazaarPlusPlus/blob/bd88473e29eca6b514a5834d0b82ed88c5e119f2/bazaarplusplus-analyzer/src/bppanalyzer/accepted_runs.py)、[统计与推荐筛选](https://github.com/BazaarPlusPlus/BazaarPlusPlus/blob/bd88473e29eca6b514a5834d0b82ed88c5e119f2/bazaarplusplus-analyzer/src/bppanalyzer/publication.py)。精确版本、更多字段、客户端地址及线上只读观察见 `evidence/bpp-upload-requirements-2026-10-23.json`。

## BazaarDB 上游投稿与个人 Tracker

丘Bot README 明列 BazaarDB 为数据来源。固定版本的 `tools/bazaardb_runs_collector.py` 从 BazaarDB 浏览器会话的 `GET /api/run` 按时间和分页游标获取对局，再提交至丘Bot `/api/ingest`；保留原始 `run.id`。默认筛选当前 phase 且胜场至少为 1，并非仅采十胜；有时间窗和最多 50 页的限制，尚未核实线上调度与覆盖，不能承诺每条新对局都会收录。独立的战斗快照补全另有 Legendary/十胜筛选。

BazaarDB 存在两条不同的个人入口：

- 网页截图投稿：`https://bazaardb.gg/run/new`。真浏览器从 Builds 页点击 Upload 后，在未登录状态跳转 `https://bazaardb.gg/login?next=/run/new`。公开前端实现为带会话的 multipart `POST /api/run`，字段 `screenshot`、`hero` 和 `cf-turnstile-response`，前端文件上限 15 MiB，直接发送原图。用户登录并完成网页验证后，可以走此投稿流程；它不使用丘Bot的运营者令牌，也不能被当成已证实的无人值守上传 API。未执行实际投稿，手机截图识别效果仍需真实验证。
- 个人 Tracker：官网说明用户在 Account settings 生成个人 Tracker Secret Key，粘贴至 Windows Tracker 自动同步。公开配置给出服务 origin `https://adira.bzdb.network`，但未找到可确认的写入路由、鉴权头和完整数据协议，也未找到官方公开 Tracker 源码。个人密钥存在，并不证明任意第三方移动端实现可以直接调用。

账号页还提供 Integration linking。加加的发行客户端使用 `{code, account_id}` 调用 `/api/profile/link/redeem`，将第三方数据归属绑定到网站账号；这一步不是投稿，也不是 Tracker 上传授权。

因此，手机可以研究“自动保存十胜截图 → 用户确认并在 BazaarDB 网页投稿 → 等待丘Bot采集”的路径。直接向丘Bot写入仍需其站方令牌；通过上游投稿则不需要该令牌，但网站接收、公开可见和被丘Bot采入均需分别验证，不能把它们当成已完成的一步。

来源：[丘Bot采集器](https://github.com/CodeHuba/bazaarqiubot/blob/a13c04c3f024b2ed5714698ac4a3805f068c7264/tools/bazaardb_runs_collector.py)、[Tracker说明](https://bazaardb.gg/tracker)、[截图投稿页面](https://bazaardb.gg/run/new)。部署资源、精确字段及只读浏览器观察见 `evidence/bazaardb-tracker-ingest-2026-10-23.json` 与 `evidence/qiubot-bazaardb-source-chain-2026-10-23.json`。

## 当前实施边界

我们可以自行完成手机端字段采集、独立社区数据导出、Bundle 编码及离线验包、持久化待上传队列和成功确认。丘Bot还需要站方发放写入授权并明确移动端投稿规则；加加不应被描述为“必须先拿到上传 token”，应先按实际发行协议补齐采集与封包。真实十胜自动记录的完整触发仍需真实对局验收。

源码还提供一条可研究的间接路径：带截图的有效 V5 包进入加加后，会建立供 BazaarDB 消费者领取的 delivery 队列；丘Bot的采集器又从 BazaarDB 读取公开对局。加加并未在这部分实现主动向 BazaarDB 外站推送，源码只证明到外部消费者可以领取。消费者当前是否运行、是否接收移动端数据、所得记录是否在丘Bot采集范围，均未确认，因此不能把“加加 → BazaarDB → 丘Bot”记为已经可用的双站上传。

队列与消费契约：[bundle-commit.ts](https://github.com/BazaarPlusPlus/BazaarPlusPlus/blob/bd88473e29eca6b514a5834d0b82ed88c5e119f2/bazaarplusplus-server/src/modules/bundle-commit.ts)、[bazaardb-delivery.ts](https://github.com/BazaarPlusPlus/BazaarPlusPlus/blob/bd88473e29eca6b514a5834d0b82ed88c5e119f2/bazaarplusplus-server/src/modules/bazaardb-delivery.ts)。该消费者的服务令牌不是手机上传所需的令牌。
