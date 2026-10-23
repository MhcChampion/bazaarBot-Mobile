// Explicit opt-in integration check against one running BPP device; restores UI/speed settings.
import { Client } from '@modelcontextprotocol/sdk/client/index.js';
import { StdioClientTransport } from '@modelcontextprotocol/sdk/client/stdio.js';
import { fileURLToPath } from 'node:url';
import { writeFile } from 'node:fs/promises';
import { setTimeout as delay } from 'node:timers/promises';
import assert from 'node:assert/strict';

const client=new Client({name:'bpp-device-smoke',version:'1.0.0'});
const serial=process.argv[2];
const evidence={started:new Date().toISOString(),checks:[]};
async function call(name,args={}) {
  const result=await client.callTool({name,arguments:{...args,...(serial?{serial}:{})}});
  if(result.isError)throw new Error(result.content[0].text);
  return JSON.parse(result.content[0].text);
}
async function state(){const d=await call('get_device_status');assert.equal(d.running,true,'game must be running');assert.equal(d.fresh,true,'snapshot is stale; wait for Mod to finish booting');return d.snapshot;}
async function waitFor(predicate) {
  const deadline=Date.now()+20000;
  while(Date.now()<deadline){const s=await state();if(predicate(s))return s;await delay(350);}
  throw new Error('Timed out waiting for Mod acknowledgement');
}
let initial;
try {
  await client.connect(new StdioClientTransport({command:process.execPath,args:[fileURLToPath(new URL('./server.mjs',import.meta.url))],stderr:'inherit'}));
  initial=await state();assert.equal(typeof initial.time_scale,'number','requires v0.85+');
  for(const mode of ['side','bubble']){
    await call('set_launcher',{mode});const s=await waitFor(s=>s.mode===mode&&s.last_command===`launcher=${mode} OK`);
    evidence.checks.push({command:'launcher',mode,ack:s.last_command});
  }
  for(const open of [false,true]){
    await call('set_panel',{open});const s=await waitFor(s=>s.open===open&&s.last_command===`${open?'open':'close'} OK`);
    evidence.checks.push({command:'panel',open,ack:s.last_command});
  }
  for(const speed of [2,3,0,1]){
    await call('set_speed',{speed});const s=await waitFor(s=>s.speed===speed&&s.last_command===`speed=${speed} OK`);
    assert.equal(s.time_scale,speed,'engine Time.timeScale differs from request');
    evidence.checks.push({command:'speed',requested:speed,actual:s.time_scale});
  }
  await call('request_snapshot');const snapshot=await waitFor(s=>s.last_command==='dump OK');
  for(const section of ['board','stash','skills'])assert.ok(snapshot[section].every(c=>typeof c.g==='string'&&c.g.length===36));
  const dump=await call('read_debug_file',{file:'bpp_dump.txt'});assert.match(dump.text,/# BPP mobile dump/);
  evidence.snapshot=snapshot;evidence.success=true;
} catch(error){evidence.success=false;evidence.error=error.message;process.exitCode=1;}
finally {
  if(initial){
    try{await call('set_speed',{speed:[0,1,2,3].includes(initial.speed)?initial.speed:1});await waitFor(s=>s.speed===initial.speed);
      await call('set_launcher',{mode:initial.mode});await waitFor(s=>s.mode===initial.mode);
      await call('set_panel',{open:initial.open});await waitFor(s=>s.open===initial.open);
    }catch(error){evidence.restoreError=error.message;process.exitCode=1;}
  }
  await client.close();
  evidence.finished=new Date().toISOString();
  const file=fileURLToPath(new URL('../../out/mcp-device-smoke.json',import.meta.url));
  await writeFile(file,JSON.stringify(evidence,null,2));
  console.log(JSON.stringify(evidence,null,2));
}
