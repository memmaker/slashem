/* SLASH'EM 0.0.7E7F3  win/web/winweb.c  browser window port (RVIP) */
/* Emscripten only.  C decides everything, web/slashem.js only draws it and
 * rvip-wm.js places the windows (RVIP.md Part W, W0):
 *   js_map:  ROWNO*COLNO tile indexes (-1 = nothing), the same cells as
 *            text (char | colour << 8, the game's own), hero, level
 *   js_text: 0 prompt, 1 status lines, 2 inventory, 3 pop-up, 4 new
 *            message, 5 messages so far are old, 6 replace last message.
 *            Rows are tab-separated (tile, letter, sel, colour, text).
 * Keys come from Module.nh.key(); Asyncify lets the game wait for them.
 * Modelled on ~/Games/nethack50/win/sdl2/winsdl.c + winweb.h. */

#include <emscripten.h>
#include "hack.h"
#include "func_tab.h"
#include "dlb.h"
#include <stdarg.h>

extern short glyph2tile[]; /* src/tile.c (util/tilemap) */

EM_JS(void, js_map, (int *c, int *t, int hx, int hy, int lev),
      { Module.nh.map(c, t, hx, hy, lev); });
EM_JS(void, js_text, (int id, const char *s),
      { Module.nh.text(id, UTF8ToString(s)); });
EM_JS(int, js_key, (int peek, int at_cmd), { return Module.nh.key(peek, at_cmd); });
EM_ASYNC_JS(void, js_end, (void), { await Module.nh.end(); });

#define HIST_MAX 300
#define MAXWIN 32
#define POP_ROWS 40 /* rows of a pop-up before it scrolls */

struct line { char *s; int attr, clr; };
struct mitem {
    anything id;
    unsigned char ch, gch; /* unsigned: M- keys arrive as 0x80|c */
    int attr, clr, tile;
    char *s;
    boolean sel;
};
struct nhw {
    int type;
    boolean used;
    struct line *lines;
    int nlines, cury;
    struct mitem *items;
    int nitems;
    char *prompt;
};

static struct nhw wins[MAXWIN];
static int mapglyphs[ROWNO][COLNO];
static boolean mapset[ROWNO][COLNO];
static char *hist[HIST_MAX];
static char hist_prev[BUFSZ];
static int hist_reps, nhist;
static int mouse_x, mouse_y, mouse_btn;
static char kq[BUFSZ]; /* RVIP: keys queued by the Enter menu (web_cmdmenu) */
static char promptbuf[BUFSZ * 2]; /* active prompt line (yn/getlin) */
static struct mitem *perm;        /* persistent inventory copy */
static int nperm;
static boolean perm_building, perm_dirty; /* dirty: refresh at the prompt */
static int popup = -1, pop_top, pop_cur = -1; /* floating window */
/* a click on a pop-up row: toggles it in a pick-any menu, else picks it */
#define POP_CLICK (pop_any ? ' ' : '\n')
static boolean pop_any;
static int cells[ROWNO * COLNO], chars[ROWNO * COLNO];
static char *tbuf;
static size_t tlen, tcap;

static void web_getlin(const char *, char *);
static void web_putstr(winid, int, const char *);
static winid web_create_nhwindow(int);
static void web_destroy_nhwindow(winid);
static void web_clear_nhwindow(winid);
static void web_update_inventory(void);

static char *
xstrdup(const char *s)
{
    char *p = (char *) alloc(strlen(s) + 1);

    Strcpy(p, s);
    return p;
}

static void
tadd(const char *fmt, ...)
{
    va_list ap;
    int n;

    for (;;) {
        va_start(ap, fmt);
        n = vsnprintf(tbuf ? tbuf + tlen : 0, tcap - tlen, fmt, ap);
        va_end(ap);
        if (tbuf && tlen + n < tcap) {
            tlen += n;
            return;
        }
        tcap = (tlen + n + 1) * 2;
        tbuf = (char *) realloc(tbuf, tcap);
    }
}

/* palette index for a text line (web/slashem.js PAL) */
static int
cidx(int attr, int clr)
{
    if (clr < 0 || clr >= CLR_MAX || clr == NO_COLOR)
        return attr == ATR_BOLD ? CLR_WHITE : CLR_GRAY;
    return clr;
}

/* the game's own colour for a glyph (menus, inventory) */
static int
glyph_color(int glyph)
{
    int ch, color;
    unsigned special;

    if (glyph == NO_GLYPH)
        return NO_COLOR;
    mapglyph(glyph, &ch, &color, &special, 0, 0);
    return color;
}

