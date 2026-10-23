import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const read=async p=>JSON.parse(await readFile(path.join(root,p),'utf8'));
async function fixture(extra={}, {initialRuns=true}={}) {
  const cards=await read('mobilebpp/web/cards.json'),data=await read('evidence/qiubot-karnok.sample.json'),theme=await read('mobilebpp/web/theme-report.json');
  const nodes=new Map(),commands=[],timers=new Map(),storage=new Map();let timerId=0;
  const element=()=>({html:'',writes:0,textContent:'',hidden:false,checked:false,scrollTop:0,dataset:{},classes:new Set(),listeners:{},
    get innerHTML(){return this.html;},set innerHTML(v){this.html=v;this.writes++;},
    classList:{add(){},remove(){},toggle(){}},addEventListener(n,f){this.listeners[n]=f;},getBoundingClientRect(){return {top:0};},setPointerCapture(){},blur(){}});
  const document={body:{dataset:{},style:{values:{},setProperty(k,v){this.values[k]=v;}}},getElementById(id){if(!nodes.has(id))nodes.set(id,element());return nodes.get(id);},querySelectorAll(){return [];},addEventListener(){}};
  const sandbox={document,CARDS:cards,GAME_THEME:theme,location:{protocol:'file:'},screen:{width:873,height:393},setInterval(){},
    setTimeout(fn){timers.set(++timerId,fn);return timerId;},clearTimeout(id){timers.delete(id);},
    localStorage:{getItem(k){return storage.get(k)||null;},setItem(k,v){storage.set(k,v);}},
    Bpp:{command(action,value){commands.push({action,value});}},...extra};
  sandbox.window=sandbox;const ctx=vm.createContext(sandbox);
  vm.runInContext(await readFile(path.join(root,'mobilebpp/web/panel.js'),'utf8'),ctx);
  const initialRecommendations=nodes.get('recommendations').innerHTML;
  const state={in_run:true,hero:'Karnok',day:2,hour:0,board:[{g:'9f098979-e499-406c-a6a1-2bc98e88afab',tier:1,size:1}],skills:[],speed:1,preferences:{floating:true,follow:true,upload:false}};
  ctx.setState(state);
  assert.equal(nodes.get('pref-floating').checked,true);
  assert.equal(nodes.get('pref-follow').checked,true);
  assert.equal(nodes.get('pref-upload').checked,false);
  assert.match(nodes.get('upload-status').textContent,/社区接入暂缓/);
  const query=JSON.parse(commands.findLast(x=>x.action==='runs').value);
  if(initialRuns)ctx.setRuns({key:query.key,data});
  const click=(node,matches)=>nodes.get(node).onclick({target:{closest(selector){return matches[selector]||null;}}});
  return {ctx,nodes,commands,timers,storage,data,state,query,click,initialRecommendations};
}

const recommendationIds=html=>[...html.matchAll(/data-run-id="([^"]+)"/g)].map(m=>m[1]);
function assertPinned(f,id) {
  const html=f.nodes.get('recommendations').innerHTML,ids=recommendationIds(html);
  assert.ok(html.startsWith('<div class="followed-recommendation"'));
  assert.equal(ids[0],id);
  assert.equal(ids.filter(value=>value===id).length,1);
  assert.match(html,/class="rec followed"/);
  assert.ok(html.includes('data-follow="'+id+'" aria-pressed="true">取消关注</button>'));
}

test('detail preserves skills, enchantments and safe screenshot URLs and rejects mismatched replies',async()=>{
  const f=await fixture(),raw=f.data.runs[0];
  f.click('recommendations',{'[data-run-id]':{dataset:{runId:raw.id}}});
  assert.equal(f.nodes.get('detail-page').hidden,false);
  assert.equal(f.commands.at(-1).action,'detail');
  assert.match(f.nodes.get('run-detail').innerHTML,/技能/);
  assert.ok(f.nodes.get('run-detail').innerHTML.includes(raw.skills[0].name));
  const before=f.nodes.get('run-detail').innerHTML;
  f.ctx.setRunDetail({id:'other',error:'obsolete'});
  assert.equal(f.nodes.get('run-detail').innerHTML,before);
  f.ctx.setRunDetail({id:raw.id,run:{...raw,screenshot:'https://evil.invalid/image.png'}});
  assert.doesNotMatch(f.nodes.get('run-detail').innerHTML,/evil.invalid/);
  assert.doesNotMatch(f.nodes.get('run-detail').innerHTML,/正在更新/);
});

