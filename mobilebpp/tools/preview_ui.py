"""Render exactly the embedded web assets for local browser QA (never sends game commands)."""
import argparse
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port', type=int, default=8765)
    p.add_argument('--hero', default='Karnok', help='Hero shown in the local fixture preview')
    args = p.parse_args()
    html = (ROOT / 'web/panel.html').read_text(encoding='utf-8')
    cards = json.loads((ROOT / 'web/cards.json').read_text(encoding='utf-8'))
    for c in cards:
        c['art'] = c.get('art', '').replace('file:///android_asset/bpp/art/', '/art/')
    frames=json.loads((ROOT/'web/frame-report.json').read_text(encoding='utf-8'))
    theme=json.loads((ROOT/'web/theme-report.json').read_text(encoding='utf-8'))
    fixtures={key:json.loads((ROOT.parent/'evidence'/file).read_text(encoding='utf-8')) for key,file in [('all','qiubot-karnok.sample.json'),('stake','qiubot-karnok-stake.sample.json'),('page2','qiubot-karnok-page2.sample.json'),('art','qiubot-board-art.sample.json')]}
    for marker, value in [('CSS', (ROOT / 'web/panel.css').read_text(encoding='utf-8').replace('file:///android_asset/bpp/','/')), ('JS', (ROOT / 'web/panel.js').read_text(encoding='utf-8')), ('CARDS', json.dumps(cards, ensure_ascii=False).replace('<', '\\u003c')), ('FRAMES', json.dumps(frames)), ('THEME', json.dumps(theme,ensure_ascii=False))]:
        html = html.replace('/*BPP_' + marker + '*/', value)
    demo = {'version': '0.0.1 preview', 'in_run': True, 'hero': args.hero, 'day': 2, 'hour': 0, 'speed': 1, 'mode': 'side', 'preferences':{'floating':True,'follow':True,'upload':False},'board': [], 'stash': [], 'skills': [], 'diagnostics': {'il2cpp': False, 'data': False, 'tick': False, 'egl': False, 'log': '本地预览，未连接游戏', 'error': ''}, 'network': '本地样本预览，未连接手机'}
    demo_guids=['0941d7dd-1264-42c5-b334-6cc5d694571b', 'a14e3bea-96b9-43de-9455-99393769b356', '459dd1f7-285e-4172-b065-1fabf65476fb','3d7961f0-62fa-4b20-94a0-9034ccda129c','9f098979-e499-406c-a6a1-2bc98e88afab']
    for c in cards:
        if c['g'] in demo_guids:
            demo['board'].append(dict(g=c['g'], tier=1 if c['g']=='9f098979-e499-406c-a6a1-2bc98e88afab' else 0))
    # Commands affect this local preview only; never touch the connected phone.
    bridge='<script>const previewFixtures='+json.dumps(fixtures,ensure_ascii=False).replace('<','\\u003c')+''';window.Bpp={command:function(a,v){
      if(a==="open"||a==="close"||a==="dock")window.setPanelState({open:a==="open",mini:false,mode:"side"});
      if(a==="runs"){const q=JSON.parse(v);const d=q.cards.includes("Silver Stake")?previewFixtures.stake:q.page===2?previewFixtures.page2:previewFixtures.all;window.setRuns({key:q.key,data:d,art:previewFixtures.art,cached:false});}
      if(a==="detail"){const run=[...previewFixtures.all.runs,...previewFixtures.stake.runs,...previewFixtures.page2.runs].find(r=>r.id===v);window.setRunDetail({id:v,run});}
      if(a==="float"){const [w,h,x,y]=v.split(',').map(Number);Object.assign(document.getElementById('mini-board').style,{position:'fixed',width:w+'px',height:h+'px',left:(x<0?300:x)+'px',top:(y<0?140:y)+'px'});window.setPanelState({open:false,mini:true,mode:"side"});}
      if(a==="float_move"){const [x,y]=v.split(',');Object.assign(document.getElementById('mini-board').style,{left:Math.max(0,Number(x))+'px',top:Math.max(0,Number(y))+'px'});}
    }};</script>'''
    html=html.replace('<script>const CARDS=',bridge+'<script>const CARDS=')
    html = html.replace('</body>', '<script>window.setState(' + json.dumps(demo, ensure_ascii=False) + '); window.setPanelState({open:true,mode:"side"});</script></body>')

    class Handler(SimpleHTTPRequestHandler):
        def do_GET(self):
            if self.path == '/':
                body = html.encode('utf-8')
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path.startswith(('/art/','/frames/','/theme/')):
                super().do_GET()
            else:
                self.send_error(404)

    print(f'Local UI preview: http://127.0.0.1:{args.port}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(ROOT / 'web'))).serve_forever()


if __name__ == '__main__':
    main()
