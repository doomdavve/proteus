#include "boat_title.h"
#include "boat_assets.h"
#include "boat_location.h"
#include "boat_music.h"
#include "boat_net_protocol.h"
#include "boat_replay.h"
#include "boat_skin.h"
#include "raylib.h"
#ifdef PROTEUS_BOAT_NET
#include "boat_net_enet.h"
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { WIDTH = 320, HEIGHT = 200, ROW_HEIGHT = 16 };

typedef enum { ROW_SINGLE, ROW_MULTI, ROW_SETTINGS, ROW_EXIT, MAIN_ROWS } MainRow;
typedef enum { ROW_HOST_ROOM, ROW_LAN, ROW_ADDRESS, ROW_CONNECT, ROW_BACK, JOIN_ROWS } JoinRow;
typedef enum { ROW_MODE, ROW_PLACE, ROW_WATER, ROW_START, ROW_HOST_BACK, HOST_ROWS } HostRow;
typedef enum { ROW_NAME, ROW_BOAT, ROW_MOUSE, ROW_MUSIC, ROW_DONE, SETTINGS_ROWS } SettingsRow;
typedef enum { PANEL_MAIN, PANEL_JOIN, PANEL_HOST, PANEL_SETTINGS } Panel;

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
#ifdef PROTEUS_BOAT_NET
    BoatNetLan *lan;
#endif
    bool searching, moved; // MOVED: the player has picked a row since opening the panel.
    BoatNetLanList found;
    unsigned shown; // The found room the LAN row offers.
    double queried;
} Title;

static void stop_search(Title *t)
{
#ifdef PROTEUS_BOAT_NET
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
#ifdef PROTEUS_BOAT_NET
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
#ifdef PROTEUS_BOAT_NET
    if (!t->searching)
        return;
    if (t->queried < 0 || t->time - t->queried >= 1) {
        boat_net_lan_query(t->lan, BOAT_NET_DISCOVERY_PORT);
        t->queried = t->time;
    }
    bool none = t->found.count == 0;
    BoatNetLanRoom room;
    while (boat_net_lan_receive(t->lan, &room))
        boat_net_lan_list_note(&t->found, &room, t->time);
    boat_net_lan_list_expire(&t->found, t->time, 3.5);
    if (t->shown >= t->found.count)
        t->shown = 0;
    if (none && t->found.count && !t->moved && t->selected == ROW_CONNECT)
        t->selected = ROW_LAN; // The first room found is the likely choice.
#else
    (void)t;
#endif
}

static Color rgb(unsigned char r, unsigned char g, unsigned char b)
{
    return (Color) { r, g, b, 255 };
}

static void draw_bent_text(const char *label, int y, int size, Color face, Color edge, Color shadow)
{
    Font font = GetFontDefault();
    size_t length = strlen(label);
    float widths[32], total = 0;
    for (size_t i = 0; i < length; ++i) {
        char glyph[] = { label[i], '\0' };
        widths[i] = label[i] == ' ' ? size * .34f : MeasureTextEx(font, glyph, size, 0).x;
        total += widths[i] + (i + 1 < length ? 1 : 0);
    }
    float cursor = (WIDTH - total) * .5f;
    for (size_t i = 0; i < length; ++i) {
        char glyph[] = { label[i], '\0' };
        float center = cursor + widths[i] * .5f;
        float side = (center - WIDTH * .5f) / (total * .5f);
        float top = y + 13 * side * side;
        float turn = 12 * side;
        Vector2 origin = { widths[i] * .5f, 0 };
        for (int depth = 5; depth >= 1; --depth) {
            Color layer = depth == 5 ? shadow : edge;
            DrawTextPro(font, glyph, (Vector2) { center + depth, top + depth + 1 }, origin, turn,
                size, 0, layer);
        }
        DrawTextPro(
            font, glyph, (Vector2) { center + 1, top + 1 }, origin, turn, size, 0, rgb(48, 55, 66));
        DrawTextPro(
            font, glyph, (Vector2) { center, top - 1 }, origin, turn, size, 0, rgb(255, 252, 224));
        DrawTextPro(font, glyph, (Vector2) { center, top }, origin, turn, size, 0, face);
        cursor += widths[i] + 1;
    }
}

static void draw_logo(double time)
{
    int bob = (int)round(1.5 * sin(time * 2.1));
    draw_bent_text(
        "WAKE VELOCITY", 7 + bob, 30, rgb(255, 239, 177), rgb(234, 133, 92), rgb(22, 50, 76));
    const char *credit = "A PROTEUS GAME";
    DrawText(credit, (WIDTH - MeasureText(credit, 10)) / 2 + 1, 64, 10, rgb(25, 63, 79));
    DrawText(credit, (WIDTH - MeasureText(credit, 10)) / 2, 63, 10, rgb(255, 248, 216));
}

static int row_count(const Title *t)
{
    return t->panel == PANEL_JOIN    ? JOIN_ROWS
        : t->panel == PANEL_HOST     ? HOST_ROWS
        : t->panel == PANEL_SETTINGS ? SETTINGS_ROWS
                                     : MAIN_ROWS;
}

