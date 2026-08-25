/* ConciliaTower roster — Cloudflare Worker + KV.
 *
 * The game (web/shell.html ctRoster) POSTs partial tower state here;
 * each tower is one KV record keyed t:<playerId>/<slotName>. The full
 * record also rides in KV metadata so GET / lists everything with a
 * single list() call (no per-key reads).
 *
 * Deployed by scripts/deploy-roster.sh (plain API upload, no wrangler).
 * Dashboard: https://conciliatower-roster.jonahss.workers.dev/
 */

const ALLOWED_ORIGINS = [
  'https://jonahss.github.io',
  'https://kvetch.io',
  'https://www.kvetch.io',
];

const RECORD_FIELDS = {
  n: { type: 'string', max: 24 },   // tower name
  d: { type: 'number', max: 100000 }, // in-game day
  p: { type: 'number', max: 100000 }, // population
  s: { type: 'number', max: 6 },      // star rating (6 = TOWER)
  m: { type: 'number', max: 4e9 },    // funds
};

function corsHeaders(req) {
  const origin = req.headers.get('Origin') || '';
  const allow = ALLOWED_ORIGINS.includes(origin) ? origin : ALLOWED_ORIGINS[0];
  return {
    'Access-Control-Allow-Origin': allow,
    'Access-Control-Allow-Methods': 'POST, GET, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
  };
}

function cleanReport(body) {
  const out = {};
  for (const [k, spec] of Object.entries(RECORD_FIELDS)) {
    const v = body[k];
    if (v === undefined || v === null) continue;
    if (spec.type === 'string') {
      if (typeof v !== 'string') return null;
      out[k] = v.slice(0, spec.max);
    } else {
      const num = Number(v);
      if (!Number.isFinite(num) || num < 0) return null;
      out[k] = Math.min(Math.round(num), spec.max);
    }
  }
  return out;
}

async function handleReport(req, env) {
  if ((req.headers.get('Content-Length') | 0) > 1024)
    return new Response('too big', { status: 413 });
  let body;
  try { body = await req.json(); } catch (e) {
    return new Response('bad json', { status: 400 });
  }
  const key = body.k;
  if (typeof key !== 'string' ||
      !/^[a-z0-9]{4,12}\/[A-Za-z0-9][A-Za-z0-9 _.-]{0,23}$/.test(key))
    return new Response('bad key', { status: 400 });
  const patch = cleanReport(body);
  if (!patch) return new Response('bad fields', { status: 400 });

  const kvKey = 't:' + key;
  const prev = (await env.TOWERS.get(kvKey, 'json')) || {};
  const now = Date.now();
  const rec = {
    ...prev,
    ...patch,
    pk: Math.max(prev.pk || 0, patch.p || 0, prev.p || 0),
    f: prev.f || now,
    l: now,
    c: (prev.c || 0) + 1,
  };
  await env.TOWERS.put(kvKey, JSON.stringify(rec), { metadata: rec });
  return new Response('ok', { status: 200 });
}

/* ---- Event counters (replaced GoatCounter, 2026-08-24) ----
 * One KV key per event name per UTC day (e:<date>:<name>); the count
 * rides in metadata so the dashboard aggregates from list() alone.
 * get+put isn't atomic — two simultaneous hits can drop a count; at
 * this scale that's an acceptable trade for staying on plain KV. */
const EVENT_RE = /^[a-z0-9-]{1,24}(\/d\d{1,5})?$/i;

async function handleEvent(req, env) {
  if ((req.headers.get('Content-Length') | 0) > 1024)
    return new Response('too big', { status: 413 });
  let body;
  try { body = await req.json(); } catch (e) {
    return new Response('bad json', { status: 400 });
  }
  const name = body.e;
  if (typeof name !== 'string' || !EVENT_RE.test(name))
    return new Response('bad event', { status: 400 });
  const day = new Date().toISOString().slice(0, 10);
  const kvKey = 'e:' + day + ':' + name.toLowerCase();
  const c = (((await env.TOWERS.get(kvKey, 'json')) | 0)) + 1;
  await env.TOWERS.put(kvKey, JSON.stringify(c), { metadata: { c } });
  return new Response('ok', { status: 200 });
}

/* Aggregate counters grouped on the base name (before any '/dNN' day
 * tag the star/pop funnel events carry): [name, {total, today}]. */
async function listEvents(env) {
  const today = new Date().toISOString().slice(0, 10);
  const agg = new Map();
  let cursor;
  do {
    const page = await env.TOWERS.list({ prefix: 'e:', cursor });
    for (const k of page.keys) {
      const m = /^e:(\d{4}-\d{2}-\d{2}):([^/]+)/.exec(k.name);
      if (!m) continue;
      const c = (k.metadata && k.metadata.c) | 0;
      const a = agg.get(m[2]) || { total: 0, today: 0 };
      a.total += c;
      if (m[1] === today) a.today += c;
      agg.set(m[2], a);
    }
    cursor = page.list_complete ? undefined : page.cursor;
  } while (cursor);
  return [...agg.entries()].sort((x, y) => y[1].total - x[1].total);
}

async function listTowers(env) {
  const towers = [];
  let cursor;
  do {
    const page = await env.TOWERS.list({ prefix: 't:', cursor });
    for (const k of page.keys)
      towers.push({ key: k.name.slice(2), ...(k.metadata || {}) });
    cursor = page.list_complete ? undefined : page.cursor;
  } while (cursor);
  towers.sort((a, b) => (b.l || 0) - (a.l || 0));
  return towers;
}

