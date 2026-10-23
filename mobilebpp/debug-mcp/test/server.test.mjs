import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { Device, isMobileApkArtifact, LEGACY, MEDIA, PACKAGE, snapshotStatus } from '../device.mjs';

const root = fileURLToPath(new URL('../../../', import.meta.url));
const panelFile = path.join(root, 'mobilebpp/web/panel.js');

test('fresh timestamps from a previous game process are rejected',()=>{
  assert.equal(snapshotStatus({updated_at:100,process_id:123},'456',101).fresh,false);
  assert.equal(snapshotStatus({updated_at:100,process_id:123},'123',101).fresh,true);
  assert.equal(snapshotStatus({updated_at:100,process_id:123},'123',110).fresh,false);
  assert.equal(snapshotStatus({updated_at:100,process_id:123},'',101).fresh,false);
});

test('debug tools default to the new app package and recognize current and historical APK names',()=>{
  assert.equal(PACKAGE,'com.tempogames.thebazaar.bazaarbotmobile');
  assert.equal(MEDIA,`/sdcard/Android/media/${PACKAGE}`);
  assert.equal(LEGACY,`/sdcard/Android/data/${PACKAGE}/files`);
  assert.equal(isMobileApkArtifact('The Bazaar - bazaarBot Mobile-v0.0.1.apk'),true);
  assert.equal(isMobileApkArtifact('TheBazaar-bpp-v0.101.apk'),true);
  assert.equal(isMobileApkArtifact('TheBazaar-bpp-v0.83.apk'),true);
  assert.equal(isMobileApkArtifact('TheBazaar.apk'),false);
  assert.equal(isMobileApkArtifact('The Bazaar-v0.0.1.apk'),false);
});

test('legacy app package is selected only through the explicit environment override',async()=>{
  const prior=process.env.BPP_ANDROID_PACKAGE;
  try {
    process.env.BPP_ANDROID_PACKAGE='com.tempogames.thebazaar';
    const legacy=await import('../device.mjs?legacy-package-test');
    assert.equal(legacy.PACKAGE,'com.tempogames.thebazaar');
    assert.equal(legacy.MEDIA,'/sdcard/Android/media/com.tempogames.thebazaar');
    assert.equal(legacy.LEGACY,'/sdcard/Android/data/com.tempogames.thebazaar/files');
  } finally {
    if(prior===undefined)delete process.env.BPP_ANDROID_PACKAGE;
    else process.env.BPP_ANDROID_PACKAGE=prior;
  }
});

test('the new preview defaults local ten-win recording to disabled while retaining the existing companions',async()=>{
  const panel=await readFile(path.join(root,'mobilebpp/web/panel.html'),'utf8');
  assert.match(panel,/id="pref-floating" type="checkbox" checked/);
  assert.match(panel,/id="pref-follow" type="checkbox" checked/);
  assert.match(panel,/id="pref-upload" type="checkbox"\s*>/);
  assert.match(panel,/十胜自动记录（暂缓）/);
  assert.match(panel,/社区接入暂缓/);
  assert.match(await readFile(path.join(root,'mobilebpp/tools/preview_ui.py'),'utf8'),/0\.0\.1 preview/);
});

test('live cards select by GUID independently and reject missing IDs', async () => {
  const ctx = vm.createContext({});
  vm.runInContext(await readFile(panelFile,'utf8'),ctx);
  const selected=new Set();
  ctx.BppModel.toggleSelection(selected,'a');
  assert.deepEqual([...selected],['a']);
  ctx.BppModel.toggleSelection(selected,undefined);
  assert.deepEqual([...selected],['a']);
  ctx.BppModel.toggleSelection(selected,'b');
  ctx.BppModel.toggleSelection(selected,'a');
  assert.deepEqual([...selected],['b']);
});

