#include "boat_title.h"
#include "boat.h"
#include "boat_assets.h"
#include "boat_location.h"
#include "boat_music.h"
#include "boat_net_protocol.h"
#include "boat_replay.h"
#include "boat_skin.h"
#include "boat_ui.h"
#include "boat_version.h"
#include "frame_pacer.h"
#include "raylib.h"
#include <stdio.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// clang-format off
EM_JS(void, refresh_public_servers, (void), { Module['boatServers'].refresh(); })
EM_JS(int, public_server_count, (void), { return Module['boatServers'].count(); })
EM_JS(int, public_server_compatible, (int index, int protocol, int physics),
    { return Module['boatServers'].compatible(index, protocol, physics); })
EM_JS(void, public_server_text, (int index, int field, char *out, int size), {
    const server = Module['boatServers'].server(index);
    const value = field === 3 ? Module['boatServers'].status()
        : server ? [server.name, server.description, server.websocket][field] : "";
    stringToUTF8(value, out, size);
})
// clang-format on
#endif
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
#include "boat_directory.h"
#include "boat_net_enet.h"

static BoatDirectory *directory;
void boat_title_directory_start(void)
{
    directory = boat_directory_create();
    boat_directory_refresh(directory);
}
void boat_title_directory_stop(void)
{
    boat_directory_destroy(directory);
    directory = NULL;
}
static void refresh_public_servers(void) { boat_directory_refresh(directory); }
static int public_server_count(void) { return (int)boat_directory_list(directory)->count; }
static int public_server_compatible(int index, int protocol, int physics)
{
    const BoatDirectoryList *list = boat_directory_list(directory);
    return index >= 0 && (unsigned)index < list->count && list->entries[index].protocol == protocol
        && list->entries[index].physics == physics;
}
static void public_server_text(int index, int field, char *out, int size)
{
    const BoatDirectoryList *list = boat_directory_list(directory);
    const char *value = field == 3 ? boat_directory_status(directory) : "";
    if (field != 3 && index >= 0 && (unsigned)index < list->count) {
        const BoatDirectoryEntry *entry = &list->entries[index];
        value = field == 0 ? entry->name : field == 1 ? entry->description : entry->address;
    }
    snprintf(out, (size_t)size, "%s", value);
}
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { WIDTH = 320, HEIGHT = 200, ROW_HEIGHT = 16 };

typedef enum { ROW_SINGLE, ROW_MULTI, ROW_SETTINGS, ROW_CREDITS, ROW_EXIT, MAIN_ROWS } MainRow;
#if defined(__EMSCRIPTEN__)
typedef enum {
    ROW_PUBLIC_SERVER,
    ROW_ADDRESS,
    ROW_CONNECT,
    ROW_BACK,
    JOIN_ROWS,
    ROW_HOST_ROOM,
    ROW_LAN
} JoinRow;
#else
typedef enum {
    ROW_PUBLIC_SERVER,
    ROW_HOST_ROOM,
    ROW_LAN,
    ROW_ADDRESS,
    ROW_CONNECT,
    ROW_BACK,
    JOIN_ROWS
} JoinRow;
#endif
typedef enum { ROW_MODE, ROW_PLACE, ROW_WATER, ROW_START, ROW_HOST_BACK, HOST_ROWS } HostRow;
typedef enum {
    ROW_NAME,
    ROW_BOAT,
    ROW_MOUSE,
    ROW_MUSIC,
    ROW_EDGES,
    ROW_DONE,
    SETTINGS_ROWS
} SettingsRow;
typedef enum {
    ROW_SOLO_PLACE,
    ROW_SOLO_WATER,
    ROW_SOLO_EDGES,
    ROW_SOLO_START,
    ROW_SOLO_BACK,
    SOLO_ROWS
} SoloRow;
typedef enum {
    PANEL_MAIN,
    PANEL_SINGLE,
    PANEL_JOIN,
    PANEL_HOST,
    PANEL_SETTINGS,
    PANEL_CREDITS
} Panel;

static const char *const host_modes[BOAT_TITLE_HOST_MODES] = { "free", "deathmatch", "race" };

const char *boat_title_host_mode(unsigned mode)
{
    return host_modes[mode < BOAT_TITLE_HOST_MODES ? mode : 0];
}

// Race rooms are always on the river.
static unsigned host_location(const BoatTitleSettings *s)
{
    return s->host_mode == BOAT_TITLE_HOST_RACE ? BOAT_LOCATION_RIVER : s->host_location;
}

typedef struct {
    Texture2D backdrop, wordmark;
    Font ui, small; // m5x7 labels/values; m3x6 hints/footer, baked at their native 16 px em.
    RenderTexture2D target;
    Panel panel; // The multiplayer or settings panel replaces the main rows.
    int selected;
    char *editing; // The text field being typed into, if any.
    char before[64]; // Its value when editing started, restored by Escape.
    double time, credits_started;
    // LAN search while the multiplayer panel is open (native builds).
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    BoatNetLan *lan;
#endif
    bool searching, moved; // MOVED: the player has picked a row since opening the panel.
    BoatNetLanList found;
    unsigned shown; // The found room the LAN row offers.
    unsigned public_shown;
    double queried;
    FramePacer pacer;
} Title;

static void stop_search(Title *t)
{
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    if (t->searching) {
        boat_net_lan_close(t->lan);
        boat_net_enet_cleanup();
    }
    t->lan = NULL;
#endif
    t->searching = false;
    t->found.count = 0;
}

static void start_search(Title *t)
{
    t->found.count = t->shown = 0;
    t->moved = false;
    t->queried = -1;
    refresh_public_servers();
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    char error[160];
    if (!t->searching && boat_net_enet_startup()) {
        t->lan = boat_net_lan_search(error, sizeof(error));
        t->searching = t->lan != NULL;
        if (!t->searching)
            boat_net_enet_cleanup();
    }
#endif
}

