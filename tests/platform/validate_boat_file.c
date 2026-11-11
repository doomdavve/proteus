#include "platform/boat_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "file check failed at %d: %s\n", __LINE__, #condition);                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void put(const char *path, const char *text)
{
    size_t size = strlen(text);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fwrite(text, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static size_t get(const char *path, char *out, size_t cap)
{
    size_t size = 0;
    CHECK(boat_file_read(path, (unsigned char *)out, cap, &size));
    CHECK(size < cap);
    out[size] = '\0';
    return size;
}

// A "cargo-" name of 130 characters, which is longer than BOAT_FILE_NAME and must be ignored.
static void long_name(char *out)
{
    memcpy(out, "cargo-", 6);
    memset(out + 6, 'x', 120);
    memcpy(out + 126, ".sav", 4);
    out[130] = '\0';
}

static void clean_leftovers(const char *dir)
{
    char path[1024];
    const char *files[] = {
        "a.sav",
        "a.sav.tmp",
        "a.sav.bak",
        "b.sav",
        "b.sav.tmp",
        "b.sav.bak",
        "big.bin",
        "missing.bin",
        "d.sav",
        "d.sav.rejected",
        "e.sav",
        "e.sav.bak",
        "sub",
    };
    for (size_t i = 0; i < sizeof files / sizeof *files; ++i) {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        (void)remove(path);
    }
    char list[1024];
    snprintf(list, sizeof list, "%s/list", dir);
    for (int i = 0; i < 30; ++i) {
        snprintf(path, sizeof path, "%s/cargo-%02d.sav", list, i);
        (void)remove(path);
    }
    const char *extras[] = {
        "cargo-02.sav.bak",
        "cargo-02.sav.tmp",
        "cargo-02.sav.rejected",
        "title.txt",
        "notcargo-04.sav",
    };
    for (size_t i = 0; i < sizeof extras / sizeof *extras; ++i) {
        snprintf(path, sizeof path, "%s/%s", list, extras[i]);
        (void)remove(path);
    }
    char name[131];
    long_name(name);
    snprintf(path, sizeof path, "%s/%s", list, name);
    (void)remove(path);
    (void)remove(list); // Empty or absent, like the nested subdirectory.
}

static void check_data_directory(void)
{
    char path[512];
    CHECK(boat_file_data_directory(path, sizeof path));
    CHECK(path[0] != '\0');
    char tiny[3];
    CHECK(!boat_file_data_directory(tiny, sizeof tiny));
}

static void check_make_directory(const char *dir)
{
    char path[1024];
    CHECK(boat_file_make_directory(dir));
    snprintf(path, sizeof path, "%s/sub", dir);
    CHECK(boat_file_make_directory(path));
    snprintf(path, sizeof path, "%s/missing/deeper", dir);
    CHECK(!boat_file_make_directory(path));
}

static void check_write(const char *dir)
{
    char save[1024], temporary[1024], backup[1024], text[16];
    snprintf(save, sizeof save, "%s/a.sav", dir);
    snprintf(temporary, sizeof temporary, "%s/a.sav.tmp", dir);
    snprintf(backup, sizeof backup, "%s/a.sav.bak", dir);
    CHECK(boat_file_write_atomic(save, "one", 3));
    CHECK(get(save, text, sizeof text) == 3);
    CHECK(!strcmp(text, "one"));
    CHECK(!boat_file_exists(temporary));
    CHECK(!boat_file_exists(backup));
}

static void check_replace(const char *dir)
{
    char save[1024], temporary[1024], backup[1024], text[16];
    snprintf(save, sizeof save, "%s/a.sav", dir);
    snprintf(temporary, sizeof temporary, "%s/a.sav.tmp", dir);
    snprintf(backup, sizeof backup, "%s/a.sav.bak", dir);
    CHECK(boat_file_write_atomic(save, "two", 3));
    CHECK(get(save, text, sizeof text) == 3 && !strcmp(text, "two"));
    CHECK(get(backup, text, sizeof text) == 3 && !strcmp(text, "one"));
    CHECK(!boat_file_exists(temporary));
    CHECK(boat_file_write_atomic(save, "three", 5));
    CHECK(get(save, text, sizeof text) == 5 && !strcmp(text, "three"));
    CHECK(get(backup, text, sizeof text) == 3 && !strcmp(text, "two"));
    CHECK(!boat_file_exists(temporary));
}

static void check_failed_write(const char *dir)
{
    char save[1024], temporary[1024], backup[1024], text[16];
    snprintf(save, sizeof save, "%s/b.sav", dir);
    snprintf(temporary, sizeof temporary, "%s/b.sav.tmp", dir);
    snprintf(backup, sizeof backup, "%s/b.sav.bak", dir);
    CHECK(boat_file_make_directory(temporary));
    put(save, "keep");
    CHECK(!boat_file_write_atomic(save, "lost", 4));
    CHECK(get(save, text, sizeof text) == 4);
    CHECK(!strcmp(text, "keep"));
    CHECK(!boat_file_exists(backup));
    (void)remove(temporary);
}

static void check_missing_directory(const char *dir)
{
    char save[1024];
    snprintf(save, sizeof save, "%s/nodir/c.sav", dir);
    CHECK(!boat_file_write_atomic(save, "x", 1));
    CHECK(!boat_file_exists(save));
}

static void check_read_bounds(const char *dir)
{
    char path[1024], text[16], prefix[16];
    size_t size = 0;
    snprintf(path, sizeof path, "%s/big.bin", dir);
    put(path, "0123456789");
    CHECK(boat_file_read(path, (unsigned char *)text, 10, &size));
    CHECK(size == 10 && !memcmp(text, "0123456789", 10));
    CHECK(!boat_file_read(path, (unsigned char *)text, 9, &size));
    size = 0;
    CHECK(boat_file_read_prefix(path, (unsigned char *)prefix, 4, &size));
    CHECK(size == 4 && !memcmp(prefix, "0123", 4));
    snprintf(path, sizeof path, "%s/missing.bin", dir);
    CHECK(!boat_file_read(path, (unsigned char *)text, sizeof text, &size));
}

static void check_set_aside(const char *dir)
{
    char save[1024], aside[1024], text[16];
    snprintf(save, sizeof save, "%s/d.sav", dir);
    snprintf(aside, sizeof aside, "%s/d.sav.rejected", dir);
    CHECK(boat_file_write_atomic(save, "d1", 2));
    CHECK(boat_file_set_aside(save, ".rejected"));
    CHECK(!boat_file_exists(save));
    CHECK(get(aside, text, sizeof text) == 2 && !strcmp(text, "d1"));
    CHECK(!boat_file_set_aside(save, ".rejected"));
    put(save, "d2");
    CHECK(boat_file_set_aside(save, ".rejected"));
    CHECK(get(aside, text, sizeof text) == 2 && !strcmp(text, "d2"));
}

static void check_remove_save(const char *dir)
{
    char save[1024], backup[1024];
    snprintf(save, sizeof save, "%s/e.sav", dir);
    snprintf(backup, sizeof backup, "%s/e.sav.bak", dir);
    CHECK(boat_file_write_atomic(save, "1", 1));
    CHECK(boat_file_write_atomic(save, "2", 1));
    CHECK(boat_file_exists(save));
    CHECK(boat_file_exists(backup));
    CHECK(boat_file_remove_save(save));
    CHECK(!boat_file_exists(save));
    CHECK(!boat_file_exists(backup));
    CHECK(boat_file_remove_save(save));
}

static void check_list(const char *dir)
{
    char list[1024], path[1024], name[131];
    char names[16][BOAT_FILE_NAME];
    snprintf(list, sizeof list, "%s/list", dir);
    for (int i = 0; i < 20; ++i) {
        snprintf(path, sizeof path, "%s/cargo-%02d.sav", list, i);
        (void)remove(path);
    }
    CHECK(boat_file_make_directory(list));
    snprintf(path, sizeof path, "%s/cargo-03.sav", list);
    put(path, "3");
    snprintf(path, sizeof path, "%s/cargo-01.sav", list);
    put(path, "1");
    snprintf(path, sizeof path, "%s/cargo-02.sav", list);
    put(path, "2");
    snprintf(path, sizeof path, "%s/cargo-02.sav.bak", list);
    put(path, "backup");
    snprintf(path, sizeof path, "%s/cargo-02.sav.tmp", list);
    put(path, "temporary");
    snprintf(path, sizeof path, "%s/cargo-02.sav.rejected", list);
    put(path, "rejected");
    snprintf(path, sizeof path, "%s/title.txt", list);
    put(path, "title");
    snprintf(path, sizeof path, "%s/notcargo-04.sav", list);
    put(path, "other");
    long_name(name);
    CHECK(strlen(name) == 130);
    snprintf(path, sizeof path, "%s/%s", list, name);
    put(path, "long");

    unsigned count = boat_file_list(list, "cargo-", ".sav", names, 16);
    CHECK(count == 3);
    CHECK(!strcmp(names[0], "cargo-01.sav"));
    CHECK(!strcmp(names[1], "cargo-02.sav"));
    CHECK(!strcmp(names[2], "cargo-03.sav"));

    for (int i = 10; i < 30; ++i) {
        snprintf(path, sizeof path, "%s/cargo-%02d.sav", list, i);
        put(path, "x");
    }
    CHECK(boat_file_list(list, "cargo-", ".sav", names, 16) == 16);
    CHECK(boat_file_list(list, "cargo-", ".sav", names, 4) == 4);
    snprintf(path, sizeof path, "%s/nolist", dir);
    CHECK(boat_file_list(path, "cargo-", ".sav", names, 16) == 0);
}

static void clean_list(const char *dir)
{
    char list[1024], path[1024], name[131];
    snprintf(list, sizeof list, "%s/list", dir);
    for (int i = 0; i < 30; ++i) {
        snprintf(path, sizeof path, "%s/cargo-%02d.sav", list, i);
        (void)remove(path);
    }
    const char *extras[] = {
        "cargo-02.sav.bak",
        "cargo-02.sav.tmp",
        "cargo-02.sav.rejected",
        "title.txt",
        "notcargo-04.sav",
    };
    for (size_t i = 0; i < sizeof extras / sizeof *extras; ++i) {
        snprintf(path, sizeof path, "%s/%s", list, extras[i]);
        (void)remove(path);
    }
    long_name(name);
    snprintf(path, sizeof path, "%s/%s", list, name);
    (void)remove(path);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s DIR\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1];
    CHECK(boat_file_make_directory(dir));
    clean_leftovers(dir);
    check_data_directory();
    check_make_directory(dir);
    check_write(dir);
    check_replace(dir);
    check_failed_write(dir);
    check_missing_directory(dir);
    check_read_bounds(dir);
    check_set_aside(dir);
    check_remove_save(dir);
    check_list(dir);
    clean_list(dir);
    puts("file ok");
    return 0;
}
