/* E2E for the web save-manager dialog flows (web-plan C2-C4 + C6).
 *
 * Drives a real chromium (headless=new) over the CDP pipe: the file
 * chooser is bypassed with DOM.setFileInputFiles and every
 * prompt/confirm/alert is answered through Page.handleJavaScriptDialog,
 * so the OS-modal flows that xdotool can't reach headlessly get real
 * coverage. No dependencies beyond node + chromium.
 *
 * Prereqs: `make web` (fresh web/dist) and a SIMTOWER.EXE (auto-found
 * via the same paths as scripts/screenshot.sh, or $SIMTOWER_EXE).
 * Run:     node tests/web/test_dialogs.js
 * Output:  tests/web/out/ — screenshots + chrome.log (gitignored).
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');
const { Cdp, DialogDirector } = require('./cdp');

const REPO = path.resolve(__dirname, '../..');
const DIST = path.join(REPO, 'web/dist');
const OUT = path.join(__dirname, 'out');
const PORT = 8614;
const URL_BASE = `http://127.0.0.1:${PORT}/index.html`;
const TDT_FIXTURE = path.join(REPO, 'ct_export.tdt');
const SAV_FIXTURE = path.join(OUT, 'fixture.sav');
const JUNK = path.join(OUT, 'junk.sav');

function findExe() {
  const cands = [
    process.env.SIMTOWER_EXE,
    path.join(REPO, '../OpenSkyscraper/data/SIMTOWER.EXE'),
    path.join(process.env.HOME || '',
      '.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE'),
  ];
  for (const c of cands) if (c && fs.existsSync(c)) return c;
  throw new Error('SIMTOWER.EXE not found — set $SIMTOWER_EXE');
}

let passed = 0, failed = 0;
function ok(cond, label) {
  if (cond) { passed++; console.log('  PASS ' + label); }
  else { failed++; console.log('  FAIL ' + label); }
}
async function step(label, fn) {
  console.log('\n== ' + label);
  await fn();
}

/* Slot state is read straight from FS — the same source of truth the
 * UI renders from — plus the rendered rows for the visible half. */
const SLOTS = `(function(){
  var out=[]; try { FS.readdir('/persist').forEach(function(f){
    var m=/^(.+)\\.sav$/.exec(f); if(m) out.push(m[1]); }); } catch(e){}
  return out.sort(); })()`;
const ROWS = `Array.from(document.querySelectorAll('#slotlist .slot')).map(function(r){
  return { name: r.querySelector('.nm').textContent,
           meta: r.querySelector('.meta').textContent,
           sel: r.className.indexOf('sel')>=0 }; })`;
function meta(name) {
  return `(function(){ try { var s=Module.ccall('ct_sav_meta','string',['string'],
    ['/persist/${name}.sav']); return s?JSON.parse(s):null; } catch(e){ return null; } })()`;
}
/* Row buttons only exist once renderSlots ran (in the syncfs callback) —
 * always wait for the RENDERED row before clicking its ops. */
function rowShown(name) {
  return `(${ROWS}).some(function(r){ return r.name===${JSON.stringify(name)}; })`;
}
function clickOp(slotName, title) {
  return `(function(){
    var rows=document.querySelectorAll('#slotlist .slot');
    for (var i=0;i<rows.length;i++){
      if (rows[i].querySelector('.nm').textContent===${JSON.stringify(slotName)}){
        var b=rows[i].querySelector('button[title^=${JSON.stringify(title)}]');
        if(!b) return 'no-button';
        b.click(); return 'clicked';
      }
    } return 'no-row'; })()`;
}

