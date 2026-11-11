#ifndef BOAT_DIRECTORY_H
#define BOAT_DIRECTORY_H
#include <stdbool.h>
#include <stddef.h>

enum { BOAT_DIRECTORY_MAX = 8, BOAT_DIRECTORY_BODY_MAX = 16384 };
typedef struct {
    char name[25], description[65], address[64];
    int protocol, physics;
} BoatDirectoryEntry;
typedef struct {
    BoatDirectoryEntry entries[BOAT_DIRECTORY_MAX];
    unsigned count;
} BoatDirectoryList;
typedef struct BoatDirectory BoatDirectory;

// Parse into a temporary list; failure never changes OUT.
bool boat_directory_parse(const char *json, size_t length, BoatDirectoryList *out);
BoatDirectory *boat_directory_create(void);
void boat_directory_refresh(BoatDirectory *directory);
void boat_directory_poll(BoatDirectory *directory);
const BoatDirectoryList *boat_directory_list(const BoatDirectory *directory);
const char *boat_directory_status(const BoatDirectory *directory);
void boat_directory_destroy(BoatDirectory *directory);
#endif