function fmtAge(ms) {
  if (!ms) return '—';
  const s = (Date.now() - ms) / 1000;
  if (s < 90) return 'just now';
  if (s < 5400) return Math.round(s / 60) + ' min ago';
  if (s < 129600) return Math.round(s / 3600) + ' h ago';
  return Math.round(s / 86400) + ' d ago';
}
function fmtMoney(n) {
  if (n === undefined) return '—';
  return '$' + String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}
function stars(s) {
  if (s === undefined) return '—';
  if (s >= 6) return 'TOWER';
  return '★'.repeat(s) || '☆';
}
function esc(t) {
  return String(t).replace(/[&<>"]/g,
    (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

function rosterPage(towers, events) {
  const rows = towers.map((t) => `
    <tr>
      <td class="nm">${esc(t.n || t.key.split('/')[1] || '?')}</td>
      <td class="pid">${esc(t.key.split('/')[0])}</td>
      <td class="star${(t.s || 0) >= 6 ? ' tower' : ''}">${stars(t.s)}</td>
      <td class="num">${t.p === undefined ? '—' : t.p.toLocaleString('en-US')}</td>
      <td class="num">${t.pk ? t.pk.toLocaleString('en-US') : '—'}</td>
      <td class="num">${t.d === undefined ? '—' : t.d}</td>
      <td class="num">${fmtMoney(t.m)}</td>
      <td class="age">${fmtAge(t.l)}</td>
      <td class="age">${fmtAge(t.f)}</td>
    </tr>`).join('');
  return `<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ConciliaTower Roster</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; padding: 24px; background: #10131a; color: #cfd6e4;
         font: 15px/1.5 system-ui, sans-serif; }
  h1 { font-size: 20px; margin: 0 0 4px; color: #eef2fa; }
  h2 { font-size: 16px; margin: 28px 0 4px; color: #eef2fa;
       font-weight: 600; }
  table.counters { width: auto; min-width: 320px; }
  .sub { color: #7d879c; font-size: 13px; margin-bottom: 20px; }
  .wrap { max-width: 1000px; margin: 0 auto; }
  .scroll { overflow-x: auto; }
  table { border-collapse: collapse; width: 100%; white-space: nowrap; }
  th, td { padding: 8px 12px; text-align: left; }
  th { color: #7d879c; font-size: 12px; text-transform: uppercase;
       letter-spacing: .06em; border-bottom: 1px solid #2a3040; }
  tr:nth-child(even) td { background: #161a24; }
  td.nm { color: #eef2fa; font-weight: 600; }
  td.pid { color: #59627a; font-family: ui-monospace, monospace; font-size: 12px; }
  td.star { color: #e8c35a; letter-spacing: 1px; }
  td.star.tower { color: #7fd4a3; font-weight: 700; }
  td.num, td.age { font-variant-numeric: tabular-nums; }
  td.age { color: #7d879c; font-size: 13px; }
  .empty { padding: 40px; text-align: center; color: #59627a; }
  a { color: #6ea8ff; }
  footer { margin-top: 20px; color: #59627a; font-size: 12px; }
</style></head><body><div class="wrap">
<h1>🏢 ConciliaTower Roster</h1>
<div class="sub">${towers.length} tower${towers.length === 1 ? '' : 's'} reporting
 · updates when a tower is opened and at each in-game dawn
 · <a href="/towers.json">json</a></div>
<div class="scroll"><table>
<thead><tr><th>Tower</th><th>Player</th><th>Rating</th><th>Pop</th>
<th>Peak</th><th>Day</th><th>Funds</th><th>Last seen</th><th>First seen</th></tr></thead>
<tbody>${rows || ''}</tbody></table></div>
${rows ? '' : '<div class="empty">No towers yet — the roster fills as people play.</div>'}
<h2>Counters</h2>
<div class="sub">everything the game reports · totals since 2026-08-24
 · <a href="/events.json">json</a></div>
<div class="scroll"><table class="counters">
<thead><tr><th>Event</th><th>Today</th><th>All time</th></tr></thead>
<tbody>${events.map(([n, a]) => `
    <tr><td class="nm">${esc(n)}</td>
    <td class="num">${a.today.toLocaleString('en-US')}</td>
    <td class="num">${a.total.toLocaleString('en-US')}</td></tr>`).join('')}
</tbody></table></div>
${events.length ? '' : '<div class="empty">No events counted yet.</div>'}
<footer>ConciliaTower · anonymous per-tower telemetry · no accounts, no cookies</footer>
</div></body></html>`;
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (req.method === 'OPTIONS')
      return new Response(null, { status: 204, headers: corsHeaders(req) });
    if (req.method === 'POST' &&
        (url.pathname === '/report' || url.pathname === '/event')) {
      const res = url.pathname === '/report'
        ? await handleReport(req, env)
        : await handleEvent(req, env);
      for (const [k, v] of Object.entries(corsHeaders(req)))
        res.headers.set(k, v);
      return res;
    }
    if (req.method === 'GET' && url.pathname === '/towers.json') {
      return new Response(JSON.stringify(await listTowers(env), null, 1), {
        headers: { 'Content-Type': 'application/json', ...corsHeaders(req) },
      });
    }
    if (req.method === 'GET' && url.pathname === '/events.json') {
      return new Response(
        JSON.stringify(Object.fromEntries(await listEvents(env)), null, 1), {
        headers: { 'Content-Type': 'application/json', ...corsHeaders(req) },
      });
    }
    if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '')) {
      const [towers, events] =
        await Promise.all([listTowers(env), listEvents(env)]);
      return new Response(rosterPage(towers, events), {
        headers: { 'Content-Type': 'text/html; charset=utf-8' },
      });
    }
    return new Response('not found', { status: 404 });
  },
};
