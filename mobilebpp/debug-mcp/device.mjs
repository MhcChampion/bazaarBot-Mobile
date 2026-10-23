import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { existsSync } from 'node:fs';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

const exec = promisify(execFile);
const DEFAULT_PACKAGE = 'com.tempogames.thebazaar.bazaarbotmobile';
// Override only when explicitly requested; never auto-select the original game package.
export const PACKAGE = process.env.BPP_ANDROID_PACKAGE || DEFAULT_PACKAGE;
export const MEDIA = `/sdcard/Android/media/${PACKAGE}`;
export const LEGACY = `/sdcard/Android/data/${PACKAGE}/files`;
export const FILES = ['bpp.log', 'bpp_dump.txt', 'bpp_state.json', 'bpp_settings.txt', 'bpp_builds.json', 'bpp_builds_v5.json', 'bpp_runs.json'];
export function isMobileApkArtifact(name) {
  return /^TheBazaar-bpp-.*\.apk$/.test(name) || /^The Bazaar - bazaarBot Mobile-v.*\.apk$/.test(name);
}

export function snapshotStatus(snapshot,pid,deviceTime) {
  const age=typeof snapshot.updated_at==='number' && Number.isFinite(deviceTime) ? deviceTime-snapshot.updated_at : null;
  const sameProcess=typeof snapshot.process_id==='number' ? pid.split(/\s+/).includes(String(snapshot.process_id)) : null;
  return {age_seconds:age,same_process:sameProcess,fresh:!!pid && age!==null && age>=-5 && age<5 && sameProcess!==false};
}

export class Device {
  constructor(root, adb = process.env.BPP_ADB) {
    const drive = path.parse(root).root;
    const candidates = [adb, path.join(root, 'tools/platform-tools/adb.exe'),
      path.join(process.env.LOCALAPPDATA || '', 'Android/Sdk/platform-tools/adb.exe'),
      path.join(drive, 'mumu/MuMuPlayer/nx_device/12.0/shell/adb.exe')];
    this.adb = candidates.find(p => p && existsSync(p)) || adb || 'adb';
  }
  async run(args, timeout = 15000) {
    try {
      const result = await exec(this.adb, args, { timeout, maxBuffer: 2 * 1024 * 1024, windowsHide: true, encoding: 'utf8' });
      return result.stdout.trim();
    } catch (error) {
      if (error.code === 'ENOENT') throw new Error('找不到 adb；设置 BPP_ADB 为 adb 可执行文件路径。');
      throw new Error(`adb 失败：${(error.stderr || error.message).trim()}`);
    }
  }
  async list() {
    const text = await this.run(['devices', '-l']);
    return text.split(/\r?\n/).slice(1).filter(line => line.trim()).map(line => {
      const [serial, state, ...details] = line.trim().split(/\s+/);
      return { serial, state, details: details.join(' ') };
    });
  }
  async serial(requested) {
    const devices = await this.list();
    if (requested) {
      if (!devices.some(d => d.serial === requested && d.state === 'device')) throw new Error('指定设备未连接或未授权。');
      return requested;
    }
    const ready = devices.filter(d => d.state === 'device');
    if (ready.length !== 1) throw new Error(ready.length ? '连接了多台设备，请指定 serial。' : '没有可用设备；请连接手机并允许 USB 调试。');
    return ready[0].serial;
  }
  async read(name, requested, lines = 200) {
    if (!FILES.includes(name)) throw new Error('不支持的调试文件。');
    const serial = await this.serial(requested);
    // Try the current directory first, then older builds. Never accept arbitrary paths.
    const names=name==='bpp_builds.json'?['bpp_runs.json','bpp_builds_v5.json',name]:[name];
    for (const filename of names) for (const dir of [MEDIA, LEGACY]) {
      try {
        const args = name === 'bpp.log' ? ['tail', '-n', String(lines)] : ['cat'];
        return { serial, path: `${dir}/${filename}`, text: await this.run(['-s', serial, 'shell', ...args, `${dir}/${filename}`]) };
      } catch (error) {
        if (!/No such file|not found/i.test(error.message)) throw error;
      }
    }
    throw new Error(`${name} 不存在；确认 Mod 已启动。bpp_state.json 需要 v0.84+。`);
  }
  async command(name, value = '', requested) {
    const allowed = ['bpp_open', 'bpp_close', 'bpp_dump', 'bpp_status', 'bpp_speed', 'bpp_launcher', 'bpp_keyboard'];
    if (!allowed.includes(name)) throw new Error('不支持的调试命令。');
    if (name === 'bpp_speed' && !['0', '1', '2', '3'].includes(value)) throw new Error('速度只能是 0、1、2、3。');
    if (name === 'bpp_launcher' && !['side', 'bubble'].includes(value)) throw new Error('显示方式只能是 side 或 bubble。');
    const serial = await this.serial(requested);
    const temp = await mkdtemp(path.join(tmpdir(), 'bpp-mcp-'));
    try {
      const local = path.join(temp, name);
      await writeFile(local, value, 'utf8');
      await this.run(['-s', serial, 'shell', 'mkdir', '-p', MEDIA]);
      const pending = `${MEDIA}/${name}.tmp`;
      await this.run(['-s', serial, 'push', local, pending]);
      await this.run(['-s', serial, 'shell', 'mv', '-f', pending, `${MEDIA}/${name}`]);
      return { serial, command: name, value, status: 'queued', note: '命令已写入；需运行中的 Mod 消费，以 bpp_state.json 或日志确认执行结果。' };
    } finally { await rm(temp, { recursive: true, force: true }); }
  }
  async safeMode(enabled, requested) {
    const serial = await this.serial(requested);
    for (const dir of [MEDIA, LEGACY]) {
      await this.run(['-s', serial, 'shell', 'mkdir', '-p', dir]);
      await this.run(['-s', serial, 'shell', ...(enabled ? ['touch'] : ['rm', '-f']), `${dir}/bpp_safe_mode`]);
    }
    return { serial, enabled, takesEffect: '下次游戏进程启动时生效；不会自动终止游戏。' };
  }
}
