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
#include "boat_work_menu.h"
#include "frame_pacer.h"
#include "raylib.h"
#include "wake_velocity_input.h"
#include "wake_velocity_servers.h"
#include <stdio.h>

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
    ROW_LIGHTING,
    ROW_DONE,
    SETTINGS_ROWS
} SettingsRow;
typedef enum {
    ROW_SOLO_PLACE,
    ROW_SOLO_CONTENT,
    ROW_SOLO_SAVE,
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

static const char *const host_modes[BOAT_TITLE_HOST_MODES]
    = { "free", "deathmatch", "race", "hunt" };
static const char *const lighting_names[BOAT_TITLE_LIGHTING_HOURS] = { "day", "sunset", "dusk" };
static const char *const lighting_hours[]
    = { "hour-v1:00", "hour-v1:01", "hour-v1:02", "hour-v1:03", "hour-v1:04", "hour-v1:05",
          "hour-v1:06", "hour-v1:07", "hour-v1:08", "hour-v1:09", "hour-v1:10", "hour-v1:11",
          "hour-v1:12", "hour-v1:13", "hour-v1:14", "hour-v1:15", "hour-v1:16", "hour-v1:17",
          "hour-v1:18", "hour-v1:19", "hour-v1:20", "hour-v1:21", "hour-v1:22", "hour-v1:23" };

const char *boat_title_lighting(unsigned lighting)
{
    if (lighting < BOAT_TITLE_LIGHTING_HOURS)
        return lighting_names[lighting];
    if (lighting < BOAT_TITLE_LIGHTING_COUNT)
        return lighting_hours[lighting - BOAT_TITLE_LIGHTING_HOURS];
    return lighting_names[BOAT_TITLE_LIGHTING_DAY];
}

int boat_title_lighting_hour(unsigned lighting)
{
    return lighting >= BOAT_TITLE_LIGHTING_HOURS && lighting < BOAT_TITLE_LIGHTING_COUNT
        ? (int)(lighting - BOAT_TITLE_LIGHTING_HOURS)
        : -1;
}

bool boat_title_lighting_cli(
    unsigned lighting, int extra_count, char **extra, const char **option, const char **value)
{
    for (int i = 0; i < extra_count; ++i)
        if (!strcmp(extra[i], "--daylight") || !strcmp(extra[i], "--lighting-hour"))
            return false;
    int hour = boat_title_lighting_hour(lighting);
    if (hour >= 0) {
        *option = "--lighting-hour";
        *value = boat_title_lighting(lighting) + sizeof("hour-v1:") - 1;
    } else {
        *option = "--daylight";
        *value = boat_title_lighting(lighting);
    }
    return true;
}
static const struct {
    const char *name, *package, *label, *hint;
} outer_contents[BOAT_TITLE_OUTER_CONTENTS] = {
    { "standard", "proteus.langedrag", "STANDARD", "THE STANDARD HARBOUR" },
    { "harbor-life", "proteus.harbor-life", "HARBOR LIFE", "AUTHORED MARINA, TRAFFIC AND GULLS" },
    { "grodan", "proteus.grodan", "GRODAN TOUR", "IN THE GRODAN, WITH ITS PASSENGERS" },
    { "canal-tour", "proteus.grodan", "CANAL TOUR", "THE GRODAN'S ROUTE IN YOUR OWN BOAT" },
    { "working-harbour", "proteus.outer-work", "WORKING HARBOUR",
        "CARGO JOBS THAT SAVE THEMSELVES (EXPERIMENT)" },
};

// The browser has no work package, so its menu stops before WORKING HARBOUR.
static unsigned content_count(void)
{
#if defined(__EMSCRIPTEN__)
    return BOAT_TITLE_OUTER_WORKING_HARBOUR;
#else
    return BOAT_TITLE_OUTER_CONTENTS;
#endif
}

const char *boat_title_outer_content(unsigned content)
{
    return outer_contents[content < BOAT_TITLE_OUTER_CONTENTS ? content : 0].name;
}

const char *boat_title_outer_package(unsigned content)
{
    return outer_contents[content < BOAT_TITLE_OUTER_CONTENTS ? content : 0].package;
}

const char *boat_title_host_mode(unsigned mode)
{
    return host_modes[mode < BOAT_TITLE_HOST_MODES ? mode : 0];
}

// Race and shelter hunt rooms are always on the outer map.
static bool fixed_place(const BoatTitleSettings *s)
{
    return s->host_mode == BOAT_TITLE_HOST_RACE || s->host_mode == BOAT_TITLE_HOST_HUNT;
}

static unsigned host_location(const BoatTitleSettings *s)
{
    return fixed_place(s) ? BOAT_LOCATION_OUTER : s->host_location;
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
    // LAN search while the multiplayer panel is open (none in the browser).
    WakeLanSearch *lan;
    bool searching, moved; // MOVED: the player has picked a row since opening the panel.
    BoatNetLanList found;
    unsigned shown; // The found room the LAN row offers.
    unsigned public_shown;
    double queried;
    bool solo_content; // The outer content row is visible in this panel.
    bool solo_save; // WORKING HARBOUR: the SAVE row replaces the WATER row.
    BoatTitleSave slot; // The working-harbour save slot as last scanned.
    BoatSaveRow save; // The SAVE row's mode and open question.
    int confirm_row; // The row that was selected when the question opened.
    FramePacer pacer;
} Title;

static void stop_search(Title *t)
{
    wake_lan_close(t->lan);
    t->lan = NULL;
    t->searching = false;
    t->found.count = 0;
}

static void start_search(Title *t)
{
    t->found.count = t->shown = 0;
    t->moved = false;
    t->queried = -1;
    wake_servers_refresh();
    if (!t->searching) {
        t->lan = wake_lan_open();
        t->searching = t->lan != NULL;
    }
}

// Query about once a second and forget rooms silent for three.
static void search(Title *t)
{
    wake_servers_poll();
    if (t->searching && (t->queried < 0 || t->time - t->queried >= 1)) {
        wake_lan_query(t->lan);
        t->queried = t->time;
    }
    BoatNetLanRoom room;
    while (t->searching && wake_lan_receive(t->lan, &room))
        boat_net_lan_list_note(&t->found, &room, t->time);
    boat_net_lan_list_expire(&t->found, t->time, 3.5);
    if (t->shown >= t->found.count)
        t->shown = 0;
    unsigned count = (unsigned)wake_servers_count();
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

// A SOLO row is shown unless it is the content row off the outer location, the SAVE row off
// WORKING HARBOUR, or the water row on WORKING HARBOUR (its job uses the sandbox's calm water).
static bool solo_visible(const Title *t, int logical)
{
    return logical == ROW_SOLO_CONTENT ? t->solo_content
        : logical == ROW_SOLO_SAVE     ? t->solo_save
        : logical == ROW_SOLO_WATER    ? !t->solo_save
                                       : true;
}

static int solo_count(const Title *t)
{
    int count = 0;
    for (int i = 0; i < SOLO_ROWS; ++i)
        count += solo_visible(t, i);
    return count;
}

static int row_count(const Title *t)
{
    return t->panel == PANEL_CREDITS ? 1
        : t->panel == PANEL_SINGLE   ? solo_count(t)
        : t->panel == PANEL_JOIN     ? JOIN_ROWS
        : t->panel == PANEL_HOST     ? HOST_ROWS
        : t->panel == PANEL_SETTINGS ? SETTINGS_ROWS
                                     : MAIN_ROWS;
}

// A visible SOLO row as its enum value.
static int solo_row(const Title *t, int visible)
{
    for (int i = 0; i < SOLO_ROWS; ++i)
        if (solo_visible(t, i) && visible-- == 0)
            return i;
    return ROW_SOLO_BACK;
}

// The visible index of a SOLO row, or -1 when it is hidden.
static int solo_index(const Title *t, int logical)
{
    int index = 0;
    for (int i = 0; i < logical; ++i)
        index += solo_visible(t, i);
    return solo_visible(t, logical) ? index : -1;
}

// After the place or content changed (or the panel opened): show the right rows and, for
// WORKING HARBOUR, rescan its save slot. The row starts on Continue when one can be continued.
static void solo_refresh(Title *t, const BoatTitleSettings *s)
{
    t->solo_content = s->location == BOAT_LOCATION_OUTER;
    t->solo_save = t->solo_content && s->outer_content == BOAT_TITLE_OUTER_WORKING_HARBOUR;
    t->slot = (BoatTitleSave) { 0 };
    if (t->solo_save)
        wake_work_title_scan(s->data_dir, &t->slot);
    boat_save_row_set(&t->save, t->slot.present, t->slot.available);
    if (t->selected >= row_count(t))
        t->selected = row_count(t) - 1;
}

// What the hint line says about WORKING HARBOUR's save, or NULL.
static const char *work_hint(const Title *t, char *out, size_t size)
{
    if (t->save.confirming)
        return t->save.pending == BOAT_SAVE_DELETE_FILE ? "REMOVES THE SAVE AND ITS BACKUP COPY"
            : t->slot.available                         ? "THE OLD SAVE STAYS AS A BACKUP COPY"
                                                        : "THE OLD SAVE IS SET ASIDE, NOT DELETED";
    if (!t->slot.present)
        return "NO SAVE YET; PROGRESS IS SAVED AUTOMATICALLY";
    if (t->slot.available)
        return t->slot.summary;
    snprintf(out, size, "SAVE UNAVAILABLE: %s; NEW SETS IT ASIDE", t->slot.reason);
    return out;
}

static Rectangle row_bounds(const Title *t, int row)
{
    if (t->panel == PANEL_CREDITS)
        return (Rectangle) { 127, 171, 66, 14 };
    if (t->panel == PANEL_MAIN)
        return (Rectangle) { 70, (float)(109 + row * 14), 118, 13 };
    int height = t->panel == PANEL_SETTINGS ? 14 : ROW_HEIGHT;
    int top = 86 + (5 - row_count(t)) * height / 2; // Centred on five rows' space.
    return (Rectangle) { 45, (float)(top + row * height), 230, height - 2 };
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

// PICTURE: plain low resolution, its 4x supersampling, or high resolution.
static const char *const pictures[] = { "PIXEL", "SMOOTH", "HIGH RES" };

static int picture(const BoatTitleSettings *s)
{
    return s->high_resolution ? 2 : s->smooth_edges ? 1 : 0;
}

static void turn_picture(BoatTitleSettings *s, int turn)
{
    int next = (picture(s) + 3 + turn) % 3;
    s->smooth_edges = next == 1;
    s->high_resolution = next == 2;
}

static void row_text(const Title *t, const BoatTitleSettings *s, int row, const char **label,
    char *value, size_t size)
{
    *label = "";
    value[0] = 0;
    if (t->panel == PANEL_SINGLE) {
        static const char *labels[SOLO_ROWS]
            = { "PLACE", "CONTENT", "SAVE", "WAVES", "PICTURE", "START", "BACK" };
        int logical = solo_row(t, row);
        *label = labels[logical];
        if (logical == ROW_SOLO_PLACE)
            snprintf(value, size, "< %s >",
                s->location == BOAT_LOCATION_COUNT ? "BASIN TRIAL"
                                                   : boat_location(s->location)->title);
        else if (logical == ROW_SOLO_CONTENT)
            snprintf(value, size, "< %s >", outer_contents[s->outer_content].label);
        else if (logical == ROW_SOLO_SAVE)
            boat_save_row_text(&t->save, value, size);
        else if (logical == ROW_SOLO_WATER)
            snprintf(value, size, "< %s >", s->waves ? "ON" : "OFF");
        else if (logical == ROW_SOLO_EDGES)
            snprintf(value, size, "< %s >", pictures[picture(s)]);
        return;
    }
    if (t->panel == PANEL_SETTINGS) {
        static const char *labels[SETTINGS_ROWS]
            = { "NAME", "BOAT", "STEERING", "SOUNDTRACK", "PICTURE", "LIGHTING", "BACK" };
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
            snprintf(value, size, "< %s >", pictures[picture(s)]);
        else if (row == ROW_LIGHTING) {
            int hour = boat_title_lighting_hour(s->lighting);
            if (hour >= 0) {
                snprintf(value, size, "< %02d:00 >", hour);
            } else {
                char lighting[16];
                upper(lighting, sizeof(lighting), boat_title_lighting(s->lighting));
                snprintf(value, size, "< %s >", lighting);
            }
        }
        return;
    }
    if (t->panel == PANEL_HOST) {
        static const char *labels[HOST_ROWS] = { "MODE", "PLACE", "WATER", "START ROOM", "BACK" };
        static const char *modes[BOAT_TITLE_HOST_MODES]
            = { "FREE BOATING", "DEATHMATCH", "RIVER RACE", "SHELTER HUNT" };
        *label = labels[row];
        const char *place = boat_location(host_location(s))->title;
        if (row == ROW_MODE)
            snprintf(value, size, "< %s >", modes[s->host_mode]);
        else if (row == ROW_PLACE)
            snprintf(value, size, fixed_place(s) ? "%s" : "< %s >", place);
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
            if (wake_servers_count()) {
                char name[25];
                wake_servers_text((int)t->public_shown, WAKE_SERVER_NAME, name, sizeof(name));
                upper(value, size, name);
                if (wake_servers_count() > 1) {
                    static char counted[32];
                    snprintf(counted, sizeof(counted), "SERVER %u/%d", t->public_shown + 1,
                        wake_servers_count());
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
        } else {
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
        return wake_lan_unavailable_hint();
    if (!t->found.count)
        return "NO ROOMS FOUND ON THIS NETWORK YET";
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
            : s->skin == BOAT_SKIN_CABIN     ? "25 KNOTS WHEN FREE RIDING SOLO"
            : s->skin == BOAT_SKIN_GRODAN    ? "15 M CANAL TOUR BOAT, SOLO ONLY"
                                             : "SINGLE PLAYER; ROOMS COLOUR BOATS BY SLOT";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_MOUSE)
        hint = "REMEMBERED; ESC MENU OR M SWITCHES IN GAME";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_MUSIC)
        hint = "LEFT / RIGHT OR CLICK: CHOOSE MUSIC";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_LIGHTING)
        hint = "REMEMBERED; WORKING HARBOUR CYCLES";
    char work_text[96];
    int solo = t->panel == PANEL_SINGLE ? solo_row(t, t->selected) : -1;
    if (!hint && t->solo_save && (solo == ROW_SOLO_SAVE || solo == ROW_SOLO_START))
        hint = work_hint(t, work_text, sizeof(work_text));
    if (!hint && solo == ROW_SOLO_CONTENT)
        hint = outer_contents[s->outer_content].hint;
    char public_hint[96];
    if (!hint && t->panel == PANEL_JOIN && t->selected == ROW_PUBLIC_SERVER) {
        if (!wake_servers_count())
            wake_servers_status(public_hint, sizeof(public_hint));
        else if (!wake_servers_compatible(
                     (int)t->public_shown, BOAT_NET_VERSION, BOAT_PHYSICS_REVISION))
            snprintf(public_hint, sizeof(public_hint), "OTHER GAME VERSION; USE MATCHING CLIENT");
        else {
            char description[65];
            wake_servers_text(
                (int)t->public_shown, WAKE_SERVER_DESCRIPTION, description, sizeof(description));
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
        static const char *modes[BOAT_TITLE_HOST_MODES]
            = { "SHARED FREE BOATING, NO SCORES", "TIMED ROUNDS: CAPSIZE THE OTHERS",
                  "SPRINT DOWN THE RIVER FROM A GRID", "FIND THE SHELTER MARKS TOGETHER" };
        if (t->selected == ROW_MODE)
            hint = modes[s->host_mode];
        else if (t->selected == ROW_PLACE && fixed_place(s))
            hint = "RACES AND HUNTS ALWAYS RUN IN THE HARBOUR";
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
    if (t->panel != PANEL_CREDITS)
        draw_footer_text(t->small, PROTEUS_VERSION, 8);
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
        BoatAppInput in = boat_app_input_sample();
        boat_app_input_poll_touch(&in);
        Vector2 pointer = in.pointer;
        bool pointer_pressed = in.pointer_pressed;
        bool activate = false, back = in.back;
        int step = 0;
        if (t.editing) {
            bool name = t.editing == s->name;
            edit(&t, name ? sizeof(s->name) : sizeof(s->address), name);
            if (in.confirm || back) {
                if (finish_edit(&t, !back) && t.panel == PANEL_JOIN)
                    t.selected = ROW_CONNECT;
                back = false;
            }
        } else {
            while (GetCharPressed() != 0) { } // Letters typed outside a field are not kept.
            step = (int)in.down - (int)in.up;
            activate = in.activate;
            int k = scale();
            Vector2 mouse = pointer;
            Vector2 point = { (mouse.x - (GetScreenWidth() - WIDTH * k) / 2) / k,
                (mouse.y - (GetScreenHeight() - HEIGHT * k) / 2) / k };
            Vector2 moved = in.pointer_delta;
            for (int i = 0; i < row_count(&t); ++i)
                if (CheckCollisionPointRec(point, row_bounds(&t, i))) {
                    if (moved.x || moved.y)
                        t.selected = i;
                    if (pointer_pressed) {
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
        // A question is answered on its own row: moving off it withdraws the question.
        if (t.save.confirming && t.selected != t.confirm_row)
            boat_save_row_cancel(&t.save);
        BoatSaveAction work_action = BOAT_SAVE_NOTHING;
        if (t.panel == PANEL_JOIN && !t.editing)
            search(&t);
        if (t.panel == PANEL_JOIN && t.selected == ROW_PUBLIC_SERVER && !t.editing
            && wake_servers_count() > 1) {
            int count = wake_servers_count();
            int turn = in.right - in.left;
            t.public_shown = (unsigned)((int)t.public_shown + count + turn) % (unsigned)count;
        }
        if (t.panel == PANEL_JOIN && t.selected == ROW_LAN && !t.editing && t.found.count > 1) {
            int turn = in.right - in.left;
            t.shown = (unsigned)((int)(t.shown + t.found.count) + turn) % t.found.count;
        }
        if (t.panel == PANEL_SINGLE) {
            int sel = solo_row(&t, t.selected);
            if (sel <= ROW_SOLO_EDGES) {
                int step_turn = in.right - in.left;
                int turn = step_turn + activate > 1 ? 1 : step_turn + activate;
                if (sel == ROW_SOLO_PLACE) {
                    s->location = (s->location + BOAT_LOCATION_COUNT + 1 + turn)
                        % (BOAT_LOCATION_COUNT + 1);
                    solo_refresh(&t, s);
                } else if (sel == ROW_SOLO_CONTENT && turn) {
                    s->outer_content
                        = (s->outer_content + content_count() + turn) % content_count();
                    solo_refresh(&t, s);
                } else if (sel == ROW_SOLO_SAVE) {
                    boat_save_row_turn(&t.save, step_turn);
                    if (activate)
                        work_action = boat_save_row_press(&t.save);
                } else if (sel == ROW_SOLO_WATER && turn)
                    s->waves = !s->waves;
                else if (sel == ROW_SOLO_EDGES && turn)
                    turn_picture(s, turn);
                activate = false;
            }
        }
        if (t.panel == PANEL_HOST && t.selected <= ROW_WATER) {
            int turn = (in.right || activate) - in.left;
            if (t.selected == ROW_MODE)
                s->host_mode
                    = (s->host_mode + BOAT_TITLE_HOST_MODES + turn) % BOAT_TITLE_HOST_MODES;
            else if (t.selected == ROW_PLACE && !fixed_place(s))
                s->host_location
                    = (s->host_location + BOAT_LOCATION_COUNT + turn) % BOAT_LOCATION_COUNT;
            else if (t.selected == ROW_WATER && turn)
                s->host_waves = !s->host_waves;
            activate = false;
        }
        bool settings = t.panel == PANEL_SETTINGS && !t.editing;
        if (settings
            && (t.selected == ROW_BOAT || t.selected == ROW_MOUSE || t.selected == ROW_MUSIC
                || t.selected == ROW_EDGES || t.selected == ROW_LIGHTING)) {
            int turn = (in.right || activate) - in.left;
            if (t.selected == ROW_BOAT)
                s->skin = (s->skin + BOAT_SKIN_COUNT + turn) % BOAT_SKIN_COUNT;
            else if (t.selected == ROW_MUSIC)
                s->soundtrack = (s->soundtrack + BOAT_MUSIC_COUNT + turn) % BOAT_MUSIC_COUNT;
            else if (t.selected == ROW_EDGES && turn)
                turn_picture(s, turn);
            else if (t.selected == ROW_LIGHTING)
                s->lighting
                    = (s->lighting + BOAT_TITLE_LIGHTING_COUNT + turn) % BOAT_TITLE_LIGHTING_COUNT;
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
                if (solo_row(&t, t.selected) == ROW_SOLO_START && t.solo_save)
                    work_action = boat_save_row_start(&t.save);
                else if (solo_row(&t, t.selected) == ROW_SOLO_START) {
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
                    if (wake_servers_compatible(
                            (int)t.public_shown, BOAT_NET_VERSION, BOAT_PHYSICS_REVISION)) {
                        wake_servers_text((int)t.public_shown, WAKE_SERVER_ADDRESS, s->address,
                            sizeof(s->address));
                        choice = BOAT_TITLE_JOIN;
                        chosen = true;
                    }
                } else if (t.selected == ROW_HOST_ROOM) {
                    t.panel = PANEL_HOST;
                    t.selected = ROW_START;
                } else if (t.selected == ROW_LAN) {
                    if (t.found.count) {
                        boat_net_lan_endpoint(
                            &t.found.rooms[t.shown], s->address, sizeof(s->address));
                        choice = BOAT_TITLE_JOIN;
                        chosen = true;
                    }
                } else if (t.selected == ROW_ADDRESS)
                    begin_edit(&t, s->address);
                else if (t.selected == ROW_CONNECT) {
                    choice = BOAT_TITLE_JOIN;
                    chosen = true;
                } else
                    back = true;
            } else if (t.selected == ROW_SINGLE) {
                t.panel = PANEL_SINGLE;
                solo_refresh(&t, s);
                t.selected = solo_index(&t, ROW_SOLO_START);
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
        if (t.panel == PANEL_SINGLE) {
            // The question shows on the SAVE row, whichever row started it.
            if (t.save.confirming && t.confirm_row != solo_index(&t, ROW_SOLO_SAVE))
                t.selected = solo_index(&t, ROW_SOLO_SAVE);
            t.confirm_row = t.selected;
            if (work_action == BOAT_SAVE_START_CONTINUE || work_action == BOAT_SAVE_START_NEW) {
                s->work_new = work_action == BOAT_SAVE_START_NEW;
                choice = BOAT_TITLE_SINGLE;
                chosen = true;
            } else if (work_action == BOAT_SAVE_DELETE_FILE) {
                bool ok = wake_work_title_delete(&t.slot);
                solo_refresh(&t, s);
                snprintf(s->message, sizeof(s->message), "%s",
                    ok ? "SAVE DELETED" : "COULD NOT DELETE THE SAVE");
            } else if (activate && t.solo_save && solo_row(&t, t.selected) == ROW_SOLO_START
                && !t.save.confirming && t.save.mode == BOAT_SAVE_DELETE)
                snprintf(s->message, sizeof(s->message), "CHOOSE CONTINUE OR NEW TO START");
        }
        if (back && t.panel == PANEL_SINGLE && boat_save_row_cancel(&t.save))
            back = false; // Escape first withdraws an open question.
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

bool boat_title_screenshot_panel(
    const BoatTitleSettings *settings, double time, const char *path, const BoatTitleCapture *cap)
{
    Title t;
    bool ok = false;
    if (open_title(&t)) {
        t.time = time;
        if (cap->solo || cap->ask) {
            t.panel = PANEL_SINGLE;
            solo_refresh(&t, settings);
            t.selected = solo_index(&t, t.solo_save ? ROW_SOLO_SAVE : ROW_SOLO_START);
            for (int i = 0; i < 3 && t.solo_save && (int)t.save.mode != cap->save_mode; ++i)
                boat_save_row_turn(&t.save, 1);
            if (cap->ask && t.save.mode == BOAT_SAVE_DELETE)
                boat_save_row_press(&t.save);
            else if (cap->ask)
                boat_save_row_start(&t.save);
        }
        draw_screen(&t, settings, NULL);
        Image image = LoadImageFromTexture(t.target.texture);
        ImageFlipVertical(&image);
        ok = ExportImage(image, path);
        UnloadImage(image);
    }
    close_title(&t);
    return ok;
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

void boat_title_defaults(BoatTitleSettings *s)
{
    *s = (BoatTitleSettings) { .name = "player",
        .skin = BOAT_SKIN_SWEDEN,
        .host_location = BOAT_LOCATION_OUTER,
        .location = BOAT_LOCATION_OUTER,
        .waves = true,
        .smooth_edges = true };
#if defined(__EMSCRIPTEN__)
    snprintf(s->address, sizeof(s->address), "ws://127.0.0.1:24682");
#else
    snprintf(s->address, sizeof(s->address), "127.0.0.1:%d", BOAT_NET_PORT);
#endif
}

void boat_title_load(BoatTitleSettings *s)
{
    boat_title_defaults(s);
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
        else if (!strcmp(line, "lighting")) {
            s->lighting = BOAT_TITLE_LIGHTING_DAY;
            for (unsigned i = 0; i < BOAT_TITLE_LIGHTING_COUNT; ++i)
                if (!strcmp(value, boat_title_lighting(i)))
                    s->lighting = i;
        } else if (!strcmp(line, "location")) {
            const BoatWorldDefinition *location = boat_location_find(value);
            if (!strcmp(value, "course"))
                s->location = BOAT_LOCATION_COUNT;
            else if (location)
                s->location = location->id;
        } else if (!strcmp(line, "waves"))
            s->waves = !strcmp(value, "on");
        else if (!strcmp(line, "smooth_edges"))
            s->smooth_edges = !strcmp(value, "on");
        else if (!strcmp(line, "resolution"))
            s->high_resolution = !strcmp(value, "high");
        else if (!strcmp(line, "outer_content")) {
            for (unsigned i = 0; i < content_count(); ++i)
                if (!strcmp(value, outer_contents[i].name))
                    s->outer_content = i;
        } else if (!strcmp(line, "host_mode")) {
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
    fprintf(file, "location=%s\nwaves=%s\nsmooth_edges=%s\nouter_content=%s\n",
        s->location == BOAT_LOCATION_COUNT ? "course" : boat_location(s->location)->name,
        s->waves ? "on" : "off", s->smooth_edges ? "on" : "off",
        boat_title_outer_content(s->outer_content));
    fprintf(file, "resolution=%s\n", s->high_resolution ? "high" : "low");
    fprintf(file, "lighting=%s\n", boat_title_lighting(s->lighting));
    fprintf(file, "host_mode=%s\nhost_location=%s\nhost_waves=%s\n",
        boat_title_host_mode(s->host_mode), boat_location(s->host_location)->name,
        s->host_waves ? "on" : "off");
    fclose(file);
}