// Query about once a second and forget rooms silent for three.
static void search(Title *t)
{
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
    boat_directory_poll(directory);
    if (t->searching && (t->queried < 0 || t->time - t->queried >= 1)) {
        boat_net_lan_query(t->lan, BOAT_NET_DISCOVERY_PORT);
        t->queried = t->time;
    }
    BoatNetLanRoom room;
    while (t->searching && boat_net_lan_receive(t->lan, &room))
        boat_net_lan_list_note(&t->found, &room, t->time);
    boat_net_lan_list_expire(&t->found, t->time, 3.5);
    if (t->shown >= t->found.count)
        t->shown = 0;
#endif
    unsigned count = (unsigned)public_server_count();
    if (t->public_shown >= count)
        t->public_shown = 0;
}

static Color rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return (Color) { r, g, b, 255 };
}

// The TTF em square is 16 px; visible capitals are seven/six pixels high. Draw the
// native atlas 1:1 and align its capital top, rather than shrinking it to a nominal 7 px.
static int text_width(Font font, const char *text)
{
    return (int)roundf(MeasureTextEx(font, text, 16, 0).x);
}

static void draw_text(Font font, const char *text, int x, int y, Color ink)
{
    int top = font.glyphs[GetGlyphIndex(font, 'A')].offsetY;
    DrawTextEx(font, text, (Vector2) { (float)x, (float)(y - top) }, 16, 0, ink);
}

// Native bitmap wordmark: common cap height, stroke width and optical spacing.
static void draw_logo(const Title *t)
{
    DrawTexture(t->wordmark, (WIDTH - t->wordmark.width) / 2, 20, WHITE);
}

static void crosshair(int x, int y, Color ink)
{
    DrawLine(x - 4, y, x + 5, y, ink);
    DrawLine(x, y - 4, x, y + 5, ink);
    DrawPixel(x, y, (PROTEUS_WARM_MENU ? boat_ui_palette().text : rgb(188, 255, 243)));
}

static int row_count(const Title *t)
{
    return t->panel == PANEL_CREDITS ? 1
        : t->panel == PANEL_SINGLE   ? SOLO_ROWS
        : t->panel == PANEL_JOIN     ? JOIN_ROWS
        : t->panel == PANEL_HOST     ? HOST_ROWS
        : t->panel == PANEL_SETTINGS ? SETTINGS_ROWS
                                     : MAIN_ROWS;
}

static Rectangle row_bounds(const Title *t, int row)
{
    if (t->panel == PANEL_CREDITS)
        return (Rectangle) { 127, 171, 66, 14 };
    if (t->panel == PANEL_MAIN)
        return (Rectangle) { 70, (float)(109 + row * 14), 118, 13 };
    int top = 86 + (5 - row_count(t)) * ROW_HEIGHT / 2; // Centred on five rows' space.
    return (Rectangle) { 45, (float)(top + row * ROW_HEIGHT), 230, ROW_HEIGHT - 2 };
}

static void upper(char *out, size_t size, const char *text)
{
    size_t i = 0;
    for (; text[i] && i + 1 < size; ++i)
        out[i] = (char)toupper((unsigned char)text[i]);
    out[i] = 0;
}

// The tail of a long value, so the cursor end stays visible.
static const char *fit(Font font, const char *text, int width)
{
    while (*text && text_width(font, text) > width)
        ++text;
    return text;
}

