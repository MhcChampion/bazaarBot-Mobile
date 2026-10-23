import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { z } from 'zod';
import { readFile, readdir, stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { Device, FILES, PACKAGE, isMobileApkArtifact, snapshotStatus } from './device.mjs';

const root = path.resolve(process.env.BPP_ROOT || fileURLToPath(new URL('../../', import.meta.url)));
const device = new Device(root);
const server = new McpServer({ name: 'bpp-mobile-debug', version: '0.2.0' });
const serial = z.string().min(1).optional().describe('adb 设备序列号；仅一台设备时可省略');
const readonly = { readOnlyHint: true, destructiveHint: false, openWorldHint: false };
function tool(name, description, inputSchema, handler, annotations = readonly) {
  server.registerTool(name, { description, inputSchema, annotations }, async args => {
    try {
      const data = await handler(args);
      return { content: [{ type: 'text', text: typeof data === 'string' ? data : JSON.stringify(data, null, 2) }] };
    } catch (error) { return { isError: true, content: [{ type: 'text', text: error.message }] }; }
  });
}
tool('list_devices', '列出 adb 设备及连接／授权状态。', {}, () => device.list());
tool('read_debug_file', '读取 BPP 日志、导出数据、设置或运行状态。仅访问固定的 Mod 调试文件。',
  { file: z.enum(FILES), serial, lines: z.number().int().min(1).max(1000).default(200) },
  ({ file, serial, lines }) => device.read(file, serial, lines));
tool('get_device_status', '读取设备型号、Mod 是否运行及最近的状态快照。', { serial }, async ({ serial: requested }) => {
  const id = await device.serial(requested);
  const model = await device.run(['-s', id, 'shell', 'getprop', 'ro.product.model']);
  let pid = '';
  try { pid = await device.run(['-s', id, 'shell', 'pidof', PACKAGE]); } catch {}
  let snapshot;
  try { snapshot = JSON.parse((await device.read('bpp_state.json', id)).text); } catch (error) { snapshot = { unavailable: error.message }; }
  const deviceTime=Number(await device.run(['-s',id,'shell','date','+%s']));
  return { serial: id, model, running: !!pid, pid, snapshot, ...snapshotStatus(snapshot,pid,deviceTime),
    note:'fresh 表示游戏运行且快照在 5 秒内更新；v0.86+ 同时核对进程 ID，旧格式 same_process 为 null。' };
});
tool('read_logcat', '读取最近的 BPP／AndroidRuntime 错误日志，不清空系统日志。',
  { serial, lines: z.number().int().min(1).max(500).default(100) }, async ({ serial: requested, lines }) => {
    const id = await device.serial(requested);
    return device.run(['-s', id, 'logcat', '-d', '-t', String(lines), '-s', 'BPP:V', 'AndroidRuntime:E', 'DEBUG:E']);
  });
tool('search_cards', '离线查询卡牌中文名、英文名、GUID、英雄和标签。',
  { query: z.string().max(200), limit: z.number().int().min(1).max(100).default(20) }, async ({ query, limit }) => {
    const cards = JSON.parse(await readFile(path.join(root, 'mobilebpp/web/cards.json'), 'utf8'));
    const q = query.trim().toLowerCase();
    const matches = cards.filter(c => [c.n, c.en, c.g, c.h, c.tags].some(v => String(v || '').toLowerCase().includes(q)));
    return { total: matches.length, cards: matches.slice(0, limit) };
  });
tool('asset_coverage', '查询图片／中文名覆盖情况及每条缺口的源数据原因，帮助定位资源映射问题。',
  { kind:z.enum(['missing_art','untranslated']),query:z.string().max(200).default(''),limit:z.number().int().min(1).max(100).default(20) },
  async ({kind,query,limit})=>{
    const art=JSON.parse(await readFile(path.join(root,'mobilebpp/web/art-report.json'),'utf8'));
    const translation=JSON.parse(await readFile(path.join(root,'mobilebpp/web/translation-report.json'),'utf8'));
    const records=kind==='missing_art' ? art.missing_details || [] : translation.fallbacks;
    const q=query.trim().toLowerCase();
    const matches=records.filter(c=>[c.guid,c.name,c.english,c.type,c.art_key,c.reason].some(v=>String(v || '').toLowerCase().includes(q)));
    return {summary:{cards:art.cards,pictures:art.pictures,translated:translation.translated,missing_art:art.missing.length,untranslated:translation.fallbacks.length},total:matches.length,records:matches.slice(0,limit)};
  });
tool('project_status', '读取项目待办及本地 APK 产物，帮助确认源码／版本状态。', {}, async () => {
  const files = (await readdir(path.join(root, 'out'))).filter(isMobileApkArtifact);
  const apks = await Promise.all(files.map(async name => { const s = await stat(path.join(root, 'out', name)); return { name, bytes: s.size, modified: s.mtime.toISOString() }; }));
  return { root, apks, todo: await readFile(path.join(root, 'mobilebpp/docs/TODO.md'), 'utf8') };
});
const commandHints = { readOnlyHint: false, destructiveHint: false, openWorldHint: false };
tool('request_snapshot', '请求运行中的 Mod 导出完整数据／状态；执行完成后用 read_debug_file 读取。',
  { serial }, ({ serial }) => device.command('bpp_dump', '', serial), commandHints);
tool('set_panel', '展开或收起游戏中的 BPP 面板。',
  { open: z.boolean(), serial }, ({ open, serial }) => device.command(open ? 'bpp_open' : 'bpp_close', '', serial), commandHints);
tool('hide_keyboard', '关闭 Mod 搜索软键盘，不发送游戏的返回键。需要 v0.87+。',
  { serial }, ({serial})=>device.command('bpp_keyboard','',serial), commandHints);
tool('set_speed', '设置游戏速度，0 为暂停；仅发出命令，需核对游戏实际效果。',
  { speed: z.union([z.literal(0), z.literal(1), z.literal(2), z.literal(3)]), serial },
  ({ speed, serial }) => device.command('bpp_speed', String(speed), serial), commandHints);
tool('set_launcher', '设置收起时的侧边把手或悬浮球，并由 Mod 保存。',
  { mode: z.enum(['side', 'bubble']), serial }, ({ mode, serial }) => device.command('bpp_launcher', mode, serial), commandHints);
tool('set_safe_mode', '设置／取消安全模式。兼容新旧目录，下次启动生效，不会重启或卸载游戏。',
  { enabled: z.boolean(), serial }, ({ enabled, serial }) => device.safeMode(enabled, serial), commandHints);

server.registerResource('todo', 'bpp://project/todo', { mimeType: 'text/markdown', description: '开发与设备验收清单' }, async uri =>
  ({ contents: [{ uri: uri.href, mimeType: 'text/markdown', text: await readFile(path.join(root, 'mobilebpp/docs/TODO.md'), 'utf8') }] }));
await server.connect(new StdioServerTransport());
