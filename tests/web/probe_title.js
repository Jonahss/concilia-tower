/* Probe: title-screen visual states at desktop size (1920x1080).
 * Screenshots: setup mode, find-help open, towers mode (EXE loaded via
 * the real file input — no auto-start), and the New-tower dialog.
 * Output: tests/web/out-title/
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');
const { Cdp } = require('./cdp');

const REPO = path.resolve(__dirname, '../..');
const DIST = path.join(REPO, 'web/dist');
const OUT = path.join(__dirname, 'out-title');
const PORT = 8619;
const URL_BASE = `http://127.0.0.1:${PORT}/index.html`;
const EXE = path.join(REPO, 'web/dev-assets/SIMTOWER.EXE');

(async () => {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
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
  cdp.launch(['--window-size=1920,1080']);

  await cdp.openPage(URL_BASE);
  await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);
  await cdp.screenshot(path.join(OUT, '1-setup.png'));

  await cdp.eval(`document.getElementById('findhelp').open = true`);
  await new Promise(r => setTimeout(r, 300));
  await cdp.screenshot(path.join(OUT, '2-findhelp.png'));
  await cdp.eval(`document.getElementById('findhelp').open = false`);

  await cdp.setFileInput('#exefile', [EXE]);
  await cdp.eval(`(document.getElementById('exefile')
      .dispatchEvent(new Event('change')), 0)`);
  await cdp.waitFor(`!document.getElementById('startbtn').disabled`, 30000);
  await new Promise(r => setTimeout(r, 600));   /* hash note settles */
  await cdp.screenshot(path.join(OUT, '3-towers.png'));

  await cdp.eval(`(document.getElementById('newslotbtn').click(), 0)`);
  await new Promise(r => setTimeout(r, 300));
  await cdp.screenshot(path.join(OUT, '4-dialog.png'));

  console.log('done:', fs.readdirSync(OUT).filter(f => f.endsWith('.png')).join(' '));
  await cdp.close();
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