test('Chinese and English search and current-season run decoding preserve IDs, dates and full art', async () => {
  const ctx=vm.createContext({});
  vm.runInContext(await readFile(panelFile,'utf8'),ctx);
  const model=ctx.BppModel;
  assert.equal(model.searchCards([{n:'猎鹰',en:'Falcon',g:'a'}],'猎鹰').length,1);
  assert.equal(model.searchCards([{n:'猎鹰',en:'Falcon',g:'a'}],'FALCON').length,1);
  const raw=JSON.parse(await readFile(path.join(root,'evidence/qiubot-karnok.sample.json'),'utf8'));
  const result=model.normalizeRuns(raw,'Karnok');
  assert.ok(result.total>0);
  assert.equal(result.runs[0].id,raw.runs[0].id);
  assert.equal(result.runs[0].created_at,raw.runs[0].created_at);
  assert.equal(result.runs[0].cards[0].g,raw.runs[0].cards[0].cardId);
  assert.equal(result.runs[0].cards[0].art,raw.runs[0].cards[0].img);
  assert.equal(model.normalizeHero('The Dragons'),'TheDragons');
  assert.throws(()=>model.normalizeRuns(raw,'Vanessa'),/英雄/);
  assert.throws(()=>model.normalizeRuns({...raw,runs:[{...raw.runs[0],wins:9}]},'Karnok'));
  assert.throws(()=>model.normalizeRuns({runs:[]},'Karnok'));
  const unsafe=structuredClone(raw);unsafe.runs[0].cards[0].img='javascript:alert(1)';
  assert.equal(model.normalizeRuns(unsafe,'Karnok').runs[0].cards[0].art,'');
});

test('production endpoint and card-filter sample use QiuBot current-season data', async () => {
  const source=JSON.parse(await readFile(path.join(root,'evidence/qiubot-source.json'),'utf8'));
  const native=await readFile(path.join(root,'mobilebpp/native/bpp_net.cpp'),'utf8');
  assert.ok(native.includes('"https://bazaarqiubot.com"'));
  assert.equal(source.api,'https://bazaarqiubot.com/api/runs');
  assert.equal(source.query.min_wins,10);
  const filtered=JSON.parse(await readFile(path.join(root,'evidence/qiubot-karnok-stake.sample.json'),'utf8'));
  assert.ok(filtered.total>0);
  assert.ok(filtered.runs.every(r=>r.hero==='Karnok'&&r.cards.some(c=>c.cardId==='3d7961f0-62fa-4b20-94a0-9034ccda129c')));
});

test('debug cache alias reads current QiuBot runs before old V5/V6 files', async () => {
  const device=new Device(root);
  device.serial=async()=> 'phone';
  const paths=[];
  device.run=async args=>{paths.push(args.at(-1));return '{"source":"qiubot"}';};
  const result=await device.read('bpp_builds.json');
  assert.equal(paths.length,1);
  assert.ok(result.path.endsWith('/bpp_runs.json'));
});

