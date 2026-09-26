/* SLASH'EM 0.0.7E7F3  win/web/websound.c  browser sound (RVIP 6b) */
/* 3.4.3 has no sound library, so messages pick the effects, like the
 * USER_SOUNDS message patterns (pmatch) but built in.  C names the sound,
 * web/slashem.js plays sound/<name>.wav via rvip-sound.js if the Sound
 * button is on; town music plays in Minetown's town area and in shops. */

#include <emscripten.h>
#include "hack.h"

EM_JS(void, js_sound, (const char *name), { Module.nh.sound(UTF8ToString(name)); });
EM_JS(void, js_music, (int on), { Module.nh.music(on); });

static const struct { const char *pat, *name; } msgsnd[] = {
    { "You die*", "die" },
    { "You kill *", "kill" }, { "You destroy *", "kill" },
    { "You hit *", "hit" }, { "You smite *", "hit" },
    { "You miss *", "miss" }, { "* misses*", "miss" },
    { "* hits!*", "hurt" }, { "* bites!*", "hurt" }, { "* stings!*", "hurt" },
    { "* butts!*", "hurt" }, { "* kicks!*", "hurt" }, { "* touches you!*", "hurt" },
    { "* claws you!*", "hurt" }, { "* suck you!*", "hurt" },
    { "Welcome to experience level *", "levelup" },
    { "You hear *", "hear" },
    { "*door opens.*", "door" }, { "*door closes.*", "door" },
    { "*gold piece*", "gold" },
};

void
web_sound_msg(const char *msg)
{
    int i;

    for (i = 0; i < SIZE(msgsnd); i++)
        if (pmatch(msgsnd[i].pat, msg)) {
            js_sound(msgsnd[i].name);
            return;
        }
}

/* after every map redraw: level changes and town music */
void
web_sound_where(void)
{
    static int lev = -1, town = -1;
    int l = u.uz.dnum * 100 + u.uz.dlevel, t;

    if (!u.uz.dlevel)
        return;
    if (lev >= 0 && l != lev)
        js_sound("stairs");
    lev = l;
    t = in_town(u.ux, u.uy) || *u.ushops;
    if (t != town)
        js_music(town = t);
}