static Rectangle row_bounds(const Title *t, int row)
{
    int top = 92 + (5 - row_count(t)) * ROW_HEIGHT / 2; // Centred on five rows' space.
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
    if (t->panel == PANEL_SETTINGS) {
        static const char *labels[SETTINGS_ROWS]
            = { "NAME", "BOAT", "STEERING", "SOUNDTRACK", "BACK" };
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
            = { "HOST A ROOM", "LAN", "ADDRESS", "CONNECT", "BACK" };
        *label = labels[row];
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
#ifdef PROTEUS_BOAT_NET
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
}

// What the LAN row's room is, or how to make one appear.
static const char *lan_hint(const Title *t, char *out, size_t size)
{
    if (!t->searching)
        return "NO LAN SEARCH: TYPE AN ADDRESS BELOW";
    if (!t->found.count)
        return "NO ROOMS FOUND ON THIS NETWORK YET";
#ifndef PROTEUS_BOAT_NET
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
    DrawRectangle(37, top, 246, height, rgb(13, 45, 67));
    DrawRectangleLines(37, top, 246, height, rgb(140, 210, 199));
    for (int i = 0; i < row_count(t); ++i) {
        Rectangle row = row_bounds(t, i);
        bool active = i == t->selected;
        if (active) {
            DrawRectangleRec(row, rgb(241, 184, 99));
            DrawRectangleLinesEx(row, 1, rgb(255, 232, 168));
            int tick = (int)round(sin(t->time * 4.0));
            DrawText("(*)", 50, (int)row.y + 2 + tick, 10, rgb(33, 55, 66));
        }
        Color ink = active ? rgb(29, 52, 68) : rgb(229, 241, 220);
        const char *label;
        char value[96];
        row_text(t, s, i, &label, value, sizeof(value));
        DrawText(label, 72, (int)row.y + 2, 10, ink);
        if (value[0]) {
            int value_x = t->panel == PANEL_SETTINGS && i == ROW_MUSIC ? 154 : 142;
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
        hint = "SINGLE PLAYER; ROOMS COLOUR BOATS BY SLOT";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_MOUSE)
        hint = "REMEMBERED; ESC MENU OR M SWITCHES IN GAME";
    if (!hint && t->panel == PANEL_SETTINGS && t->selected == ROW_MUSIC)
        hint = "LEFT / RIGHT OR CLICK: CHOOSE MUSIC";
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
        int x = (WIDTH - MeasureText(shown, 10)) / 2, y = top + height + 3;
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
    DrawRectangle(0, 0, WIDTH, 88, (Color) { 9, 43, 70, 75 });
    draw_logo(t->time);
    draw_menu(t, s, status);
    const char *map_credit = "MAP: OSM CONTRIBUTORS";
    draw_footer_text("PROTEUS  2026", 8);
    draw_footer_text(map_credit, WIDTH - 9 - MeasureText(map_credit, 7));
    EndTextureMode();
}

static int scale(void)
{
    int x = GetScreenWidth() / WIDTH, y = GetScreenHeight() / HEIGHT;
    return x < y ? (x < 1 ? 1 : x) : (y < 1 ? 1 : y);
}

static void present(const Title *t)
{
    int k = scale();
    Rectangle destination = { (float)((GetScreenWidth() - WIDTH * k) / 2),
        (float)((GetScreenHeight() - HEIGHT * k) / 2), (float)(WIDTH * k), (float)(HEIGHT * k) };
    BeginDrawing();
    ClearBackground(rgb(10, 38, 57));
    DrawTexturePro(t->target.texture, (Rectangle) { 0, 0, WIDTH, -HEIGHT }, destination,
        (Vector2) { 0 }, 0, WHITE);
    EndDrawing();
}

static bool open_title(Title *t)
{
    char path[4096];
    *t = (Title) { 0 };
    if (!boat_asset_path(path, sizeof(path), "title/river-backdrop.png"))
        return false;
    t->backdrop = LoadTexture(path);
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
        if (t.panel == PANEL_JOIN && t.selected == ROW_LAN && !t.editing && t.found.count > 1) {
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            t.shown = (unsigned)((int)(t.shown + t.found.count) + turn) % t.found.count;
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
            && (t.selected == ROW_BOAT || t.selected == ROW_MOUSE || t.selected == ROW_MUSIC)) {
            int turn = (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || activate)
                - (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
            if (t.selected == ROW_BOAT)
                s->skin = (s->skin + BOAT_SKIN_COUNT + turn) % BOAT_SKIN_COUNT;
            else if (t.selected == ROW_MUSIC)
                s->soundtrack = (s->soundtrack + BOAT_MUSIC_COUNT + turn) % BOAT_MUSIC_COUNT;
            else if (turn)
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
            } else if (t.panel == PANEL_HOST) {
                if (t.selected == ROW_START) {
                    choice = BOAT_TITLE_HOST;
                    chosen = true;
                } else
                    back = true;
            } else if (t.panel == PANEL_JOIN) {
                if (t.selected == ROW_HOST_ROOM) {
                    t.panel = PANEL_HOST;
                    t.selected = ROW_START;
                } else if (t.selected == ROW_LAN) {
#ifdef PROTEUS_BOAT_NET
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
                choice = BOAT_TITLE_SINGLE;
                chosen = true;
            } else if (t.selected == ROW_MULTI) {
                t.panel = PANEL_JOIN;
                t.selected = ROW_CONNECT;
                start_search(&t);
            } else if (t.selected == ROW_SETTINGS) {
                t.panel = PANEL_SETTINGS;
                t.selected = ROW_MOUSE;
            } else if (t.selected == ROW_EXIT)
                chosen = true;
        }
        if (back && t.panel == PANEL_HOST) {
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
    *s = (BoatTitleSettings) {
        .name = "player", .skin = BOAT_SKIN_SWEDEN, .host_location = BOAT_LOCATION_RIVER
    };
    snprintf(s->address, sizeof(s->address), "127.0.0.1:%d", BOAT_NET_PORT);
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
    fprintf(file, "host_mode=%s\nhost_location=%s\nhost_waves=%s\n",
        boat_title_host_mode(s->host_mode), boat_location(s->host_location)->name,
        s->host_waves ? "on" : "off");
    fclose(file);
}
