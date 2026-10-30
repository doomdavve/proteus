#ifndef PROTEUS_CLI_USAGE_H
#define PROTEUS_CLI_USAGE_H

#include <stdio.h>

// Shared layout for the command-line tools: aligned options under a section heading.
void cli_usage_section(FILE *out, const char *title);
void cli_usage_line(FILE *out, const char *option, const char *description);

#endif
