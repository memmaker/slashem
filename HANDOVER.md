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