static void
redraw(void)
{
    int x, y, i, n;

    if (!iflags.window_inited)
        return;
    for (y = 0; y < ROWNO; y++)
        for (x = 0; x < COLNO; x++) {
            int ch = ' ', color = NO_COLOR;
            unsigned special;

            if (mapset[y][x])
                mapglyph(mapglyphs[y][x], &ch, &color, &special, x, y);
            cells[y * COLNO + x] = mapset[y][x] ? glyph2tile[mapglyphs[y][x]] : -1;
            chars[y * COLNO + x] = (ch & 0xff) | cidx(0, color) << 8;
        }
    js_map(cells, chars, u.ux, u.uy, u.uz.dnum * 100 + u.uz.dlevel);
    js_text(0, promptbuf);

    tlen = 0, tadd("%s", "");
    if (WIN_STATUS != WIN_ERR)
        for (i = 0; i < wins[WIN_STATUS].nlines; i++)
            tadd("%s\n", wins[WIN_STATUS].lines[i].s
                             ? wins[WIN_STATUS].lines[i].s : "");
    js_text(1, tbuf);

    /* rows: tile, letter, 0/1 selected or 2 heading, colour, text */
    tlen = 0, tadd("%s", "");
    for (i = 0; i < nperm; i++)
        tadd("%d\t%c\t%d\t%d\t%s\n", perm[i].tile,
             perm[i].ch ? perm[i].ch : ' ', perm[i].id.a_void ? 0 : 2,
             perm[i].id.a_void ? cidx(perm[i].attr, perm[i].clr) : CLR_YELLOW,
             perm[i].s);
    js_text(2, tbuf);

    tlen = 0, tadd("%s", "");
    if (popup >= 0) { /* first row: top, cursor, prompt */
        struct nhw *w = &wins[popup];

        tadd("%d\t%d\t%s\n", pop_top, pop_cur, w->prompt ? w->prompt : "");
        n = w->nitems ? w->nitems : w->nlines;
        for (i = 0; i < n; i++)
            if (w->nitems) {
                struct mitem *m = &w->items[i];

                tadd("%d\t%c\t%d\t%d\t%s\n", m->tile, m->ch ? m->ch : ' ',
                     m->id.a_void ? m->sel : 2,
                     m->id.a_void ? cidx(m->attr, m->clr) : CLR_YELLOW, m->s);
            } else
                tadd("-1\t \t2\t%d\t%s\n",
                     cidx(w->lines[i].attr, w->lines[i].clr),
                     w->lines[i].s ? w->lines[i].s : "");
    }
    js_text(3, tbuf);
}

/* returns a key, or 0 for a map click (mouse_* set).  From JS: ASCII,
 * 0x101.. arrows/Home/PgUp/End/PgDn, 0x10000|y<<8|x map click (0x8000 =
 * right button), 0x20000|row click on a pop-up row */
static int
getkey(boolean want_mouse)
{
    int k;

    if (perm_dirty && want_mouse && !restoring) /* names are loaded now */
        web_update_inventory();
    redraw();
    for (;;) {
        boolean np = iflags.num_pad;

        if (*kq) {
            k = (unsigned char) kq[0];
            memmove(kq, kq + 1, strlen(kq));
            return k;
        }

        /* at the command prompt: nh_poskey (mouse allowed), no pop-up/prompt */
        if ((k = js_key(0, want_mouse && popup < 0 && !*promptbuf)) < 0) {
            emscripten_sleep(15);
            continue;
        }
        if (k & 0x20000) {
            int i = k & 0xffff;

            if (popup >= 0 && i < wins[popup].nitems
                && wins[popup].items[i].id.a_void) {
                pop_cur = i;
                return POP_CLICK;
            }
            continue;
        }
        if (k & 0x10000) {
            if (!want_mouse || popup >= 0)
                continue;
            mouse_x = k & 0xff, mouse_y = (k >> 8) & 0x7f;
            mouse_btn = (k & 0x8000) ? CLICK_2 : CLICK_1;
            return 0;
        }
        switch (k) {
        case 0x101: return np ? '8' : 'k';
        case 0x102: return np ? '2' : 'j';
        case 0x103: return np ? '4' : 'h';
        case 0x104: return np ? '6' : 'l';
        case 0x105: return np ? '7' : 'y';
        case 0x106: return np ? '9' : 'u';
        case 0x107: return np ? '1' : 'b';
        case 0x108: return np ? '3' : 'n';
        }
        return k;
    }
}

/* ---------- messages ---------- */

static void
add_msg(const char *s)
{
    snprintf(toplines, TBUFSZ, "%s", s); /* tty keeps this; explore reads it */
    /* RVIP: a repeat of the newest message becomes "message (xN)" */
    if (nhist && !strcmp(s, hist_prev)) {
        char fold[BUFSZ + 16];

        snprintf(fold, sizeof fold, "%s (x%d)", s, ++hist_reps);
        free(hist[(nhist - 1) % HIST_MAX]);
        hist[(nhist - 1) % HIST_MAX] = xstrdup(fold);
        js_text(6, fold);
        return;
    }
    snprintf(hist_prev, sizeof hist_prev, "%s", s);
    hist_reps = 1;
    free(hist[nhist % HIST_MAX]);
    hist[nhist % HIST_MAX] = xstrdup(s);
    js_text(4, s);
    nhist++;
}

static void
bail(void)
{
    clearlocks();
    terminate(EXIT_SUCCESS);
}

/* ---------- window procs ---------- */

static void
web_init_nhwindows(int *argc, char **argv)
{
    flags.perm_invent = TRUE;
    iflags.window_inited = TRUE;
    redraw();
}

