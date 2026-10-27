#include "boat_hud.h"
#include "boat_throttle.h"
#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const Color face = { 20, 34, 39, 170 };
static const Color ink = { 218, 223, 202, 255 };
static const Color muted = { 133, 158, 153, 255 };
static const Color ahead = { 118, 196, 178, 255 };
static const Color amber = { 233, 180, 104, 255 };

typedef struct {
    int x, y;
} Panel;

static float bounded(float value, float low, float high)
{
    return isfinite(value) ? fmaxf(low, fminf(high, value)) : 0;
}

static void box(Panel p, int x, int y, int w, int h, Color color)
{
    DrawRectangle(p.x + x, p.y + y, w, h, color);
}

// Rasterize on whole framebuffer pixels, including diagonal needles and circular bezels.
static void line(Panel p, int x, int y, int end_x, int end_y, Color color)
{
    int dx = abs(end_x - x), sx = x < end_x ? 1 : -1;
    int dy = -abs(end_y - y), sy = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        box(p, x, y, 1, 1, color);
        if (x == end_x && y == end_y)
            break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
}

static void circle(Panel p, int x, int y, int radius, Color color)
{
    for (int row = -radius; row <= radius; ++row) {
        int half = (int)sqrtf((float)(radius * radius - row * row));
        box(p, x - half, y + row, half * 2 + 1, 1, color);
    }
}

// Three-by-five lettering: each row is a three-bit mask. Every stroke is one scene pixel.
static void label(Panel p, const char *text, int x, int y, Color color)
{
    static const struct {
        char character;
        const char *rows;
    } glyphs[] = { { '0', "75557" }, { '1', "26227" }, { '2', "71247" }, { '3', "71217" },
        { '4', "55711" }, { '5', "74717" }, { '6', "74757" }, { '7', "71111" }, { '8', "75757" },
        { '9', "75717" }, { '.', "00002" }, { ':', "02020" }, { 'L', "44447" }, { '%', "51451" },
        { '+', "02720" }, { '-', "00700" }, { 'C', "74447" }, { 'D', "65556" }, { 'E', "74647" },
        { 'F', "74644" }, { 'H', "55755" }, { 'I', "72227" }, { 'K', "55655" }, { 'O', "75557" },
        { 'N', "57775" }, { 'P', "75544" }, { 'R', "75655" }, { 'S', "74717" }, { 'T', "72222" },
        { 'U', "55557" }, { 'A', "25755" }, { 'B', "65656" }, { 'M', "57755" }, { 'V', "55552" },
        { 'W', "55577" }, { 'Y', "55222" }, { 'Q', "75573" } };
    int left = x - ((int)strlen(text) * 4 - 1) / 2;
    for (const char *c = text; *c; ++c, left += 4) {
        for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
            if (glyphs[i].character != *c)
                continue;
            for (int row = 0; row < 5; ++row)
                for (int col = 0; col < 3; ++col)
                    if ((glyphs[i].rows[row] - '0') & (4 >> col))
                        box(p, left + col, y + row, 1, 1, color);
            break;
        }
    }
}

static void radial(Panel p, int x, int y, float angle, int inner, int outer, Color color)
{
    float radians = angle * .01745329252f;
    line(p, x + (int)roundf(sinf(radians) * inner), y - (int)roundf(cosf(radians) * inner),
        x + (int)roundf(sinf(radians) * outer), y - (int)roundf(cosf(radians) * outer), color);
}

