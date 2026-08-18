/* Probe: does the tenant-info live-crop picture box render in the WEB build?
 * Boots web/dist with the dev tower save (repo-root concilliatower.sav,
 * has party halls), turns on the inspector, clicks units given on the
 * command line as game-coords, screenshots each dialog.
 *
 * Usage: node tests/web/probe_inspect.js [gx,gy ...]
 *   With no args: boots, screenshots the initial view (shot_boot.png),
 *   prints canvas mapping info, exits. With args: also toggles the
 *   inspector via a click at INSPECT_BTN and clicks each gx,gy,
 *   saving shot_click_<i>.png.
 * Output: tests/web/out-probe/
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');
const { Cdp } = require('./cdp');

const REPO = path.resolve(__dirname, '../..');
const DIST = path.join(REPO, 'web/dist');
const OUT = path.join(__dirname, 'out-probe');
const PORT = 8615;
const URL_BASE = `http://127.0.0.1:${PORT}/index.html`;
const SAV = process.env.PROBE_SAV ||
            path.join(REPO, 'tests/fixtures/SCHMITT.TDT');

function findExe() {
  const cands = [
    process.env.SIMTOWER_EXE,
    path.join(REPO, '../OpenSkyscraper/data/SIMTOWER.EXE'),
    path.join(process.env.HOME || '',
      '.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE'),
  ];
  for (const c of cands) if (c && fs.existsSync(c)) return c;
  throw new Error('SIMTOWER.EXE not found');
}

(async () => {
  const clicks = process.argv.slice(2).map(s => s.split(',').map(Number));
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });

  const exeStaged = path.join(DIST, 'SIMTOWER.EXE');
  fs.copyFileSync(findExe(), exeStaged);
  try { execSync(`fuser -k ${PORT}/tcp 2>/dev/null`); } catch (e) {}
  const server = spawn('python3', ['-m', 'http.server', String(PORT)],
    { cwd: DIST, stdio: 'ignore' });
  const cdp = new Cdp({
    profile: path.join(OUT, 'profile'),
    chromeLog: path.join(OUT, 'chrome.log'),
  });
  const cleanup = () => {
    try { fs.unlinkSync(exeStaged); } catch (e) {}
    try { server.kill(); } catch (e) {}
    try { cdp.chrome.kill(); } catch (e) {}
  };
  process.on('exit', cleanup);
  await new Promise(r => setTimeout(r, 800));
  cdp.launch();
  // auto-accept every dialog so nothing blocks the probe
  cdp.on('Page.javascriptDialogOpening', (p, sessionId) => {
    console.log('dialog:', p.type, p.message);
    const params = { accept: true };
    if (p.type === 'prompt') params.promptText = p.defaultPrompt || 'probe';
    cdp.send('Page.handleJavaScriptDialog', params, sessionId).catch(() => {});
  });

  console.log('== boot landing');
  await cdp.openPage(URL_BASE);
  await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);

  console.log('== import dev save');
  await cdp.setFileInput('#importfile', [SAV]);
  cdp.evalDetached(`document.getElementById('importfile')
      .dispatchEvent(new Event('change'))`);
  await cdp.waitFor(`document.querySelectorAll('#slotlist .slot').length > 0`, 20000);

  const rows = await cdp.eval(`Array.from(document.querySelectorAll('#slotlist .slot'))
    .map(function(r){ return { name: r.querySelector('.nm').textContent,
      meta: r.querySelector('.meta').textContent,
      sel: r.className.indexOf('sel')>=0 }; })`);
  console.log('slots:', JSON.stringify(rows));
  // select the imported (non-empty) slot explicitly
  await cdp.eval(`(function(){
    var rs = document.querySelectorAll('#slotlist .slot');
    for (var i=0;i<rs.length;i++){
      var m = rs[i].querySelector('.meta').textContent;
      if (!/^empty lot$/.test(m)) { rs[i].click(); return m; }
    } return 'none'; })()`).then(m => console.log('selected slot meta:', m));

  console.log('== provide EXE, start');
  await cdp.setFileInput('#exefile', [exeStaged]);
  cdp.evalDetached(`document.getElementById('exefile')
      .dispatchEvent(new Event('change'))`);
  await cdp.waitFor(`!document.getElementById('startbtn').disabled`, 20000);
  cdp.evalDetached(`document.getElementById('startbtn').click()`);
  await cdp.waitFor(`document.getElementById('canvaswrap').style.display==='block'`, 60000);
  await new Promise(r => setTimeout(r, 8000));

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
  await cdp.send('Runtime.enable', {}, cdp.sessionId).catch(() => {});
  cdp.on('Runtime.consoleAPICalled', (p) => {
    const txt = (p.args || []).map(a => a.value).filter(Boolean).join(' ');
    if (txt) console.log('[game]', txt);
  });

  await cdp.screenshot(path.join(OUT, 'shot_boot.png'));
  console.log('boot screenshot saved');

  if (clicks.length) {
    // first pair = inspector tool button, rest = units to inspect
    const [bx, by] = clicks[0];
    await clickAt(bx, by);
    await cdp.screenshot(path.join(OUT, 'shot_tool.png'));
    for (let i = 1; i < clicks.length; i++) {
      await clickAt(clicks[i][0], clicks[i][1]);
      await cdp.screenshot(path.join(OUT, `shot_click_${i}.png`));
    }
  }
  console.log('done');
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
