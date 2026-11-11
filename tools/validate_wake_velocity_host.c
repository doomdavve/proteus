// The room command line `wake_velocity --host` gives its `--serve` child (src/room/boat_server.h).
#include "boat_test_cases.h"
#include "wake_velocity_host.h"

#include <stdio.h>
#include <string.h>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// ARGV as one space-separated line.
static const char *joined(const char *const *argv)
{
    static char line[512];
    line[0] = '\0';
    for (unsigned i = 0; argv[i]; ++i) {
        if (i)
            strcat(line, " ");
        strcat(line, argv[i]);
    }
    return line;
}

static int modes(void)
{
    char port[8];
    const char *argv[NET_HOST_ARGUMENTS];
    NetHostOptions o = { .mode = "free", .location = "basin", .port = 24680 };
    CHECK(net_host_arguments(&o, port, argv) == 10);
    CHECK(!strcmp(joined(argv),
        "--serve --bind 0.0.0.0 --port 24680 --mode free --location basin --exit-with-stdin"));
    o = (NetHostOptions) { .mode = "race", .location = "basin", .port = 24681 };
    net_host_arguments(&o, port, argv);
    CHECK(
        !strcmp(joined(argv), "--serve --bind 0.0.0.0 --port 24681 --mode race --exit-with-stdin"));
    // The shelter hunt is a free room on outer with its package, whatever the location says.
    o = (NetHostOptions) { .mode = "hunt", .location = "basin", .port = 24682 };
    net_host_arguments(&o, port, argv);
    CHECK(!strcmp(joined(argv),
        "--serve --bind 0.0.0.0 --port 24682 --mode free --location outer"
        " --package proteus.outer-hunt --exit-with-stdin"));
    return 0;
}

// The longest command line (hunt with waves) fits with its terminator.
static int longest(void)
{
    char port[8];
    const char *argv[NET_HOST_ARGUMENTS + 1];
    argv[NET_HOST_ARGUMENTS] = "sentinel";
    NetHostOptions o = { .mode = "hunt", .location = "outer", .waves = true, .port = 65535 };
    unsigned n = net_host_arguments(&o, port, argv);
    CHECK(n < NET_HOST_ARGUMENTS && argv[n] == NULL);
    CHECK(!strcmp(argv[4], "65535") && !strcmp(argv[n - 2], "--waves"));
    CHECK(!strcmp(argv[NET_HOST_ARGUMENTS], "sentinel"));
    return 0;
}

static const TestCase cases[] = {
    TEST_CASE(modes),
    TEST_CASE(longest),
};

int main(int argc, char **argv)
{
    return test_cases_main(cases, TEST_CASE_COUNT(cases), argc, argv);
}
