/* Minimal CDP client over chromium's --remote-debugging-pipe (fd 3/4).
 * Zero dependencies: JSON messages, NUL-delimited, flat sessions. */
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');

class Cdp {
  constructor(opts = {}) {
    this.nextId = 1;
    this.pending = new Map();          // id -> {resolve, reject, method}
    this.eventHandlers = new Map();    // method -> [fn]
    this.buf = Buffer.alloc(0);
    this.opts = opts;
  }

  launch(extraArgs = []) {
    const args = [
      '--remote-debugging-pipe',
      '--headless=new',
      `--user-data-dir=${this.opts.profile}`,
      '--no-first-run', '--no-default-browser-check',
      '--disable-features=TranslateUI',
      '--window-size=1360,960',
      '--enable-unsafe-swiftshader',
      ...extraArgs,
      'about:blank',
    ];
    this.chrome = spawn('chromium', args, {
      stdio: ['ignore', 'ignore',
              fs.openSync(this.opts.chromeLog || '/dev/null', 'w'),
              'pipe', 'pipe'],
    });
    this.writePipe = this.chrome.stdio[3];
    this.readPipe = this.chrome.stdio[4];
    this.readPipe.on('data', (d) => this._onData(d));
    this.exited = new Promise((res) => this.chrome.on('exit', res));
  }

  _onData(d) {
    this.buf = Buffer.concat([this.buf, d]);
    let idx;
    while ((idx = this.buf.indexOf(0)) >= 0) {
      const raw = this.buf.slice(0, idx).toString('utf8');
      this.buf = this.buf.slice(idx + 1);
      let msg;
      try { msg = JSON.parse(raw); } catch (e) { continue; }
      if (msg.id !== undefined && this.pending.has(msg.id)) {
        const p = this.pending.get(msg.id);
        this.pending.delete(msg.id);
        if (msg.error) p.reject(new Error(`${p.method}: ${msg.error.message}`));
        else p.resolve(msg.result);
      } else if (msg.method) {
        for (const fn of this.eventHandlers.get(msg.method) || []) fn(msg.params, msg.sessionId);
      }
    }
  }

  send(method, params = {}, sessionId) {
    const id = this.nextId++;
    const msg = { id, method, params };
    if (sessionId) msg.sessionId = sessionId;
    this.writePipe.write(JSON.stringify(msg) + '\0');
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject, method });
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error(`CDP timeout: ${method}`));
        }
      }, this.opts.timeout || 60000);
    });
  }

  on(method, fn) {
    if (!this.eventHandlers.has(method)) this.eventHandlers.set(method, []);
    this.eventHandlers.get(method).push(fn);
  }

  async openPage(url) {
    const { targetId } = await this.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await this.send('Target.attachToTarget', { targetId, flatten: true });
    this.sessionId = sessionId;
    await this.send('Page.enable', {}, sessionId);
    await this.send('Runtime.enable', {}, sessionId);
    await this.send('DOM.enable', {}, sessionId);
    await this.navigate(url);
    return sessionId;
  }

  async navigate(url) {
    const loaded = new Promise((res) => {
      const h = () => res();
      this.on('Page.loadEventFired', h);
    });
    await this.send('Page.navigate', { url }, this.sessionId);
    await loaded;
  }

  /* Evaluate an expression; returns the JSON value. awaitPromise on. */
  async eval(expr) {
    const r = await this.send('Runtime.evaluate', {
      expression: expr, returnByValue: true, awaitPromise: true,
    }, this.sessionId);
    if (r.exceptionDetails) {
      throw new Error('page threw: ' +
        (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
    }
    return r.result.value;
  }

  /* Fire-and-forget evaluate for calls that will block on a dialog. */
  evalDetached(expr) {
    return this.send('Runtime.evaluate', {
      expression: expr, returnByValue: true, awaitPromise: false,
    }, this.sessionId);
  }

  async waitFor(expr, ms = 30000, step = 250) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      let v = false;
      try { v = await this.eval(expr); } catch (e) { /* mid-nav */ }
      if (v) return v;
      await new Promise((r) => setTimeout(r, step));
    }
    throw new Error(`waitFor timed out: ${expr}`);
  }

  async setFileInput(selector, files) {
    const doc = await this.send('DOM.getDocument', {}, this.sessionId);
    const { nodeId } = await this.send('DOM.querySelector',
      { nodeId: doc.root.nodeId, selector }, this.sessionId);
    if (!nodeId) throw new Error(`no node for ${selector}`);
    await this.send('DOM.setFileInputFiles', { files, nodeId }, this.sessionId);
  }

  async screenshot(path) {
    const r = await this.send('Page.captureScreenshot', { format: 'png' }, this.sessionId);
    fs.writeFileSync(path, Buffer.from(r.data, 'base64'));
  }

  async key(key, code, vk) {
    for (const type of ['rawKeyDown', 'keyUp']) {
      await this.send('Input.dispatchKeyEvent', {
        type, key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk,
      }, this.sessionId);
    }
  }

  async close() {
    try { await this.send('Browser.close'); } catch (e) { this.chrome.kill(); }
    await this.exited;
  }
}

/* Dialog choreography: every prompt/confirm/alert must be EXPECTED.
 * Unexpected dialogs are dismissed and recorded as failures. */
class DialogDirector {
  constructor(cdp) {
    this.cdp = cdp;
    this.transcript = [];
    this.waiting = null;      // {resolve}
    this.arrived = [];        // buffered events
    cdp.on('Page.javascriptDialogOpening', (p, sessionId) => {
      const ev = { type: p.type, message: p.message, default: p.defaultPrompt, sessionId };
      if (this.waiting) { const w = this.waiting; this.waiting = null; w.resolve(ev); }
      else this.arrived.push(ev);
    });
  }
  _next(ms) {
    if (this.arrived.length) return Promise.resolve(this.arrived.shift());
    return new Promise((resolve, reject) => {
      /* The timer may only cancel ITS OWN waiter — a stale timer from a
       * completed expect must not clobber the next step's waiter. */
      const w = { resolve };
      this.waiting = w;
      setTimeout(() => {
        if (this.waiting === w) {
          this.waiting = null;
          reject(new Error('no dialog arrived in ' + ms + 'ms'));
        }
      }, ms);
    });
  }
  /* Expect a dialog of `type` whose message contains `contains`;
   * answer it. Returns the dialog event. */
  async expect(type, contains, { accept = true, text } = {}) {
    const ev = await this._next(10000);
    const entry = { ...ev, answered: { accept, text }, expected: `${type} ~ "${contains}"` };
    this.transcript.push(entry);
    if (ev.type !== type || !ev.message.includes(contains)) {
      await this.cdp.send('Page.handleJavaScriptDialog',
        { accept: false }, ev.sessionId);
      throw new Error(`dialog mismatch: wanted ${type} ~ "${contains}", ` +
                      `got ${ev.type}: "${ev.message}"`);
    }
    const params = { accept };
    if (text !== undefined) params.promptText = text;
    await this.cdp.send('Page.handleJavaScriptDialog', params, ev.sessionId);
    return ev;
  }
  assertQuiet() {
    if (this.arrived.length) {
      throw new Error('unexpected dialogs: ' +
        this.arrived.map((d) => `${d.type}: "${d.message}"`).join('; '));
    }
  }
}

module.exports = { Cdp, DialogDirector };