/* tty_player_selection with the tty screen calls dropped */
static int
web_role_select(char *pbuf, char *plbuf)
{
    int i, n;
    char thisch, lastch = 0;
    char rolenamebuf[QBUFSZ];
    winid win;
    anything any;
    menu_item *selected = 0;

    win = create_nhwindow(NHW_MENU);
    start_menu(win);
    any.a_void = 0;
    for (i = 0; roles[i].name.m; i++) {
        if (ok_role(i, flags.initrace, flags.initgend, flags.initalign)) {
            any.a_int = i + 1;
            thisch = lowc(roles[i].name.m[0]);
            if (thisch == lastch)
                thisch = highc(thisch);
            if (flags.initgend != ROLE_NONE && flags.initgend != ROLE_RANDOM) {
                if (flags.initgend == 1 && roles[i].name.f)
                    Strcpy(rolenamebuf, roles[i].name.f);
                else
                    Strcpy(rolenamebuf, roles[i].name.m);
            } else if (roles[i].name.f) {
                Strcpy(rolenamebuf, roles[i].name.m);
                Strcat(rolenamebuf, "/");
                Strcat(rolenamebuf, roles[i].name.f);
            } else
                Strcpy(rolenamebuf, roles[i].name.m);
            add_menu(win, NO_GLYPH, &any, thisch, 0, ATR_NONE,
                     an(rolenamebuf), MENU_UNSELECTED);
            lastch = thisch;
        }
    }
    any.a_int = pick_role(flags.initrace, flags.initgend, flags.initalign,
                          PICK_RANDOM) + 1;
    if (any.a_int == 0)
        any.a_int = randrole() + 1;
    add_menu(win, NO_GLYPH, &any, '*', 0, ATR_NONE, "Random", MENU_UNSELECTED);
    any.a_int = i + 1;
    add_menu(win, NO_GLYPH, &any, 'q', 0, ATR_NONE, "Quit", MENU_UNSELECTED);
    Sprintf(pbuf, "Pick a role for your %s", plbuf);
    end_menu(win, pbuf);
    n = select_menu(win, PICK_ONE, &selected);
    destroy_nhwindow(win);
    if (n != 1 || selected[0].item.a_int == any.a_int) {
        free((genericptr_t) selected);
        return -1;
    }
    flags.initrole = selected[0].item.a_int - 1;
    free((genericptr_t) selected);
    return flags.initrole;
}

static int
web_race_select(char *pbuf, char *plbuf)
{
    int i, k, n;
    char thisch, lastch = 0;
    winid win;
    anything any;
    menu_item *selected = 0;

    n = 0, k = 0;
    for (i = 0; races[i].noun; i++)
        if (ok_race(flags.initrole, i, flags.initgend, flags.initalign))
            n++, k = i;
    if (n == 0)
        for (i = 0; races[i].noun; i++)
            if (validrace(flags.initrole, i))
                n++, k = i;
    if (n > 1) {
        win = create_nhwindow(NHW_MENU);
        start_menu(win);
        any.a_void = 0;
        for (i = 0; races[i].noun; i++)
            if (ok_race(flags.initrole, i, flags.initgend, flags.initalign)) {
                any.a_int = i + 1;
                thisch = lowc(races[i].noun[0]);
                if (thisch == lastch)
                    thisch = highc(thisch);
                add_menu(win, NO_GLYPH, &any, thisch, 0, ATR_NONE,
                         races[i].noun, MENU_UNSELECTED);
                lastch = thisch;
            }
        any.a_int = pick_race(flags.initrole, flags.initgend, flags.initalign,
                              PICK_RANDOM) + 1;
        if (any.a_int == 0)
            any.a_int = randrace(flags.initrole) + 1;
        add_menu(win, NO_GLYPH, &any, '*', 0, ATR_NONE, "Random",
                 MENU_UNSELECTED);
        any.a_int = i + 1;
        add_menu(win, NO_GLYPH, &any, 'q', 0, ATR_NONE, "Quit",
                 MENU_UNSELECTED);
        Sprintf(pbuf, "Pick the race of your %s", plbuf);
        end_menu(win, pbuf);
        n = select_menu(win, PICK_ONE, &selected);
        destroy_nhwindow(win);
        if (n != 1 || selected[0].item.a_int == any.a_int)
            return -1;
        k = selected[0].item.a_int - 1;
        free((genericptr_t) selected);
    }
    flags.initrace = k;
    return k;
}

