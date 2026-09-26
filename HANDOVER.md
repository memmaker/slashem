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
