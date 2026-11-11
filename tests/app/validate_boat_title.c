#include "wake_velocity/boat_title.h"

#include "core/boat_location.h"
#include "render/boat_skin.h"
#include "session/boat_replay.h"
#include "wake_velocity/boat_music.h"
#include "wake_velocity/wake_velocity_servers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// T3: title lighting settings persistence and the title's command-line lighting hand-off.
// File fixtures live under the build directory; no window, input or audio is initialized.
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "title settings check failed at %d: %s\n", __LINE__, #condition);      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char directory[1024];
static char settings_file[1100];

bool boat_replay_data_directory(char *path, size_t size)
{
    int count = snprintf(path, size, "%s", directory);
    return count > 0 && (size_t)count < size;
}

// These settings tests never enter the Single Player working-harbour panel.
void wake_work_title_scan(const char *data_dir, BoatTitleSave *save)
{
    (void)data_dir;
    (void)save;
    abort();
}

bool wake_work_title_delete(const BoatTitleSave *save)
{
    (void)save;
    abort();
}

bool wake_work_title_set_aside(const BoatTitleSave *save)
{
    (void)save;
    abort();
}

// The title object declares its server-discovery helpers, but the settings tests never open or
// draw the multiplayer panel.
void wake_servers_start(void) { abort(); }
void wake_servers_stop(void) { abort(); }
void wake_servers_refresh(void) { abort(); }
void wake_servers_poll(void) { abort(); }
int wake_servers_count(void) { abort(); }
bool wake_servers_compatible(int index, int protocol, int physics)
{
    (void)index;
    (void)protocol;
    (void)physics;
    abort();
}
void wake_servers_text(int index, WakeServerField field, char *out, size_t size)
{
    (void)index;
    (void)field;
    (void)out;
    (void)size;
    abort();
}
void wake_servers_status(char *out, size_t size)
{
    (void)out;
    (void)size;
    abort();
}
WakeLanSearch *wake_lan_open(void) { abort(); }
void wake_lan_query(WakeLanSearch *search)
{
    (void)search;
    abort();
}
bool wake_lan_receive(WakeLanSearch *search, BoatNetLanRoom *room)
{
    (void)search;
    (void)room;
    abort();
}
void wake_lan_close(WakeLanSearch *search)
{
    (void)search;
    abort();
}

