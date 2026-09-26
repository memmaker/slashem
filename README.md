**RVIP port** of SLASH'EM 0.0.7E7F3 (SourceForge tarball `se007e7f3.tar.gz`,
sha256 `3b55b7fa6f4a8b703382cdcfb0af6dd25e65fb55a723298b02624af566e36df9`,
https://sourceforge.net/projects/slashem/files/slashem-source/0.0.7E7F3/),
committed unchanged as `ab6287b`. Play: https://ruzzoli.de/roguelikes/slashem/
Our changes: https://github.com/memmaker/slashem/compare/ab6287b...main

SLASH'EM (Super Lotsa Added Stuff Hack - Extended Magic) is a NetHack 3.4.3
variant with more roles, races, monsters, items and dungeon branches.
Upstream's own notes: `README.34` (NetHack 3.4.3), `dat/history`, `doc/`.

What this port adds:
- **Web build** (Emscripten/WASM): window port `win/web/winweb.c`, page
  `web/index.html` + `web/slashem.js` (map, messages, status, inventory
  windows; SLASH'EM's own 16x16 tiles), saves in IndexedDB, Export/Import.
- **Explore** `~` (`#autoexplore`), `<`/`>` walk to the nearest known stairs
  (`src/hack.c`, `src/do.c`).
- **Enter menu** of every command (built from `dat/hh` at run time).
- **Inventory** `i`: pick an item, then an action menu.
- **Recovery**: checkpoints while idle; a closed tab resumes in place
  (`util/recover.c` logic in `getlock()`).
- **Sound** (off by default): message-driven effects, synthesized wavs
  (`web/mksounds.py`, CC0).

Build: native tty build first (see `HANDOVER.md`, stage 1), then
`sh web/build.sh` → `web/dist` (needs emcc and node). Deploy: `sh web/deploy.sh`.
