#include "platform/boat_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

bool boat_file_data_directory(char *path, size_t size)
{
    const char *base = NULL, *suffix = NULL;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    suffix = "/Proteus";
#elif defined(__APPLE__)
    base = getenv("HOME");
    suffix = "/Library/Application Support/Proteus";
#else
    base = getenv("XDG_DATA_HOME");
    suffix = "/proteus";
    if (!base || !base[0]) {
        base = getenv("HOME");
        suffix = "/.local/share/proteus";
    }
#endif
    if (!base || !base[0])
        return false;
    int n = snprintf(path, size, "%s%s", base, suffix);
    return n >= 0 && (size_t)n < size;
}

bool boat_file_make_directory(const char *path)
{
#ifdef _WIN32
    if (CreateDirectoryA(path, NULL))
        return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
#else
    if (mkdir(path, 0755) == 0)
        return true;
    struct stat info;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool boat_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    fclose(file);
    return true;
}

static bool read_file(
    const char *path, unsigned char *out, size_t capacity, size_t *size, bool whole)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    size_t got = fread(out, 1, capacity, file);
    bool ok = !ferror(file);
    if (ok && whole) {
        unsigned char extra;
        ok = fread(&extra, 1, 1, file) == 0 && !ferror(file); // Anything more is too large.
    }
    fclose(file);
    if (ok)
        *size = got;
    return ok;
}

bool boat_file_read(const char *path, unsigned char *out, size_t capacity, size_t *size)
{
    return read_file(path, out, capacity, size, true);
}

bool boat_file_read_prefix(const char *path, unsigned char *out, size_t capacity, size_t *size)
{
    return read_file(path, out, capacity, size, false);
}

static bool move_file(const char *from, const char *to)
{
#ifdef _WIN32
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static bool suffixed(char *out, size_t capacity, const char *path, const char *suffix)
{
    int n = snprintf(out, capacity, "%s%s", path, suffix);
    return n >= 0 && (size_t)n < capacity;
}

bool boat_file_write_atomic(const char *path, const void *bytes, size_t size)
{
    char temporary[1100], backup[1100];
    if (!suffixed(temporary, sizeof(temporary), path, ".tmp")
        || !suffixed(backup, sizeof(backup), path, ".bak"))
        return false;
    FILE *file = fopen(temporary, "wb");
    if (!file)
        return false;
    bool ok = fwrite(bytes, 1, size, file) == size;
    ok = fflush(file) == 0 && ok;
    ok = fclose(file) == 0 && ok;
    bool had = ok && boat_file_exists(path);
    if (ok && had)
        ok = move_file(path, backup);
    if (ok) {
        ok = move_file(temporary, path);
        if (!ok && had)
            move_file(backup, path); // Put the previous file back.
    }
    if (!ok)
        remove(temporary);
    return ok;
}

bool boat_file_set_aside(const char *path, const char *suffix)
{
    char aside[1100];
    return boat_file_exists(path) && suffixed(aside, sizeof(aside), path, suffix)
        && move_file(path, aside);
}

bool boat_file_remove_save(const char *path)
{
    char backup[1100];
    if (!suffixed(backup, sizeof(backup), path, ".bak"))
        return false;
    bool ok = true;
    if (boat_file_exists(path))
        ok = remove(path) == 0;
    if (boat_file_exists(backup))
        ok = remove(backup) == 0 && ok;
    return ok;
}

static bool listed(const char *name, const char *prefix, const char *suffix)
{
    size_t length = strlen(name), head = strlen(prefix), tail = strlen(suffix);
    return length < BOAT_FILE_NAME && length >= head + tail && !strncmp(name, prefix, head)
        && !strcmp(name + length - tail, suffix);
}

static void insert_sorted(char names[][BOAT_FILE_NAME], unsigned count, const char *name)
{
    unsigned at = count;
    while (at && strcmp(names[at - 1], name) > 0) {
        memcpy(names[at], names[at - 1], BOAT_FILE_NAME);
        --at;
    }
    snprintf(names[at], BOAT_FILE_NAME, "%s", name);
}

unsigned boat_file_list(const char *directory, const char *prefix, const char *suffix,
    char names[][BOAT_FILE_NAME], unsigned capacity)
{
    unsigned count = 0;
#ifdef _WIN32
    char pattern[1100];
    if (snprintf(pattern, sizeof(pattern), "%s/*", directory) >= (int)sizeof(pattern))
        return 0;
    WIN32_FIND_DATAA entry;
    HANDLE found = FindFirstFileA(pattern, &entry);
    if (found == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (count < capacity && listed(entry.cFileName, prefix, suffix))
            insert_sorted(names, count++, entry.cFileName);
    } while (FindNextFileA(found, &entry));
    FindClose(found);
#else
    DIR *dir = opendir(directory);
    if (!dir)
        return 0;
    for (struct dirent *entry; count < capacity && (entry = readdir(dir));)
        if (listed(entry->d_name, prefix, suffix))
            insert_sorted(names, count++, entry->d_name);
    closedir(dir);
#endif
    return count;
}