/* gender (what == 0) or alignment (1) menu; -1 = quit */
static int
web_pick_ga(int what, char *pbuf, char *plbuf)
{
    int i, k = 0, n = 0, cnt = what ? ROLE_ALIGNS : ROLE_GENDERS;
    winid win;
    anything any;
    menu_item *selected = 0;

    for (i = 0; i < cnt; i++)
        if (what ? ok_align(flags.initrole, flags.initrace, flags.initgend, i)
                 : ok_gend(flags.initrole, flags.initrace, i, flags.initalign))
            n++, k = i;
    if (n == 0)
        for (i = 0; i < cnt; i++)
            if (what ? validalign(flags.initrole, flags.initrace, i)
                     : validgend(flags.initrole, flags.initrace, i))
                n++, k = i;
    if (n <= 1)
        return k;
    win = create_nhwindow(NHW_MENU);
    start_menu(win);
    any.a_void = 0;
    for (i = 0; i < cnt; i++)
        if (what ? ok_align(flags.initrole, flags.initrace, flags.initgend, i)
                 : ok_gend(flags.initrole, flags.initrace, i, flags.initalign)) {
            const char *adj = what ? aligns[i].adj : genders[i].adj;

            any.a_int = i + 1;
            add_menu(win, NO_GLYPH, &any, adj[0], 0, ATR_NONE, adj,
                     MENU_UNSELECTED);
        }
    any.a_int = (what ? pick_align(flags.initrole, flags.initrace,
                                   flags.initgend, PICK_RANDOM)
                      : pick_gend(flags.initrole, flags.initrace,
                                  flags.initalign, PICK_RANDOM)) + 1;
    if (any.a_int == 0)
        any.a_int = (what ? randalign(flags.initrole, flags.initrace)
                          : randgend(flags.initrole, flags.initrace)) + 1;
    add_menu(win, NO_GLYPH, &any, '*', 0, ATR_NONE, "Random", MENU_UNSELECTED);
    any.a_int = cnt + 1;
    add_menu(win, NO_GLYPH, &any, 'q', 0, ATR_NONE, "Quit", MENU_UNSELECTED);
    Sprintf(pbuf, "Pick the %s of your %s", what ? "alignment" : "gender",
            plbuf);
    end_menu(win, pbuf);
    n = select_menu(win, PICK_ONE, &selected);
    destroy_nhwindow(win);
    if (n != 1 || selected[0].item.a_int == any.a_int)
        return -1;
    k = selected[0].item.a_int - 1;
    free((genericptr_t) selected);
    return k;
}

static void
web_player_selection(void)
{
    int k;
    char pick4u = 'n';
    char pbuf[QBUFSZ], plbuf[QBUFSZ];

    rigid_role_checks();
    if (!flags.randomall
        && (flags.initrole == ROLE_NONE || flags.initrace == ROLE_NONE
            || flags.initgend == ROLE_NONE || flags.initalign == ROLE_NONE)) {
        char *prompt = build_plselection_prompt(pbuf, QBUFSZ, flags.initrole,
                                                flags.initrace, flags.initgend,
                                                flags.initalign);

        snprintf(promptbuf, sizeof promptbuf, "%s ", prompt);
        do {
            pick4u = lowc(getkey(FALSE));
            if (index(quitchars, pick4u))
                pick4u = 'y';
        } while (!index(ynqchars, pick4u));
        *promptbuf = 0;
        if (pick4u != 'y' && pick4u != 'n')
            bail();
    }
    (void) root_plselection_prompt(plbuf, QBUFSZ - 1, flags.initrole,
                                   flags.initrace, flags.initgend,
                                   flags.initalign);
    if (flags.initrole < 0) {
        if (pick4u == 'y' || flags.initrole == ROLE_RANDOM || flags.randomall) {
            flags.initrole = pick_role(flags.initrace, flags.initgend,
                                       flags.initalign, PICK_RANDOM);
            if (flags.initrole < 0)
                flags.initrole = randrole();
        } else if (web_role_select(pbuf, plbuf) < 0)
            bail();
        (void) root_plselection_prompt(plbuf, QBUFSZ - 1, flags.initrole,
                                       flags.initrace, flags.initgend,
                                       flags.initalign);
    }
    if (flags.initrace < 0 || !validrace(flags.initrole, flags.initrace)) {
        if (pick4u == 'y' || flags.initrace == ROLE_RANDOM || flags.randomall) {
            flags.initrace = pick_race(flags.initrole, flags.initgend,
                                       flags.initalign, PICK_RANDOM);
            if (flags.initrace < 0)
                flags.initrace = randrace(flags.initrole);
        } else if (web_race_select(pbuf, plbuf) < 0)
            bail();
        (void) root_plselection_prompt(plbuf, QBUFSZ - 1, flags.initrole,
                                       flags.initrace, flags.initgend,
                                       flags.initalign);
    }
    if (flags.initgend < 0
        || !validgend(flags.initrole, flags.initrace, flags.initgend)) {
        if (pick4u == 'y' || flags.initgend == ROLE_RANDOM || flags.randomall) {
            flags.initgend = pick_gend(flags.initrole, flags.initrace,
                                       flags.initalign, PICK_RANDOM);
            if (flags.initgend < 0)
                flags.initgend = randgend(flags.initrole, flags.initrace);
        } else {
            if ((k = web_pick_ga(0, pbuf, plbuf)) < 0)
                bail();
            flags.initgend = k;
        }
        (void) root_plselection_prompt(plbuf, QBUFSZ - 1, flags.initrole,
                                       flags.initrace, flags.initgend,
                                       flags.initalign);
    }
    if (flags.initalign < 0
        || !validalign(flags.initrole, flags.initrace, flags.initalign)) {
        if (pick4u == 'y' || flags.initalign == ROLE_RANDOM || flags.randomall) {
            flags.initalign = pick_align(flags.initrole, flags.initrace,
                                         flags.initgend, PICK_RANDOM);
            if (flags.initalign < 0)
                flags.initalign = randalign(flags.initrole, flags.initrace);
        } else {
            if ((k = web_pick_ga(1, pbuf, plbuf)) < 0)
                bail();
            flags.initalign = k;
        }
    }
}

