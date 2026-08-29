/* kvetch.io front door — Cloudflare Worker.
 *
 * Serves Jonah's idle domain now that its zone lives on Cloudflare:
 *   /                → minimal holding page (E7 portfolio stub is
 *                      Jonah's content, later — this just isn't a 404)
 *   /conciliatower/* → reverse proxy to the game on GitHub Pages
 *                      (jonahss.github.io/concilia-tower — CI unchanged)
 *   www.kvetch.io    → 301 to the apex
 *
 * Deployed by scripts/deploy-roster.sh's sibling flow (see repo notes);
 * attached to the zone as a custom domain / route for kvetch.io.
 */

const GAME_ORIGIN = 'https://jonahss.github.io/concilia-tower';

const STUB = `<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>kvetch.io</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; min-height: 100vh; display: grid; place-items: center;
         background: #10131a; color: #cfd6e4;
         font: 17px/1.6 Georgia, 'Times New Roman', serif; }
  main { text-align: center; padding: 40px 24px; }
  h1 { font-size: 26px; letter-spacing: .04em; color: #eef2fa; margin: 0 0 6px; }
  p { color: #7d879c; margin: 0 0 28px; }
  a { color: #7fa7dd; text-decoration: none; }
  a:hover { text-decoration: underline; }
  ul { list-style: none; padding: 0; margin: 0; display: grid; gap: 14px; }
  .news { margin-top: 44px; }
  .news p { margin-bottom: 12px; font-size: 15px; }
  .news form { display: flex; gap: 8px; justify-content: center; flex-wrap: wrap; }
  .news input { font: 15px/1.4 system-ui, sans-serif; padding: 9px 12px;
    min-width: 15rem; background: #161a24; color: #eef2fa;
    border: 1px solid #2a3040; border-radius: 6px; }
  .news button { font: 600 15px/1.4 system-ui, sans-serif; padding: 9px 18px;
    background: #7fa7dd; color: #10131a; border: 0; border-radius: 6px;
    cursor: pointer; }
  .news button:hover { background: #9dbde8; }
</style></head><body><main>
  <h1>kvetch.io</h1>
  <p>Jonah's corner of the internet — proper site coming.</p>
  <ul>
    <li><a href="/conciliatower/">🏢 ConciliaTower — SimTower, faithfully, in your browser</a></li>
    <li><a href="https://www.wyldcard.io">🃏 Wyldcard — E-Ink Trading Cards</a></li>
    <li><a href="https://github.com/Jonahss">GitHub</a></li>
  </ul>
  <div class="news">
    <p>Occasional emails when something new happens — same list as
       <a href="https://www.wyldcard.io">Wyldcard</a>.</p>
    <form action="https://buttondown.com/api/emails/embed-subscribe/wyldcard"
          method="post">
      <input type="email" name="email" placeholder="you@example.com"
             aria-label="Email address" required>
      <button type="submit">Sign up</button>
    </form>
  </div>
</main></body></html>`;

export default {
  async fetch(req) {
    const url = new URL(req.url);
    if (url.hostname.startsWith('www.'))
      return Response.redirect(
        'https://kvetch.io' + url.pathname + url.search, 301);
    if (url.pathname === '/conciliatower')
      return Response.redirect('https://kvetch.io/conciliatower/', 301);
    if (url.pathname.startsWith('/conciliatower/')) {
      const rest = url.pathname.slice('/conciliatower/'.length);
      const upstream = await fetch(GAME_ORIGIN + '/' + rest + url.search, {
        cf: { cacheTtl: 300, cacheEverything: true },
      });
      /* Re-head the response so CF is free to cache/stream it. */
      return new Response(upstream.body, {
        status: upstream.status,
        headers: upstream.headers,
      });
    }
    if (url.pathname === '/')
      return new Response(STUB,
        { headers: { 'Content-Type': 'text/html; charset=utf-8' } });
    return new Response('Not found on kvetch.io (yet)', { status: 404 });
  },
};
