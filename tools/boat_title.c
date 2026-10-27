#include "boat_title.h"
#include "boat.h"
#include "boat_assets.h"
#include "boat_location.h"
#include "boat_music.h"
#include "boat_net_protocol.h"
#include "boat_replay.h"
#include "boat_skin.h"
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

typedef enum { ROW_SINGLE, ROW_MULTI, ROW_SETTINGS, ROW_EXIT, MAIN_ROWS } MainRow;
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
typedef enum { PANEL_MAIN, PANEL_SINGLE, PANEL_JOIN, PANEL_HOST, PANEL_SETTINGS } Panel;

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
    Texture2D backdrop;
    RenderTexture2D target;
    Panel panel; // The multiplayer or settings panel replaces the main rows.
    int selected;
    char *editing; // The text field being typed into, if any.
    char before[64]; // Its value when editing started, restored by Escape.
    double time;
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

// Deliberately small, title-owned block alphabet. Expand first, then outline the joined
// silhouette so adjoining cells do not acquire seams. No filtered/scaled font atlas.
static void draw_logo(void)
{
    static const struct {
        char letter;
        int width;
        unsigned char rows[7];
    } glyphs[] = {
        { 'W', 5, { 17, 17, 17, 21, 21, 31, 10 } },
        { 'A', 5, { 14, 27, 17, 31, 17, 17, 17 } },
        { 'K', 5, { 17, 19, 22, 28, 22, 19, 17 } },
        { 'E', 5, { 31, 16, 16, 30, 16, 16, 31 } },
        { 'V', 5, { 17, 17, 17, 17, 27, 14, 4 } },
        { 'L', 4, { 8, 8, 8, 8, 8, 8, 15 } },
        { 'O', 5, { 14, 27, 17, 17, 17, 27, 14 } },
        { 'C', 5, { 15, 24, 16, 16, 16, 24, 15 } },
        { 'I', 3, { 7, 2, 2, 2, 2, 2, 7 } },
        { 'T', 5, { 31, 4, 4, 4, 4, 4, 4 } },
        { 'Y', 5, { 17, 17, 27, 14, 4, 4, 4 } },
    };
    enum { LOGO_WIDTH = 274, LOGO_HEIGHT = 30, LEFT = 23, TOP = 20 };
    bool mask[LOGO_HEIGHT][LOGO_WIDTH] = { 0 };
    int cursor = 1;
    for (const char *c = "WAKE VELOCITY"; *c; ++c) {
        if (*c == ' ') {
            cursor += 9;
            continue;
        }
        for (unsigned g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); ++g) {
            if (glyphs[g].letter != *c)
                continue;
            for (int y = 0; y < 28; ++y)
                for (int x = 0; x < glyphs[g].width * 4; ++x)
                    mask[y + 1][cursor + x]
                        = (glyphs[g].rows[y / 4] & (1 << (glyphs[g].width - 1 - x / 4))) != 0;
            cursor += glyphs[g].width * 4 + 3;
        }
    }
    for (int y = 1; y < LOGO_HEIGHT - 1; ++y)
        for (int x = 1; x < LOGO_WIDTH - 1; ++x)
            if (mask[y][x])
                DrawPixel(LEFT + x + 2, TOP + y + 2, rgb(231, 128, 105));
    for (int y = 1; y < LOGO_HEIGHT - 1; ++y)
        for (int x = 1; x < LOGO_WIDTH - 1; ++x) {
            if (!mask[y][x])
                continue;
            bool inside = mask[y - 1][x] && mask[y + 1][x] && mask[y][x - 1] && mask[y][x + 1];
            DrawPixel(LEFT + x, TOP + y, inside ? rgb(12, 45, 72) : rgb(174, 245, 238));
        }
    const char *credit = "A PROTEUS GAME";
    int x = (WIDTH - MeasureText(credit, 10)) / 2;
    DrawText(credit, x, 57, 10, rgb(174, 245, 238));
    DrawLine(x - 37, 61, x - 5, 61, rgb(118, 203, 211));
    DrawLine(WIDTH - x + 5, 61, WIDTH - x + 37, 61, rgb(118, 203, 211));
}

static void crosshair(int x, int y, Color ink)
{
    DrawLine(x - 4, y, x + 5, y, ink);
    DrawLine(x, y - 4, x, y + 5, ink);
    DrawPixel(x, y, rgb(188, 255, 243));
}

static int row_count(const Title *t)
{
    return t->panel == PANEL_SINGLE  ? SOLO_ROWS
        : t->panel == PANEL_JOIN     ? JOIN_ROWS
        : t->panel == PANEL_HOST     ? HOST_ROWS
        : t->panel == PANEL_SETTINGS ? SETTINGS_ROWS
                                     : MAIN_ROWS;
}