static void
web_askname(void)
{
    char buf[BUFSZ];
    int i, n;

    do {
        web_getlin("Who are you?", buf);
        if (*buf == '\033')
            bail();
        for (i = n = 0; buf[i] && n < (int) sizeof plname - 1; i++) {
            char c = buf[i];

            if (c != '-' && c != '@' && (c < 'A' || (c > 'Z' && c < 'a') || c > 'z'))
                c = '_';
            plname[n++] = c;
        }
        plname[n] = 0;
    } while (!n);
}

static void
web_get_nh_event(void)
{
    static double last;

    if (js_key(1, 0) > 0) { /* RVIP: a typed key stops runs and explore */
        rvip_keyhit = TRUE;
        nomul(0);
    }
    if (emscripten_get_now() - last > 50) { /* let the page paint */
        last = emscripten_get_now();
        redraw();
        emscripten_sleep(0);
    }
}

static void
web_exit_nhwindows(const char *str)
{
    if (str && *str)
        add_msg(str);
    if (iflags.window_inited) {
        redraw();
        js_end(); /* sync IndexedDB before the runtime goes */
    }
    iflags.window_inited = FALSE;
}

static void
web_suspend_nhwindows(const char *str)
{
}

static void
web_resume_nhwindows(void)
{
}

static void
freewin(struct nhw *w)
{
    int i;

    for (i = 0; i < w->nlines; i++)
        free(w->lines[i].s);
    for (i = 0; i < w->nitems; i++)
        free(w->items[i].s);
    free(w->lines);
    free(w->items);
    free(w->prompt);
    w->lines = 0, w->items = 0, w->prompt = 0;
    w->nlines = w->nitems = w->cury = 0;
}

static winid
web_create_nhwindow(int type)
{
    winid i;

    for (i = 0; i < MAXWIN; i++)
        if (!wins[i].used) {
            memset(&wins[i], 0, sizeof wins[i]);
            wins[i].used = TRUE;
            wins[i].type = type;
            return i;
        }
    panic("web: out of windows");
    return WIN_ERR;
}

static void
web_clear_nhwindow(winid w)
{
    if (w < 0 || w >= MAXWIN)
        return;
    switch (wins[w].type) {
    case NHW_MESSAGE:
        js_text(5, "");
        break;
    case NHW_MAP:
        memset(mapset, 0, sizeof mapset);
        break;
    default:
        freewin(&wins[w]);
    }
}

/* show a text window floating; wait for dismissal */
static void
show_text(winid w)
{
    int n = wins[w].nlines, rows = POP_ROWS - (wins[w].prompt ? 1 : 0), k;

    popup = w, pop_top = 0, pop_cur = -1, pop_any = FALSE;
    for (;;) {
        k = getkey(FALSE);
        if ((k == ' ' || k == '>' || k == 'j' || k == '2') && pop_top + rows < n)
            pop_top += (k == ' ' || k == '>') ? rows : 1;
        else if ((k == '<' || k == 'k' || k == '8') && pop_top > 0)
            pop_top -= (k == '<') ? min(rows, pop_top) : 1;
        else
            break;
    }
    popup = -1;
}

static void
web_display_nhwindow(winid w, int blocking)
{
    if (w < 0 || w >= MAXWIN)
        return;
    if ((wins[w].type == NHW_TEXT || wins[w].type == NHW_MENU)
        && wins[w].nlines)
        show_text(w);
    else
        redraw();
}

static void
web_destroy_nhwindow(winid w)
{
    if (w < 0 || w >= MAXWIN)
        return;
    freewin(&wins[w]);
    wins[w].used = FALSE;
}

static void
web_curs(winid w, int x, int y)
{
    if (w >= 0 && w < MAXWIN)
        wins[w].cury = y;
}

static void
addline(struct nhw *w, int at, int attr, const char *s)
{
    if (at >= w->nlines) {
        w->lines = (struct line *) realloc(w->lines, (at + 1) * sizeof *w->lines);
        memset(w->lines + w->nlines, 0, (at + 1 - w->nlines) * sizeof *w->lines);
        w->nlines = at + 1;
    }
    free(w->lines[at].s);
    w->lines[at].s = xstrdup(s);
    w->lines[at].attr = attr;
    w->lines[at].clr = NO_COLOR;
}

static void
web_putstr(winid w, int attr, const char *s)
{
    struct nhw *p;

    if (w < 0 || w >= MAXWIN)
        return;
    p = &wins[w];
    switch (p->type) {
    case NHW_MESSAGE:
        add_msg(s);
        redraw();
        break;
    case NHW_STATUS:
        addline(p, p->cury, attr, s);
        break;
    default:
        addline(p, p->nlines, attr, s);
    }
}

static void
web_display_file(const char *fname, int complain)
{
    dlb *f = dlb_fopen(fname, "r");
    char buf[BUFSZ], *cr;
    winid w;

    if (!f) {
        if (complain)
            pline("Cannot open \"%s\".", fname);
        return;
    }
    w = web_create_nhwindow(NHW_TEXT);
    while (dlb_fgets(buf, BUFSZ, f)) {
        if ((cr = index(buf, '\n')) != 0)
            *cr = 0;
        web_putstr(w, 0, buf);
    }
    (void) dlb_fclose(f);
    if (wins[w].nlines)
        show_text(w);
    web_destroy_nhwindow(w);
}

