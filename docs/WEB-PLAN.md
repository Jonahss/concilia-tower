# ConcilliaTower on the Web — stage plan (drafted 2026-08-09)

Goal: the faithful port running in a browser tab, shareable by URL, with
sound (which VNC never carried — this stage is the first time anyone
hears the tower). Strategy: **Emscripten/WASM of the existing C+SDL2
codebase** — the sim stays the single source of byte-verified mechanics
truth. A TypeScript rewrite was considered and rejected for this stage:
it would re-open every referee verdict. If web-native code is ever
wanted, the shape is engine/shell (C sim as wasm engine, TS shell for
UI), not a rewrite.

Feasibility notes (2026-08-09 code scan): single-threaded, no
threads/fork/exec; all file I/O is plain stdio (MEMFS-compatible);
SDL2 + SDL2_ttf both have first-class Emscripten ports. The only
native assumptions are system font paths and /tmp screenshot paths.

## Phase A — build & main loop (make it run)
- [ ] A1. Emscripten toolchain decision: build in GitHub Actions
      (emsdk on ubuntu-latest; the Pi never needs emsdk). Optional
      local emsdk for dev iteration if wanted.
- [ ] A2. `make web` target: emcc with `-sUSE_SDL=2 -sUSE_SDL_TTF=2
      -sALLOW_MEMORY_GROWTH -O2`, custom shell file.
- [ ] A3. Main-loop conversion: the blocking SDL loop becomes an
      `emscripten_set_main_loop` callback (fixed-timestep 15Hz sim /
      60fps render interpolation already exists — maps cleanly onto
      requestAnimationFrame).
- [ ] A4. Bundle a font: system TTF paths (main.c:8923-8935) don't
      exist in wasm. Embed Liberation Sans (SIL OFL — bundleable) in
      the wasm FS; keep native paths for the native build.
- [ ] A5. Audio unlock: browsers require a user gesture before audio —
      the landing page's "start" click doubles as the unlock.
- [ ] A6. Guard native-only affordances behind `#ifndef __EMSCRIPTEN__`:
      /tmp screenshot paths (F12 → browser download instead), demo/env
      hooks, Xvfb-era assumptions.

## Phase B — asset flow (bring your own EXE)
- [x] B1. (2026-08-10) Landing page: drag-drop / file-input for the user's own
      SIMTOWER.EXE. Clear copy: "your file is parsed in your browser
      and never uploaded — there is no server."
- [x] B2. (2026-08-10, policy: warn-never-block; recruiting-poster copy still queued) Hash check (SHA-256) against OUR byte-verified build before
      accepting. Policy decision needed (open question Q3): hard-reject
      other builds vs warn-and-attempt. (tower-together's +8 segment
      mystery proves multiple builds exist; all our mechanics were
      verified against ours.)
- [x] B3. (2026-08-10) Write accepted EXE into MEMFS; boot the game with that path
      (existing argv plumbing unchanged).
- [x] B4. (2026-08-10) Persist the EXE in IndexedDB so return visits skip the
      upload; add a "forget my copy" button.
- [x] B5. (2026-08-10, v0: styling/scaling polish continues) Custom HTML shell: 960×720 canvas, integer/CSS scaling,
      page styling + instructions. (The deferred custom-intro/splash
      slot naturally lives here later — queued.)

## Phase C — saves & persistence
- [ ] C1. Mount saves on IDBFS; FS.syncfs after every game_save (F5 /
      quit-prompt path) and on load.
- [ ] C2. Import/export `.sav` as browser file download/upload.
- [ ] C3. `.TDT` export (twr_export, F6) → browser download. A browser
      tower openable in real 1994 SimTower under DOSBox is a launch
      demo that sells itself.
- [ ] C4. `.TDT` IMPORT via upload too (twr_load exists — original
      1995/96 towers playable in the browser). Decided 2026-08-09.
- [ ] C5. Web autosave: YES (Jonah 2026-08-09) — periodic IDBFS
      autosave, flagged as web-port behavior.

## Phase D — input & UX adaptation
- [x] D1. (2026-08-10) Keyboard capture: Ctrl/Cmd+S aliases F5 in the
      engine; the shell preventDefaults F5 + Ctrl/Cmd+S (capture phase,
      only while the game is up). Menu save unchanged.
- [ ] D2. Context-menu suppression on canvas (right-click).
- [ ] D3. Scaling/fullscreen: CSS integer scale + a fullscreen button.
- [ ] D4. Background-tab policy: browsers throttle rAF when hidden —
      decide pause vs catch-up (open question Q5).
- [ ] D5. In-game version stamp (git short hash) for bug reports.

## Phase E — hosting, CI, launch hygiene
- [ ] E1. GitHub Actions: emsdk build → deploy to GitHub Pages on push
      (or tag). Artifact = single .html + .js + .wasm (+ font).