static void row_text(const Title *t, const BoatTitleSettings *s, int row, const char **label,
    char *value, size_t size)
{
    *label = "";
    value[0] = 0;
    if (t->panel == PANEL_SINGLE) {
        static const char *labels[SOLO_ROWS] = { "PLACE", "WAVES", "SMOOTHING", "START", "BACK" };
        *label = labels[row];
        if (row == ROW_SOLO_PLACE)
            snprintf(value, size, "< %s >",
                s->location == BOAT_LOCATION_COUNT       ? "BASIN TRIAL"
                    : s->location == BOAT_LOCATION_OUTER ? "OUTER RIVER"
                    : s->location == BOAT_LOCATION_RIVER ? "RIVER"
                                                         : "BASIN");
        else if (row == ROW_SOLO_WATER)
            snprintf(value, size, "< %s >", s->waves ? "ON" : "OFF");
        else if (row == ROW_SOLO_EDGES)
            snprintf(value, size, "< %s >", s->smooth_edges ? "ON" : "OFF");
        return;
    }
    if (t->panel == PANEL_SETTINGS) {
        static const char *labels[SETTINGS_ROWS]
            = { "NAME", "BOAT", "STEERING", "SOUNDTRACK", "SMOOTHING", "BACK" };
        *label = labels[row];
        if (row == ROW_NAME)
            snprintf(value, size, "%s", s->name);
        else if (row == ROW_BOAT) {
            char country[32];
            upper(country, sizeof(country), boat_skin_name(s->skin));
            snprintf(value, size, "< %s >", country);
        } else if (row == ROW_MOUSE)
            snprintf(value, size, "< %s >", s->mouse_steering ? "MOUSE" : "KEYBOARD");
        else if (row == ROW_MUSIC)
            snprintf(value, size, "%s", boat_music(s->soundtrack)->label);
        else if (row == ROW_EDGES)
            snprintf(value, size, "< %s >", s->smooth_edges ? "ON" : "OFF");
        return;
    }
    if (t->panel == PANEL_HOST) {
        static const char *labels[HOST_ROWS] = { "MODE", "PLACE", "WATER", "START ROOM", "BACK" };
        static const char *modes[BOAT_TITLE_HOST_MODES]
            = { "FREE BOATING", "DEATHMATCH", "RIVER RACE" };
        *label = labels[row];
        char place[32];
        upper(place, sizeof(place), boat_location(host_location(s))->name);
        if (row == ROW_MODE)
            snprintf(value, size, "< %s >", modes[s->host_mode]);
        else if (row == ROW_PLACE)
            snprintf(value, size, s->host_mode == BOAT_TITLE_HOST_RACE ? "%s" : "< %s >", place);
        else if (row == ROW_WATER)
            snprintf(value, size, "< %s >", s->host_waves ? "WAVES" : "CALM");
        return;
    }
    if (t->panel == PANEL_JOIN) {
        static const char *labels[JOIN_ROWS]
#if defined(__EMSCRIPTEN__)
            = { "SERVER", "URL", "CONNECT", "BACK" };
#else
            = { "SERVER", "HOST A ROOM", "LAN", "ADDRESS", "CONNECT", "BACK" };
#endif
        *label = labels[row];
        if (row == ROW_PUBLIC_SERVER) {
            if (public_server_count()) {
                char name[25];
                public_server_text((int)t->public_shown, 0, name, sizeof(name));
                upper(value, size, name);
                if (public_server_count() > 1) {
                    static char counted[32];
                    snprintf(counted, sizeof(counted), "SERVER %u/%d", t->public_shown + 1,
                        public_server_count());
                    *label = counted;
                }
            } else
                snprintf(value, size, "UNAVAILABLE");
            return;
        }
        if (row == ROW_ADDRESS)
            snprintf(value, size, "%s", s->address);
        if (row != ROW_LAN)
            return;
        if (!t->searching) {
            snprintf(value, size, "UNAVAILABLE");
        } else if (!t->found.count) {
            int dots = (int)fmod(t->time * 2, 4);
            snprintf(value, size, "SEARCHING%.*s", dots, "...");
        }
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
        else {
            const BoatNetLanRoom *room = &t->found.rooms[t->shown];
            char host[32];
            boat_net_lan_endpoint(room, host, sizeof(host));
            if (room->name[0])
                snprintf(host, sizeof(host), "%s", room->name);
            char upper_host[32];
            upper(upper_host, sizeof(upper_host), host);
            snprintf(value, size, "%s %u/%u", upper_host, room->players, room->max_players);
            if (t->found.count > 1) {
                static char counted[32];
                snprintf(counted, sizeof(counted), "LAN %u/%u", t->shown + 1, t->found.count);
                *label = counted;
            }
        }
#endif
        return;
    }
    static const char *labels[MAIN_ROWS]
        = { "SINGLE PLAYER", "MULTIPLAYER", "SETTINGS", "CREDITS", "EXIT" };
    *label = labels[row];
#if defined(__EMSCRIPTEN__)
    if (row == ROW_EXIT)
        *label = "BACK TO LAUNCHER";
#endif
}

// What the LAN row's room is, or how to make one appear.
static const char *lan_hint(const Title *t, char *out, size_t size)
{
    if (!t->searching)
#if defined(__EMSCRIPTEN__)
        return "JOIN A WEBRTC ROOM: WS:// OR WSS://";
#else
        return "NO LAN SEARCH: TYPE AN ADDRESS BELOW";
#endif
    if (!t->found.count)
        return "NO ROOMS FOUND ON THIS NETWORK YET";
#if !defined(PROTEUS_BOAT_NET) || defined(__EMSCRIPTEN__)
    (void)out;
    (void)size;
    return NULL;
#else
    const BoatNetLanRoom *room = &t->found.rooms[t->shown];
    char endpoint[32];
    boat_net_lan_endpoint(room, endpoint, sizeof(endpoint));
    const char *what = room->mode == BOAT_NET_MODE_DEATHMATCH ? "DEATHMATCH"
        : room->mode == BOAT_NET_MODE_RACE                    ? "RIVER RACE"
                                                              : "FREE BOATING";
    if (!boat_net_lan_room_compatible(room))
        what = "OTHER GAME VERSION";
    else if (room->token)
        what = "NEEDS A TOKEN";
    snprintf(out, size, "%s AT %s%s", what, endpoint, t->found.count > 1 ? "  < >" : "");
    return out;
#endif
}