(async () => {
  if (!fs.existsSync(path.join(DIST, 'index.html')))
    throw new Error('web/dist/index.html missing — run `make web` first');
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(JUNK, 'this is not a sav');

  /* Stage the EXE for the ?exe= dev hook; dist is gitignored and CI
   * builds fresh, so it can never reach the published site. */
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
  await new Promise((r) => setTimeout(r, 800));
  cdp.launch();
  const dialogs = new DialogDirector(cdp);

  /* ---------- Phase A: boot fresh, build a genuine wasm .sav ---------- */
  await step('Phase A: boot via ?exe= and save with F5', async () => {
    await cdp.openPage(URL_BASE + '?exe=SIMTOWER.EXE');
    await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);
    await cdp.waitFor(`document.getElementById('canvaswrap').style.display==='block'`, 60000);
    await new Promise(r => setTimeout(r, 6000));   /* let the campaign draw */
    await cdp.eval(`Module.canvas.focus()`);
    await cdp.key('F5', 'F5', 116);
    let saved = false;
    try {
      await cdp.waitFor(`(function(){ try { FS.stat('/persist/concilliatower.sav');
        return true; } catch(e){ return false; } })()`, 10000);
      saved = true;
    } catch (e) {
      console.log('  F5 did not land, trying Ctrl+S');
      await cdp.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 's',
        code: 'KeyS', windowsVirtualKeyCode: 83, modifiers: 2 }, cdp.sessionId);
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 's',
        code: 'KeyS', windowsVirtualKeyCode: 83, modifiers: 2 }, cdp.sessionId);
      await cdp.waitFor(`(function(){ try { FS.stat('/persist/concilliatower.sav');
        return true; } catch(e){ return false; } })()`, 10000);
      saved = true;
    }
    ok(saved, 'F5/Ctrl+S produced /persist/concilliatower.sav');
    const m = await cdp.eval(meta('concilliatower'));
    ok(m && m.exact, 'ct_sav_meta reads the save exactly: ' + JSON.stringify(m));
    const b64 = await cdp.eval(`(function(){
      var b=FS.readFile('/persist/concilliatower.sav');
      var s=''; for(var i=0;i<b.length;i+=0x8000)
        s+=String.fromCharCode.apply(null,b.subarray(i,i+0x8000));
      return btoa(s); })()`);
    fs.writeFileSync(SAV_FIXTURE, Buffer.from(b64, 'base64'));
    ok(fs.statSync(SAV_FIXTURE).size > 1000, '.sav fixture extracted (' +
       fs.statSync(SAV_FIXTURE).size + ' bytes)');
    await cdp.eval(`new Promise(function(res){ FS.syncfs(false,function(){ res(1); }); })`);
    await cdp.screenshot(path.join(OUT, 'shot_A_ingame.png'));
  });

  /* ---------- Phase B: landing page, all dialog flows ---------- */
  await step('Phase B: reload landing page, slot listed with vitals', async () => {
    await cdp.navigate(URL_BASE);
    await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);
    const rows = await cdp.eval(ROWS);
    ok(rows.length === 1 && rows[0].name === 'concilliatower',
       'slot listed: ' + JSON.stringify(rows));
    ok(/Day \d+/.test(rows[0].meta) && /pop \d+/.test(rows[0].meta),
       'vitals rendered: "' + rows[0].meta + '"');
    await cdp.screenshot(path.join(OUT, 'shot_B_landing.png'));
  });

  await step('New tower: prompt cancel → no slot', async () => {
    cdp.evalDetached(`document.getElementById('newslotbtn').click()`);
    await dialogs.expect('prompt', 'Name your new tower', { accept: false });
    const rows = await cdp.eval(ROWS);
    ok(rows.length === 1, 'still one slot after cancel');
  });

  await step('New tower: invalid name → explanatory alert', async () => {
    cdp.evalDetached(`document.getElementById('newslotbtn').click()`);
    await dialogs.expect('prompt', 'Name your new tower',
                         { accept: true, text: 'bad/name!' });
    await dialogs.expect('alert', 'Tower names:');
    const rows = await cdp.eval(ROWS);
    ok(rows.length === 1, 'invalid name created nothing');
  });

  await step('New tower: "Probe Tower" → draft slot, selected', async () => {
    cdp.evalDetached(`document.getElementById('newslotbtn').click()`);
    await dialogs.expect('prompt', 'Name your new tower',
                         { accept: true, text: 'Probe Tower' });
    const rows = await cdp.eval(ROWS);
    const probe = rows.find(r => r.name === 'Probe Tower');
    ok(probe && probe.sel && /empty lot/.test(probe.meta),
       'draft row present+selected: ' + JSON.stringify(probe));
  });

  await step('Rename: concilliatower → "Main Tower"', async () => {
    cdp.evalDetached(clickOp('concilliatower', 'Rename'));
    const ev = await dialogs.expect('prompt', 'New name for "concilliatower"',
                                    { accept: true, text: 'Main Tower' });
    ok(ev.default === 'concilliatower', 'prompt pre-filled with old name');
    await cdp.waitFor(`(${SLOTS}).indexOf('Main Tower')>=0`, 8000);
    const slots = await cdp.eval(SLOTS);
    ok(slots.includes('Main Tower') && !slots.includes('concilliatower'),
       'FS renamed: ' + JSON.stringify(slots));
    const m = await cdp.eval(meta('Main Tower'));
    ok(m && m.exact, 'renamed save still readable');
    await cdp.waitFor(rowShown('Main Tower'), 8000);
  });

  await step('Rename collision → "already exists" alert', async () => {
    cdp.evalDetached(clickOp('Main Tower', 'Rename'));
    await dialogs.expect('prompt', 'New name for "Main Tower"',
                         { accept: true, text: 'Probe Tower' });
    await dialogs.expect('alert', 'already exists');
    const slots = await cdp.eval(SLOTS);
    ok(slots.includes('Main Tower'), 'collision left original untouched');
  });

  await step('Duplicate: "Main Tower" → copy with equal vitals', async () => {
    cdp.evalDetached(clickOp('Main Tower', 'Duplicate'));
    const ev = await dialogs.expect('prompt', 'Name for the copy',
                                    { accept: true, text: 'Main Tower copy' });
    ok(ev.default === 'Main Tower copy', 'copy prompt suggests "<name> copy"');
    await cdp.waitFor(`(${SLOTS}).indexOf('Main Tower copy')>=0`, 8000);
    await cdp.waitFor(rowShown('Main Tower copy'), 8000);
    const m1 = await cdp.eval(meta('Main Tower'));
    const m2 = await cdp.eval(meta('Main Tower copy'));
    ok(m2 && m2.exact && JSON.stringify(m1) === JSON.stringify(m2),
       'copy vitals equal original: ' + JSON.stringify(m2));
  });

  await step('Delete: confirm-cancel keeps, confirm-accept removes', async () => {
    cdp.evalDetached(clickOp('Main Tower copy', 'Delete'));
    await dialogs.expect('confirm', 'Delete tower "Main Tower copy"',
                         { accept: false });
    let slots = await cdp.eval(SLOTS);
    ok(slots.includes('Main Tower copy'), 'cancel kept the tower');
    cdp.evalDetached(clickOp('Main Tower copy', 'Delete'));
    await dialogs.expect('confirm', 'Delete tower "Main Tower copy"',
                         { accept: true });
    await cdp.waitFor(`(${SLOTS}).indexOf('Main Tower copy')<0`, 8000);
    slots = await cdp.eval(SLOTS);
    ok(!slots.includes('Main Tower copy'), 'accept deleted it: ' + JSON.stringify(slots));
  });

  async function importFile(file) {
    await cdp.setFileInput('#importfile', [file]);
    /* setFileInputFiles sets .files but does not fire the change event. */
    cdp.evalDetached(`document.getElementById('importfile')
      .dispatchEvent(new Event('change'))`);
  }

  await step('Import junk file → rejected with format alert', async () => {
    await importFile(JUNK);
    await dialogs.expect('alert', 'not a ConciliaTower .sav');
    const slots = await cdp.eval(SLOTS);
    ok(!slots.some(s => /junk/i.test(s)), 'nothing imported from junk');
  });

  await step('Import .sav backup → "Restored Tower" with vitals', async () => {
    await importFile(SAV_FIXTURE);
    const ev = await dialogs.expect('prompt', 'Restore backup as a tower named',
                                    { accept: true, text: 'Restored Tower' });
    ok(ev.default === 'fixture', 'name defaults from the filename');
    await cdp.waitFor(`(${SLOTS}).indexOf('Restored Tower')>=0`, 8000);
    const m = await cdp.eval(meta('Restored Tower'));
    ok(m && m.exact, 'restored save readable: ' + JSON.stringify(m));
    /* selectSlot fires in the syncfs callback — wait for the render. */
    await cdp.waitFor(`(${ROWS}).some(function(r){
      return r.name==='Restored Tower' && r.sel; })`, 8000);
    const rows = await cdp.eval(ROWS);
    const r = rows.find(x => x.name === 'Restored Tower');
    ok(r && r.sel && /Day \d+/.test(r.meta), 'restored row selected w/ vitals');
  });

  await step('Re-import same name: overwrite confirm, cancel then accept', async () => {
    await importFile(SAV_FIXTURE);
    await dialogs.expect('prompt', 'Restore backup as a tower named',
                         { accept: true, text: 'Restored Tower' });
    await dialogs.expect('confirm', 'Overwrite the existing tower "Restored Tower"',
                         { accept: false });
    let slots = await cdp.eval(SLOTS);
    ok(slots.filter(s => s === 'Restored Tower').length === 1, 'cancel: still one');
    await importFile(SAV_FIXTURE);
    await dialogs.expect('prompt', 'Restore backup as a tower named',
                         { accept: true, text: 'Restored Tower' });
    await dialogs.expect('confirm', 'Overwrite the existing tower',
                         { accept: true });
    await new Promise(r => setTimeout(r, 1000));
    const m = await cdp.eval(meta('Restored Tower'));
    ok(m && m.exact, 'accept: overwrite landed, save readable');
  });

  await step('Import original .TDT → staged, Start opens + persists it', async () => {
    if (!fs.existsSync(TDT_FIXTURE)) {
      console.log('  SKIP — no ct_export.tdt fixture');
      return;
    }
    await importFile(TDT_FIXTURE);
    await dialogs.expect('prompt', 'Import "ct_export.tdt" as a new tower named',
                         { accept: true, text: 'TDT Tower' });
    await cdp.waitFor(rowShown('TDT Tower'), 8000);
    const rows = await cdp.eval(ROWS);
    const r = rows.find(x => x.name === 'TDT Tower');
    ok(r && r.sel && /imported ct_export\.tdt/.test(r.meta),
       'staged row: ' + JSON.stringify(r));
    await cdp.screenshot(path.join(OUT, 'shot_C_staged.png'));
    cdp.evalDetached(`document.getElementById('startbtn').click()`);
    await cdp.waitFor(`(function(){ try { FS.stat('/persist/TDT Tower.sav');
      return true; } catch(e){ return false; } })()`, 45000);
    const m = await cdp.eval(meta('TDT Tower'));
    ok(m && m.exact, 'TDT imported+persisted, meta: ' + JSON.stringify(m));
    await new Promise(r => setTimeout(r, 5000));
    await cdp.screenshot(path.join(OUT, 'shot_D_tdt_ingame.png'));
  });

  await step('Reload: everything survived into IDBFS', async () => {
    await cdp.navigate(URL_BASE);
    await cdp.waitFor(`typeof Module!=='undefined' && Module.engineReady===true`, 90000);
    const slots = await cdp.eval(SLOTS);
    ok(slots.includes('Main Tower') && slots.includes('Restored Tower') &&
       slots.includes('TDT Tower') && !slots.includes('concilliatower'),
       'persisted slots: ' + JSON.stringify(slots));
    await cdp.screenshot(path.join(OUT, 'shot_E_final.png'));
  });

  dialogs.assertQuiet();
  console.log('\nDialog transcript:');
  for (const d of dialogs.transcript)
    console.log(`  [${d.type}] "${d.message}" -> ` +
                (d.answered.accept ? `accept${d.answered.text !== undefined ?
                 ' "' + d.answered.text + '"' : ''}` : 'dismiss'));
  console.log(`\n${passed} passed, ${failed} failed`);
  await cdp.close();
  process.exit(failed ? 1 : 0);
})().catch((e) => {
  console.error('\nFATAL:', e.message);
  process.exit(2);
});