static void
web_start_menu(winid w)
{
    if (w >= 0 && w < MAXWIN)
        freewin(&wins[w]);
}

static void
web_add_menu(winid w, int glyph, const ANY_P *id, int ch, int gch, int attr,
             const char *str, int preselected)
{
    struct nhw *p = &wins[w];
    struct mitem *m;

    p->items = (struct mitem *) realloc(p->items, (p->nitems + 1) * sizeof *p->items);
    m = &p->items[p->nitems++];
    m->id = *id;
    m->ch = ch, m->gch = gch, m->attr = attr;
    m->clr = glyph_color(glyph);
    m->tile = (glyph != NO_GLYPH) ? glyph2tile[glyph] : -1;
    m->s = xstrdup(str);
    m->sel = preselected;
}

static void
web_end_menu(winid w, const char *prompt)
{
    struct nhw *p = &wins[w];
    int i;
    char next = 'a';

    free(p->prompt);
    p->prompt = prompt ? xstrdup(prompt) : 0;
    for (i = 0; i < p->nitems; i++)
        if (p->items[i].gch) /* keyed by gch: no letters */
            next = 0;
    for (i = 0; i < p->nitems && next; i++)
        if (p->items[i].id.a_void && !p->items[i].ch) {
            p->items[i].ch = next;
            next = next == 'z' ? 'A' : next == 'Z' ? 0 : next + 1;
        }
}

static int
web_select_menu(winid w, int how, menu_item **sel)
{
    struct nhw *p = &wins[w];
    int i, k, n, rows;

    *sel = 0;
    if (perm_building) { /* copy into the inventory pane */
        for (i = 0; i < nperm; i++)
            free(perm[i].s);
        free(perm);
        perm = (struct mitem *) malloc((p->nitems + 1) * sizeof *perm);
        nperm = p->nitems;
        for (i = 0; i < nperm; i++) {
            perm[i] = p->items[i];
            perm[i].s = xstrdup(p->items[i].s);
        }
        return 0;
    }
    rows = POP_ROWS - (p->prompt ? 1 : 0);
    popup = w, pop_top = 0, pop_cur = -1, pop_any = how == PICK_ANY;
    for (i = 0; i < p->nitems; i++)
        if (p->items[i].id.a_void) {
            pop_cur = i;
            break;
        }
    for (;;) {
        if (pop_cur >= 0) {
            if (pop_cur < pop_top)
                pop_top = pop_cur;
            if (pop_cur >= pop_top + rows)
                pop_top = pop_cur - rows + 1;
        }
        k = getkey(FALSE);
        rvip_pick = 'm'; /* 'i' list: letter = main action (invent.c) */
        if (rvip_invlist && how == PICK_ONE && k >= 1 && k <= 26
            && !index("\b\t\n\r", k))
            rvip_pick = 'x', k += 'a' - 1; /* Ctrl+letter examines */
        for (i = 0; i < p->nitems && p->items[i].ch != k
                    && !(p->items[i].gch == k && p->items[i].id.a_void); i++)
            ;
        if (i < p->nitems) /* a real accelerator wins over numpad keys */
            ;
        else if (k == '\r' || k == '5')
            k = '\n';
        else if (k == '0' || (k == '.' && how != PICK_ANY))
            k = '\033';
        if (k == '\033') {
            popup = -1;
            return -1;
        }
        if (i == p->nitems) { /* Enter/Space/click: 0 = the action menu */
            rvip_pick = 0;
            if (rvip_invlist && how == PICK_ONE && pop_cur >= 0
                && (k == '+' || k == '-' || k == '*')) {
                rvip_pick = k == '+' ? 'm' : k == '-' ? 'd' : 'x';
                k = '\n';
            }
        }
        if (how == PICK_NONE) {
            if ((k == ' ' || k == '>') && pop_top + rows < p->nitems)
                pop_top += rows;
            else if (k == '<')
                pop_top = max(0, pop_top - rows);
            else
                break;
            continue;
        }
        if (k == '\n' || (k == ' ' && how == PICK_ONE)) {
            if (how == PICK_ONE && pop_cur >= 0) {
                for (i = 0; i < p->nitems; i++)
                    p->items[i].sel = FALSE;
                p->items[pop_cur].sel = TRUE;
            }
            break;
        }
        if (k == 'j' || k == '2' || k == 'k' || k == '8') {
            int d = (k == 'j' || k == '2') ? 1 : -1;

            for (i = pop_cur + d; i >= 0 && i < p->nitems; i += d)
                if (p->items[i].id.a_void) {
                    pop_cur = i;
                    break;
                }
            continue;
        }
        if (k == ' ' && pop_cur >= 0) {
            p->items[pop_cur].sel = !p->items[pop_cur].sel;
            continue;
        }
        if (how == PICK_ANY && (k == ',' || k == '@' || k == '-')) {
            for (i = 0; i < p->nitems; i++)
                if (p->items[i].id.a_void)
                    p->items[i].sel = (k != '-');
            continue;
        }
        for (i = 0; i < p->nitems; i++)
            if (p->items[i].id.a_void
                && (p->items[i].ch == k || p->items[i].gch == k)) {
                if (how == PICK_ONE) {
                    int j;

                    for (j = 0; j < p->nitems; j++)
                        p->items[j].sel = FALSE;
                    p->items[i].sel = TRUE;
                    popup = -1;
                    goto done;
                }
                p->items[i].sel = !p->items[i].sel;
                pop_cur = i;
            }
    }
    popup = -1;
 done:
    n = 0;
    for (i = 0; i < p->nitems; i++)
        if (p->items[i].sel && p->items[i].id.a_void)
            n++;
    if (n) {
        *sel = (menu_item *) alloc(n * sizeof **sel);
        n = 0;
        for (i = 0; i < p->nitems; i++)
            if (p->items[i].sel && p->items[i].id.a_void) {
                (*sel)[n].item = p->items[i].id;
                (*sel)[n].count = -1L;
                n++;
            }
    }
    return n;
}