test('follow sends both card and skill GUIDs, and disabling reminders clears native watch targets',async()=>{
  const f=await fixture(),raw=f.data.runs[0];
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  const ids=JSON.parse(f.commands.findLast(x=>x.action==='watch').value);
  assert.ok(ids.includes(raw.cards[0].cardId));assert.ok(ids.includes(raw.skills[0].cardId));
  assert.ok(f.storage.get('bazaarBot.companion'));
  f.nodes.get('pref-follow').listeners.change({target:{checked:false}});
  assert.equal(f.commands.findLast(x=>x.action==='preference').value,'follow=0');
  assert.deepEqual(JSON.parse(f.commands.findLast(x=>x.action==='watch').value),[]);
  f.nodes.get('pref-follow').listeners.change({target:{checked:true}});
  assert.ok(JSON.parse(f.commands.findLast(x=>x.action==='watch').value).includes(raw.skills[0].cardId));
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  assert.deepEqual(JSON.parse(f.commands.findLast(x=>x.action==='watch').value),[]);
});

test('following pins a single highlighted run before recommendations and cancelling restores server order',async()=>{
  const f=await fixture(),original=f.data.runs.map(r=>r.id),id=original[2];
  f.click('recommendations',{'[data-follow]':{dataset:{follow:id}}});
  assertPinned(f,id);
  assert.deepEqual(recommendationIds(f.nodes.get('recommendations').innerHTML),[id,...original.filter(value=>value!==id)]);
  const replacement=original[1];
  f.click('recommendations',{'[data-follow]':{dataset:{follow:replacement}}});
  assertPinned(f,replacement);
  assert.deepEqual(recommendationIds(f.nodes.get('recommendations').innerHTML),[replacement,...original.filter(value=>value!==replacement)]);
  f.click('recommendations',{'[data-follow]':{dataset:{follow:replacement}}});
  assert.doesNotMatch(f.nodes.get('recommendations').innerHTML,/followed-recommendation|取消关注/);
  assert.deepEqual(recommendationIds(f.nodes.get('recommendations').innerHTML),original);
  assert.equal(JSON.parse(f.storage.get('bazaarBot.companion')).followed,null);
});

test('following from below the list reveals the pinned controls inside the scroller without moving detail or return positions',async()=>{
  const f=await fixture(),raw=f.data.runs[2],scroll=f.ctx.document.getElementById('panel-scroll'),recommendations=f.nodes.get('recommendations');
  scroll.scrollTop=640;
  scroll.getBoundingClientRect=()=>({top:60});
  recommendations.getBoundingClientRect=()=>{
    assertPinned(f,raw.id);
    return {top:-440};
  };
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  assert.equal(scroll.scrollTop,140);
  scroll.scrollTop=310;
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  assert.equal(scroll.scrollTop,310);
  scroll.scrollTop=520;
  f.click('recommendations',{'[data-run-id]':{dataset:{runId:raw.id}}});
  assert.equal(scroll.scrollTop,0);
  scroll.scrollTop=65;
  recommendations.getBoundingClientRect=()=>{throw Error('hidden recommendation list must not be measured');};
  f.click('detail-actions',{'[data-follow]':{dataset:{follow:raw.id}}});
  assertPinned(f,raw.id);
  assert.equal(f.nodes.get('detail-page').hidden,false);
  assert.equal(scroll.scrollTop,65);
  f.nodes.get('detail-back').onclick();
  assert.equal(scroll.scrollTop,520);
});