test('selection and pagination query the server, ignore stale replies and restore the latest list', async () => {
  const cards=JSON.parse(await readFile(path.join(root,'mobilebpp/web/cards.json'),'utf8'));
  const all=JSON.parse(await readFile(path.join(root,'evidence/qiubot-karnok.sample.json'),'utf8'));
  const filtered=JSON.parse(await readFile(path.join(root,'evidence/qiubot-karnok-stake.sample.json'),'utf8'));
  const page2=JSON.parse(await readFile(path.join(root,'evidence/qiubot-karnok-page2.sample.json'),'utf8'));
  const nodes=new Map(),commands=[];
  const element=()=>({innerHTML:'',textContent:'',hidden:false,dataset:{},classList:{toggle(){}},listeners:{},addEventListener(n,f){this.listeners[n]=f;},blur(){}});
  const document={body:{dataset:{}},getElementById(id){if(!nodes.has(id))nodes.set(id,element());return nodes.get(id);},querySelectorAll(){return [];},addEventListener(){}};
  const sandbox={document,CARDS:cards,location:{protocol:'file:'},setInterval(){},Bpp:{command(a,v){if(a==='runs')commands.push(JSON.parse(v));}}};
  sandbox.window=sandbox;
  const ctx=vm.createContext(sandbox);vm.runInContext(await readFile(panelFile,'utf8'),ctx);
  const stake='3d7961f0-62fa-4b20-94a0-9034ccda129c';
  ctx.setState({in_run:true,hero:'Karnok',day:2,hour:0,board:[{g:stake,tier:0,size:1}],speed:1});
  const initial=commands.at(-1);assert.equal(initial.cards.length,0);
  ctx.setRuns({key:initial.key,data:all});
  assert.equal((nodes.get('recommendations').innerHTML.match(/data-run-id=/g)||[]).length,5);
  const click=()=>nodes.get('board-page').listeners.click({target:{closest(){return {dataset:{guid:stake}};}}});
  click();const selected=commands.at(-1);
  assert.deepEqual(selected.cards,['Silver Stake']);
  ctx.setRuns({key:initial.key,data:all});
  assert.match(nodes.get('recommendations').innerHTML,/正在查询/);
  ctx.setRuns({key:selected.key,data:filtered});
  assert.match(nodes.get('recommendations').innerHTML,/包含：银制尖桩/);
  click();const canceled=commands.at(-1);assert.equal(canceled.cards.length,0);
  ctx.setRuns({key:selected.key,data:filtered});
  assert.match(nodes.get('recommendations').innerHTML,/正在查询/);
  ctx.setRuns({key:canceled.key,data:all,cached:true});
  assert.doesNotMatch(nodes.get('recommendations').innerHTML,/包含：/);
  assert.match(nodes.get('recommendations').innerHTML,/缓存/);
  const pageClick=page=>nodes.get('recommendations').onclick({target:{closest(selector){return selector==='[data-runs-page]'?{dataset:{runsPage:String(page)},disabled:false}:null;}}});
  pageClick(2);const next=commands.at(-1);assert.equal(next.page,2);
  ctx.setRuns({key:canceled.key,data:all});
  assert.match(nodes.get('recommendations').innerHTML,/正在查询/);
  ctx.setRuns({key:next.key,data:page2});
  assert.match(nodes.get('recommendations').innerHTML,/2 \/ /);
  for(const run of page2.runs)assert.ok(nodes.get('recommendations').innerHTML.includes('data-run-id="'+run.id+'"'));
  pageClick(1);const previous=commands.at(-1);assert.equal(previous.page,1);
  ctx.setRuns({key:previous.key,data:all});
  assert.match(nodes.get('recommendations').innerHTML,/1 \/ /);
});

test('MCP negotiates stdio, lists tools, queries Chinese cards and reads resources', async () => {
  const transport = new StdioClientTransport({command:process.execPath,args:[fileURLToPath(new URL('../server.mjs',import.meta.url))],stderr:'pipe'});
  const client=new Client({name:'bpp-protocol-test',version:'1.0.0'});
  try {
    await client.connect(transport);
    const {tools}=await client.listTools();
    assert.ok(tools.some(t=>t.name==='read_debug_file'));
    assert.ok(tools.some(t=>t.name==='set_speed'));
    assert.ok(tools.some(t=>t.name==='hide_keyboard'));
    const reply=await client.callTool({name:'search_cards',arguments:{query:'猎鹰',limit:10}});
    assert.ok(!reply.isError);
    assert.ok(JSON.parse(reply.content[0].text).cards.some(c=>c.n==='猎鹰'));
    const coverage=await client.callTool({name:'asset_coverage',arguments:{kind:'untranslated',query:'Poison Blades'}});
    assert.ok(!coverage.isError);
    const gap=JSON.parse(coverage.content[0].text);
    assert.ok(gap.records.some(r=>r.english==='Poison Blades'&&r.reason==='official_translation_absent'));
    const resource=await client.readResource({uri:'bpp://project/todo'});
    assert.match(resource.contents[0].text,/开发与验收清单/);
    const rejected=await client.callTool({name:'set_speed',arguments:{speed:'1; rm -rf /'}});
    assert.equal(rejected.isError,true);
  } finally { await client.close(); }
});

test('device selection refuses ambiguous/offline devices and command validation rejects injection', async () => {
  const device=new Device(root,'missing-adb');
  device.run=async()=> 'List of devices attached\na device product:one\nb device product:two';
  await assert.rejects(device.serial(),/多台设备/);
  assert.equal(await device.serial('a'),'a');
  await assert.rejects(device.serial('other'),/未连接/);
  await assert.rejects(device.command('shell','rm'),/不支持/);
  await assert.rejects(device.command('bpp_speed','1; reboot'),/速度只能/);
  device.run=async()=> 'List of devices attached\na unauthorized';
  await assert.rejects(device.serial(),/没有可用设备/);
});