static void draw_menu(const Title *t, const BoatTitleSettings *s, const char *status)
{
    Rectangle first = row_bounds(t, 0), last = row_bounds(t, row_count(t) - 1);
    int top = (int)first.y - 5, height = (int)(last.y + last.height - first.y) + 10;
    bool main = t->panel == PANEL_MAIN;
    int left = main ? 66 : 37, width = main ? 126 : 246;
    Color cyan = boat_ui_palette().edge, dim = boat_ui_palette().border;
    DrawRectangle(left, top, width, height, boat_ui_palette().panel);
    // Fine rules with brighter open corner brackets.
    DrawRectangleLines(left, top, width, height, dim);
    for (int side = 0; side < 2; ++side)
        for (int end = 0; end < 2; ++end) {
            int x = left + side * (width - 1), y = top + end * (height - 1);
            DrawRectangle(side ? x - 4 : x, y, 5, 1, cyan);
            DrawRectangle(x, end ? y - 4 : y, 1, 5, cyan);
        }
    if (main) {
        DrawLine(left - 6, top - 3, left - 6, top + height + 4, dim);
        DrawLine(left - 9, top + height + 3, left + width + 9, top + height + 3, dim);
        for (int y = top + 7; y < top + height; y += 7)
            DrawLine(left - 7, y, left - 4, y, dim);
        crosshair(left - 6, top - 3, cyan);
        crosshair(left + width + 6, top + height + 3, cyan);
    }
    for (int i = 0; i < row_count(t); ++i) {
        Rectangle row = row_bounds(t, i);
        bool active = i == t->selected;
        int label_x = main ? 84 : 72;
        if (main)
            DrawLine(71, (int)row.y + 13, 187, (int)row.y + 13, dim);
        if (active) {
            Rectangle highlight = row;
            highlight.x = row.x;
            highlight.width = row.x + row.width - highlight.x;
            DrawRectangleRec(highlight, boat_ui_palette().selected);
            int x = main ? 74 : 51, y = (int)row.y + 4;
            for (int j = 0; j < 3; ++j)
                DrawLine(x + j, y + j, x + j, y + 5 - j, boat_ui_palette().accent);
        }
        Color ink = active ? boat_ui_palette().selected_text : boat_ui_palette().text;
        const char *label;
        char value[96];
        row_text(t, s, i, &label, value, sizeof(value));
        // The web's longer BACK TO LAUNCHER label also fits the compact main panel.
        draw_text(t->ui, label, label_x, (int)row.y + 3, ink);
        if (value[0]) {
            int value_x = t->panel == PANEL_JOIN && i == ROW_ADDRESS ? 122
                : t->panel == PANEL_SETTINGS && i == ROW_MUSIC       ? 154
                                                                     : 142;
            const char *shown = fit(t->ui, value, 268 - value_x);
            draw_text(t->ui, shown, value_x, (int)row.y + 3, ink);
            bool typing = t->editing && active;
            if (typing && fmod(t->time, .8) < .5)
                DrawRectangle(value_x + 1 + text_width(t->ui, shown), (int)row.y + 3, 3, 7, ink);
        }
    }
    const char *hint = status;
    if (!hint && s->message[0])
        hint = s->message;
    if (!hint && t->editing)
        hint = "TYPE, THEN ENTER / ESC: CANCEL";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_BOAT)
        hint = s->skin == BOAT_SKIN_EXPLORER ? "FASTER ENGINE WHEN FREE RIDING SOLO"
                                             : "SINGLE PLAYER; ROOMS COLOUR BOATS BY SLOT";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_MOUSE)
        hint = "REMEMBERED; ESC MENU OR M SWITCHES IN GAME";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_MUSIC)
        hint = "LEFT / RIGHT OR CLICK: CHOOSE MUSIC";
    char public_hint[96];
    if (!hint && t->panel == PANEL_JOIN && t->selected == ROW_PUBLIC_SERVER) {
        if (!public_server_count())
            public_server_text(0, 3, public_hint, sizeof(public_hint));
        else if (!public_server_compatible(
                     (int)t->public_shown, BOAT_NET_VERSION, BOAT_PHYSICS_REVISION))
            snprintf(public_hint, sizeof(public_hint), "OTHER GAME VERSION; USE MATCHING CLIENT");
        else {
            char description[65];
            public_server_text((int)t->public_shown, 1, description, sizeof(description));
            upper(public_hint, sizeof(public_hint), description);
        }
        hint = public_hint;
    }
#if defined(__EMSCRIPTEN__)
    if (!hint && t->panel == PANEL_JOIN)
        hint = "JOIN A WEBRTC ROOM: WS:// OR WSS://";
#endif
    char lan[96];
    if (!hint && t->panel == PANEL_JOIN && t->selected == ROW_LAN)
        hint = lan_hint(t, lan, sizeof(lan));
    if (!hint && t->panel == PANEL_JOIN && t->selected == ROW_HOST_ROOM)
        hint = "RUN A ROOM ON THIS COMPUTER AND JOIN IT";
    if (!hint && t->panel == PANEL_HOST) {
        static const char *modes[BOAT_TITLE_HOST_MODES] = { "SHARED FREE BOATING, NO SCORES",
            "TIMED ROUNDS: CAPSIZE THE OTHERS", "SPRINT DOWN THE RIVER FROM A GRID" };
        if (t->selected == ROW_MODE)
            hint = modes[s->host_mode];
        else if (t->selected == ROW_PLACE && s->host_mode == BOAT_TITLE_HOST_RACE)
            hint = "RIVER RACES ALWAYS RUN ON THE RIVER";
        else if (t->selected == ROW_START) {
            snprintf(
                lan, sizeof(lan), "OPEN TO YOUR LAN ON UDP %d; LEAVING CLOSES IT", BOAT_NET_PORT);
            hint = lan;
        }
    }
    if (hint) {
        const char *shown = fit(t->small, hint, WIDTH - 12);
        int x = (WIDTH - text_width(t->small, shown)) / 2;
        int y = main ? top - 15 : top + height + 3;
        DrawRectangle(x - 3, y - 1, text_width(t->small, shown) + 6, 9, boat_ui_palette().shadow);
        draw_text(t->small, shown, x, y + 1, status ? rgb(255, 236, 175) : boat_ui_palette().hint);
    }
}

static void draw_footer_text(Font font, const char *label, int x)
{
    draw_text(font, label, x + 1, 191, (Color) { 10, 45, 65, 130 });
    draw_text(
        font, label, x, 190, (PROTEUS_WARM_MENU ? boat_ui_palette().edge : rgb(162, 219, 217)));
}

// Coverage antialiasing for the credits water only. Keep fractional positions until
// rasterization so slow movement fades between pixels instead of jumping whole rows.
static void credits_water_line(Vector2 a, Vector2 b, Color ink)
{
    bool steep = fabsf(b.y - a.y) > fabsf(b.x - a.x);
    if (steep) {
        a = (Vector2) { a.y, a.x };
        b = (Vector2) { b.y, b.x };
    }
    if (a.x > b.x) {
        Vector2 swap = a;
        a = b;
        b = swap;
    }
    if (b.x <= a.x)
        return;
    float slope = (b.y - a.y) / (b.x - a.x);
    int first = (int)fmaxf(0, ceilf(a.x));
    int limit = (int)fminf(steep ? HEIGHT : WIDTH, ceilf(b.x));
    for (int x = first; x < limit; ++x) {
        float y = a.y + (x - a.x) * slope;
        int base = (int)floorf(y);
        float fraction = y - base;
        Color lower = ink, upper = ink;
        lower.a = (unsigned char)roundf(ink.a * (1 - fraction));
        upper.a = (unsigned char)roundf(ink.a * fraction);
        DrawPixel(steep ? base : x, steep ? x : base, lower);
        DrawPixel(steep ? base + 1 : x, steep ? x : base + 1, upper);
    }
}

