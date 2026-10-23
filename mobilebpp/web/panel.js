(function () {
  'use strict';
  function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  }
  function searchCards(cards, query) {
    const q = query.trim().toLowerCase();
    return cards.filter(c => [c.n,c.en,c.h,c.tags,c.g].some(v => String(v || '').toLowerCase().includes(q)));
  }
  const normalizeHero = h => h === 'The Dragons' ? 'TheDragons' : h;
  const safeArt = url => typeof url==='string' && url.startsWith('https://s.bazaardb.gg/') ? url : '';
  const safeScreenshot = url => typeof url==='string' && /^https:\/\/(usercontent\.bzdb\.network|s\.bazaardb\.gg)\//.test(url) ? url : '';
  const enchantmentNames=['Deadly','Ethereal','Fiery','Golden','Heavy','Icy','Mystical','Obsidian','Radiant','Restorative','Shielded','Shiny','Tiny','Toxic','Turbo','Mossy'];
  function enchantmentLabel(value){
    if(value===null||value===undefined||value==='')return '';
    if(typeof value==='number'||/^-?\d+$/.test(value))return enchantmentNames[Number(value)]||'';
    return String(value);
  }
  function normalizeRun(run, hero) {
    if(!run || normalizeHero(run.hero)!==hero || run.wins!==10 || !Array.isArray(run.cards) || !/^[A-Za-z0-9_-]{1,128}$/.test(run.id))throw Error('阵容数据与当前英雄不符');
    const convert=c=>{
      if(!c || !/^[0-9a-f-]{36}$/i.test(c.cardId))throw Error('卡牌数据格式异常');
      return {g:c.cardId.toLowerCase(),n:c.name||c.name_en,en:c.name_en,art:safeArt(c.img),s:c.size,t:c.isLegendary?'Legendary':c.tier,slot:c.slotPosition,enchantment:enchantmentLabel(c.enchantment_zh||c.enchantment)};
    };
    return {...run,hero:normalizeHero(run.hero),cards:run.cards.map(convert),skills:(Array.isArray(run.skills)?run.skills:[]).map(convert),screenshot:safeScreenshot(run.screenshot)};
  }
  function normalizeRuns(data, hero) {
    if (!data || !Array.isArray(data.runs) || !Number.isInteger(data.total) || data.total<0 || !Number.isInteger(data.page) || data.page<1 || !Number.isInteger(data.pages) || data.pages<0) throw Error('阵容接口返回格式异常');
    return {...data,runs:data.runs.map(run=>normalizeRun(run,hero))};
  }
  function toggleSelection(set, guid) {
    if (typeof guid !== 'string' || !guid) return;
    if (set.has(guid)) set.delete(guid); else set.add(guid);
  }
  globalThis.BppModel = {escapeHtml,searchCards,normalizeRuns,normalizeRun,normalizeHero,toggleSelection,enchantmentLabel};
  if (typeof document === 'undefined') return;

  const byId = id => document.getElementById(id);
  const cards = typeof CARDS === 'undefined' ? [] : CARDS;
  const cardIndex = new Map(cards.map(c => [c.g,c]));
  const selection = new Set();
  let snapshot = null, activeTab = 'board';
  let runs=null, runsKey='', runsPage=1, runsLoading=false, runsError='', runsCached=false, requestSerial=0;
  const artIndex=new Map();
  const decodedArt=new Map();
  let detailRun=null,detailLoading=false,detailError='',followedRun=null,floatingRun=null,miniActive=false;
  let prefs={floating:true,follow:true,upload:false},lastRecommendationKey='',lastWatchKey='',drag=null,suppressMiniClick=false;
  let floatPosition={x:-1,y:-1};
  let listScroll=0,nativeWindowHeight=Infinity;
  try {
    const saved=JSON.parse(localStorage.getItem('bazaarBot.companion')||'null');
    const restore=r=>r&&r.id&&Array.isArray(r.cards)&&r.cards.every(c=>/^[0-9a-f-]{36}$/i.test(c.g))?r:null;
    followedRun=restore(saved?.followed);floatingRun=restore(saved?.floating);
    if(Number.isFinite(saved?.position?.x)&&Number.isFinite(saved?.position?.y))floatPosition=saved.position;
  } catch(_) {}
  const frames=typeof CARD_FRAMES==='undefined'?{}:CARD_FRAMES.frames;
  const assetRoot=location.protocol==='file:'?'file:///android_asset/bpp/':'/';
  const theme=typeof GAME_THEME==='undefined'?{}:GAME_THEME;
  const carpets=theme.carpets||[];
  let carpetId=carpets[0]?.id||'carpet_1',miniSkillsOpen=false,lastCarpetRender='';
  try{const id=localStorage.getItem('bazaarBot.carpet');if(carpets.some(c=>c.id===id))carpetId=id;}catch(_){}
  let lastSpeed = 1, searchQuery = '', panelOpen = false, launcherMode = 'side';
  const heroNames = {Common:'通用',Vanessa:'瓦妮莎',Pygmalien:'皮格马利翁',Dooley:'杜利',Mak:'马克',Jules:'朱尔斯',Stelle:'斯黛拉',Karnok:'卡诺克',TheDragons:'巨龙'};
  const tiers = ['青铜','白银','黄金','钻石','传说'];
  const tierNames = {Bronze:'青铜',Silver:'白银',Gold:'黄金',Diamond:'钻石',Legendary:'传说'};
  const heroLabel = h => heroNames[h] || h || '未知';
  function command(action,value='') {
    if (globalThis.Bpp && typeof globalThis.Bpp.command === 'function') {
      globalThis.Bpp.command(action,String(value));
    } else { byId('notice').textContent = '预览模式：尚未连接游戏'; }
  }
  function nameOf(guid) { return (cardIndex.get(guid) || {}).n || guid.slice(0,8); }
  function merged(card) { const c={...(cardIndex.get(card.g)||{}),...(artIndex.get(card.g)||{}),...card};if(!c.art)c.art=cardIndex.get(card.g)?.art||'';return c; }
  function art(card) {
    const fallback=cardIndex.get(card.g)?.art||'';
    const remote=safeArt(card.art),local=remote?fallback:(card.art||fallback);
    return '<span class="art-wrap">'+(local?'<img class="art art-local" src="'+escapeHtml(local)+'" alt="" decoding="async">':'')+(remote?'<img class="art art-remote" data-src="'+escapeHtml(remote)+'" alt="">':'')+'</span>';
  }
  function upgradeArt() {
    if(typeof Image==='undefined')return;
    document.querySelectorAll('img[data-src]').forEach(el=>{
      const url=el.dataset.src;
      let pending=decodedArt.get(url);
      if(!pending){
        const img=new Image();img.decoding='async';
        pending=new Promise(resolve=>{img.onload=async()=>{try{await img.decode();}catch(_){}resolve(true);};img.onerror=()=>resolve(false);});
        decodedArt.set(url,pending);img.src=url;
        if(decodedArt.size>160)decodedArt.delete(decodedArt.keys().next().value);
      }
      pending.then(ok=>{if(ok&&el.isConnected){el.src=url;el.classList.add('ready');}});
    });
  }
  function cardSize(c) {
    return c.size?Math.max(1,Math.min(3,Number(c.size))):({Small:1,Medium:2,Large:3})[c.s]||1;
  }
  function cardFace(raw, interactive=true) {
    const c=merged(raw), tier=typeof c.tier==='number'?c.tier:['Bronze','Silver','Gold','Diamond','Legendary'].indexOf(c.t);
    const filename=(['bronze','silver','gold','diamond','legendary'][tier]||'unknown')+'-'+(['s','m','l'][cardSize(c)-1])+'.webp';
    const geometry=frames[filename], aspect=geometry?.aspect || cardSize(c)/2;
    const on=interactive&&selection.has(c.g), tag=interactive?'button':'span',enchantment=enchantmentLabel(c.enchantment);
    return '<'+tag+' class="game-card tier-'+tier+(on?' on':'')+'" style="--aspect:'+aspect+'" '+
      (interactive?'data-guid="'+escapeHtml(c.g)+'" aria-pressed="'+on+'" ':'')+'title="'+escapeHtml(c.n)+' · '+escapeHtml(tiers[tier]||'传说')+'" aria-label="'+escapeHtml(c.n)+'">'+
      '<span class="card-face">'+(geometry?'<img class="frame" src="'+assetRoot+'frames/'+filename+'" alt="">':'')+art(c)+'<span class="selected-mark" aria-hidden="true">✓</span></span><span class="card-name">'+escapeHtml(c.n)+'</span>'+(enchantment?'<span class="enchantment">'+escapeHtml(enchantment)+'</span>':'')+'</'+tag+'>';
  }
  function cardButton(raw,result=false) {
    const c=merged(raw), on=selection.has(c.g), quality=typeof c.tier==='number'?tiers[c.tier] || String(c.tier):tierNames[c.t] || c.t || '';
    return '<button class="'+(result?'result':'card')+(on?' on':'')+'" data-guid="'+escapeHtml(c.g)+'" aria-pressed="'+on+'">'+art(c)+
      '<div><div class="name">'+escapeHtml(c.n)+'</div><div class="meta">'+escapeHtml(quality)+' · '+escapeHtml(heroLabel(c.h))+'</div></div></button>';
  }
  function skillFace(raw) {
    const c=merged(raw),tier=typeof c.tier==='number'?c.tier:['Bronze','Silver','Gold','Diamond','Legendary'].indexOf(c.t);
    const quality=['bronze','silver','gold','diamond','legendary'][tier]||'bronze';
    return '<span class="skill-token" title="'+escapeHtml(c.n)+' · '+escapeHtml(tiers[tier]||'')+'" aria-label="'+escapeHtml(c.n)+'"><img class="skill-frame" src="'+assetRoot+'theme/skill-'+quality+'.webp" alt="">'+art(c)+'</span>';
  }
  function renderInventory() {
    const s=snapshot || {};
    byId('run-summary').textContent = s.in_run ? heroLabel(s.hero)+' · 第 '+s.day+' 天 · '+s.hour+' 时' : '等待进入对局';
    const board=s.board||[];
    byId('inventory').innerHTML='<div class="section-head"><h2>我的棋盘 · '+board.length+'</h2><span class="muted">点选筛选 · 再点取消</span></div><div class="cards">'+
      (board.length?board.map(c=>cardFace(c)).join(''):'<span class="muted">暂无棋盘物品</span>')+'</div>';
    upgradeArt();
  }
  function renderResults() {
    const results=byId('results');
    if (!searchQuery.trim()) { results.innerHTML=''; return; }
    const found=searchCards(cards,searchQuery), shown=found.slice(0,100);
    results.innerHTML='<div class="muted">找到 '+found.length+' 张'+(found.length>100?'，显示前 100 张':'')+'</div>'+shown.map(c => cardButton(c,true)).join('');
    upgradeArt();
  }
  function queryRuns(force=false) {
    const s=snapshot||{};
    if(!s.in_run||!s.hero||s.hero==='Common')return;
    const selected=[...selection].sort();
    const names=selected.map(g=>cardIndex.get(g)?.en||'');
    if(names.some(n=>!n)){runsError='所选卡牌缺少英文名称，无法查询';renderRecommendations();return;}
    const request={hero:s.hero,page:runsPage,cards:names,board:[...new Set((s.board||[]).map(c=>c.g))].sort()};
    const key=JSON.stringify(request);
    if(!force&&key===runsKey)return;
    runsKey=key;runs=null;runsError='';runsLoading=true;runsCached=false;
    renderRecommendations();
    command('runs',JSON.stringify({...request,key,serial:++requestSerial}));
  }
  function renderRecommendations() {
    const s=snapshot||{};
    byId('clear-selection').hidden=!selection.size;
    const key=JSON.stringify([runs,runsKey,runsLoading,runsError,runsCached,[...selection],s.board,s.in_run,followedRun,prefs]);
    if(key===lastRecommendationKey)return;lastRecommendationKey=key;
    let html=followedRun?'<div class="followed-recommendation" aria-label="已关注阵容"><div class="followed-heading">★ 已关注 · 固定置顶</div>'+recommendationCard(followedRun)+'</div>':'';
    html+='<div class="muted rec-summary">巴扎丘Bot · 当前赛季 · 最近 7 天';
    if(runs)html+=' · '+runs.total+' 条十胜记录 · '+escapeHtml(runs.scope?.phase||'');
    html+='</div>';
    if(selection.size)html+='<div class="selection-summary">包含：'+[...selection].map(g=>escapeHtml(nameOf(g))).join('、')+'</div>';
    if(runsCached)html+='<p class="cache-note">联网失败，显示上次相同筛选的缓存</p>';
    if(runsLoading)html+='<p class="empty-state">正在查询'+escapeHtml(heroLabel(s.hero))+'的最新十胜记录…</p>';
    else if(runsError)html+='<p class="empty-state">'+escapeHtml(runsError)+'</p>';
    else if(!s.in_run)html+='<p class="empty-state">进入对局后自动读取当前英雄</p>';
    else if(runs&&!runs.runs.length)html+='<p class="empty-state">最近 7 天没有符合当前筛选的十胜记录，可再次点卡取消筛选</p>';
    for(const run of runs?.runs||[])if(run.id!==followedRun?.id)html+=recommendationCard(run);
    html+='<div class="pagination"><button data-runs-page="'+(runsPage-1)+'" '+(!runs||runsPage<=1||runsLoading?'disabled':'')+'>上一页</button><span class="muted">'+(runs?runs.page+' / '+Math.max(1,runs.pages):'')+'</span><button data-runs-page="'+(runsPage+1)+'" '+(!runs||runsPage>=runs.pages||runsLoading?'disabled':'')+'>下一页</button><button id="retry-runs" '+(runsLoading?'disabled':'')+'>刷新</button></div>';
    byId('recommendations').innerHTML=html;
    upgradeArt();
  }
  function recommendationCard(run) {
    const owned=new Map();for(const c of snapshot?.board||[])owned.set(c.g,(owned.get(c.g)||0)+1);
    let have=0;for(const c of run.cards){if(owned.get(c.g)>0){have++;owned.set(c.g,owned.get(c.g)-1);}}
    const date=new Date(run.created_at);
    const when=isNaN(date)?'时间未知':date.toLocaleString('zh-CN',{month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hour12:false});
    return '<article class="rec'+(followedRun?.id===run.id?' followed':'')+'" data-run-id="'+escapeHtml(run.id)+'" tabindex="0" role="button" aria-label="查看'+escapeHtml(heroLabel(run.hero))+'十胜阵容详情"><div class="rec-title">'+escapeHtml(heroLabel(run.hero))+' · '+run.wins+' 胜 '+run.losses+' 负<span class="muted">已有 '+have+'/'+run.cards.length+' 张</span><div class="rec-actions">'+runActions(run)+'</div></div><div class="rec-board">'+run.cards.map(c=>cardFace(c,false)).join('')+'</div><div class="meta">'+escapeHtml(when)+' · 第 '+escapeHtml(run.season)+' 赛季 · '+escapeHtml(run.phase)+'</div></article>';
  }
  function persistCompanion() {
    try{localStorage.setItem('bazaarBot.companion',JSON.stringify({followed:followedRun,floating:floatingRun,position:floatPosition}));}catch(_){}
  }
  function runActions(run) {
    const id=escapeHtml(run.id),followed=followedRun?.id===run.id;
    return (prefs.follow||followed?'<button data-follow="'+id+'" aria-pressed="'+followed+'">'+(followed?'取消关注':'☆ 关注')+'</button>':'')+(prefs.floating?'<button data-float="'+id+'">悬浮</button>':'');
  }
  function syncWatch() {
    const ids=prefs.follow&&followedRun?[...new Set([...followedRun.cards,...(followedRun.skills||[])].map(c=>c.g))].sort():[];
    const key=JSON.stringify(ids);if(key===lastWatchKey)return;lastWatchKey=key;command('watch',key);
  }
  function toggleFollow(run) {
    followedRun=followedRun?.id===run.id?null:run;persistCompanion();syncWatch();renderShortcut();renderRecommendations();if(detailRun)renderDetail();
    if(followedRun&&activeTab==='board'){
      const scroller=byId('panel-scroll');
      scroller.scrollTop=Math.max(0,scroller.scrollTop+byId('recommendations').getBoundingClientRect().top-scroller.getBoundingClientRect().top);
    }
  }
  function renderShortcut() {
    const run=floatingRun||followedRun;
    byId('companion-shortcut').innerHTML=run?'<div class="section-head companion-shortcut"><span class="muted">参考阵容 · '+escapeHtml(heroLabel(run.hero))+'</span><div class="rec-actions"><button data-show-detail="'+escapeHtml(run.id)+'">查看详情</button>'+runActions(run)+'</div></div>':'';
    if(followedRun&&prefs.follow)byId('companion-shortcut').innerHTML+='<div id="follow-status" class="follow-status"></div>';
    renderFollowStatus();
  }
  function renderFollowStatus() {
    const node=byId('follow-status');if(!node)return;
    const run=followedRun;
    node.textContent=run&&prefs.follow?'关注中 · '+run.cards.length+' 件物品 / '+(run.skills||[]).length+' 个技能 · '+(snapshot?.watch_count===0?'正在同步':(snapshot?.markers?.length?'当前出现 '+snapshot.markers.length+' 个目标':snapshot?.watch_hits?'发现 '+snapshot.watch_hits+' 个目标，已显示提醒':snapshot?.in_combat?'战斗中，尚未出现目标':snapshot?.shop?.length?'当前商店没有目标':'等待商店或战斗')):'';
  }
  function showPage(name) {
    activeTab=name;byId('panel-scroll').scrollTop=0;
    for(const p of ['board','speed','settings','diag','detail'])byId(p+'-page').hidden=p!==name;
    document.querySelectorAll('[data-tab]').forEach(t=>t.classList.toggle('on',t.dataset.tab===name));
  }
  function openDetails(run) {
    listScroll=byId('panel-scroll').scrollTop||0;
    detailRun=run;detailLoading=true;detailError='';showPage('detail');byId('panel-scroll').scrollTop=0;renderDetail();command('open');command('detail',run.id);
  }
  function renderDetail() {
    if(!detailRun)return;
    const run=detailRun,owned=new Set([...(snapshot?.board||[]),...(snapshot?.skills||[])].map(c=>c.g));
    byId('detail-back').textContent='‹ 阵容列表';
    byId('detail-actions').innerHTML=runActions(run);
    const skill=c=>'<div class="detail-skill'+(owned.has(c.g)?' follow-match':'')+'">'+skillFace(c)+'<span>'+escapeHtml(c.n)+'<small>'+escapeHtml(tierNames[c.t]||c.t||'')+(c.enchantment?' · '+escapeHtml(c.enchantment):'')+'</small></span></div>';
    byId('run-detail').innerHTML='<div class="detail-head"><h2>'+escapeHtml(heroLabel(run.hero))+' · '+run.wins+' 胜 '+run.losses+' 负</h2><span class="muted">第 '+escapeHtml(run.day||'?')+' 天</span></div>'+
      (detailLoading?'<p class="muted">正在更新详情…</p>':'')+(detailError?'<p class="cache-note">'+escapeHtml(detailError)+'，先显示已读取的阵容</p>':'')+
      '<div class="detail-cards">'+run.cards.map(c=>cardFace(c,false)).join('')+'</div><h2>技能 · '+(run.skills||[]).length+'</h2><div class="detail-skills">'+(run.skills||[]).map(skill).join('')+'</div>'+
      (run.screenshot?'<details class="detail-extra"><summary>查看原局截图</summary><img class="detail-screenshot" src="'+escapeHtml(run.screenshot)+'" alt="该十胜对局的原始截图" loading="lazy"></details>':'')+
      '<details class="detail-extra"><summary>对局信息</summary><div class="meta">'+escapeHtml(run.username||'')+' · '+escapeHtml(run.created_at||'')+'<br>第 '+escapeHtml(run.season)+' 赛季 · '+escapeHtml(run.phase)+'</div></details>';
    upgradeArt();
  }
  function showFloating(run) {
    if(!prefs.floating)return;
    if(floatingRun?.id!==run.id)miniSkillsOpen=false;
    floatingRun=run;persistCompanion();renderMini();renderShortcut();
    resizeFloating();
  }
  function floatingSize() {
    const run=floatingRun,total=(run?.cards||[]).reduce((n,c)=>n+cardSize(c)/2,0);
    const width=Math.min(screen.width-12,Math.max(230,total*52+Math.max(0,(run?.cards.length||0)-1)+38));
    const height=Math.max(60,Math.ceil((width-12)/4.85382457));
    const rows=miniSkillsOpen?Math.ceil((run?.skills||[]).length/Math.max(1,Math.floor((width-12)/32))):0;
    return {width,height,fullHeight:height+14+(rows?rows*33+5:0)};
  }
  function resizeFloating() {
    const size=floatingSize();command('float',[size.width,size.fullHeight,floatPosition.x,floatPosition.y].join(','));
  }
  function renderMini() {
    const run=floatingRun;if(!run){byId('mini-board').innerHTML='';return;}
    const size=floatingSize(),carpet=carpets.find(c=>c.id===carpetId);
    const showSkills=miniSkillsOpen&&(!miniActive||nativeWindowHeight>=size.fullHeight-.5);
    byId('mini-board').innerHTML=run?'<div class="mini-carpet" style="height:'+size.height+'px;background-image:url('+assetRoot+'theme/'+escapeHtml(carpet?.file||'carpet_1.webp')+')"><div class="mini-cards">'+run.cards.map(c=>cardFace(c,false)).join('')+'</div></div><div class="mini-skills" '+(!showSkills?'hidden':'')+'>'+((run.skills||[]).map(skillFace).join(''))+'</div>':'';
    byId('mini-board').dataset.skills=String(miniSkillsOpen);
    upgradeArt();
  }
  function toggleFloatingSkills() {
    if(!floatingRun||!miniActive)return;
    miniSkillsOpen=!miniSkillsOpen;renderMini();resizeFloating();
  }
  function renderCarpets(open=false) {
    if(lastCarpetRender===carpetId)return;lastCarpetRender=carpetId;
    const selected=carpets.find(c=>c.id===carpetId);
    byId('carpet-selection').innerHTML='<div class="carpet-current"><img src="'+assetRoot+'theme/'+escapeHtml(selected?.file||'carpet_1.webp')+'" alt=""><span>'+escapeHtml(selected?.name||'经典地毯')+'</span></div><details '+(open?'open':'')+'><summary>更换地毯 · '+carpets.length+' 款</summary><div id="carpet-gallery" class="carpet-gallery">'+carpets.map(c=>'<button data-carpet="'+escapeHtml(c.id)+'" class="carpet-choice'+(c.id===carpetId?' on':'')+'" aria-pressed="'+(c.id===carpetId)+'"><img src="'+assetRoot+'theme/'+escapeHtml(c.file)+'" alt="" loading="lazy"><span>'+escapeHtml(c.name)+'</span></button>').join('')+'</div></details>';
  }
  function collapsePanel() {
    command('close');
  }
  function findRun(id) {
    return [followedRun,...(runs?.runs||[]),detailRun,floatingRun].find(r=>r&&r.id===id);
  }
  function runAction(e) {
    const follow=e.target.closest('[data-follow]'),floating=e.target.closest('[data-float]');
    const id=follow?.dataset.follow||floating?.dataset.float;
    const run=findRun(id);
    if(run){if(follow)toggleFollow(run);else showFloating(run);return true;}return false;
  }
  function renderPreferences() {
    renderCarpets();
    for(const name of ['floating','follow','upload'])byId('pref-'+name).checked=!!prefs[name];
    byId('upload-status').textContent=(prefs.upload&&snapshot?.capture_status)||'社区接入暂缓。开启本机记录后，十胜数据只保存在设备上。';
  }
  function renderStatus() {
    const s=snapshot, d=s?.diagnostics;
    byId('speed-state').textContent=s ? (s.speed===0?'已暂停':'设置 '+s.speed+'x')+(typeof s.time_scale==='number'?' · 引擎实际 '+s.time_scale+'x':'') : '等待游戏状态';
    document.querySelectorAll('[data-speed]').forEach(b=>b.classList.toggle('on',s?.speed===Number(b.dataset.speed)));
    document.querySelectorAll('#settings-page [data-mode]').forEach(b=>b.classList.toggle('on',b.dataset.mode===launcherMode));
    if (!d) { byId('diagnostics').textContent='等待游戏状态…'; return; }
    const rows=[['版本',s.version],['IL2CPP',d.il2cpp?'已连接':'未就绪'],['游戏数据',d.data?'已连接':'未就绪'],['主线程 Hook',d.tick?'已安装':'未就绪'],['渲染',d.swaps>0?'GLES + WebView':d.vulkan?'Vulkan + WebView':'WebView（尚未观察到渲染帧）'],['卡牌表',cards.length+' 张'],['网络阵容库',s.network || '等待'],['最后命令',s.last_command || '无'],['日志',d.log],['错误',d.error || '无'],['更新',new Date().toLocaleTimeString()]];
    byId('diagnostics').innerHTML='<dl>'+rows.map(([k,v])=>'<dt>'+escapeHtml(k)+'</dt><dd>'+escapeHtml(v)+'</dd>').join('')+'</dl>';
  }
  byId('query').addEventListener('input',e=>{searchQuery=e.target.value;renderInventory();renderResults();});
  const finishSearch=()=>{byId('query').blur();command('keyboard');};
  byId('hide-keyboard').onclick=finishSearch;
  byId('query').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();e.stopPropagation();finishSearch();}});
  byId('board-page').addEventListener('click',e=>{
    const b=e.target.closest('[data-guid]');if(!b)return;
    if(selection.size>=10&&!selection.has(b.dataset.guid)){byId('notice').textContent='最多同时筛选 10 张卡牌';return;}
    toggleSelection(selection,b.dataset.guid);runsPage=1;renderInventory();renderResults();queryRuns();
  });
  document.addEventListener('error',e=>{if(e.target.tagName==='IMG')e.target.classList.add('missing');},true);
  byId('tabs').addEventListener('click',e=>{
    const b=e.target.closest('[data-tab]');if(b)showPage(b.dataset.tab);
  });
  byId('speed-buttons').addEventListener('click',e=>{const b=e.target.closest('[data-speed]');if(b){const v=Number(b.dataset.speed);if(v)lastSpeed=v;command('speed',v);}});
  byId('resume').onclick=()=>command('speed',lastSpeed);
  byId('settings-page').addEventListener('click',e=>{const b=e.target.closest('[data-mode]');if(b)command('launcher',b.dataset.mode);});
  for(const name of ['floating','follow','upload'])byId('pref-'+name).addEventListener('change',e=>{
    prefs[name]=e.target.checked;command('preference',name+'='+(prefs[name]?'1':'0'));syncWatch();renderRecommendations();if(detailRun)renderDetail();
  });
  byId('clear-selection').onclick=()=>{selection.clear();runsPage=1;renderInventory();renderResults();queryRuns();};
  byId('recommendations').onclick=e=>{
    if(runAction(e))return;
    const b=e.target.closest('[data-runs-page]');if(b&&!b.disabled){runsPage=Number(b.dataset.runsPage);queryRuns();}
    else if(e.target.closest('#retry-runs'))queryRuns(true);
    else {const article=e.target.closest('[data-run-id]'),run=findRun(article?.dataset.runId);if(run)openDetails(run);}
  };
  byId('recommendations').addEventListener('keydown',e=>{if(e.key==='Enter'&&e.target.dataset.runId){e.preventDefault();const r=findRun(e.target.dataset.runId);if(r)openDetails(r);}});
  byId('detail-actions').onclick=runAction;
  byId('companion-shortcut').onclick=e=>{
    if(runAction(e))return;
    const b=e.target.closest('[data-show-detail]'),run=[floatingRun,followedRun].find(r=>r&&r.id===b?.dataset.showDetail);
    if(run)openDetails(run);
  };
  byId('detail-back').onclick=()=>{showPage('board');byId('panel-scroll').scrollTop=listScroll;};
  byId('mini-board').onclick=()=>{if(!suppressMiniClick)toggleFloatingSkills();suppressMiniClick=false;};
  byId('mini-board').addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();toggleFloatingSkills();}});
  window.toggleFloatingSkills=toggleFloatingSkills;
  byId('carpet-selection').onclick=e=>{
    const id=e.target.closest('[data-carpet]')?.dataset.carpet;
    if(!carpets.some(c=>c.id===id))return;
    const scroll=byId('carpet-gallery').scrollTop||0;
    carpetId=id;try{localStorage.setItem('bazaarBot.carpet',id);}catch(_){}
    renderCarpets(true);byId('carpet-gallery').scrollTop=scroll;renderMini();if(miniActive)resizeFloating();
  };
  window.setFloatingPosition=(x,y)=>{if(Number.isFinite(x)&&Number.isFinite(y)){floatPosition={x,y};persistCompanion();}};
  byId('mini-board').addEventListener('pointerdown',e=>{
    if(!miniActive)return;
    drag={id:e.pointerId,startX:e.screenX,startY:e.screenY,offsetX:e.clientX,offsetY:e.clientY,active:false,x:e.screenX-e.clientX,y:e.screenY-e.clientY,timer:null};
    byId('mini-board').setPointerCapture(e.pointerId);
    drag.timer=setTimeout(()=>{if(drag){drag.active=true;suppressMiniClick=true;byId('mini-board').classList.add('dragging');command('float_drag','1');}},350);
  });
  byId('mini-board').addEventListener('pointermove',e=>{
    if(!drag)return;
    if(!drag.active&&Math.hypot(e.screenX-drag.startX,e.screenY-drag.startY)>10){clearTimeout(drag.timer);suppressMiniClick=true;return;}
    if(!drag.active)return;
    drag.x=e.screenX-drag.offsetX;drag.y=e.screenY-drag.offsetY;
    const docking=drag.x<38&&Math.abs(e.screenY-screen.height*.5)<52;
    byId('mini-board').classList.toggle('dock-ready',docking);command('float_move',drag.x+','+drag.y);
  });
  const finishDrag=e=>{
    if(!drag)return;clearTimeout(drag.timer);
    if(drag.active){
      floatPosition={x:Math.max(0,drag.x),y:Math.max(0,drag.y)};persistCompanion();
      if(drag.x<38&&Math.abs(e.screenY-screen.height*.5)<52)command('dock');
      command('float_drag','0');suppressMiniClick=true;
    }
    drag=null;byId('mini-board').classList.remove('dragging','dock-ready');
  };
  byId('mini-board').addEventListener('pointerup',finishDrag);
  byId('mini-board').addEventListener('pointercancel',e=>{if(drag){clearTimeout(drag.timer);drag=null;command('float_drag','0');byId('mini-board').classList.remove('dragging','dock-ready');suppressMiniClick=true;}});
  byId('launcher').onclick=()=>command('open');
  byId('collapse').onclick=collapsePanel;
  byId('backdrop').onclick=collapsePanel;
  byId('dump').onclick=()=>command('dump');
  byId('refresh').onclick=()=>command('status');
  window.setPanelState=function(s){
    if(s.nativePanel)document.body.dataset.nativePanel='true';
    if(s.windowWidth>0&&s.windowHeight>0){
      document.body.style.setProperty('--window-width',s.windowWidth+'px');
      document.body.style.setProperty('--window-height',s.windowHeight+'px');
      const resized=nativeWindowHeight!==s.windowHeight;nativeWindowHeight=s.windowHeight;
      // Reveal skills after the native window has acknowledged the larger size.
      if(resized&&miniActive&&s.mini)renderMini();
    }
    const mode=s.mode==='bubble'?'bubble':'side';
    if(document.body.dataset.open===String(!!s.open)&&document.body.dataset.mini===String(!!s.mini)&&document.body.dataset.mode===mode)return;
    panelOpen=!!s.open;miniActive=!!s.mini;launcherMode=mode;document.body.dataset.open=String(panelOpen);document.body.dataset.mini=String(miniActive);byId('mini-board').hidden=!miniActive;document.body.dataset.mode=launcherMode;renderStatus();
  };
  window.reportLayout=function(){
    const box=id=>{const r=byId(id).getBoundingClientRect();return [r.x,r.y,r.width,r.height].map(Math.round).join(',');};
    command('status','ui '+innerWidth+'x'+innerHeight+'@'+devicePixelRatio+';nav:'+box('tabs')+';fold:'+box('collapse')+';mini:'+box('mini-board'));
  };
  let inventoryKey='';
  window.setState=function(s){
    if(snapshot&&(snapshot.hero!==s.hero||!s.in_run||s.day<snapshot.day)){selection.clear();runsPage=1;runsKey='';runs=null;}
    snapshot=s;if(s.speed>0)lastSpeed=s.speed;if(s.mode)launcherMode=s.mode;
    if(s.preferences)prefs={...prefs,...s.preferences};renderPreferences();syncWatch();renderShortcut();
    const key=JSON.stringify([s.in_run,s.hero,s.day,s.hour,s.board]);
    if(key!==inventoryKey){inventoryKey=key;renderInventory();queryRuns();}renderRecommendations();
    renderStatus();
  };
  window.setRunDetail=function(reply){
    if(!reply||!detailRun||reply.id!==detailRun.id)return;
    detailLoading=false;detailError=reply.error||'';
    if(reply.run){try{
      detailRun=normalizeRun(reply.run,detailRun.hero);
      if(followedRun?.id===detailRun.id){followedRun=detailRun;syncWatch();renderShortcut();renderRecommendations();}
      if(floatingRun?.id===detailRun.id){floatingRun=detailRun;renderMini();}
      persistCompanion();
    }catch(e){detailError=e.message;}}
    renderDetail();
  };
  window.setRuns=function(reply){
    if(!reply||reply.key!==runsKey)return;
    runsLoading=false;runsCached=!!reply.cached;
    if(reply.error){runsError=reply.error;runs=null;renderRecommendations();return;}
    try{runs=normalizeRuns(reply.data,snapshot.hero);runsPage=runs.page;runsError='';}
    catch(e){runsError=e.message;runs=null;}
    for(const [g,info] of Object.entries(reply.art||{})){if(safeArt(info.art))artIndex.set(g,info);}
    renderInventory();renderRecommendations();
  };
  setInterval(()=>{if(panelOpen&&snapshot?.in_run)queryRuns(true);},300000);
  window.setCardNames=function(names){for(const [g,n] of Object.entries(names)){const c=cardIndex.get(g);if(c&&n)c.n=n;}renderInventory();renderResults();renderRecommendations();};
  renderInventory();renderRecommendations();renderMini();renderShortcut();renderPreferences();renderStatus();
  command('ready');
})();