test('pinned run remains actionable across pagination, filters, refresh, errors and empty cached results',async()=>{
  const f=await fixture(),raw=f.data.runs[2],other=f.data.runs.filter(r=>r.id!==raw.id);
  const latestQuery=()=>JSON.parse(f.commands.findLast(x=>x.action==='runs').value);
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  f.click('recommendations',{'[data-runs-page]':{dataset:{runsPage:'2'},disabled:false}});
  assert.equal(latestQuery().page,2);
  assertPinned(f,raw.id);
  assert.match(f.nodes.get('recommendations').innerHTML,/正在查询/);
  f.ctx.setRuns({key:latestQuery().key,data:{...f.data,page:2,pages:3,runs:other}});
  assertPinned(f,raw.id);
  assert.deepEqual(recommendationIds(f.nodes.get('recommendations').innerHTML),[raw.id,...other.map(r=>r.id)]);
  const guid=f.state.board[0].g;
  f.nodes.get('board-page').listeners.click({target:{closest:()=>({dataset:{guid}})}});
  assert.equal(latestQuery().page,1);
  assert.equal(latestQuery().cards.length,1);
  assertPinned(f,raw.id);
  f.ctx.setRuns({key:latestQuery().key,error:'网络连接失败'});
  assertPinned(f,raw.id);
  assert.match(f.nodes.get('recommendations').innerHTML,/网络连接失败/);
  f.click('recommendations',{'[data-run-id]':{dataset:{runId:raw.id}}});
  assert.equal(f.commands.at(-1).action,'detail');
  assert.equal(f.commands.at(-1).value,raw.id);
  f.nodes.get('detail-back').onclick();
  let prevented=false;
  f.nodes.get('recommendations').listeners.keydown({key:'Enter',target:{dataset:{runId:raw.id}},preventDefault(){prevented=true;}});
  assert.ok(prevented);
  assert.equal(f.commands.at(-1).value,raw.id);
  f.nodes.get('detail-back').onclick();
  f.click('recommendations',{'[data-float]':{dataset:{float:raw.id}}});
  assert.equal(f.commands.at(-1).action,'float');
  f.click('recommendations',{'#retry-runs':{}});
  assertPinned(f,raw.id);
  assert.match(f.nodes.get('recommendations').innerHTML,/正在查询/);
  f.ctx.setRuns({key:latestQuery().key,cached:true,data:{...f.data,runs:[],total:0,pages:0,page:1}});
  assertPinned(f,raw.id);
  assert.match(f.nodes.get('recommendations').innerHTML,/缓存/);
  assert.match(f.nodes.get('recommendations').innerHTML,/没有符合当前筛选/);
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  assert.deepEqual(recommendationIds(f.nodes.get('recommendations').innerHTML),[]);
  assert.deepEqual(JSON.parse(f.commands.findLast(x=>x.action==='watch').value),[]);
});

test('persisted follow restores above loading results and stays cancellable with reminders disabled',async()=>{
  const f=await fixture(),raw=f.data.runs[2];
  f.click('recommendations',{'[data-run-id]':{dataset:{runId:raw.id}}});
  f.click('detail-actions',{'[data-follow]':{dataset:{follow:raw.id}}});
  const updated={...raw,cards:raw.cards.map((c,i)=>({...c,name:i===0?'持久化更新物品':c.name}))};
  f.ctx.setRunDetail({id:raw.id,run:updated});
  assert.ok(f.nodes.get('recommendations').innerHTML.includes('持久化更新物品'));
  const restored=await fixture({localStorage:{getItem(k){return f.storage.get(k)||null;},setItem(k,v){f.storage.set(k,v);}}},{initialRuns:false});
  assert.equal(recommendationIds(restored.initialRecommendations)[0],raw.id);
  assert.ok(restored.initialRecommendations.includes('持久化更新物品'));
  assertPinned(restored,raw.id);
  assert.match(restored.nodes.get('recommendations').innerHTML,/正在查询/);
  restored.ctx.setRuns({key:restored.query.key,data:{...restored.data,runs:restored.data.runs.filter(r=>r.id!==raw.id)}});
  assertPinned(restored,raw.id);
  restored.ctx.setState({...restored.state,preferences:{...restored.state.preferences,follow:false}});
  assertPinned(restored,raw.id);
  assert.deepEqual(JSON.parse(restored.commands.findLast(x=>x.action==='watch').value),[]);
  restored.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  assert.doesNotMatch(restored.nodes.get('recommendations').innerHTML,/followed-recommendation/);
  assert.equal(JSON.parse(f.storage.get('bazaarBot.companion')).followed,null);
  const cancelled=await fixture({localStorage:{getItem(k){return f.storage.get(k)||null;},setItem(){}}});
  assert.doesNotMatch(cancelled.initialRecommendations,/followed-recommendation/);
});