static void
web_update_inventory(void)
{
    if (!flags.perm_invent || perm_building || !iflags.window_inited
        || WIN_INVEN == WIN_ERR)
        return;
    if (restoring) { /* object names aren't back yet: "(null) wand" */
        perm_dirty = TRUE;
        return;
    }
    perm_dirty = FALSE;
    perm_building = TRUE;
    (void) display_inventory((char *) 0, FALSE);
    perm_building = FALSE;
    redraw();
}

static void
web_mark_synch(void)
{
    redraw();
}

static void
web_wait_synch(void)
{
    redraw();
}

#ifdef CLIPPING
static void
web_cliparound(int x, int y)
{
}
#endif

static void
web_print_glyph(winid w, int x, int y, int glyph)
{
    if (x < 0 || x >= COLNO || y < 0 || y >= ROWNO)
        return;
    mapglyphs[y][x] = glyph;
    mapset[y][x] = TRUE;
}

static void
web_raw_print(const char *s)
{
    fprintf(stderr, "%s\n", s);
    add_msg(s);
}

static int
web_nhgetch(void)
{
    return getkey(FALSE);
}

static int
web_nh_poskey(int *x, int *y, int *mod)
{
    int k = getkey(TRUE);

    if (!k)
        *x = mouse_x, *y = mouse_y, *mod = mouse_btn;
    return k;
}

static void
web_nhbell(void)
{
}

static int
web_doprev_message(void)
{
    winid w = web_create_nhwindow(NHW_TEXT);
    int i;

    for (i = max(0, nhist - HIST_MAX); i < nhist; i++)
        web_putstr(w, 0, hist[i % HIST_MAX]);
    show_text(w);
    web_destroy_nhwindow(w);
    return 0;
}

static char
web_yn_function(const char *q, const char *resp, int def)
{
    char c, lo[BUFSZ], done[BUFSZ * 2 + 8];
    int k;

    if (resp)
        snprintf(promptbuf, sizeof promptbuf, "%s [%s] ", q, resp);
    else
        snprintf(promptbuf, sizeof promptbuf, "%s ", q);
    if (resp && def)
        snprintf(eos(promptbuf), sizeof promptbuf - strlen(promptbuf), "(%c) ", def);
    Strcpy(lo, resp ? resp : "");
    for (;;) {
        k = getkey(FALSE);
        c = (char) k;
        if (!resp)
            break;
        if (k == '\033') {
            c = index(lo, 'q') ? 'q' : index(lo, 'n') ? 'n' : def;
            break;
        }
        if ((k == '\n' || k == '\r' || k == ' ') && def) {
            c = def;
            break;
        }
        if (index(lo, c))
            break;
        if (index(lo, lowc(c))) {
            c = lowc(c);
            break;
        }
    }
    snprintf(done, sizeof done, "%s%c", promptbuf, (c == '\033') ? ' ' : c);
    *promptbuf = 0;
    add_msg(done);
    return c;
}

static void
web_getlin(const char *q, char *buf)
{
    int n = 0, k;

    buf[0] = 0;
    for (;;) {
        snprintf(promptbuf, sizeof promptbuf, "%s %s_", q, buf);
        k = getkey(FALSE);
        if (k == '\033') {
            Strcpy(buf, "\033");
            break;
        }
        if (k == '\n' || k == '\r')
            break;
        if ((k == '\b' || k == 127) && n > 0)
            buf[--n] = 0;
        else if (k >= ' ' && k < 127 && n < BUFSZ - 1)
            buf[n++] = (char) k, buf[n] = 0;
    }
    *promptbuf = 0;
    if (*buf != '\033') {
        char done[BUFSZ * 2 + 2];

        snprintf(done, sizeof done, "%s %s", q, buf);
        add_msg(done);
    }
}

