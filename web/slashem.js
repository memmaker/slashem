/*
 * SLASH'EM in the browser: draws what win/web/winweb.c sends
 * (Module.nh): the map as tile indexes, prompt, status, inventory, pop-up
 * rows and messages. No tile logic here: C picks every tile. Windows are
 * placed by the shared rvip-wm.js. Keyboard + mouse, saves in IndexedDB
 * (IDBFS, /slashem). Loaded before slashem-core.js.
 * Copied from ~/Games/nethack50/web/nethack.js (RVIP).
 */
(function () {
	'use strict';

	var DIR = '/slashem', SAVES = DIR + '/save', SEED = '/seed', COLNO = 80, ROWNO = 21;
	var PAL = ['#555', '#c82828', '#28aa28', '#aa6e28', '#3c3cdc', '#aa28aa', '#28aaaa', '#c8c8c8',
		'#646464', '#ff8c00', '#5aff5a', '#ffff50', '#6e6eff', '#ff5aff', '#5affff', '#fff'];
	/* arrows/Home/PgUp/End/PgDn: 0x101.. (winweb.h makes them hjklyubn or the number pad) */
	var KEYS = { ArrowUp: 0x101, ArrowDown: 0x102, ArrowLeft: 0x103, ArrowRight: 0x104, Home: 0x105, PageUp: 0x106,
		End: 0x107, PageDown: 0x108, Enter: 13, Escape: 27, Backspace: 8, Delete: 8, Tab: 9 };

	var events = [], running = false, lastSync = 0;
	var cells = null, chars = null, hero = { x: 0, y: 0, lev: -1 }, off = { x: 0, y: 0 };
	var cv, ctx, cell = 32, auto = true, sheet = new Image(), perRow = 40;
	var dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
	var log = [], prompt = '', rects = {}, wm = null;
	var L = { cell: 0, font: 13, wm: null, text: false, sound: false, music: false }, LAYOUT = DIR + '/web-layout.json';

	function $(id) { return document.getElementById(id); }
	function status(msg, isError) {
		var s = $('status');
		s.textContent = msg; s.hidden = !msg; s.classList.toggle('error', !!isError);
	}
	function esc(t) { return t.replace(/[&<>]/g, function (c) { return '&' + (c === '&' ? 'amp' : c === '<' ? 'lt' : 'gt') + ';'; }); }

	/* ---------- map ---------- */
	function measure() {
		var w = COLNO * cell, h = ROWNO * cell;
		cv.width = w * dpr; cv.height = h * dpr;
		cv.style.width = w + 'px'; cv.style.height = h + 'px';
		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		ctx.imageSmoothingEnabled = false;     /* nearest-neighbour tiles */
	}
	function fit() {       /* biggest cell that shows the whole map in its window */
		var b = $('map'), best = 12;
		for (var c = 12; c <= 64; c++) if (COLNO * c <= b.clientWidth && ROWNO * c <= b.clientHeight) best = c;
		return best;
	}
	function draw() {
		if (!cells) return;
		ctx.fillStyle = '#000'; ctx.fillRect(0, 0, COLNO * cell, ROWNO * cell);
		if (L.text) {       /* text mode: the game's own characters and colours */
			ctx.font = 'bold ' + Math.round(cell * 0.8) + 'px monospace';
			ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
			for (var ty = 0; ty < ROWNO; ty++)
				for (var tx = 0; tx < COLNO; tx++) {
					var k = chars[ty * COLNO + tx];
					if ((k & 0xff) <= 32) continue;
					ctx.fillStyle = PAL[(k >> 8) & 15];
					ctx.fillText(String.fromCharCode(k & 0xff), (tx + 0.5) * cell, (ty + 0.5) * cell + 1);
				}
		} else if (!sheet.complete || !sheet.width) return;
		else for (var y = 0; y < ROWNO; y++)
			for (var x = 0; x < COLNO; x++) {
				var t = cells[y * COLNO + x];
				if (t >= 0) ctx.drawImage(sheet, (t % perRow) * 16, Math.floor(t / perRow) * 16, 16, 16, x * cell, y * cell, cell, cell);
			}
	}
	/* keep the hero in the middle half of the map window; recentre when it leaves it */
	function scrollMap() {
		off = RvipWM.center(cv, (hero.x + 0.5) * cell, (hero.y + 0.5) * cell, COLNO * cell, ROWNO * cell);
	}
	/* a row's icon: the tile with tiles on, else the item's own map symbol (C sends both) */
	function icon(t, sym) {
		if (L.text || !sheet.width) return sym > 32 ? esc(String.fromCharCode(sym)) + ' ' : '';
		return tileSpan(t);
	}
	function tileSpan(t) {     /* a tile at text size, for menus and the inventory */
		if (t < 0) return '';
		return '<span class="ti" style="background-position:-' + (t % perRow) * 16 + 'px -' + Math.floor(t / perRow) * 16 + 'px"></span>';
	}

	/* ---------- text windows ---------- */
	function drawMsgs() {
		var ml = $('msg'), body = ml.parentNode;
		ml.innerHTML = log.map(function (m) { return m.old ? '<span class="old">' + esc(m.t) + '</span>' : esc(m.t); }).join('\n') +
			(prompt ? (log.length ? '\n' : '') + '<span class="pr">' + esc(prompt) + '</span>' : '');
		body.scrollTop = body.scrollHeight;     /* the newest message stays in view */
	}
	/* rows "tile \t letter \t 0|1 selected, 2 heading \t colour \t symbol \t text" (winweb.c) */
	function rowsHtml(t, cur) {
		return t.split('\n').filter(function (l, i, a) { return l || i < a.length - 1; }).map(function (l, i) {
			var f = l.split('\t'), text = f.slice(5).join('\t'), sel = +f[2];
			var h = sel === 2 ? '' : f[1] === ' ' ? '    ' : esc(f[1]) + (sel ? ' + ' : ' - ');   /* no letter: keyed by symbol */
			return '<div class="row' + (i === cur ? ' cur' : '') + (sel === 2 ? '' : ' pick') + '" data-i="' + i + '" style="color:' + PAL[+f[3]] + '">' +
				h + icon(+f[0], +f[4]) + esc(text) + '</div>';
		}).join('');
	}
	function drawPop(t) {
		var pop = $('pop');
		if (!t) { pop.hidden = true; return; }
		var nl = t.indexOf('\n'), head = t.slice(0, nl).split('\t'), top = +head[0], cur = +head[1], p = head.slice(2).join('\t');
		pop.innerHTML = (p ? '<div class="pp">' + esc(p) + '</div>' : '') + '<div class="rows">' + rowsHtml(t.slice(nl + 1), cur) + '</div>';
		pop.hidden = false;
		RvipWM.popup(pop, { center: true });
		var rows = pop.querySelectorAll('.row'), r = rows[cur >= 0 ? cur : top];
		if (r) { if (cur >= 0) r.scrollIntoView({ block: 'nearest' }); else pop.scrollTop = r.offsetTop - pop.firstChild.offsetHeight; }
	}

	/* ---------- layout (shared rvip-wm.js, RVIP.md 5b) ---------- */
	function saveLayout() {
		try { Module.FS.writeFile(LAYOUT, JSON.stringify(L)); syncFiles(); } catch (e) { console.warn('layout not saved', e); }
	}
	function fonts() { ['msg', 'stat', 'inv', 'pop'].forEach(function (id) { $(id).style.fontSize = L.font + 'px'; }); }
	function makeWM() {
		try { var s = JSON.parse(Module.FS.readFile(LAYOUT, { encoding: 'utf8' })); if (s) L = { cell: s.cell | 0, font: s.font || 13, wm: s.wm, text: !!s.text, sound: s.sound === true, music: s.music === true }; } catch (e) { }
		showMode(); showAudio();
		if (L.cell >= 12 && L.cell <= 64) { cell = L.cell; auto = false; }
		var H = $('game').clientHeight || 600, line = Math.ceil(L.font * 1.4) + 6;
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'),
			wins: [{ id: 'map', title: 'Map' }, { id: 'msg', title: 'Log messages' }, { id: 'stat', title: 'Status' }, { id: 'inv', title: 'Inventory' }],
			multi: { d: 'h', r: 0.75, a: { d: 'v', r: 0.2, a: 'msg', b: { d: 'v', r: 0.84, a: 'map', b: 'stat' } }, b: 'inv' },
			single: { d: 'v', r: 3 * line / H, a: 'msg', b: { d: 'v', r: 1 - 3 * line / (H - 3 * line), a: 'map', b: 'stat' } },
			state: L.wm, noFont: 'map',
			save: function (st) { L.wm = st; saveLayout(); },
			layout: function (r) { rects = r; fonts(); if (auto) { cell = fit(); measure(); } scrollMap(true); draw(); },
			font: function (id, d) { L.font = Math.max(8, Math.min(28, L.font + d)); fonts(); saveLayout(); },
			onReset: function () { auto = true; L.cell = 0; L.font = 13; L.wm = wm.state(); fonts(); cell = fit(); measure(); scrollMap(true); draw(); saveLayout(); }
		});
		wm.apply();
	}
	function showMode() { $('btn-tiles').textContent = L.text ? 'Tiles: None' : 'Tiles: SLASH\'EM'; }
	/* ---------- sound (RVIP 6b): C names the effect (win/web/websound.c), off by default ---------- */
	var song = null, town = false;
	function showAudio() {
		$('btn-sound').textContent = 'Sound: ' + (L.sound ? 'on' : 'off');
		$('btn-music').textContent = 'Music: ' + (L.music ? 'on' : 'off');
		if (L.music && town) {
			if (!song) { song = new Audio('sound/town.wav'); song.loop = true; song.volume = 0.4; }
			song.play().catch(function () { });
		} else if (song) song.pause();
	}
	function toggleAudio(k) { L[k] = !L[k]; showAudio(); saveLayout(); }
	function zoom(d) {
		auto = false;
		cell = Math.max(12, Math.min(64, cell + d));
		L.cell = cell; saveLayout();
		measure(); scrollMap(true); draw();
	}

	/* inventory and pop-up again from the game's last rows (tile set changed) */
	function relist() {
		[2, 3].forEach(function (id) { var t = nh.last[id]; if (t != null) { nh.last[id] = null; nh.text(id, t); } });
	}

	var nh = {
		map: function (cp, tp, hx, hy, lev) {
			cells = Module.HEAP32.slice(cp >> 2, (cp >> 2) + COLNO * ROWNO);
			chars = Module.HEAP32.slice(tp >> 2, (tp >> 2) + COLNO * ROWNO);
			if ($('game').hidden) { $('game').hidden = false; status(''); measure(); makeWM(); }
			var moved = hx !== hero.x || hy !== hero.y, lv = lev !== hero.lev;
			hero.x = hx; hero.y = hy; hero.lev = lev;
			if (moved || lv) scrollMap(lv);
			draw();
		},
		sound: function (name) { if (L.sound) RVIPSound.play([name], 0.6); },
		music: function (on) { town = !!on; showAudio(); },
		last: [],
		text: function (id, t) {
			if (id === 4) { log.push({ t: t }); if (log.length > 300) log.shift(); drawMsgs(); return; }
			if (id === 6) { if (log.length) log[log.length - 1] = { t: t }; drawMsgs(); return; }   /* the game folded a repeat */
			if (id === 5) { log.forEach(function (m) { m.old = true; }); drawMsgs(); return; }
			if (nh.last[id] === t) return;
			nh.last[id] = t;
			if (id === 0) { prompt = t; RvipWM.prompt.text(t); drawMsgs(); }
			else if (id === 1) $('stat').textContent = t.replace(/\n$/, '');
			else if (id === 2) $('inv').innerHTML = rowsHtml(t, -1);
			else if (id === 3) drawPop(t);
		},
		/* peek: number of waiting keys; otherwise the next key or -1.
		 * While the game waits, the files go to IndexedDB every 2 s. */
		key: function (peek, atCmd) {
			RvipWM.prompt.wait(atCmd);
			if (peek) return events.length;
			if (events.length) return events.shift();
			var now = performance.now();
			if (now - lastSync > 2000) { lastSync = now; syncFiles(); }
			return -1;
		},
		end: function () {
			running = false;
			return new Promise(function (done) {
				syncFiles(function () {
					$('overlay-msg').textContent = saveFile() ? 'Your game has been saved. Play again to continue it.' : 'The game is over.';
					$('overlay').hidden = false;
					done();
				});
			});
		}
	};

	/* ---------- input ---------- */
	function onKey(e) {
		if (!$('help').hidden) {
			if (e.key === 'Escape') { $('help').hidden = true; e.preventDefault(); }
			return;
		}
		if (!running || e.isComposing || e.metaKey || /^(INPUT|TEXTAREA)$/.test(e.target.tagName)) return;
		var k = e.key, c;
		if (e.code === 'NumpadEnter') c = 13;
		else if (KEYS[k] !== undefined) c = KEYS[k];
		else if (k.length === 1) {
			c = k.charCodeAt(0);
			if (e.ctrlKey && !e.altKey) {
				var u = k.toUpperCase().charCodeAt(0);
				if (u >= 65 && u <= 90) c = u & 0x1f; else return;
			} else if (e.altKey && c < 128) c |= 0x80;     /* Alt = meta, as NetHack's M- keys */
			if (c > 255) return;
		}
		else return;
		events.push(c);
		e.preventDefault();
	}
	function onMapClick(e) {
		if (!running) return;
		var r = cv.getBoundingClientRect(), x = Math.floor((e.clientX - r.left) / cell), y = Math.floor((e.clientY - r.top) / cell);
		if (x > 0 && x < COLNO && y >= 0 && y < ROWNO) events.push(0x10000 | y << 8 | x | (e.button === 2 ? 0x8000 : 0));
		e.preventDefault();
	}

	/* ---------- saves: IndexedDB (IDBFS) ---------- */
	var syncing = false, syncAgain = false, pendingCbs = [];
	function syncFiles(cb) {
		if (!Module.FS) { if (cb) cb(); return; }
		if (typeof cb === 'function') pendingCbs.push(cb);
		if (syncing) { syncAgain = true; return; }
		syncing = true;
		var cbs = pendingCbs; pendingCbs = [];
		Module.FS.syncfs(false, function (err) {
			syncing = false;
			if (err) status('Saving to browser storage (IndexedDB) failed: ' + err + '. Use "Export save" to keep a copy.', true);
			cbs.forEach(function (f) { f(err); });
			if (syncAgain) { syncAgain = false; syncFiles(); }
		});
	}
	function ls(d, re) { try { return Module.FS.readdir(d).filter(function (f) { return re.test(f); }); } catch (e) { return []; } }
	/* save/<uid><name> after S; <uid><name>.0.. level files (checkpoint) while a game runs */
	function saveFile() { return ls(SAVES, /^\d+.+$/)[0] || null; }
	function charName() {
		var f = saveFile() || ls(DIR, /^\d+.+\.0$/)[0];
		return f ? f.replace(/^\d+/, '').replace(/\.0$/, '') : null;
	}
	function exportSave() {
		var f = saveFile();
		if (!f) { status('There is no saved game file: press S in the game first.', true); setTimeout(function () { status(''); }, 2500); return; }
		var a = document.createElement('a');
		a.href = URL.createObjectURL(new Blob([Module.FS.readFile(SAVES + '/' + f)], { type: 'application/octet-stream' }));
		a.download = f;
		document.body.appendChild(a); a.click();
		setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
	}
	function clearGame() {
		ls(SAVES, /^\d/).forEach(function (f) { Module.FS.unlink(SAVES + '/' + f); });
		ls(DIR, /^\d+.+\.\d+$/).forEach(function (f) { Module.FS.unlink(DIR + '/' + f); });
	}
	function importSave(file) {
		var name = file.name.replace(/^\d+/, '').replace(/\.gz$/, '').replace(/[^\w-]/g, '');
		if (!name) { status('A SLASH\'EM save file is named like 0Name (user number, then the character name).', true); return; }
		var r = new FileReader();
		r.onload = function () {
			if (!confirm('Replace the current game with "' + file.name + '"?')) return;
			running = false;
			clearGame();
			Module.FS.writeFile(SAVES + '/0' + name, new Uint8Array(r.result));
			syncFiles(function (err) { if (!err) location.reload(); });
		};
		r.readAsArrayBuffer(file);
	}
	function newGame() {
		if (!confirm('Delete the saved game in this browser and start a new one?')) return;
		running = false;
		clearGame();
		syncFiles(function (err) { if (!err) location.reload(); });
	}

	/* ---------- help ---------- */
	var helpLoaded = false;
	function toggleHelp() {
		var h = $('help');
		h.hidden = !h.hidden;
		if (!h.hidden && !helpLoaded) {
			helpLoaded = true;
			fetch('help.html').then(function (r) { if (!r.ok) throw new Error(r.status); return r.text(); })
				.then(function (t) { $('help-body').innerHTML = t; })
				.catch(function (err) { helpLoaded = false; $('help-body').textContent = 'Could not load the guide (' + err + '). Press ? in the game for its own help.'; });
		}
		if (!h.hidden) $('help-body').focus();
	}

	/* ---------- startup ---------- */
	window.Module = {
		nh: nh,
		arguments: ['-d', DIR],
		preRun: [function () {
			var FS = Module.FS;
			Module.ENV.HOME = DIR;
			Module.ENV.USER = 'player';
			FS.mkdirTree(DIR);
			FS.mount(Module.IDBFS, {}, DIR);
			Module.addRunDependency('idbfs');
			FS.syncfs(true, function (err) {
				if (err) status('Could not read saved games from IndexedDB (' + err + '). Saving may not work in this browser mode.', true);
				FS.readdir(SEED).forEach(function (f) { if (f[0] !== '.') FS.writeFile(DIR + '/' + f, FS.readFile(SEED + '/' + f)); });
				['perm', 'record', 'logfile', 'xlogfile', 'livelog'].forEach(function (f) { try { FS.stat(DIR + '/' + f); } catch (e) { FS.writeFile(DIR + '/' + f, ''); } });
				try { FS.mkdir(SAVES); } catch (e) { }
				var n = charName();
				if (n) Module.arguments.push('-u', n);
				Module.removeRunDependency('idbfs');
			});
		}],
		onRuntimeInitialized: function () { running = true; },
		print: function (s) { console.log(s); },
		printErr: function (s) { console.warn(s); },
		setStatus: function (s) { if (s && !running) status(s.replace(/\(\d+\/\d+\)/, '').trim() || 'Loading…'); },
		onAbort: function (what) { crashed(what); }
	};
	function crashed(err) {
		if (!running) return;
		running = false;
		var msg = (err && (err.message || err.reason && err.reason.message)) || String(err);
		console.error('[slashem] crash:', err);
		status('The game crashed (' + msg + '). Reload the page to recover from the last checkpoint.', true);
	}
	window.addEventListener('unhandledrejection', function (e) {
		if (e.reason && e.reason.name === 'ExitStatus') return;   /* exit() is the normal end */
		crashed(e.reason);
	});
	window.addEventListener('error', function (e) {
		if (e.error && e.error.name === 'ExitStatus') return;
		if (e.error instanceof WebAssembly.RuntimeError || /slashem-core/.test(e.filename || '')) crashed(e.error || e.message);
	});
	document.addEventListener('visibilitychange', function () { if (document.hidden) syncFiles(); });
	window.addEventListener('pagehide', function () { syncFiles(); });
	setInterval(function () { if (running) syncFiles(); }, 15000);

	window.addEventListener('resize', function () { if (wm) wm.apply(); });
	document.addEventListener('keydown', onKey);
	document.addEventListener('DOMContentLoaded', function () {
		cv = document.querySelector('#map canvas');
		ctx = cv.getContext('2d');
		sheet.onload = function () { perRow = sheet.width / 16; draw(); relist(); };
		sheet.src = 'tiles.png';
		cv.addEventListener('mousedown', onMapClick);
		cv.addEventListener('contextmenu', function (e) { e.preventDefault(); });
		$('pop').addEventListener('mousedown', function (e) {
			var r = e.target.closest('.row.pick');
			if (r && running) { events.push(0x20000 | +r.dataset.i); e.preventDefault(); }
		});
		$('btn-export').onclick = exportSave;
		$('btn-import').onclick = function () { $('import-file').click(); };
		$('import-file').onchange = function () { if (this.files[0]) importSave(this.files[0]); this.value = ''; };
		$('btn-new').onclick = newGame;
		$('btn-help').onclick = toggleHelp;
		$('help-close').onclick = toggleHelp;
		$('btn-tiles').onclick = function () { L.text = !L.text; showMode(); saveLayout(); draw(); relist(); };
		$('btn-sound').onclick = function () { toggleAudio('sound'); };
		$('btn-music').onclick = function () { toggleAudio('music'); };
		$('btn-zoom-in').onclick = function () { zoom(4); };
		$('btn-zoom-out').onclick = function () { zoom(-4); };
		$('btn-restart').onclick = function () { location.reload(); };
		document.querySelectorAll('button').forEach(function (b) {
			b.addEventListener('mousedown', function (e) { e.preventDefault(); });
		});
	});
})();