- [ ] E2. Public-repo hygiene: README for the web version, screenshots,
      "no game assets included / bring your own EXE" disclaimer.
- [ ] E3. Code license decision before the repo goes public (Q1).
- [ ] E4. Cross-browser pass: Chrome, Firefox, Safari (audio unlock and
      IndexedDB quirks differ; Safari is the usual troublemaker).
- [ ] E5. Error handling: wasm abort → friendly "please report" page,
      not a frozen canvas.
- [ ] E6. Performance sanity pass (per-frame TTF text rendering is the
      one thing to watch; native has headroom, wasm should too).

## Queue — later ideas (explicitly deferred, not this stage)
- Shareable towers: export/import as file first; maybe compressed-in-URL
  links later.
- Touch/tablet controls.
- Custom ConcilliaTower intro/splash (the long-deferred slot — its
  natural home is the web landing page).
- itch.io mirror at launch (discoverability, comments).
- Engine/shell TS evolution (only if outside contributors want a
  web-native UI; the C sim remains the mechanics truth).
- Mods track (feature-flag menu, F3→F7 graphs, multi-tower bridges,
  window washers, destination dispatch, clairvoyant elevators...) —
  more valuable once the game is shareable.
- Errata issue to phulin/tower-together — Jonah sends at launch
  (draft ready in the decomp repo).
- OpenSkyscraper errata publication (standing practice doc, same
  launch window).
- No-EXE preview mode for the landing page (screenshots/video only —
  nothing bundled, keeps it legal).
- Analytics/hit counter: Jonah's call, privacy-light if at all.
- Decomp repo to GitHub once Jonah creates it (backup currently on
  sexica).

## Decisions (Jonah, 2026-08-09 evening)
- D1. License: **GPL** — keeps mods open.
- D2. Hosting: **kvetch.io** (Jonah's domain, currently idle). Game at
  a path (e.g. kvetch.io/tower); **root gets a small portfolio site**
  linking to his other things (new scope item, see E7).
- D3. Wrong-EXE policy: reject with a **contributor call-to-action** —
  "here's the prompt/recipe to decompile your build and send a PR
  adding it to the supported list." Hash-check against known builds;
  the supported-builds table is designed to grow via PRs.
- D4. Web autosave: yes (C5).
- D5. Hidden tab: **mute sounds, keep the sim running** — the game has
  a pause button for pausing.
- D6. Launch: **quiet URL first**, big launch later. Launch channels to
  hit when it's time (Jonah's list): his blog, r/tycoon, r/SimTower,
  Hacker News, Twitter. (Same window: errata send to tower-together +
  OpenSkyscraper errata publication.)

## Added scope from decisions
- [ ] E7. Portfolio stub at kvetch.io root (links out; the tower lives
      at a path). Keep minimal — its own mini-project, Jonah's content.
- [ ] E8. DNS/hosting wiring for kvetch.io → Pages (CNAME) or wherever
      the static bundle lands.
- [ ] B6. Supported-builds registry: hashes + per-build notes, PR-able;
      the wrong-EXE screen links the decompile-and-contribute recipe.

## Beta testers (quiet-launch list, Jonah 2026-08-09)
nisan, cousin Ben, Zack, Sarah, LR, crash, Noam, Wen

## Phase F — analytics (added by Jonah 2026-08-09)
- [ ] F1. Anonymous aggregate telemetry: visitors, total days simulated,
      star-ups counted per level (how many towers hit 1★, 2★, ... TOWER),
      total/peak population, "things like that."
- [ ] F2. Needs the stage's ONE backend exception (static hosting has no
      server): a tiny counter endpoint (Cloudflare Worker + KV or
      similar) or a privacy-light service (Plausible) with custom
      events. Fire-and-forget beacons: session start, day milestones,
      star-up(level), pop milestones.
- [ ] F3. Privacy stance stays loud: counters only — no tower data, no
      files, and the EXE-never-leaves-your-browser promise is untouched.
- [ ] F4. Fun option: a public "community stats" page on kvetch.io
      (all-time days simulated, TOWER count) — nice launch-day flex.

## Community (added 2026-08-09)
- [ ] G1. ConcilliaTower Discord server for the community — tower-file
      sharing channel (.TDT/.sav uploads play in browser OR real 1994
      SimTower), bug reports, build-registry contributions. Spin up
      before the big launch; link from kvetch.io and the README.
      (Pairs with F4's community stats + the queued shareable-tower
      links.)
- [ ] C6. Save MANAGER on the splash page (Jonah 2026-08-09): named
      save slots in IndexedDB with metadata pulled from the header
      (tower name, day, stars, money — maybe a minimap thumbnail),
      load/rename/duplicate/delete, plus the import/export buttons
      (C2-C4). The splash menu = load screen; autosave (C5) gets its
      own visible slot.