// Presentation-only animation: no session or random state.
static void draw_credits(const Title *t)
{
    BoatUiPalette p = boat_ui_palette();
    double elapsed = t->time - t->credits_started;
    float phase = (float)elapsed;
    DrawRectangle(0, 58, WIDTH, 129, p.shadow);
    // A receding water grid, with travelling crests instead of a space/star effect.
    for (int z = 0; z < 13; ++z) {
        float depth = (float)fmod(z + elapsed * .6, 13.0) / 13;
        float y = 73 + depth * depth * 111;
        Color ink = Fade(p.border, .25f + depth * .3f);
        for (int x = 0; x < WIDTH; x += 4) {
            float dy = sinf(x * .035f + phase * 1.8f + z) * depth * 3;
            float next = sinf((x + 4) * .035f + phase * 1.8f + z) * depth * 3;
            credits_water_line(
                (Vector2) { (float)x, y + dy }, (Vector2) { (float)x + 4, y + next }, ink);
        }
    }
    for (int x = -320; x <= 640; x += 64)
        credits_water_line((Vector2) { 160 + (x - 160) / 8.0f, 73 }, (Vector2) { (float)x, 185 },
            Fade(p.border, .2f));
    const char *heading = "CREDITS";
    DrawRectangle(119, 49, 82, 14, p.panel);
    draw_text(t->ui, heading, (WIDTH - text_width(t->ui, heading)) / 2, 52, p.selected);
    // A movie-style roll: one font, centred lines and a quiet gap between sections.
    static const struct {
        const char *text;
        bool heading;
    } lines[] = {
        { "THANKS TO", true },
        { "erik", false },
        { "bjoto", false },
        { "w", false },
        { "", false },
        { "", false },
        { "FONTS", true },
        { "Daniel Linssen", false },
        { "m5x7 / m3x6", false },
        { "", false },
        { "", false },
        { "MAP DATA", true },
        { "OpenStreetMap contributors", false },
        { "ODbL 1.0", false },
        { "", false },
        { "", false },
        { "LIBRARIES", true },
        { "raylib", false },
        { "ENet", false },
        { "libcurl", false },
        { "", false },
        { "", false },
        { "A PROTEUS GAME", true },
    };
    int count = (int)(sizeof(lines) / sizeof(lines[0]));
    int period = count * 14 + 112;
    int origin = 98 - (int)fmod(elapsed * 10, period);
    DrawLine(24, 64, 296, 64, p.border);
    DrawLine(24, 165, 296, 165, p.border);
    // Scissor coordinates are in the title's native 320x200 render target.
    BeginScissorMode(24, 66, 272, 97);
    for (int copy = 0; copy < 2; ++copy)
        for (int i = 0; i < count; ++i) {
            int y = origin + copy * period + i * 14;
            if (y < 59 || y >= 163 || !lines[i].text[0])
                continue;
            int x = (WIDTH - text_width(t->ui, lines[i].text)) / 2;
            draw_text(t->ui, lines[i].text, x + 1, y + 1, p.shadow);
            draw_text(t->ui, lines[i].text, x, y, lines[i].heading ? p.selected : p.text);
        }
    EndScissorMode();
    DrawRectangleRec(row_bounds(t, 0), p.selected);
    draw_text(t->ui, "< BACK", 142, 175, p.selected_text);
}

static void draw_screen(const Title *t, const BoatTitleSettings *s, const char *status)
{
    BeginTextureMode(t->target);
    DrawTexture(t->backdrop, 0, 0, WHITE);
    draw_logo(t);
    crosshair(22, 32, (PROTEUS_WARM_MENU ? boat_ui_palette().border : rgb(118, 203, 211)));
    crosshair(297, 32, (PROTEUS_WARM_MENU ? boat_ui_palette().border : rgb(118, 203, 211)));
    if (t->panel == PANEL_CREDITS)
        draw_credits(t);
    else
        draw_menu(t, s, status);
    draw_footer_text(t->panel == PANEL_CREDITS ? t->ui : t->small, PROTEUS_VERSION, 8);
    EndTextureMode();
}

static int scale(void)
{
    int x = GetScreenWidth() / WIDTH, y = GetScreenHeight() / HEIGHT;
    return x < y ? (x < 1 ? 1 : x) : (y < 1 ? 1 : y);
}

static void present(Title *t)
{
    int k = scale();
    Rectangle destination = { (float)((GetScreenWidth() - WIDTH * k) / 2),
        (float)((GetScreenHeight() - HEIGHT * k) / 2), (float)(WIDTH * k), (float)(HEIGHT * k) };
    BeginDrawing();
    ClearBackground(boat_ui_palette().shadow);
    DrawTexturePro(t->target.texture, (Rectangle) { 0, 0, WIDTH, -HEIGHT }, destination,
        (Vector2) { 0 }, 0, WHITE);
    frame_pacer_wait(&t->pacer);
    EndDrawing();
}

static Font load_title_font(const char *asset)
{
    char path[4096];
    if (!boat_asset_path(path, sizeof(path), asset))
        return (Font) { 0 };
    Font font = LoadFont(path);
    // LoadFont falls back to raylib's default when a required bitmap asset is missing.
    if (font.texture.id == GetFontDefault().texture.id)
        return (Font) { 0 };
    if (IsTextureValid(font.texture))
        SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    return font;
}