/* "#" then a name (unique prefix is enough); an empty line lists them */
static int
web_get_ext_cmd(void)
{
    char buf[BUFSZ];
    int i, n, hit = -1;

    web_getlin("#", buf);
    (void) mungspaces(buf);
    if (*buf == '\033')
        return -1;
    if (!*buf) {
        winid w = web_create_nhwindow(NHW_MENU);
        anything any;
        menu_item *sel;

        web_start_menu(w);
        for (i = 0; extcmdlist[i].ef_txt; i++) {
            snprintf(buf, sizeof buf, "%-14s %s", extcmdlist[i].ef_txt,
                     extcmdlist[i].ef_desc);
            any.a_int = i + 1;
            web_add_menu(w, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, FALSE);
        }
        web_end_menu(w, "Extended commands");
        n = web_select_menu(w, PICK_ONE, &sel);
        web_destroy_nhwindow(w);
        if (n == 1) {
            hit = sel[0].item.a_int - 1;
            free((genericptr_t) sel);
        }
        return hit;
    }
    for (i = n = 0; extcmdlist[i].ef_txt; i++) {
        if (!strcmpi(buf, extcmdlist[i].ef_txt))
            return i;
        if (!strncmpi(buf, extcmdlist[i].ef_txt, strlen(buf)))
            n++, hit = i;
    }
    if (n == 1)
        return hit;
    pline("%s: %s extended command.", buf, n ? "ambiguous" : "unknown");
    return -1;
}

/* RVIP 3b: Enter = menu of every command, grouped like the '?' key list
   (dat/hh "General" and "Game" commands, key shown) plus every extended
   command.  The choice is queued as keys (kq), so prefixes and prompts
   work as if typed.  Called from rhack() (src/cmd.c). */
void
web_cmdmenu(void)
{
    dlb *f = dlb_fopen(NH_SHELP, "r");
    char buf[BUFSZ], *t;
    winid w = web_create_nhwindow(NHW_MENU);
    anything any;
    menu_item *sel;
    int i, k, on = 0;

    web_start_menu(w);
    while (f && dlb_fgets(buf, BUFSZ, f)) {
        if ((t = index(buf, '\n')) != 0)
            *t = 0;
        (void) tabexpand(buf);
        if (!strncmp(buf, "General", 7))
            on = 1;
        if (!strncmp(buf, "Keyboards", 9)) /* M- keys: listed as #name */
            break;
        if (!on || !*buf || *buf == ' ')
            continue;
        any.a_void = 0;
        if (buf[strlen(buf) - 1] == ':') { /* group heading */
            web_add_menu(w, NO_GLYPH, &any, 0, 0, ATR_BOLD, buf, FALSE);
            continue;
        }
        k = buf[1] == ' ' ? buf[0]
            : (buf[0] == '^' && buf[2] == ' ') ? buf[1] & 0x1f : 0;
        if (!k) /* "n#", "),[,=": not one key */
            continue;
        any.a_int = k;
        web_add_menu(w, NO_GLYPH, &any, 0, k, ATR_NONE, buf, FALSE);
    }
    if (f)
        (void) dlb_fclose(f);
    any.a_void = 0;
    web_add_menu(w, NO_GLYPH, &any, 0, 0, ATR_BOLD,
                 "Extended commands (# name):", FALSE);
    for (i = 0; extcmdlist[i].ef_txt; i++) {
        snprintf(buf, sizeof buf, "#%-14s %s", extcmdlist[i].ef_txt,
                 extcmdlist[i].ef_desc);
        any.a_int = 0x1000 + i;
        web_add_menu(w, NO_GLYPH, &any, 0, 0, ATR_NONE, buf, FALSE);
    }
    web_end_menu(w, "Commands");
    if (web_select_menu(w, PICK_ONE, &sel) > 0) {
        k = sel[0].item.a_int;
        free((genericptr_t) sel);
        if (k >= 0x1000)
            snprintf(kq, sizeof kq, "#%s\n", extcmdlist[k - 0x1000].ef_txt);
        else
            kq[0] = (char) k, kq[1] = 0;
    }
    web_destroy_nhwindow(w);
}

static void
web_number_pad(int state)
{
}

static void
web_delay_output(void)
{
    redraw();
    emscripten_sleep(30);
}

static void
web_start_screen(void)
{
}

static void
web_end_screen(void)
{
}

struct window_procs web_procs = {
    "web",
    WC_COLOR | WC_HILITE_PET | WC_TILED_MAP | WC_PERM_INVENT | WC_MOUSE_SUPPORT,
    0L,
    web_init_nhwindows, web_player_selection, web_askname,
    web_get_nh_event, web_exit_nhwindows, web_suspend_nhwindows,
    web_resume_nhwindows, web_create_nhwindow, web_clear_nhwindow,
    web_display_nhwindow, web_destroy_nhwindow, web_curs, web_putstr,
    web_display_file, web_start_menu, web_add_menu, web_end_menu,
    web_select_menu, genl_message_menu, web_update_inventory,
    web_mark_synch, web_wait_synch,
#ifdef CLIPPING
    web_cliparound,
#endif
#ifdef POSITIONBAR
    0,
#endif
    web_print_glyph, web_raw_print, web_raw_print, web_nhgetch,
    web_nh_poskey, web_nhbell, web_doprev_message, web_yn_function,
    web_getlin, web_get_ext_cmd, web_number_pad, web_delay_output,
    web_start_screen, web_end_screen, genl_outrip, genl_preference_update,
};
