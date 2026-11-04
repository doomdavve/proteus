#include "boat_voyage_view.h"
#include "boat_ui.h"
#include "raylib.h"

#include <math.h>
#include <stdio.h>

#define NOTICE_SECONDS 4.0
#define CAPTION_SECONDS 8.0 // A tour stop's caption takes longer to read.

static const char *notice(const BoatVoyage *voyage, double time)
{
    const BoatVoyageRoute *route = voyage->route;
    const char *shelter = route->shelters[voyage->event_shelter].name;
    double seconds
        = route->ordered && voyage->event == BOAT_VOYAGE_SHELTER ? CAPTION_SECONDS : NOTICE_SECONDS;
    if (voyage->event != BOAT_VOYAGE_NONE && time - voyage->event_time < seconds)
        switch (voyage->event) {
        case BOAT_VOYAGE_SHELTER:
            if (route->ordered && voyage->event_shelter + 1 < route->shelter_count)
                return route->shelters[voyage->event_shelter].caption;
            return TextFormat("REACHED %s", shelter);
        case BOAT_VOYAGE_EXPOSED:
            return "EXPOSED WATER";
        case BOAT_VOYAGE_SHELTERED:
            return "OUT OF THE EXPOSED WATER";
        case BOAT_VOYAGE_ARRIVED:
            return TextFormat("ARRIVED AT %s", shelter);
        case BOAT_VOYAGE_RECOVERED:
            return TextFormat("RECOVERED AT %s", shelter);
        case BOAT_VOYAGE_WEATHER:
            return "SQUALL BUILDING OFFSHORE";
        case BOAT_VOYAGE_NONE:
            break;
        }
    if (voyage->too_fast)
        return "SLOW DOWN TO ARRIVE";
    if (voyage->exposed)
        return "EXPOSED WATER";
    if (route->ordered && !voyage->arrived)
        return TextFormat("STOP %u OF %u", boat_voyage_next(voyage), route->shelter_count - 1);
    return TextFormat("SHELTER: %s", route->shelters[voyage->shelter].name);
}

void boat_voyage_hud(const BoatVoyage *voyage, const Boat *boat, double time, int width)
{
    if (!voyage->route)
        return;
    const Color panel = { 14, 23, 32, 245 }, accent = { 255, 216, 93, 255 };
    const Color warning = { 255, 161, 0, 255 };
    float distance;
    float bearing = boat_voyage_bearing(voyage, boat->position, boat->heading, &distance);
    // A tour's names and captions can be longer than a destination's: the panel grows to fit.
    const char *name = voyage->route->shelters[boat_voyage_next(voyage)].name;
    const char *headline;
    if (voyage->arrived) {
        int seconds = (int)(voyage->arrival - voyage->started);
        headline = TextFormat("%s  %d:%02d", name, seconds / 60, seconds % 60);
    } else if (distance < 1000)
        headline = TextFormat("%s  %d M", name, (int)distance);
    else
        headline = TextFormat("%s  %.1f KM", name, distance / 1000);
    char line[96];
    snprintf(line, sizeof(line), "%s", headline);
    const char *note = notice(voyage, time);
    int text = boat_ui_width(line, 20) > boat_ui_width(note, 12) ? boat_ui_width(line, 20)
                                                                 : boat_ui_width(note, 12);
    int panel_width = 56 + text + 12 > 260 ? 56 + text + 12 : 260;
    int left = width / 2 - panel_width / 2, top = 8;
    DrawRectangle(left, top, panel_width, 52, panel);
    // Up is straight ahead; the arrow turns the way the boat must steer.
    Vector2 centre = { (float)left + 26, (float)top + 26 };
    Vector2 tip = { centre.x + sinf(bearing) * 16, centre.y - cosf(bearing) * 16 };
    Vector2 back_left
        = { centre.x + sinf(bearing - 2.5f) * 12, centre.y - cosf(bearing - 2.5f) * 12 };
    Vector2 back_right
        = { centre.x + sinf(bearing + 2.5f) * 12, centre.y - cosf(bearing + 2.5f) * 12 };
    DrawCircleLines((int)centre.x, (int)centre.y, 20, (Color) { 90, 104, 116, 255 });
    DrawTriangle(tip, back_left, back_right, voyage->arrived ? accent : RAYWHITE);
    boat_ui_text(line, left + 56, top + 8, 20, RAYWHITE);
    bool alert = voyage->too_fast || voyage->exposed;
    boat_ui_text(note, left + 56, top + 32, 12, alert ? warning : accent);
}