static void write_text(const char *text)
{
    FILE *file = fopen(settings_file, "wb");
    CHECK(file != NULL);
    size_t size = strlen(text);
    CHECK(fwrite(text, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void load_text(BoatTitleSettings *settings, const char *text)
{
    write_text(text);
    boat_title_load(settings);
}

static void load_lighting(BoatTitleSettings *settings, const char *lighting)
{
    char text[96];
    snprintf(text, sizeof(text), "name=fixture\nlighting=%s\n", lighting);
    load_text(settings, text);
}

static bool file_contains(const char *line)
{
    char text[2048];
    FILE *file = fopen(settings_file, "rb");
    CHECK(file != NULL);
    size_t size = fread(text, 1, sizeof(text) - 1, file);
    CHECK(!ferror(file));
    CHECK(fclose(file) == 0);
    text[size] = 0;
    return strstr(text, line) != NULL;
}

static void check_missing_and_old(void)
{
    BoatTitleSettings settings;
    remove(settings_file);
    boat_title_load(&settings);
    CHECK(settings.lighting == BOAT_TITLE_LIGHTING_DAY);

    load_text(&settings,
        "name=legacy\naddress=127.0.0.1:24680\nskin=sweden\nsoundtrack=off\nwaves=off\n");
    CHECK(settings.lighting == BOAT_TITLE_LIGHTING_DAY);
    CHECK(!strcmp(settings.name, "legacy"));
}

static void check_legacy_and_hours(void)
{
    static const char *const preset_names[] = { "day", "sunset", "dusk" };
    BoatTitleSettings settings;
    for (unsigned i = 0; i < BOAT_TITLE_LIGHTING_HOURS; ++i) {
        load_lighting(&settings, preset_names[i]);
        CHECK(settings.lighting == i);
        CHECK(boat_title_lighting_hour(settings.lighting) == -1);
        CHECK(!strcmp(boat_title_lighting(settings.lighting), preset_names[i]));
    }

    for (unsigned hour = 0; hour < 24; ++hour) {
        char value[16], fixture[96], line[32];
        snprintf(value, sizeof(value), "hour-v1:%02u", hour);
        snprintf(fixture, sizeof(fixture), "name=hour\nlighting=%s\n", value);
        load_text(&settings, fixture);
        unsigned expected = BOAT_TITLE_LIGHTING_HOURS + hour;
        CHECK(settings.lighting == expected);
        CHECK(boat_title_lighting_hour(settings.lighting) == (int)hour);
        CHECK(!strcmp(boat_title_lighting(settings.lighting), value));

        boat_title_save(&settings);
        snprintf(line, sizeof(line), "lighting=%s\n", value);
        CHECK(file_contains(line));
        BoatTitleSettings again;
        boat_title_load(&again);
        CHECK(again.lighting == expected);
        CHECK(boat_title_lighting_hour(again.lighting) == (int)hour);
    }
}

static void check_rejected(void)
{
    static const char *const rejected[] = { "hour-v2:07", "hour-v1:7", "hour-v1:007", "7",
        "hour-v1:24", "hour-v1:-1", "hour-v1:nan", "hour-v1:inf", "hour-v1:00x", "" };
    BoatTitleSettings settings;
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        load_lighting(&settings, rejected[i]);
        CHECK(settings.lighting == BOAT_TITLE_LIGHTING_DAY);
    }
}

static void check_invalid_enum(void)
{
    BoatTitleSettings settings;
    boat_title_defaults(&settings);
    settings.lighting = BOAT_TITLE_LIGHTING_COUNT;
    CHECK(!strcmp(boat_title_lighting(settings.lighting), "day"));
    settings.lighting = 999;
    CHECK(!strcmp(boat_title_lighting(settings.lighting), "day"));
    boat_title_save(&settings);
    CHECK(file_contains("lighting=day\n"));
}

static void expect_cli(
    unsigned lighting, int extra_count, char **extra, const char *option, const char *value)
{
    const char *shown_option = "untouched", *shown_value = "untouched";
    CHECK(boat_title_lighting_cli(lighting, extra_count, extra, &shown_option, &shown_value));
    CHECK(!strcmp(shown_option, option));
    CHECK(!strcmp(shown_value, value));
}

static void expect_suppressed(unsigned lighting, int extra_count, char **extra)
{
    const char *shown_option = "untouched", *shown_value = "untouched";
    CHECK(!boat_title_lighting_cli(lighting, extra_count, extra, &shown_option, &shown_value));
    CHECK(!strcmp(shown_option, "untouched"));
    CHECK(!strcmp(shown_value, "untouched"));
}

static void check_cli(void)
{
    static const char *const preset_names[] = { "day", "sunset", "dusk" };
    for (unsigned i = 0; i < BOAT_TITLE_LIGHTING_HOURS; ++i)
        expect_cli(i, 0, NULL, "--daylight", preset_names[i]);
    for (unsigned hour = 0; hour < 24; ++hour) {
        char value[3];
        snprintf(value, sizeof(value), "%02u", hour);
        expect_cli(BOAT_TITLE_LIGHTING_HOURS + hour, 0, NULL, "--lighting-hour", value);
    }
    expect_cli(999, 0, NULL, "--daylight", "day");
}

static void check_cli_priority(void)
{
    char *daylight[] = { "--daylight", "sunset" };
    char *hour[] = { "--lighting-hour", "7" };
    char *both_a[] = { "--daylight", "sunset", "--lighting-hour", "7" };
    char *both_b[] = { "--lighting-hour", "7", "--daylight", "sunset" };
    char *unrelated[] = { "--verbose", "--daylighting", "sunset", "--lighting-hours", "7" };
    const unsigned saved[] = { BOAT_TITLE_LIGHTING_DAY, BOAT_TITLE_LIGHTING_HOURS + 13 };

    for (unsigned i = 0; i < sizeof(saved) / sizeof(saved[0]); ++i) {
        expect_suppressed(saved[i], 2, daylight);
        expect_suppressed(saved[i], 2, hour);
        expect_suppressed(saved[i], 4, both_a);
        expect_suppressed(saved[i], 4, both_b);
    }
    expect_cli(saved[0], 5, unrelated, "--daylight", "day");
    expect_cli(saved[1], 5, unrelated, "--lighting-hour", "13");
}

static void check_other_fields(void)
{
    BoatTitleSettings saved, loaded;
    boat_title_defaults(&saved);
    snprintf(saved.name, sizeof(saved.name), "T3 Tester");
    snprintf(saved.address, sizeof(saved.address), "example.test:24680");
    saved.skin = BOAT_SKIN_CABIN;
    saved.mouse_steering = true;
    saved.soundtrack = BOAT_MUSIC_ACID_WAKE;
    saved.location = BOAT_LOCATION_BASIN;
    saved.waves = false;
    saved.smooth_edges = false;
    saved.high_resolution = true;
    saved.outer_content = BOAT_TITLE_OUTER_GRODAN;
    saved.lighting = BOAT_TITLE_LIGHTING_HOURS + 17;
    saved.host_mode = BOAT_TITLE_HOST_RACE;
    saved.host_location = BOAT_LOCATION_BASIN;
    saved.host_waves = true;

    boat_title_save(&saved);
    boat_title_load(&loaded);
    CHECK(!strcmp(loaded.name, "T3 Tester"));
    CHECK(!strcmp(loaded.address, "example.test:24680"));
    CHECK(loaded.skin == BOAT_SKIN_CABIN);
    CHECK(loaded.mouse_steering);
    CHECK(loaded.soundtrack == BOAT_MUSIC_ACID_WAKE);
    CHECK(loaded.location == BOAT_LOCATION_BASIN);
    CHECK(!loaded.waves);
    CHECK(!loaded.smooth_edges);
    CHECK(loaded.high_resolution);
    CHECK(loaded.outer_content == BOAT_TITLE_OUTER_GRODAN);
    CHECK(loaded.lighting == BOAT_TITLE_LIGHTING_HOURS + 17);
    CHECK(boat_title_lighting_hour(loaded.lighting) == 17);
    CHECK(loaded.host_mode == BOAT_TITLE_HOST_RACE);
    CHECK(loaded.host_location == BOAT_LOCATION_BASIN);
    CHECK(loaded.host_waves);
    CHECK(file_contains("lighting=hour-v1:17\n"));
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    snprintf(directory, sizeof(directory), "%s", argv[1]);
    snprintf(settings_file, sizeof(settings_file), "%s/title.txt", directory);
    remove(settings_file);

    check_missing_and_old();
    check_legacy_and_hours();
    check_rejected();
    check_invalid_enum();
    check_cli();
    check_cli_priority();
    check_other_fields();
    printf("boat title settings checks passed\n");
    return 0;
}
