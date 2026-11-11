#include "boat_directory.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *json, BoatDirectoryList *list)
{
    return boat_directory_parse(json, strlen(json), list);
}
int main(void)
{
    const char *first = "{\"version\":1,\"servers\":[{\"name\":\"Winterfell\","
                        "\"description\":\"River\",\"native\":\"wake.davve.net:24680\","
                        "\"protocol\":6,\"physics\":9}]}";
    BoatDirectoryList list = { 0 };
    assert(parse(first, &list));
    assert(list.count == 1 && !strcmp(list.entries[0].address, "wake.davve.net:24680"));
    assert(list.entries[0].protocol == 6 && list.entries[0].physics == 9);
    assert(!parse("{\"version\":1 \"servers\":[]}", &list));
    assert(list.count == 1);
    assert(!parse("{\"version\":2,\"servers\":[]}", &list));
    assert(!parse("{\"version\":1,\"servers\":[{\"name\":\"A\",\"description\":\"B\","
                  "\"native\":\"bad\",\"protocol\":6,\"physics\":9}]}",
        &list));
    assert(parse("{\"version\":1,\"servers\":[{\"name\":\"A\",\"description\":\"B\","
                 "\"protocol\":6,\"physics\":9}]}",
        &list));
    assert(list.count == 0);
    const char *two = "{\"version\":1,\"servers\":["
                      "{\"name\":\"One\",\"description\":\"First\",\"native\":\"one.example:1\","
                      "\"protocol\":6,\"physics\":9},"
                      "{\"name\":\"Two\",\"description\":\"Second\",\"native\":\"two.example:2\","
                      "\"protocol\":7,\"physics\":10}]}";
    assert(parse(two, &list) && list.count == 2);
    assert(!strcmp(list.entries[1].name, "Two") && list.entries[1].protocol == 7);
    assert(!parse("{\"version\":1,\"servers\":[,]}", &list));
    assert(list.count == 2); // Failed refresh leaves the previous list unchanged.
    assert(!parse("{\"version\":1,\"version\":1,\"servers\":[]}", &list));
    char many[2048] = "{\"version\":1,\"servers\":[";
    for (int i = 0; i < 9; ++i) {
        if (i)
            strcat(many, ",");
        strcat(many,
            "{\"name\":\"A\",\"description\":\"B\","
            "\"native\":\"a.example:1\",\"protocol\":6,\"physics\":9}");
    }
    strcat(many, "]}");
    assert(!parse(many, &list));
    assert(list.count == 2);
    char oversized[BOAT_DIRECTORY_BODY_MAX + 1];
    memset(oversized, ' ', sizeof(oversized));
    assert(!boat_directory_parse(oversized, sizeof(oversized), &list));
    assert(parse("{\"version\":1,\"servers\":[]}", &list));
    puts("boat_directory=ok");
    return 0;
}
