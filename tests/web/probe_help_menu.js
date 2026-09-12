/* Probe: the Help > SimTower Manual item in the WEB build. Boots web/dist
 * with a fresh tower, clicks "Help" on the Win3.1 menu bar, screenshots
 * the open dropdown, clicks the item and asserts that a NEW browser tab
 * was created pointing at archive.org (window.open from the animation
 * frame under transient user activation).
 * Output: tests/web/out-help/
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');
const { Cdp } = require('./cdp');

const REPO = path.resolve(__dirname, '../..');
const DIST = path.join(REPO, 'web/dist');
const OUT = path.join(__dirname, 'out-help');
const PORT = 8621;
const URL_BASE = `http://127.0.0.1:${PORT}/index.html`;
const MANUAL = 'archive.org/details/SimTower_-_Manual_-_PC';

/* Menu-bar geometry from src/main.c: cx starts at 4, each label is
 * strlen*7 + MENU_ITEM_PAD*2 (12*2) wide, bar is MENU_BAR_H (18) tall. */
const LABELS = ['Game','Build Res.','Build Com.','Transport','Services',
                'Speed','Options','Windows','View','Help'];
function menuX(name) {
  let cx = 4;
  for (const l of LABELS) {
    const w = l.length * 7 + 24;
    if (l === name) return cx + w / 2;
    cx += w;
  }
  throw new Error('no menu ' + name);
}
const ITEM_DY = Number(process.argv[2] || 30);   /* y of first dropdown row */

function findExe() {
  const cands = [
    process.env.SIMTOWER_EXE,
    path.join(REPO, 'web/dev-assets/SIMTOWER.EXE'),
    path.join(process.env.HOME || '',
      '.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE'),
  ];
  for (const c of cands) if (c && fs.existsSync(c)) return c;
  throw new Error('SIMTOWER.EXE not found');
}

(async () => {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
  const exe = findExe();
  try { execSync(`fuser -k ${PORT}/tcp 2>/dev/null`); } catch (e) {}
  const server = spawn('python3', ['-m', 'http.server', String(PORT)],
    { cwd: DIST, stdio: 'ignore' });
  const cdp = new Cdp({
    profile: path.join(OUT, 'profile'),
    chromeLog: path.join(OUT, 'chrome.log'),
  });
  const cleanup = () => {
    try { server.kill(); } catch (e) {}
    try { cdp.chrome.kill(); } catch (e) {}
  };
  process.on('exit', cleanup);
  await new Promise(r => setTimeout(r, 800));
  cdp.launch();
  cdp.on('Page.javascriptDialogOpening', (p, sessionId) => {
    console.log('dialog:', p.type, p.message);
    const params = { accept: true };
    if (p.type === 'prompt') params.promptText = p.defaultPrompt || 'probe';
    cdp.send('Page.handleJavaScriptDialog', params, sessionId).catch(() => {});
  });

  console.log('== boot');
  await cdp.openPage(URL_BASE);
  await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);
  await cdp.setFileInput('#exefile', [exe]);
  cdp.evalDetached(`document.getElementById('exefile')
      .dispatchEvent(new Event('change'))`);
  await cdp.waitFor(`!document.getElementById('startbtn').disabled`, 20000);
  cdp.evalDetached(`document.getElementById('newslotbtn').click()`);
  await new Promise(r => setTimeout(r, 400));
  await cdp.eval(`(document.getElementById('minput').value='Probe',
                   document.getElementById('mok').click(), 0)`).catch(()=>{});
  await new Promise(r => setTimeout(r, 400));
  cdp.evalDetached(`document.getElementById('startbtn').click()`);
  await cdp.waitFor(`document.getElementById('canvaswrap').style.display==='block'`, 60000);
  await new Promise(r => setTimeout(r, 6000));

  const map = await cdp.eval(`(function(){
    var c = Module.canvas, r = c.getBoundingClientRect();
    return { left: r.left, top: r.top, cssW: r.width, cssH: r.height,
             w: c.width, h: c.height }; })()`);
  console.log('canvas map:', JSON.stringify(map));
  const toView = (gx, gy) => ({
    x: map.left + gx * (map.cssW / map.w),
    y: map.top + gy * (map.cssH / map.h),
  });
  async function clickAt(gx, gy) {
    const v = toView(gx, gy);
    await cdp.send('Input.dispatchMouseEvent',
      { type: 'mouseMoved', x: v.x, y: v.y, button: 'none' }, cdp.sessionId);
    await new Promise(r => setTimeout(r, 120));
    for (const type of ['mousePressed', 'mouseReleased']) {
      await cdp.send('Input.dispatchMouseEvent',
        { type, x: v.x, y: v.y, button: 'left', buttons: 1, clickCount: 1 },
        cdp.sessionId);
      await new Promise(r => setTimeout(r, 90));
    }
    await new Promise(r => setTimeout(r, 700));
  }
  cdp.on('Runtime.consoleAPICalled', (p) => {
    const txt = (p.args || []).map(a => a.value).filter(Boolean).join(' ');
    if (txt) console.log('[game]', txt);
  });

  const before = (await cdp.send('Target.getTargets')).targetInfos
    .filter(t => t.type === 'page');
  console.log('tabs before:', before.map(t => t.url).join(' | '));

  const hx = menuX('Help');
  await clickAt(hx, 9);
  await cdp.screenshot(path.join(OUT, '1-help-open.png'));
  await clickAt(hx + 40, ITEM_DY);
  await new Promise(r => setTimeout(r, 1500));
  await cdp.screenshot(path.join(OUT, '2-after-click.png'));

  const after = (await cdp.send('Target.getTargets')).targetInfos
    .filter(t => t.type === 'page');
  console.log('tabs after:', after.map(t => t.url).join(' | '));
  const opened = after.filter(t => t.url.includes(MANUAL));
  if (opened.length !== 1) {
    console.error(`FAIL: expected 1 manual tab, found ${opened.length}`);
    process.exit(1);
  }
  console.log('PASS: manual tab opened ->', opened[0].url);
  await cdp.close();
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