static bool open_title(Title *t)
{
    char path[4096];
    *t = (Title) { 0 };
    if (!boat_asset_path(path, sizeof(path), "title/river-backdrop.png"))
        return false;
    // Grade the captured river once at load, preserving its exact geometry and pixel grid.
    // This is a title palette, not a gameplay time-of-day or world-lighting model.
    Image image = LoadImage(path);
    if (!IsImageValid(image))
        return false;
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
#if !PROTEUS_WARM_MENU
    Color *pixels = image.data;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            Color *p = &pixels[y * image.width + x];
            bool water = y >= 87;
            p->r = (unsigned char)(p->r * (water ? .43f : .22f));
            p->g = (unsigned char)(p->g * (water ? .73f : .37f));
            p->b = (unsigned char)(p->b * (water ? .91f : .58f));
        }
    // A few occupied windows in this fixed 320x200 river composition, not a star field.
    static const Vector2 windows[] = { { 1, 76 }, { 4, 79 }, { 8, 76 }, { 13, 82 }, { 17, 79 },
        { 21, 76 }, { 28, 81 }, { 31, 78 }, { 35, 82 }, { 229, 82 }, { 238, 81 }, { 244, 80 },
        { 248, 83 }, { 255, 81 }, { 263, 82 }, { 271, 80 }, { 278, 83 }, { 285, 81 }, { 292, 80 },
        { 299, 82 }, { 306, 79 }, { 313, 82 } };
    if (image.width == WIDTH && image.height == HEIGHT)
        for (unsigned i = 0; i < sizeof(windows) / sizeof(windows[0]); ++i)
            pixels[(int)windows[i].y * WIDTH + (int)windows[i].x]
                = i % 3 ? rgb(205, 181, 119) : rgb(140, 165, 154);
#endif
    t->backdrop = LoadTextureFromImage(image);
    UnloadImage(image);
    t->target = LoadRenderTexture(WIDTH, HEIGHT);
    if (!boat_asset_path(path, sizeof(path), "title/wake-velocity.png"))
        return false;
    Image wordmark = LoadImage(path);
    if (!IsImageValid(wordmark))
        return false;
#if PROTEUS_WARM_MENU
    // Keep the authored silhouette, stems and spacing with one sunny yellow ink
    // inside a strong navy edge and offset shadow.
    ImageColorReplace(&wordmark, rgb(174, 245, 238), rgb(19, 40, 62));
    ImageColorReplace(&wordmark, rgb(231, 128, 105), rgb(19, 40, 62));
    ImageColorReplace(&wordmark, rgb(12, 45, 72), boat_ui_palette().selected);
#endif
    t->wordmark = LoadTextureFromImage(wordmark);
    UnloadImage(wordmark);
    t->ui = load_title_font("fonts/m5x7.fnt");
    t->small = load_title_font("fonts/m3x6.fnt");
    if (!IsTextureValid(t->backdrop) || !IsTextureValid(t->wordmark)
        || !IsRenderTextureValid(t->target) || !IsFontValid(t->ui) || !IsTextureValid(t->ui.texture)
        || !IsFontValid(t->small) || !IsTextureValid(t->small.texture)) {
        fprintf(stderr, "Cannot load the title screen (%s).\n", path);
        return false;
    }
    SetTextureFilter(t->backdrop, TEXTURE_FILTER_POINT);
    SetTextureFilter(t->wordmark, TEXTURE_FILTER_POINT);
    SetTextureFilter(t->target.texture, TEXTURE_FILTER_POINT);
    return true;
}

static void close_title(Title *t)
{
    stop_search(t);
    if (t->ui.glyphs)
        UnloadFont(t->ui);
    if (t->small.glyphs)
        UnloadFont(t->small);
    if (IsRenderTextureValid(t->target))
        UnloadRenderTexture(t->target);
    if (IsTextureValid(t->backdrop))
        UnloadTexture(t->backdrop);
    if (IsTextureValid(t->wordmark))
        UnloadTexture(t->wordmark);
}

static void begin_edit(Title *t, char *field)
{
    t->editing = field;
    snprintf(t->before, sizeof(t->before), "%s", field);
}

// Printable ASCII only: names travel as protocol bytes and addresses as host names.
static void edit(Title *t, size_t capacity, bool spaces)
{
    size_t length = strlen(t->editing);
    int character;
    while ((character = GetCharPressed()) != 0)
        if (character > 32 - spaces && character < 127 && length + 1 < capacity) {
            t->editing[length++] = (char)character;
            t->editing[length] = 0;
        }
    if (length && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)))
        t->editing[--length] = 0;
}

static void trim(char *text)
{
    size_t start = strspn(text, " "), length = strlen(text);
    while (length > start && text[length - 1] == ' ')
        --length;
    memmove(text, text + start, length - start);
    text[length - start] = 0;
}

static bool finish_edit(Title *t, bool keep)
{
    trim(t->editing);
    if (!keep || !t->editing[0])
        snprintf(t->editing, sizeof(t->before), "%s", t->before);
    t->editing = NULL;
    return keep;
}

