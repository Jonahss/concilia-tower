---
name: verify
description: Drive the built simtower binary headless (Xvfb + xdotool) and capture screenshots/stdout to verify sim or render changes at the real surface.
---

# Verifying simtower-linux changes

The surface is the SDL app. `make` at repo root builds `./simtower`.
Assets come from SIMTOWER.EXE at runtime:
`EXE=~/.claude-agent/archive/openclaw/workspace/projects/OpenSkyscraper/data/SIMTOWER.EXE`

## Launch headless

```bash
pgrep -f "Xvfb :99" || { setsid Xvfb :99 -screen 0 960x720x24 & sleep 1; }
DISPLAY=:99 CT_TWR=path/to/save.tdt setsid ./simtower $EXE > run.log 2>&1 &
```

- `CT_TWR=path` loads a .TDT/.TWR wholesale (fixtures in tests/fixtures/:
  BARKLE4D 4⭐ hotels, SCHMITT 5⭐, THEECSTA TOWER).
- `--screenshot out.bmp` = one-shot mode: sim runs SHOT_FRAME frames
  (default 200), captures, exits. Camera stays at ground level — for
  anything above ~floor 8, run interactively and scroll instead.

## Drive & capture

```bash
DISPLAY=:99 xdotool key Up          # scroll 16px/press (Down/Left/Right; PgUp/PgDn page)
DISPLAY=:99 xdotool key F12         # screenshot -> /tmp/simtower_screenshot.bmp
ffmpeg -y -i /tmp/simtower_screenshot.bmp out.png
```

- Map overlay tabs (minimap, top-left): Map/Eval/Rent/Hotel at y≈289,
  Hotel ≈ x=171. `xdotool mousemove X Y click 1`.
- stdout is a real event log (day rollovers, 🪳 infestation, promotions,
  TWR import summary) — capture it, it's evidence.

## Gotchas

- **Speed**: ~1 game day per ~4 min on the Pi under Xvfb with a big
  imported tower (render-bound; SDL_Delay(16) assumes 60fps it doesn't
  get). Multi-day observations = several wall-clock minutes each.
- **State crafting**: hex-stamping fixture .TDTs beats driving the UI.
  Floor map starts at file offset 0x230: per floor `u16 n; skip 4;`
  then n×18-byte tenant records (`t[4]`=type: 3/4/5 hotel single/twin/
  suite; `t[5]`=condition byte: 0x18 clean, 0x28 dirty, 0x38 infested — the importer reads t[5], NOT t[0x0b]),
  then `skip 0xbc`.
- **`concilliatower-vnc.service` (systemd, Restart=always) owns a simtower
  on :99** — pkill it and systemd respawns a plain instance (no CT_ env)
  that steals your captures. `sudo systemctl stop concilliatower-vnc`
  before a verification session, `sudo systemctl restart` after (stopping
  it also tears down Xvfb :99 — bring your own). And **use `pkill -x
  simtower`**, not `-f "simtower.*"` — the -f pattern matches your own
  shell and kills it (exit 144).
- **The disaster modal eats F12** (it's fully modal). Screenshot it from
  outside: `ffmpeg -f x11grab -video_size 960x720 -i :99 -frames:v 1 out.png`.
  Buttons at y≈413: free path x≈392, paid path x≈566. Keys: d/Enter = free,
  p/y = pay.
- **xdotool key events** leave a build tool armed and can spam
  harmless `[reject] Office at ...` placement lines in the log —
  ignore them (or investigate someday).
- SDL-free sim checks live in tests/test_sim.c (gcc line in its header)
  — those are CI's job, not verification; use them only as specs.