static Rectangle row_bounds(const Title *t, int row)
{
    if (t->panel == PANEL_MAIN)
        return (Rectangle) { 70, (float)(123 + row * 14), 154, 13 };
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
static const char *fit(const char *text, int width)
{
    while (*text && MeasureText(text, 10) > width)
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
    static const char *labels[MAIN_ROWS] = { "SINGLE PLAYER", "MULTIPLAYER", "SETTINGS", "EXIT" };
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
    int left = main ? 66 : 37, width = main ? 162 : 246;
    Color cyan = rgb(142, 224, 226), dim = rgb(61, 133, 157);
    DrawRectangle(left, top, width, height, (Color) { 8, 32, 54, 228 });
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
        int label_x = main ? 99 : 72;
        if (main) {
            char number[8];
            snprintf(number, sizeof(number), "%02d", i + 1);
            DrawText(number, 72, (int)row.y + 2, 10, cyan);
            DrawLine(88, (int)row.y + 2, 88, (int)row.y + 11, dim);
            DrawLine(71, (int)row.y + 13, 223, (int)row.y + 13, dim);
        }
        if (active) {
            Rectangle highlight = row;
            highlight.x = main ? 87 : row.x;
            highlight.width = row.x + row.width - highlight.x;
            DrawRectangleRec(highlight, rgb(156, 241, 231));
            int x = main ? 90 : 51, y = (int)row.y + 4;
            for (int j = 0; j < 3; ++j)
                DrawLine(x + j, y + j, x + j, y + 5 - j, rgb(231, 128, 105));
        }
        Color ink = active ? rgb(11, 40, 62) : rgb(190, 242, 235);
        const char *label;
        char value[96];
        row_text(t, s, i, &label, value, sizeof(value));
        // The web's longer BACK TO LAUNCHER label also fits the compact main panel.
        DrawText(label, label_x, (int)row.y + 2, 10, ink);
        if (value[0]) {
            int value_x = t->panel == PANEL_JOIN && i == ROW_ADDRESS ? 122
                : t->panel == PANEL_SETTINGS && i == ROW_MUSIC       ? 154
                                                                     : 142;
            const char *shown = fit(value, 268 - value_x);
            DrawText(shown, value_x, (int)row.y + 2, 10, ink);
            bool typing = t->editing && active;
            if (typing && fmod(t->time, .8) < .5)
                DrawRectangle(value_x + 1 + MeasureText(shown, 10), (int)row.y + 2, 5, 9, ink);
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
        const char *shown = fit(hint, WIDTH - 12);
        int x = (WIDTH - MeasureText(shown, 10)) / 2;
        int y = main ? top - 15 : top + height + 3;
        DrawRectangle(x - 3, y - 1, MeasureText(shown, 10) + 6, 12, (Color) { 10, 38, 57, 200 });
        DrawText(shown, x, y + 1, 10, status ? rgb(255, 236, 175) : rgb(255, 196, 150));
    }
}

static void draw_footer_text(const char *label, int x)
{
    DrawText(label, x + 1, 191, 7, (Color) { 10, 45, 65, 130 });
    DrawText(label, x, 190, 7, (Color) { 162, 197, 199, 190 });
}

static void draw_screen(const Title *t, const BoatTitleSettings *s, const char *status)
{
    BeginTextureMode(t->target);
    DrawTexture(t->backdrop, 0, 0, WHITE);
    draw_logo();
    crosshair(12, 37, rgb(118, 203, 211));
    crosshair(307, 37, rgb(118, 203, 211));
    draw_menu(t, s, status);
    const char *map_credit = "MAP: OSM CONTRIBUTORS";
    draw_footer_text(PROTEUS_VERSION, 8);
    if (t->panel == PANEL_SETTINGS)
        draw_footer_text(map_credit, WIDTH - 9 - MeasureText(map_credit, 7));
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
    ClearBackground(rgb(10, 38, 57));
    DrawTexturePro(t->target.texture, (Rectangle) { 0, 0, WIDTH, -HEIGHT }, destination,
        (Vector2) { 0 }, 0, WHITE);
    frame_pacer_wait(&t->pacer);
    EndDrawing();
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
    t->backdrop = LoadTextureFromImage(image);
    UnloadImage(image);
    t->target = LoadRenderTexture(WIDTH, HEIGHT);
    if (!IsTextureValid(t->backdrop) || !IsRenderTextureValid(t->target)) {
        fprintf(stderr, "Cannot load the title screen (%s).\n", path);
        return false;
    }
    SetTextureFilter(t->backdrop, TEXTURE_FILTER_POINT);
    SetTextureFilter(t->target.texture, TEXTURE_FILTER_POINT);
    SetTextureFilter(GetFontDefault().texture, TEXTURE_FILTER_POINT);
    return true;
}

static void close_title(Title *t)
{
    stop_search(t);
    if (IsRenderTextureValid(t->target))
        UnloadRenderTexture(t->target);
    if (IsTextureValid(t->backdrop))
        UnloadTexture(t->backdrop);
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
            if (t.panel == PANEL_SETTINGS) {
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