BoatTitleChoice boat_title_run(BoatTitleSettings *s)
{
    Title t;
    if (!open_title(&t)) {
        close_title(&t);
        return BOAT_TITLE_EXIT;
    }
    SetWindowTitle("Wake Velocity");
    frame_pacer_init_window(&t.pacer);
    BoatTitleChoice choice = BOAT_TITLE_EXIT;
    bool chosen = false;
    bool settled = false;
    while (!chosen && !WindowShouldClose()) {
        t.time += GetFrameTime();
        if (!settled) {
            // The key or click that ended a session still reads as pressed in this frame.
            draw_screen(&t, s, NULL);
            present(&t);
            settled = true;
            continue;
        }
        bool activate = false, back = IsKeyPressed(KEY_ESCAPE);
        int step = 0;
        if (t.editing) {
            bool name = t.editing == s->name;
            edit(&t, name ? sizeof(s->name) : sizeof(s->address), name);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || back) {
                if (finish_edit(&t, !back) && t.panel == PANEL_JOIN)
                    t.selected = ROW_CONNECT;
                back = false;
            }
        } else {
            while (GetCharPressed() != 0) { } // Letters typed outside a field are not kept.
            step = (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
                - (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W));
            activate
                = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
            int k = scale();
            Vector2 mouse = GetMousePosition();
            Vector2 point = { (mouse.x - (GetScreenWidth() - WIDTH * k) / 2) / k,
                (mouse.y - (GetScreenHeight() - HEIGHT * k) / 2) / k };
            Vector2 moved = GetMouseDelta();
            for (int i = 0; i < row_count(&t); ++i)
                if (CheckCollisionPointRec(point, row_bounds(&t, i))) {
                    if (moved.x || moved.y)
                        t.selected = i;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        t.selected = i;
                        activate = true;
                    }
                }
        }
        if (step) {
            t.selected = (t.selected + step + row_count(&t)) % row_count(&t);
            t.moved = true;
            s->message[0] = 0;
        }
        if (t.panel == PANEL_JOIN && !t.editing)
            search(&t);
        if (t.panel == PANEL_JOIN && t.selected == ROW_PUBLIC_SERVER && !t.editing
            && public_server_count() > 1) {
            int count = public_server_count();
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            t.public_shown = (unsigned)((int)t.public_shown + count + turn) % (unsigned)count;
        }
        if (t.panel == PANEL_JOIN && t.selected == ROW_LAN && !t.editing && t.found.count > 1) {
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            t.shown = (unsigned)((int)(t.shown + t.found.count) + turn) % t.found.count;
        }
        if (t.panel == PANEL_SINGLE && t.selected <= ROW_SOLO_EDGES) {
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || activate)
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            if (t.selected == ROW_SOLO_PLACE)
                s->location
                    = (s->location + BOAT_LOCATION_COUNT + 1 + turn) % (BOAT_LOCATION_COUNT + 1);
            else if (t.selected == ROW_SOLO_WATER && turn)
                s->waves = !s->waves;
            else if (turn)
                s->smooth_edges = !s->smooth_edges;
            activate = false;
        }
        if (t.panel == PANEL_HOST && t.selected <= ROW_WATER) {
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || activate)
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            if (t.selected == ROW_MODE)
                s->host_mode
                    = (s->host_mode + BOAT_TITLE_HOST_MODES + turn) % BOAT_TITLE_HOST_MODES;
            else if (t.selected == ROW_PLACE && s->host_mode != BOAT_TITLE_HOST_RACE)
                s->host_location
                    = (s->host_location + BOAT_LOCATION_COUNT + turn) % BOAT_LOCATION_COUNT;
            else if (t.selected == ROW_WATER && turn)
                s->host_waves = !s->host_waves;
            activate = false;
        }
        bool settings = t.panel == PANEL_SETTINGS && !t.editing;
        if (settings
            && (t.selected == ROW_BOAT || t.selected == ROW_MOUSE || t.selected == ROW_MUSIC
                || t.selected == ROW_EDGES)) {
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || activate)
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            if (t.selected == ROW_BOAT)
                s->skin = (s->skin + BOAT_SKIN_COUNT + turn) % BOAT_SKIN_COUNT;
            else if (t.selected == ROW_MUSIC)
                s->soundtrack = (s->soundtrack + BOAT_MUSIC_COUNT + turn) % BOAT_MUSIC_COUNT;
            else if (t.selected == ROW_EDGES && turn)
                s->smooth_edges = !s->smooth_edges;
            else if (t.selected == ROW_MOUSE && turn)
                s->mouse_steering = !s->mouse_steering;
            activate = false;
        }
        if (activate) {
            s->message[0] = 0;
            if (t.panel == PANEL_CREDITS) {
                back = true;
            } else if (t.panel == PANEL_SETTINGS) {
                if (t.selected == ROW_NAME)
                    begin_edit(&t, s->name);
                else
                    back = true;
            } else if (t.panel == PANEL_SINGLE) {
                if (t.selected == ROW_SOLO_START) {
                    choice = BOAT_TITLE_SINGLE;
                    chosen = true;
                } else
                    back = true;
            } else if (t.panel == PANEL_HOST) {
                if (t.selected == ROW_START) {
                    choice = BOAT_TITLE_HOST;
                    chosen = true;
                } else
                    back = true;
            } else if (t.panel == PANEL_JOIN) {
                if (t.selected == ROW_PUBLIC_SERVER) {
                    if (public_server_compatible(
                            (int)t.public_shown, BOAT_NET_VERSION, BOAT_PHYSICS_REVISION)) {
                        public_server_text((int)t.public_shown, 2, s->address, sizeof(s->address));
                        choice = BOAT_TITLE_JOIN;
                        chosen = true;
                    }
                } else if (t.selected == ROW_HOST_ROOM) {
                    t.panel = PANEL_HOST;
                    t.selected = ROW_START;
                } else if (t.selected == ROW_LAN) {
#if defined(PROTEUS_BOAT_NET) && !defined(__EMSCRIPTEN__)
                    if (t.found.count) {
                        boat_net_lan_endpoint(
                            &t.found.rooms[t.shown], s->address, sizeof(s->address));
                        choice = BOAT_TITLE_JOIN;
                        chosen = true;
                    }
#endif
                } else if (t.selected == ROW_ADDRESS)
                    begin_edit(&t, s->address);
                else if (t.selected == ROW_CONNECT) {
                    choice = BOAT_TITLE_JOIN;
                    chosen = true;
                } else
                    back = true;
            } else if (t.selected == ROW_SINGLE) {
                t.panel = PANEL_SINGLE;
                t.selected = ROW_SOLO_START;
            } else if (t.selected == ROW_MULTI) {
                t.panel = PANEL_JOIN;
                t.selected = ROW_PUBLIC_SERVER;
                start_search(&t);
            } else if (t.selected == ROW_SETTINGS) {
                t.panel = PANEL_SETTINGS;
                t.selected = ROW_MOUSE;
            } else if (t.selected == ROW_CREDITS) {
                t.panel = PANEL_CREDITS;
                t.selected = 0;
                t.credits_started = t.time;
            } else if (t.selected == ROW_EXIT)
                chosen = true;
        }
        if (back && t.panel == PANEL_SINGLE) {
            boat_title_save(s);
            t.panel = PANEL_MAIN;
            t.selected = ROW_SINGLE;
        } else if (back && t.panel == PANEL_HOST) {
            boat_title_save(s);
            t.panel = PANEL_JOIN;
            t.selected = ROW_HOST_ROOM;
        } else if (back && t.panel == PANEL_JOIN) {
            stop_search(&t);
            t.panel = PANEL_MAIN;
            t.selected = ROW_MULTI;
        } else if (back && t.panel == PANEL_SETTINGS) {
            boat_title_save(s);
            t.panel = PANEL_MAIN;
            t.selected = ROW_SETTINGS;
        } else if (back && t.panel == PANEL_CREDITS) {
            t.panel = PANEL_MAIN;
            t.selected = ROW_CREDITS;
        } else if (back)
            chosen = true;
        char status[128];
        snprintf(status, sizeof(status), "CONNECTING TO %s ...", s->address);
        if (choice == BOAT_TITLE_HOST)
            snprintf(status, sizeof(status), "STARTING THE ROOM ...");
        draw_screen(&t, s, choice == BOAT_TITLE_JOIN || choice == BOAT_TITLE_HOST ? status : NULL);
        present(&t);
    }
    close_title(&t);
    return choice;
}

