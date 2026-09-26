#!/bin/sh
# Build SLASH'EM for the browser (Emscripten + Asyncify) into web/dist.
# win/web/winweb.c is the window port, web/slashem.js draws, rvip-wm.js
# places the windows.  Needs the native build first (playground/ data
# files, generated headers).  Deploy: web/deploy.sh.
set -e
cd "$(dirname "$0")/.."
OUT=web/dist SEED=web/seed
[ -f playground/dungeon ] || { echo "build natively first (make ... install)"; exit 1; }
[ -x util/tilemap ] || (cd util && make tilemap CFLAGS="-O -I../include -w")
(cd util && ./tilemap)          # src/tile.c + include/tile.h (glyph2tile)
rm -rf "$OUT" "$SEED" && mkdir -p "$OUT" "$SEED"
# the playground minus binaries, docs and per-player files
for f in playground/*; do
	case "$(basename "$f")" in
	slashem|recover|Guidebook.txt|save|perm|record|logfile|*.0) ;;
	*) cp "$f" "$SEED/" ;;
	esac
done
# level and dungeon compilers as wasm too: their file headers hold longs
# (4 bytes in wasm32, 8 natively), so native output is "incompatible"
TOOLS=web/tools; mkdir -p "$TOOLS"
[ -f util/lev_yacc.c ] || (cd util && make lev_yacc.c lev_lex.c dgn_yacc.c dgn_lex.c)
CW="-O1 -w -Wno-implicit-int -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-return-mismatch -Iinclude -sNODERAWFS -sENVIRONMENT=node -sEXIT_RUNTIME=1"
[ "$TOOLS/lev_comp.js" -nt util/lev_yacc.c ] || emcc $CW util/lev_yacc.c util/lev_lex.c util/lev_main.c util/panic.c \
	src/alloc.c src/drawing.c src/decl.c src/monst.c src/objects.c -o "$TOOLS/lev_comp.js"
[ "$TOOLS/dgn_comp.js" -nt util/dgn_yacc.c ] || emcc $CW util/dgn_yacc.c util/dgn_lex.c util/dgn_main.c util/panic.c src/alloc.c -o "$TOOLS/dgn_comp.js"
rm -f "$SEED"/*.lev "$SEED/dungeon"
cp dat/dungeon.pdf "$SEED/"
(cd "$SEED" && for d in ../../dat/*.des; do
	case "$d" in */template.des) ;; *) node ../tools/lev_comp.js "$d" >/dev/null || exit 1 ;; esac
 done && node ../tools/dgn_comp.js dungeon.pdf >/dev/null && rm dungeon.pdf)
echo "$(ls "$SEED"/*.lev | wc -l) levels compiled"
SRCS=$(ls src/*.c | grep -v '/borg\.c$')
emcc -O2 $EMFLAGS -w -Wno-implicit-int -Wno-implicit-function-declaration -Wno-int-conversion \
	-Wno-incompatible-function-pointer-types -Wno-return-mismatch -Iinclude \
	$SRCS sys/share/ioctl.c sys/share/unixtty.c \
	sys/unix/unixmain.c sys/unix/unixres.c sys/unix/unixunix.c win/web/winweb.c win/web/websound.c \
	--preload-file "$SEED@/seed" -o "$OUT/slashem-core.js" \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=131072 -sSTACK_SIZE=2097152 \
	-sALLOW_MEMORY_GROWTH -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=64MB \
	-sEXPORTED_FUNCTIONS=_main \
	-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,ENV,HEAP32 \
	-sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web
rm -rf "$SEED"
python3 web/mktiles.py win/share "$OUT/tiles.png"
cp web/index.html web/slashem.js "$HOME/Games/rvip-tools/web/rvip-wm.js" \
	"$HOME/Games/rvip-tools/web/rvip-sound.js" "$OUT/"
python3 web/mksounds.py "$OUT/sound"      # synthesized effects + town loop (CC0)
python3 web/make-help.py > "$OUT/help.html"
ls -la "$OUT"