void boat_hud_draw(BoatHudTelemetry telemetry, int width, int height)
{
    if (width < 168 || height < 80)
        return;
    Panel p = { (width - 166) / 2, height - 46 };
    float thrust = bounded(telemetry.thrust, -1, 1);
    float steering = bounded(telemetry.steering, -1, 1);
    float knots = bounded(telemetry.speed_mps, 0, 999) * 1.94384449f;
    Color thrust_color = thrust < -.005f ? amber : ahead;

    box(p, 1, 2, 166, 44, (Color) { 4, 11, 15, 110 });
    box(p, 0, 0, 166, 44, (Color) { 56, 74, 77, 130 });
    box(p, 1, 1, 164, 1, (Color) { 153, 166, 157, 255 });
    box(p, 2, 3, 162, 38, face);
    for (int x = 4; x <= 162; x += 158)
        for (int y = 5; y <= 39; y += 34) {
            box(p, x - 1, y - 1, 3, 3, muted);
            line(p, x - 1, y + 1, x + 1, y - 1, face);
        }
    line(p, 54, 8, 54, 36, (Color) { 43, 61, 65, 255 });
    line(p, 112, 8, 112, 36, (Color) { 43, 61, 65, 255 });

    label(p, "THRUST", 28, 7, muted);
    // Give each detent distinct travel, including the low-power docking settings.
    box(p, 26, 14, 5, 23, (Color) { 8, 20, 25, 255 });
    int travel = (int)roundf(boat_throttle_position(thrust) * 11);
    box(p, 28, travel < 0 ? 25 + travel : 25, 1, abs(travel), thrust_color);
    for (int i = 0; i < BOAT_THROTTLE_COUNT; ++i) {
        int y = 25 + (int)roundf(boat_throttle_position(boat_throttle_setting(i)) * 11);
        line(p, 20, y, 23, y, muted);
    }
    line(p, 24, 25, 33, 25, muted);
    box(p, 24, 24 + travel, 9, 3, ink);
    box(p, 25, 25 + travel, 7, 1, thrust_color);
    label(p, "F", 39, 12, ahead);
    label(p, "N", 39, 23, muted);
    label(p, "R", 39, 34, amber);

    circle(p, 83, 22, 20, muted);
    circle(p, 83, 22, 19, (Color) { 8, 21, 26, 255 });
    circle(p, 83, 22, 17, face);
    for (int tick = 0; tick <= 12; ++tick)
        radial(p, 83, 22, -130 + tick * (260.0f / 12), tick % 2 ? 17 : 15, 18, muted);
    label(p, "SPEED", 83, 12, muted);
    radial(p, 83, 22, -130 + bounded(knots / 60, 0, 1) * 260, -3, 14, amber);
    circle(p, 83, 22, 2, ink);

    label(p, "STEER", 139, 7, muted);
    for (int tick = -2; tick <= 2; ++tick)
        radial(p, 139, 27, tick * 32.0f, 10, 12, muted);
    radial(p, 139, 27, steering * 64, 0, 11, ahead);
    circle(p, 139, 27, 1, ink);
    label(p, "P", 122, 22, muted);
    label(p, "S", 156, 22, muted);
    if (telemetry.precision)
        label(p, "PRECISION", 83, -8, ahead);
    float progress = bounded(telemetry.reset_progress, 0, 1);
    if (progress > 0) {
        box(p, 45, -22, 76, 12, face);
        label(p, "HOLD: RESET", 83, -21, amber);
        box(p, 47, -13, 72, 2, muted);
        box(p, 47, -13, (int)roundf(72 * progress), 2, amber);
    }
}

static void instrument(Panel p)
{
    box(p, 1, 2, 54, 44, (Color) { 4, 11, 15, 110 });
    box(p, 0, 0, 54, 44, (Color) { 56, 74, 77, 130 });
    box(p, 1, 1, 52, 1, (Color) { 153, 166, 157, 255 });
    box(p, 2, 3, 50, 38, face);
    box(p, 4, 5, 2, 2, muted);
    box(p, 48, 37, 2, 2, muted);
}

void boat_hud_combat_draw(BoatHudCombat combat, int width, int height)
{
    if (width < 286 || height < 80)
        return;
    Panel hull = { (width - 166) / 2 - 58, height - 46 };
    Panel weapons = { (width - 166) / 2 + 170, height - 46 };
    instrument(hull);
    instrument(weapons);
    label(hull, "HULL", 27, 7, muted);
    float health = bounded(combat.hull, 0, 100);
    Color color = health <= 30 ? amber : ahead;
    char text[24];
    snprintf(text, sizeof(text), "%.0f", health);
    label(hull, text, 27, 17, color);
    for (int i = 0; i < 10; ++i)
        box(hull, 8 + i * 4, 25, 3, 4, health >= (float)(i + 1) * 10 ? color : muted);
    label(hull, "HEEL", 19, 34, muted);
    box(hull, 32, 34, 13, 4, muted);
    box(hull, 32, 34, (int)(13 * bounded(combat.pressure, 0, 1)), 4, amber);
    label(weapons, combat.overheated ? "COOL" : "SPACE", 27, 7, combat.overheated ? amber : muted);
    box(weapons, 8, 15, 38, 3, muted);
    box(weapons, 8, 15, (int)(38 * bounded(combat.heat, 0, 1)), 3, amber);
    if (combat.grenade_seconds > 0)
        snprintf(text, sizeof(text), "E %.1f", combat.grenade_seconds);
    else
        snprintf(text, sizeof(text), "E READY");
    label(weapons, text, 27, 23, combat.grenade_seconds > 0 ? muted : ahead);
    snprintf(text, sizeof(text), "Q %u", combat.torpedoes);
    label(weapons, text, 27, 33, combat.torpedoes && combat.torpedo_seconds <= 0 ? ahead : amber);
}