bool boat_title_screenshot(const BoatTitleSettings *settings, double time, const char *path)
{
    Title t;
    bool ok = false;
    if (open_title(&t)) {
        t.time = time;
        draw_screen(&t, settings, NULL);
        Image image = LoadImageFromTexture(t.target.texture);
        ImageFlipVertical(&image);
        ok = ExportImage(image, path);
        UnloadImage(image);
    }
    close_title(&t);
    return ok;
}

static bool settings_path(char *path, size_t size)
{
    char directory[2048];
    if (!boat_replay_data_directory(directory, sizeof(directory)))
        return false;
    int n = snprintf(path, size, "%s/title.txt", directory);
    return n > 0 && (size_t)n < size;
}

void boat_title_load(BoatTitleSettings *s)
{
    *s = (BoatTitleSettings) { .name = "player",
        .skin = BOAT_SKIN_SWEDEN,
        .host_location = BOAT_LOCATION_RIVER,
        .location = BOAT_LOCATION_RIVER,
        .waves = true,
        .smooth_edges = true };
#if defined(__EMSCRIPTEN__)
    snprintf(s->address, sizeof(s->address), "ws://127.0.0.1:24682");
#else
    snprintf(s->address, sizeof(s->address), "127.0.0.1:%d", BOAT_NET_PORT);
#endif
    char path[4096], line[160];
    FILE *file = settings_path(path, sizeof(path)) ? fopen(path, "r") : NULL;
    if (!file)
        return;
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;
        char *value = strchr(line, '=');
        if (!value || !value[1])
            continue;
        *value++ = 0;
        if (!strcmp(line, "name"))
            snprintf(s->name, sizeof(s->name), "%s", value);
        else if (!strcmp(line, "address"))
            snprintf(s->address, sizeof(s->address), "%s", value);
        else if (!strcmp(line, "skin"))
            boat_skin_parse(value, &s->skin);
        else if (!strcmp(line, "mouse_steering"))
            s->mouse_steering = !strcmp(value, "on");
        else if (!strcmp(line, "soundtrack"))
            boat_music_parse(value, &s->soundtrack);
        else if (!strcmp(line, "location")) {
            const BoatWorldDefinition *location = boat_location_find(value);
            if (!strcmp(value, "course"))
                s->location = BOAT_LOCATION_COUNT;
            else if (location)
                s->location = location->id;
        } else if (!strcmp(line, "waves"))
            s->waves = !strcmp(value, "on");
        else if (!strcmp(line, "smooth_edges"))
            s->smooth_edges = !strcmp(value, "on");
        else if (!strcmp(line, "host_mode")) {
            for (unsigned i = 0; i < BOAT_TITLE_HOST_MODES; ++i)
                if (!strcmp(value, host_modes[i]))
                    s->host_mode = i;
        } else if (!strcmp(line, "host_location")) {
            const BoatWorldDefinition *location = boat_location_find(value);
            if (location)
                s->host_location = location->id;
        } else if (!strcmp(line, "host_waves"))
            s->host_waves = !strcmp(value, "on");
    }
    fclose(file);
}

void boat_title_save(const BoatTitleSettings *s)
{
    char path[4096];
    if (!settings_path(path, sizeof(path)))
        return;
    MakeDirectory(GetDirectoryPath(path));
    FILE *file = fopen(path, "w");
    if (!file)
        return;
    fprintf(file, "name=%s\naddress=%s\nskin=%s\nmouse_steering=%s\nsoundtrack=%s\n", s->name,
        s->address, boat_skin_name(s->skin), s->mouse_steering ? "on" : "off",
        boat_music(s->soundtrack)->id);
    fprintf(file, "location=%s\nwaves=%s\nsmooth_edges=%s\n",
        s->location == BOAT_LOCATION_COUNT ? "course" : boat_location(s->location)->name,
        s->waves ? "on" : "off", s->smooth_edges ? "on" : "off");
    fprintf(file, "host_mode=%s\nhost_location=%s\nhost_waves=%s\n",
        boat_title_host_mode(s->host_mode), boat_location(s->host_location)->name,
        s->host_waves ? "on" : "off");
    fclose(file);
}
