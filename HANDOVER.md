# SLASH'EM 0.0.7E7F3 — handover

Web port of SLASH'EM 0.0.7E7F3 (all RVIP stages 1–9 done, live at
https://ruzzoli.de/roguelikes/slashem/). Procedure: `~/Games/rvip-tools/RVIP.md`.

## Source and repo
- SourceForge tarball `se007e7f3.tar.gz` (sha256
  `3b55b7fa6f4a8b703382cdcfb0af6dd25e65fb55a723298b02624af566e36df9`,
  https://sourceforge.net/projects/slashem/files/slashem-source/0.0.7E7F3/),
  base commit `ab6287b`. Repo https://github.com/memmaker/slashem (remote
  `memmaker`, branch `main`); README links the tarball page and the compare view
  `ab6287b...main`.
- Case O, NetHack 3.4.3 family; closest relative `~/Games/nethack50` (window port
  and page were copied from there).

## Build and deploy
- **Native build first** (tty; `web/build.sh` needs `playground/` data and the
  generated headers):
  ```
  sh sys/unix/setup.sh
  make CFLAGS="-O -I../include -w -Wno-error -Wno-implicit-function-declaration -Wno-implicit-int -Wno-incompatible-function-pointer-types -Wno-int-conversion -Wno-return-mismatch" \
       WINTTYLIB=-lncurses GAMEUID=$(id -un) GAMEGRP=staff \
       GAMEDIR=$PWD/playground PREFIX=$PWD/install SHELLDIR=$PWD/install/bin all install
  ```
  On Linux also `CC=clang` and `-DLINUX` in CFLAGS. **No `-j`** (util's yacc rules
  race on `y.tab.c`). Missing `nroff`/`tbl` and `rmdir ./-p` errors are harmless.
- Web: `sh web/build.sh` → `web/dist` (emcc flags in the script). `lev_comp`/
  `dgn_comp` are rebuilt with emcc into `web/tools/` and run under node, because
  the data headers hold `unsigned long` (8 bytes native, 4 in wasm32).
  `util/tilemap` writes `src/tile.c`. Test: `python3 -m http.server <port> -d web/dist`.
- `web/deploy.sh` → `ruzzoli.de:/var/www/ruzzoli.de/roguelikes/slashem`.
- Shared page code from the parent folder: `../rvip-wm.js`, `../rvip-app.js`,
  `../rvip-sound.js`.

## File map
- `win/web/winweb.c`: window port (`web_procs`), Enter menu `web_cmdmenu()` (reads
  `dat/hh` at run time, choice queued as keys), inventory actions (`rvip_ia[]`,
  `rvip_menu()`), checkpoint in `getkey()`, beacon `be_run_end()`/`js_beacon`.
- `win/web/websound.c`: message → effect via a built-in `pmatch` table; stairs on
  level change; town music = `in_town() || *u.ushops`.
- `web/slashem.js` (draws; layout in `/slashem/web-layout.json`), `web/index.html`,
  `web/mktiles.py` (SLASH'EM's own 1404 tiles), `web/make-help.py`,
  `web/mksounds.py` (own synth wavs, CC0).
- Game changes: `src/hack.c` end (`rvip_*`, `doexplore`; `~` = `#autoexplore` in
  `src/cmd.c`), `src/allmain.c` (`rvip_continue()` hook), `src/do.c` (`<`/`>` off
  stairs walk there; press again to take them), `src/lock.c` (`doopen_indir`),
  `src/invent.c`/`getobj()` (lists), `src/cmd.c` `parse()` sets `web_at_cmd`,
  `sys/unix/unixunix.c` `web_recover()` (recover.c logic in `getlock()`),
  `src/end.c` → beacon. Config: `include/config.h`, `global.h`, `unixconf.h`.
  Help lines in `dat/hh`, `dat/cmdhelp` (copied into `playground/`).

## Facts and gotchas
- Playground data preloaded at `/seed`, copied into IDBFS `/slashem` on each load;
  run as `-d /slashem -u <name>`.
- Checkpoint: `save_currentstate()` after 1 s idle at the command prompt; a reload
  recovers via `web_recover()` without asking "Destroy old game?".
- ASan fix: `nul[40]` too small for `struct fruit` on 64-bit (save.c, `nul[64]`).
- Wizard `-D` is unreachable on the web (`getpwuid` never matches `WIZARD`).
- Browser pane: `shift+s`, `?`, `#` don't arrive via the key tool; dispatch a
  `KeyboardEvent` from JS.
- Shrine year disagreement kept: history file Nov 1997 vs NetHackWiki 7 Jan 1998.

## Open
- Any message stops explore ("You hear…", displacing the pet, hunger); "(xN)"
  folding hides identical repeats from the stop test.
- Enter menu has no movement rows; item prompts where `-`/`,`/`.` are valid still
  start as a letter prompt.
- No pet marker in tiles mode.
- Town music not heard in real Minetown/shop play; sound patterns cover common
  hit/miss/kill texts only.
