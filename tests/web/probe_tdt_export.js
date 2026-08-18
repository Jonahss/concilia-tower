/* Probe: landing-page slot -> .TDT conversion (ct_sav_to_tdt KEEPALIVE).
 * Imports a .sav ($PROBE_SAV), converts it headless, validates the TDT
 * bytes by re-importing them as a second slot.
 * Usage: PROBE_SAV=/path/to.sav node tests/web/probe_tdt_export.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');
const { Cdp } = require('./cdp');

const REPO = path.resolve(__dirname, '../..');
const DIST = path.join(REPO, 'web/dist');
const OUT = path.join(__dirname, 'out-probe');
const PORT = 8616;
const SAV = process.env.PROBE_SAV;
if (!SAV) throw new Error('set PROBE_SAV');

function findExe() {
  const c = path.join(process.env.HOME,
    '.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE');
  if (fs.existsSync(c)) return c;
  throw new Error('EXE missing');
}

(async () => {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
  const exeStaged = path.join(DIST, 'SIMTOWER.EXE');
  fs.copyFileSync(findExe(), exeStaged);
  try { execSync(`fuser -k ${PORT}/tcp 2>/dev/null`); } catch (e) {}
  const server = spawn('python3', ['-m', 'http.server', String(PORT)],
    { cwd: DIST, stdio: 'ignore' });
  const cdp = new Cdp({ profile: path.join(OUT, 'profile'),
                        chromeLog: path.join(OUT, 'chrome.log') });
  process.on('exit', () => {
    try { fs.unlinkSync(exeStaged); } catch (e) {}
    try { server.kill(); } catch (e) {}
    try { cdp.chrome.kill(); } catch (e) {}
  });
  await new Promise(r => setTimeout(r, 800));
  cdp.launch();
  cdp.on('Page.javascriptDialogOpening', (p, sessionId) => {
    console.log('dialog:', p.type, p.message);
    const params = { accept: true };
    if (p.type === 'prompt') params.promptText = p.defaultPrompt || 'probe';
    cdp.send('Page.handleJavaScriptDialog', params, sessionId).catch(() => {});
  });

  await cdp.send('Runtime.enable', {}, cdp.sessionId).catch(() => {});
  cdp.on('Runtime.consoleAPICalled', (p) => {
    const t = (p.args || []).map(a => a.value).filter(Boolean).join(' ');
    if (t) console.log('[game]', t);
  });
  await cdp.openPage(`http://127.0.0.1:${PORT}/index.html`);
  await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);

  await cdp.setFileInput('#importfile', [SAV]);
  cdp.evalDetached(`document.getElementById('importfile')
      .dispatchEvent(new Event('change'))`);
  await cdp.waitFor(`document.querySelectorAll('#slotlist .slot').length > 0`, 20000);

  const res = await cdp.eval(`(function(){
    var rows=document.querySelectorAll('#slotlist .slot');
    var name=rows[0].querySelector('.nm').textContent;
    var p=Module.ccall('ct_sav_to_tdt','string',['string'],
                       ['/persist/'+name+'.sav']);
    if(!p) return {err:'convert failed'};
    var bytes=FS.readFile(p);
    return {name:name, len:bytes.length,
            head:Array.from(bytes.slice(0,8))};
  })()`);
  console.log('convert:', JSON.stringify(res));
  if (res.err) process.exit(1);

  // pull the TDT out and re-import it through the UI as validation
  const b64 = await cdp.eval(`(function(){
    var b=FS.readFile('/slot_export.tdt');var s='';
    for(var i=0;i<b.length;i+=0x8000)
      s+=String.fromCharCode.apply(null,b.subarray(i,i+0x8000));
    return btoa(s); })()`);
  const tdt = path.join(OUT, 'roundtrip.TDT');
  fs.writeFileSync(tdt, Buffer.from(b64, 'base64'));
  await cdp.setFileInput('#importfile', [tdt]);
  cdp.evalDetached(`document.getElementById('importfile')
      .dispatchEvent(new Event('change'))`);
  await new Promise(r => setTimeout(r, 2500));
  const rows = await cdp.eval(`Array.from(document.querySelectorAll('#slotlist .slot'))
    .map(function(r){ return r.querySelector('.nm').textContent + ' | ' +
                             r.querySelector('.meta').textContent; })`);
  console.log('slots after roundtrip:', JSON.stringify(rows, null, 1));
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