test('run details preserve the list position in the content scroller and follow feedback distinguishes a detected target',async()=>{
  const f=await fixture(),raw=f.data.runs[0],scroll=f.ctx.document.getElementById('panel-scroll');
  scroll.scrollTop=420;
  f.click('recommendations',{'[data-run-id]':{dataset:{runId:raw.id}}});
  assert.equal(scroll.scrollTop,0);
  f.nodes.get('detail-back').onclick();
  assert.equal(scroll.scrollTop,420);
  f.click('recommendations',{'[data-follow]':{dataset:{follow:raw.id}}});
  f.ctx.setState({...f.state,watch_count:19,watch_hits:1,markers:[]});
  assert.match(f.nodes.get('follow-status').textContent,/发现 1 个目标/);
  f.ctx.setState({...f.state,watch_count:19,watch_hits:1,markers:[{g:raw.cards[0].cardId,x:.4,y:.3}]});
  assert.match(f.nodes.get('follow-status').textContent,/当前出现 1 个目标/);
});

test('native window size updates independently of mode and live no-enchantment values never become card captions',async()=>{
  const f=await fixture();
  f.ctx.setPanelState({nativePanel:true,open:false,mini:true,windowWidth:300,windowHeight:74});
  f.ctx.setPanelState({nativePanel:true,open:false,mini:true,windowWidth:300,windowHeight:145});
  assert.equal(f.ctx.document.body.style.values['--window-height'],'145px');
  f.ctx.setState({...f.state,board:[{...f.state.board[0],enchantment:-1}]});
  assert.doesNotMatch(f.nodes.get('inventory').innerHTML,/class="enchantment"/);
  f.ctx.setState({...f.state,board:[{...f.state.board[0],enchantment:0}]});
  assert.match(f.nodes.get('inventory').innerHTML,/class="enchantment">Deadly</);
  assert.equal(f.ctx.BppModel.enchantmentLabel('-1'),'');
  assert.equal(f.ctx.BppModel.enchantmentLabel(null),'');
  assert.equal(f.ctx.BppModel.enchantmentLabel(999),'');
  assert.equal(f.ctx.BppModel.enchantmentLabel('沉重'),'沉重');
});

test('floating tap toggles skill tokens without opening the panel, and a long drag still docks',async()=>{
  const f=await fixture(),raw=f.data.runs[0];
  f.click('recommendations',{'[data-float]':{dataset:{float:raw.id}}});
  assert.equal(f.commands.at(-1).action,'float');
  assert.ok(f.nodes.get('mini-board').innerHTML.includes(raw.cards[0].name));
  f.ctx.setPanelState({open:false,mini:true,mode:'side'});
  const initial=f.commands.at(-1).value.split(',').map(Number),before=f.commands.length;
  f.ctx.toggleFloatingSkills();
  assert.equal(f.commands.at(-1).action,'float');
  assert.ok(f.commands.at(-1).value.split(',').map(Number)[1]>initial[1]);
  assert.equal(f.nodes.get('mini-board').dataset.skills,'true');
  assert.match(f.nodes.get('mini-board').innerHTML,/skill-token/);
  assert.ok(!f.commands.slice(before).some(c=>c.action==='open'||c.action==='detail'));
  f.nodes.get('mini-board').onclick();
  assert.equal(f.nodes.get('mini-board').dataset.skills,'false');
  assert.equal(f.commands.at(-1).value.split(',').map(Number)[1],initial[1]);
  const mini=f.nodes.get('mini-board');
  mini.listeners.pointerdown({pointerId:1,screenX:330,screenY:160,clientX:20,clientY:20});
  for(const timer of f.timers.values())timer();
  mini.listeners.pointermove({screenX:25,screenY:180});
  mini.listeners.pointerup({screenX:25,screenY:180});
  assert.ok(f.commands.some(c=>c.action==='dock'));
  const count=f.commands.length;mini.onclick();assert.equal(f.commands.length,count);
});

