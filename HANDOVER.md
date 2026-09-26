# SLASH'EM 0.0.7E7F3 — handover

## RVIP progress

### Stage 1 (get + build) — done 2026-09-26
- Folder `~/Games/slashem`. Source: SourceForge tarball `se007e7f3.tar.gz`
  (sha256 `3b55b7fa6f4a8b703382cdcfb0af6dd25e65fb55a723298b02624af566e36df9`,
  https://sourceforge.net/projects/slashem/files/slashem-source/0.0.7E7F3/),
  upstream commit `ab6287b`. No git mirror with the same tree was used.
- Case **O**, NetHack 3.4.3 family: same layout as nethack50 (`sys/unix/setup.sh`
  copies Makefiles, `util/makedefs` generates headers). Closest relative:
  `~/Games/nethack50` (window port + web harness copied from there).
- **Web frontend:** `win/web/winweb.c` (window port `web_procs`, Emscripten
  only), page `web/index.html` + `web/slashem.js` (copy of nethack50's
  `nethack.js`: draws only), `web/build.sh`, `web/mktiles.py`.
  Wiring: `include/config.h` (`WEB_GRAPHICS`, `DEFAULT_WINDOW_SYS "web"`,
  no `TTY_GRAPHICS`/`COMPRESS` under `__EMSCRIPTEN__`), `include/global.h`
  (`USE_TILES`), `include/unixconf.h` (`NO_FILE_LINKS`, `LOCKDIR "/slashem"`),
  `src/windows.c`, `src/rip.c` (`TEXT_TOMBSTONE`).
- **Native build (tty, needed for the playground data and generated headers):**
  ```
  sh sys/unix/setup.sh
  make CFLAGS="-O -I../include -w -Wno-error -Wno-implicit-function-declaration -Wno-implicit-int -Wno-incompatible-function-pointer-types -Wno-int-conversion -Wno-return-mismatch" \
       WINTTYLIB=-lncurses GAMEUID=$(id -un) GAMEGRP=staff \
       GAMEDIR=$PWD/playground PREFIX=$PWD/install SHELLDIR=$PWD/install/bin all install
  ```
  **No `-j`:** util's yacc rules race on `y.tab.c` (dgn_comp/lev_comp).
  `nroff`/`tbl` missing (Guidebook) and `rmdir ./-p` errors are harmless.
  Run: `cd playground && ./slashem -d $PWD -u <name>` (`TERM=vt100`).
  Objects cleaned (`make clean` in src); playground kept (build.sh needs it).
- **Web build:** `sh web/build.sh` → `web/dist` (flags in the script: emcc
  `-O2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=131072 -sSTACK_SIZE=2097152
  -sALLOW_MEMORY_GROWTH -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=64MB
  -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,ENV,HEAP32 -sFORCE_FILESYSTEM -lidbfs.js
  -sENVIRONMENT=web`, plus `-Wno-implicit-int -Wno-implicit-function-declaration
  -Wno-int-conversion -Wno-incompatible-function-pointer-types
  -Wno-return-mismatch` for the K&R code). Sources: all `src/*.c` but
  `borg.c`, `sys/share/{ioctl,unixtty}.c`, `sys/unix/{unixmain,unixres,unixunix}.c`,
  `win/web/winweb.c`. `util/tilemap` writes `src/tile.c` (glyph2tile, 1404 tiles,
  `substitute_tiles()` for mines/hell/knox/sokoban walls).
  Test locally: `python3 -m http.server <port> -d web/dist`.
- **Quirk, data files:** `.lev`/`dungeon` headers hold `unsigned long`s (8 bytes
  natively, 4 in wasm32) → "Configuration incompatibility". build.sh compiles
  `lev_comp`/`dgn_comp` with emcc (`-sNODERAWFS -sENVIRONMENT=node`) into
  `web/tools/` and runs them with node in the seed dir (`dat/*.des`,
  `dat/dungeon.pdf`). Same trap for every 3.4.3-family game.
- Playground data preloaded at `/seed`, copied into the IDBFS mount
  `/slashem` on every load (`slashem.js` preRun); `-d /slashem -u <name>`
  (name from `save/0<name>` or a `0<name>.0` lock).
- **ASan** (native tty, `-fsanitize=address`, random keys + play/save/restore/save):
  found `nul[40]` written as `sizeof(struct fruit)` = 48 on 64-bit (save.c;
  fixed: `nul[64]`, commit d9303e0). Other port fixes: `tparm()` redeclaration
  (termcap.c), `int` passed as `short *` (potion.c), null cast to a nonexistent
  struct tag (lev_comp.y). ASan binary and objects removed.
- Window port notes: no `--More--` at all (messages accumulate in the
  Messages window = auto_more); `perm_invent` on; item colours via
  `mapglyph()`; the persistent inventory refresh is deferred while
  `restoring` (else "(null) wand"); `#` + empty line = menu of all extended
  commands; SLASH'EM's own **Enter = Main Menu** (g/i/a/p/d/?) already exists
  (stage 3 can build on it); keys: 0x101.. arrows, `0x10000|y<<8|x` map click,
  `0x20000|row` pop-up click (as nethack50).
- Tested in the browser pane: name → "Shall I pick" → role/race/gender/align
  menus → map with tiles, Messages, Status, Inventory (tiles + colours) →
  moves, pet → `i` pop-up → `?` help + text pages → `#` menu → zoom → `S`
  save → reload restores → 300 random keys alive, no console errors.
- Browser-pane key quirks: `shift+s` arrives as `s`, `?`/`#` don't arrive —
  dispatch `KeyboardEvent('keydown', {key})` from JS for those.
- **Open (later stages):** no autosave/checkpoint recovery yet: a closed tab
  mid-game leaves `0<name>.0` locks and the next start asks "Destroy old
  game?" (answer y). Stage 5: checkpoint via `save_currentstate()` when idle
  and port `util/recover.c`'s logic into `getlock()` (no SELF_RECOVER in
  3.4.3). `help.html` not built yet (Help button shows the load error).
