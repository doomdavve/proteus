#ifndef PROTEUS_BOAT_FILE_H
#define PROTEUS_BOAT_FILE_H

// SV1d: the small file helpers for user data (saves, personal bests). No raylib, and not part
// of proteus_boat_core: the core and the net library do no file I/O.

#include <stdbool.h>
#include <stddef.h>

#define BOAT_FILE_NAME 128 // One listed name, including the terminator.

// The game's per-user data directory (the path boat_replay used to build).
bool boat_file_data_directory(char *path, size_t size);
// Create one directory level. An existing directory is fine.
bool boat_file_make_directory(const char *path);
bool boat_file_exists(const char *path);
// Read a whole file of at most CAPACITY bytes. A larger file fails.
bool boat_file_read(const char *path, unsigned char *out, size_t capacity, size_t *size);
// Read up to CAPACITY leading bytes and report how many came back; a larger file is not an error.
bool boat_file_read_prefix(const char *path, unsigned char *out, size_t capacity, size_t *size);
// Write PATH.tmp, move a current PATH to PATH.bak, then move the temporary file into place.
// A failure leaves the previous PATH (or, mid-swap, PATH.bak) and removes the temporary file.
bool boat_file_write_atomic(const char *path, const void *bytes, size_t size);
// Rename PATH to PATH+SUFFIX, replacing an older one. False when PATH is not there.
bool boat_file_set_aside(const char *path, const char *suffix);
// Remove PATH and PATH.bak. True when neither remains.
bool boat_file_remove_save(const char *path);
// Names in DIRECTORY that start with PREFIX and end with SUFFIX, sorted, at most CAPACITY of
// them; longer names and other files are ignored. A missing directory lists nothing.
unsigned boat_file_list(const char *directory, const char *prefix, const char *suffix,
    char names[][BOAT_FILE_NAME], unsigned capacity);

#endif