test('floating skills wait for the native height acknowledgement before becoming visible',async()=>{
  const f=await fixture(),raw=f.data.runs[0];
  f.click('recommendations',{'[data-float]':{dataset:{float:raw.id}}});
  const [width,height]=f.commands.at(-1).value.split(',').map(Number);
  f.ctx.setPanelState({nativePanel:true,open:false,mini:true,windowWidth:width,windowHeight:height});
  f.ctx.toggleFloatingSkills();
  const expanded=f.commands.at(-1).value.split(',').map(Number)[1];
  assert.ok(expanded>height);
  assert.match(f.nodes.get('mini-board').innerHTML,/class="mini-skills" hidden/);
  f.ctx.setPanelState({nativePanel:true,open:false,mini:true,windowWidth:width,windowHeight:expanded});
  assert.doesNotMatch(f.nodes.get('mini-board').innerHTML,/class="mini-skills" hidden/);
  f.ctx.toggleFloatingSkills();
  assert.match(f.nodes.get('mini-board').innerHTML,/class="mini-skills" hidden/);
});

test('carpet selection uses extracted game assets and survives a new UI instance',async()=>{
  const f=await fixture(),theme=await read('mobilebpp/web/theme-report.json'),choice=theme.carpets[4];
  f.nodes.get('carpet-selection').onclick({target:{closest(){return {dataset:{carpet:choice.id}};}}});
  assert.equal(f.storage.get('bazaarBot.carpet'),choice.id);
  const restored=await fixture({localStorage:{getItem(k){return f.storage.get(k)||null;},setItem(){}}});
  assert.ok(restored.nodes.get('carpet-selection').innerHTML.includes(choice.name));
  const raw=restored.data.runs[0];restored.click('recommendations',{'[data-float]':{dataset:{float:raw.id}}});
  assert.ok(restored.nodes.get('mini-board').innerHTML.includes(choice.file));
});

test('new remote images keep local artwork visible until decoding and repeated snapshots preserve the list DOM',async()=>{
  const images=[],remote={dataset:{src:'https://s.bazaardb.gg/example@400S.webp'},isConnected:true,classes:new Set(),classList:{add(v){remote.classes.add(v);}}};
  class Image {constructor(){images.push(this);}decode(){return Promise.resolve();}set src(v){this.url=v;}}
  const f=await fixture({Image});
  assert.match(f.nodes.get('inventory').innerHTML,/art-local[^>]+src="file:\/\/\/android_asset/);
  f.ctx.document.querySelectorAll=selector=>selector==='img[data-src]'?[remote]:[];
  const count=f.nodes.get('recommendations').writes;
  f.ctx.setState({...f.state});
  assert.equal(f.nodes.get('recommendations').writes,count);
  f.ctx.setRuns({key:f.query.key,data:f.data,art:{'9f098979-e499-406c-a6a1-2bc98e88afab':{art:remote.dataset.src,s:'Small'}}});
  assert.equal(remote.src,undefined);assert.equal(images.length,1);
  await images[0].onload();await Promise.resolve();
  assert.equal(remote.src,remote.dataset.src);assert.ok(remote.classes.has('ready'));
  f.ctx.setRuns({key:f.query.key,data:f.data});assert.equal(images.length,1);
});