- Suggested RVIP.md additions (rvip-tools is read-only for this session):
  the wasm32 `long`-size data-file trap + node-run compilers; no `-j` with
  yacc rules; `nul[]` too small for `struct fruit` on 64-bit (3.4.3 family).
- Next: stage 2 (explore + stairs), like nethack50 (`rvip_*` in `src/hack.c`,
  main-loop hook in `src/allmain.c`, `<`/`>` in `src/do.c`).

### Stage 2 (explore + stairs) — done 2026-09-26
- Explore key `~` = `#autoexplore` (`src/cmd.c`: `cmdlist` entry replaces the
  duplicate `` ` ``/`~` → `domenusystem` binding; `extcmdlist` entry after
  "adjust"). Help lines in `dat/hh` (`?` → b) and `dat/cmdhelp` (copied into
  `playground/` for the web seed).
- Code: end of `src/hack.c` (`rvip_goal`, `rvip_step`, `rvip_hostile_in_view`,
  `rvip_start`, `rvip_continue`, `doexplore`), externs in `include/extern.h`.
  `src/lock.c`: `doopen()` split into `doopen()` + `doopen_indir(x,y)` (3.4.3
  vanilla has no autoopen); explore calls `doopen_indir` on a closed door.
- Main-loop hook: `src/allmain.c` `moveloop()`, `multi == 0` branch:
  `if (!rvip_continue()) rhack((char *)0);` — one BFS step per idle turn.
- `<`/`>`: `src/do.c` at the top of `dodown()`/`doup()`: not on matching
  stairs → `rvip_start('>'/'<')` walks to the nearest known one and calls
  `dodown()`/`doup()` on arrival (3.4.3 stairs = `xupstair/yupstair`,
  `xdnstair/ydnstair`, `xupladder/yupladder`, `xdnladder/ydnladder`, `sstairs`).
- "Known grid" test: `levl[x][y].seenv` (frontier = seenv cell with a
  seenv==0 neighbour; stairs must have seenv). Steps: `test_move(x,y,dx,dy,
  TEST_TRAV)` (closed doors pass orthogonally), skipping `is_pool`/`is_lava`,
  seen traps, boulders (a stuck one stopped explore for good), visible
  non-tame monsters, and doors that answered "locked" (per-level table
  `locked[][]` in `rvip_step`, reset when `u.uz` changes).
- Stops: hostile in view, `toplines` changed (the web port now copies every
  message into `toplines` in `add_msg()` — vanilla only tty does), a key
  (`rvip_keyhit`, set in `web_get_nh_event()` when `js_key(1,0)` reports a
  waiting key; it also `nomul(0)`s runs), `u.uinwater`, `multi`, failed move.
- Tested in the browser pane: `~` mapped Dlvl 1 (5 rooms, opened 4 doors,
  stopped on rabbit/lichen/grid bug, on "You hear…" messages, skipped the
  stuck boulder and the locked door after one try, ended with "Nothing left
  to explore here."); `<` walked 13 cells to the up stairs → "Still climb?";
  `>` walked to the down stairs and descended (Dlvl:2); a queued key stopped
  explore after one step; help shows the new lines; no console errors.
- Open: any message stops explore ("You hear…", "You displaced your
  kitten", hunger), so long explores need several presses; the fold
  "(xN)" hides identical repeats from the toplines test. Walk segments
  shorter than 50 ms never yield to the browser, so a human key can only
  interrupt longer walks. No autosave yet (stage 1 note).
- Next: stage 3 (Enter menu + inventory item menus) in `win/web/winweb.c`,
  building on SLASH'EM's own Enter = Main Menu.

### Stage 3 (Enter menu + inventory) — done 2026-09-26
- Note: SLASH'EM's own "Main Menu" is on Esc/`` ` `` (`domenusystem`), not
  Enter; web Enter (13) was an unknown command. Now `rhack()` (`src/cmd.c`,
  `#ifdef WEB_GRAPHICS`) sends `'\r'` to `web_cmdmenu()` and nothing else.
- **Enter menu:** `web_cmdmenu()` in `win/web/winweb.c` reads `dat/hh`
  (`NH_SHELP`) at run time: "General commands" + "Game commands" lines with
  one key (`x` or `^X`) become rows (key = `gch`, hh line as text), then every
  `extcmdlist` entry as `#name`. The choice is **queued as keys** (`kq[]`,
  read first by `getkey()`), so prefixes (F, m, n) and prompts work as typed;
  extended commands queue `#name\n`. Meta (`M-`) lines are not parsed (all
  reachable as `#name`). New hh lines appear automatically.
- `web_select_menu`: accelerator check also matches `gch`; `mitem.ch/gch`
  are `unsigned char` now (M- keys = 0x80|c).
- **Inventory `i`:** `ddoinv()` in `src/invent.c` = `display_inventory(0,
  TRUE)` (PICK_ONE, cursor). `rvip_invlist` makes `web_select_menu` set
  `rvip_pick`: letter 'm' main, Ctrl+letter 'x' examine (not for h/i/j/m:
  Ctrl = BS/Tab/LF/CR), `+` 'm', `-` 'd', `*` 'x', Enter/Space/5/click 0 =
  action menu `rvip_menu()` (main action first; keys e q r z a W T P R w Q t
  E d `*`, M-d/M-r/M-i by cursor). Table `rvip_ia[]`, `rvip_fits()`,
  `rvip_main()`. **Actions are direct calls** (`doeat`, `dodrink`, …) with
  `rvip_obj` set; `getobj()` returns it once without asking. Examine = pline
  `doname` + `checkfile(xname)` (encyclopedia; `checkfile` made non-static in
  `pager.c`).
- Reopen: `rvip_reopen` set after any action; `rvip_continue()` (hack.c)
  calls `ddoinv()` next idle turn unless `rvip_hostile_in_view()`.
- Shift+letter drop not done: A–Z are item letters. `-` drops instead.
- Item prompts: `getobj()` (WEB_GRAPHICS) opens the list at once (title =
  the question, `rvip_prompt`) unless `-`, `,` or `.` are valid answers
  (wield, engrave, …): there the letter prompt stays and Enter opens it.
  One candidate → full list (`*`), since `?` with one item only plines.
- 3d: no `--More--` anywhere; two births (Cavewoman, Tourist) had no stops.
- Help: `dat/hh` (Enter line, `i` key block), `dat/cmdhelp` (`^M`, `i`),
  copied to `playground/`.
- Tested (browser pane, local server): Enter menu lists General/Game/extended;
  8/2 + Esc; `~` from menu explored; `i` from menu; cursor + Enter → sling
  menu → `w` wielded, list reopened; `f` quaffed; `-` dropped; Ctrl+d
  examined (encyclopedia); `+` wielded; `0` closed; `d` prompt list; `w`
  prompt + Enter list; `e` → "What do you want to eat?" list → ate; `i` read
  magic mapping, reopened; row click (mousedown) ran `^X` and `#quit`;
  save/restore; no console errors. Test DB `/slashem` deleted.
- Open: Enter menu has no movement rows (hh "Move commands" block skipped);
  item prompts with `-`/`,`/`.` still start as a letter prompt.
- Next: stage 4 (tiles).

### Stage 4 (tiles) — done 2026-09-26
- Tile set: SLASH'EM's own 16×16 tiles (`win/share/{monsters,objects,other}.txt`,
  1404 tiles) → `web/mktiles.py` → `dist/tiles.png` (40 per row). Only this
  set; no mixing, no fallback needed.
- Mapping (C decides): `util/tilemap` → `src/tile.c` (`glyph2tile[]`,
  `substitute_tiles()` for mines/hell/knox/sokoban walls, called from
  `goto_level()`/restore); `o_init.c` `shuffle_tiles()` gives flavoured
  objects their appearance tile. `win/web/winweb.c` `redraw()` sends
  `glyph2tile[glyph]` per cell, `-1` for never-printed cells (blank).
- Loader: `web/slashem.js` `draw()` (canvas backing store = CSS size × dpr,
  `imageSmoothingEnabled = false` after every resize, so no CSS scaling);
  menu/inventory tiles are 16px `.ti` spans with `image-rendering: pixelated`.
  No pref files. Scale: cell 12–64 px (fit + Zoom ±).
- Checked (local server, own tab, wizard mode via a temporary `-D` hack in
  `unixmain.c` + dist args, both reverted and rebuilt): canvas pixels of the
  hero tile at cell 12 and 24 (dpr 2) use only the tile's own 6 colours
  (nearest-neighbour, no blending); hero = role tile (wizard, priestess),
  pet = kitten tile; rings/potions/wands/gems in inventory = appearance
  tile (steel, pearl, muddy, pink, puce, engagement, bamboo, black — checked
  against Discoveries); unexplored = blank, no stone tile; dark room
  interiors blank after ^F (3.4.3 has no dark-floor glyph); doors, doorways,
  corridors; Minetown (^V minetn) uses the mines wall tiles 1360–1370;
  inventory tiles at font height, text windows in the normal font.
- Statues and figurines use the generic statue/figurine object tiles: 3.4.3
  has no per-monster statue glyphs or tiles (what the game has; not faked).
- No code changes were needed.
- Open: pets have no marker in tiles mode (tty hilites them; the tile set has
  no pet overlay). Traps, `I` (remembered invisible) and Sokoban/Gehennom/
  Knox walls were checked only in `src/tile.c`, not seen in play.
  Wizard `-D` is unreachable on the web (emscripten `getpwuid` never matches
  `WIZARD`), fine for players, awkward for testing.
- Next: stage 5 web page: window layout, persistence, autosave/recovery
  (stage 1 note), Help button (`help.html`), deploy prep.

### Stage 5 (web page) — done 2026-09-26, tested locally, NOT deployed
- Harness (`web/slashem.js`, `web/index.html`, copy of nethack50's): rvip-wm.js
  tiling (Map, Log messages, Status, Inventory; one/multi toggle, Windows
  drop-down, rename/A−/A+ on hover, Reset windows), automatic split + cell
  size until dragged/zoomed, layout/zoom/fonts/titles/tiles-or-text in
  `/slashem/web-layout.json` (IDBFS). Map camera `RvipWM.center` with the
  hero cell from C (`js_map`). Prompt line `RvipWM.prompt.text` (id 0) +
  `.wait(atCmd)`. Top bar: "SLASH'EM", `Based on SLASH'EM 0.0.7E7F3 ·
  se007e7f3.tar.gz`, Help, Export/Import save, New character (clear only
  `save/0*` and `0*.N`, layout kept). Game end → sync → "Play again";
  `unhandledrejection`/`error` show "The game crashed … reload".
- "Waiting for a command" flag: 3.4.3 has none, so `parse()` (`src/cmd.c`,
  `WEB_GRAPHICS`) sets `web_at_cmd` (defined in `win/web/winweb.c`) around
  its command/count read; `getkey()` passes `web_at_cmd && popup < 0`.
- **Checkpoint:** `getkey()` calls `save_currentstate()` (INSURANCE: current
  level file + full state in `0<name>.0`) after 1 s idle at the command
  prompt once per key (and once after start/restore). JS syncs IDBFS every
  2 s while waiting for keys, every 15 s, on `visibilitychange`/`pagehide`
  and after save/end.
- **Recovery:** `sys/unix/unixunix.c` `web_recover()` = `util/recover.c`'s
  `restore_savefile()` (version + checkpoint level + game state + other
  levels → `save/0<name>`); `getlock()` under `__EMSCRIPTEN__` calls it and
  erases the level files instead of asking "Destroy old game?", then the
  normal restore loads the save. Recovery fails → old files erased, new game.
- `web/build.sh`: `help.html` only when `web/make-help.py` exists (prints a
  "not built yet" note; the Help button shows the 404). `web/deploy.sh` with
  the step-9 guard → `ruzzoli.de:/var/www/ruzzoli.de/roguelikes/slashem`
  (not run: no memmaker remote yet, stage 7).
- Tested (local server, own tab, throwaway names): birth → tiles → all
  windows filled; `~`, Enter menu, `i`; rename + gutter drag + zoom survive
  reload; zoomed map keeps the hero centred while exploring; prompt box
  shows `[yn]` questions and `#` input, hides on a command key; reload
  mid-game ×2 + reload with no key after restore → recovered in place, no
  question; `S` + Play again restores; 300 random keys, no console errors
  (only the expected help.html 404); `#quit` → "The game is over" → Play
  again → new game; Export (0Imp, 21 KB) → New character (layout kept) →
  Import → restored; resize 1000×650 → 1440×900 → 1200×750 (prompt open) →
  760×500, one/multi toggle, no page scroll, backing store = CSS × dpr. Test
  DB `/slashem` deleted.
- Open: at cell 12 (minimum) the 80-column map is wider than a small map
  window and scrolls with the hero (by design). No `beforeunload` warning
  (checkpoints make it unnecessary). help.html missing until stage 6.
- Next: stage 6 (docs + `web/make-help.py` → help.html, sound).

### Stage 6 (docs + sound) — done 2026-09-26
- Docs: `~/Desktop/Games/Roguelikes/Docs/` entry `slashem.html` (`build-docs.py`
  GAMES, facts per W1, essentials incl. `~`, `<`/`>`, Enter, `i`; "In the
  browser" section; new `parse_nethack343()` for 3.4.3's column-format
  `dat/hh`, 88 keys incl. M- keys) + `guides.py` guide (differences, first
  steps, staying alive) and a web "Saving" section (checkpoint recovery, `S`
  + reload, Export/Import). `python3 build-docs.py` rebuilt all 30 pages.
- Help: `web/make-help.py` (copy of nethack50's, reads that Docs entry,
  Saving from `guides.SAVING`, web notes incl. Sound/Music, "About this
  version" with the SourceForge 0.0.7E7F3 link and github.com/memmaker/slashem
  (not created yet)). `build.sh` always writes `dist/help.html`.
- Sound: no shared `rvip-tools/web/websound.c` exists (only `rvip-sound.js`),
  and nethack50's is a 5.0 soundlib, so `win/web/websound.c` is our own:
  `web_sound_msg()` (called in `add_msg()`) matches every message against a
  `pmatch` table (the USER_SOUNDS idea, built in, no config file) → effect
  name; `web_sound_where()` (after each `js_map`) plays `stairs` on a level
  change and reports town = `in_town(u.ux,u.uy) || *u.ushops` for the music.
  JS `nh.sound/nh.music` in `slashem.js` play only if the top-bar toggles are
  on (`sound`/`music` in `web-layout.json`, default off); `RVIPSound.play`,
  town loop via a lazy `Audio('sound/town.wav')`.
- Wavs: synthesized by `web/mksounds.py` at build time into `dist/sound/`
  (hit kill miss hurt die levelup hear door gold stairs + town loop), our own
  work, CC0. nethack50's wavs were not used (instrument/squeak samples, some
  CC-BY-4.0 needing attribution, none fit combat).
- Tested (local server 127.0.0.1:8791, own tab, throwaway "Tsnd"): Sound/Music
  "off" at start, no wav requests; Sound on → door/stairs/hear/kill played
  (`sound/*.wav` 200 in the resource log); state survived reload; music
  toggle persisted, `nh.music(1)` loaded town.wav; Help: 7 sections, 88-key
  list, both links, keys don't reach the game while open, Escape closes; no
  console errors. Test DB `/slashem` deleted, tab closed, server killed.
- Open: town music not heard in real Minetown/shop play (C test untested in
  game); message patterns cover common hit/miss/kill texts only (misses
  "The X claws" variants beyond those listed).
- Next: stage 7 publish: `gh repo create memmaker/slashem` + push, deploy via
  `web/deploy.sh`, card + tree entry on the selection page, RVIP.md
  self-improve (3.4.3 notes from stages 1–6).

### Stage 7 (publish) — done 2026-09-26
- Repo https://github.com/memmaker/slashem (remote `memmaker`, branch `main`),
  base `ab6287b` = SourceForge `se007e7f3.tar.gz`; `README.md` head links the
  tarball page and the compare view `ab6287b...main`.
- Live: https://ruzzoli.de/roguelikes/slashem/ (`web/build.sh` +
  `web/deploy.sh` from pushed `e36af5d`). curl 200: page, wasm, js,
  help.html, tiles.png, sound/*.wav. Own tab: title → "Who are you?" →
  born (neutral gnomish Archeologist, throwaway "Tpub"); then only the
  `/slashem` IndexedDB on ruzzoli.de deleted, tab closed.
- Selection page: card (`slashem.png`, 60 monster tiles from our tiles.png,
  ×2 nearest), tag "NetHack variant · 1997"; tree: existing SLASH'EM span
  under NetHack turned into a gold link (`slashem/`), no ✦ yet. Deployed,
  live index = local.
- Lineage: `dat/history` + `doc/Guidebook.txt`: SLASH (Tom Proudfoot 1996)
  → SLASH 4.1.2 (Enrico Horn, on NetHack 3.2); Warren Cheung combined SLASH
  4.1.2 + Wizard Patch (Larry Stewart-Zerba) into SLASH'EM 0.1, **November
  1997**. 0.0.7E7F3 is on NetHack 3.4.3 (`README.34`, patchlevel.h SCCS
  3.4). NetHackWiki: based on 3.4.3, first release **7 January 1998** —
  disagrees with the history file on the date; we keep 1997 (history file).
- RVIP.md (rvip-tools, local commit `356f044`, not pushed): new Part O
  section "O-SLASH'EM", case-table worked example, W2 row, Part 2 rule
  "delete only the game's own IDBFS database", O-Hack `gh repo create` note
  updated (works from the main session in bypass mode).
- Note: the browser pane's `computer type` keys didn't reach the page on
  the live site; `KeyboardEvent` dispatch from JS worked (as stage 1).
- Next: stage 8 shrine (`~/Games/roguelikes-index/shrine/slashem.html`),
  then Info button + tree ✦.

### Stage 8 (shrine) — done 2026-09-26
- Page `~/Games/roguelikes-index/shrine/slashem.html` + `shrine/slashem/`
  (Guidebook.txt from `doc/`, license.txt from `dat/license`, NGPL). No
  screenshots (RVIP step 11 dropped them). Linked from the card (Info), the
  tree (✦) and this game's `#bar h1` (`web/index.html`).
- Sources: `dat/history`, `doc/Guidebook.txt` (history chapter),
  `readme.txt` (0.0.7E7F3 = 30 Dec 2006, dev team), `include/patchlevel.h`;
  fetched: NetHackWiki SLASH'EM + "Standard strategy (SLASH'EM)", Wikipedia,
  slashem.sourceforge.net, RogueBasin. Counts grepped from the upstream tree
  `ab6287b` (`src/role.c`, `src/monst.c`/`include/pm.h` NUMMONS 612,
  `src/objects.c`/NUM_OBJECTS 537, `src/tech.c` 41, `include/artilist.h` 69,
  `include/trap.h` 22, `dat/dungeon.def` 21 dungeons); 184,611 lines in 214
  `src/*.c include/*.h`.
- Year disagreement kept on the page: history file Nov 1997 vs NetHackWiki
  7 Jan 1998. Wizard Patch is by Larry Stewart-Zerba (+ Warwick Allison),
  not Kevin Hugo (Guidebook, sourceforge, RogueBasin agree). SourceForge and
  RogueBasin say "based on NetHack 3.3.1" (true for 0.0.6, not 0.0.7).
- Minetown's 7 variants are vanilla 3.4.3 too, so not listed as unique.
- Manual: Guidebook copied. Walkthrough: none; NetHackWiki "Standard
  strategy (SLASH'EM)" linked. Cheats: `-D` unreachable in the browser;
  `X`/`#explore` works; Export/Import. No exploits listed (none fetched).
