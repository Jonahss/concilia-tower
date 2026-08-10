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
- [ ] B1. Landing page: drag-drop / file-input for the user's own
      SIMTOWER.EXE. Clear copy: "your file is parsed in your browser
      and never uploaded — there is no server."
- [ ] B2. Hash check (SHA-256) against OUR byte-verified build before
      accepting. Policy decision needed (open question Q3): hard-reject
      other builds vs warn-and-attempt. (tower-together's +8 segment
      mystery proves multiple builds exist; all our mechanics were
      verified against ours.)
- [ ] B3. Write accepted EXE into MEMFS; boot the game with that path
      (existing argv plumbing unchanged).
- [ ] B4. Persist the EXE in IndexedDB so return visits skip the
      upload; add a "forget my copy" button.
- [ ] B5. Custom HTML shell: 960×720 canvas, integer/CSS scaling,
      page styling + instructions. (The deferred custom-intro/splash
      slot naturally lives here later — queued.)

## Phase C — saves & persistence
- [ ] C1. Mount saves on IDBFS; FS.syncfs after every game_save (F5 /
      quit-prompt path) and on load.
- [ ] C2. Import/export `.sav` as browser file download/upload.
- [ ] C3. `.TDT` export (twr_export, F6) → browser download. A browser
      tower openable in real 1994 SimTower under DOSBox is a launch
      demo that sells itself.
- [ ] C4. Decide: optional periodic autosave on web (a web nicety the
      native port doesn't have — flag as port-web behavior if added).
      (Open question Q4.)

## Phase D — input & UX adaptation
- [ ] D1. Keyboard capture: browsers steal F-keys — **F5 is
      "reload page" and our SAVE key**. preventDefault on the canvas +
      keep the menu alternatives (already built for VNC). Possibly
      remap saves to Ctrl+S as an additional binding.
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

## Open questions for Jonah (planning)
- Q1. Code license when the repo goes public? (MIT/GPL/other — affects
  contributions and the tower-together exchange.)
- Q2. Hosting home: `jonahss.github.io/concilia-tower` via Pages, a
  custom domain, or both? Same repo or a separate site repo?
- Q3. Wrong-EXE policy: hard reject, or warn-and-attempt with a
  "mechanics verified against build X only" banner?
- Q4. Web autosave: add it (worldly) or keep native parity (pure)?
- Q5. Hidden-tab behavior: pause the sim, or let it throttle and
  catch up?
- Q6. Launch shape: quiet URL for friends first, then the full launch
  (errata send, OS errata, announcements) — or one big bang?
