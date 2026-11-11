#include "cli_usage.h"

void cli_usage_section(FILE *out, const char *title) { fprintf(out, "\n%s\n", title); }

void cli_usage_line(FILE *out, const char *option, const char *description)
{
    fprintf(out, "  %-29s %s\n", option, description);
}
