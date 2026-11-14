// H3: replay and deterministic-mutation driver for the fuzz harnesses (see fuzz_driver.h).
//
//   fuzz_x [--runs N] [--seed S] [--write-seeds DIR] [CORPUS_FILE...]
//
// Runs the harness's seeds and any corpus files unchanged, then N inputs mutated from them
// with a fixed pseudo-random sequence. Never reads the clock; a failure is an ordinary crash,
// abort or sanitizer report with the iteration printed first.
#include "fuzz/fuzz_driver.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT 2048
#define MAX_CORPUS 256

static uint8_t *corpus[MAX_CORPUS];
static size_t corpus_size[MAX_CORPUS];
static size_t corpus_count;
static const char *write_dir;
static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void add(const uint8_t *data, size_t size)
{
    if (corpus_count == MAX_CORPUS || size > MAX_INPUT)
        return;
    corpus[corpus_count] = malloc(size ? size : 1);
    if (!corpus[corpus_count])
        return;
    if (size)
        memcpy(corpus[corpus_count], data, size);
    corpus_size[corpus_count++] = size;
    if (write_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/seed-%03zu", write_dir, corpus_count - 1);
        FILE *file = fopen(path, "wb");
        if (file) {
            fwrite(data, 1, size, file);
            fclose(file);
        }
    }
}

static bool load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    uint8_t buffer[MAX_INPUT];
    size_t size = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    add(buffer, size);
    return true;
}

static size_t mutate(uint8_t *buffer, size_t size)
{
    static const uint8_t edge[] = { 0x00, 0x01, 0x7f, 0x80, 0xff };
    for (unsigned ops = 1 + (unsigned)(rng() % 4); ops; --ops) {
        switch (rng() % 7) {
        case 0:
            if (size)
                buffer[rng() % size] ^= (uint8_t)(1u << (rng() % 8));
            break;
        case 1:
            if (size)
                buffer[rng() % size] = (uint8_t)rng();
            break;
        case 2:
            if (size)
                buffer[rng() % size] = edge[rng() % sizeof(edge)];
            break;
        case 3:
            if (size < MAX_INPUT) {
                size_t at = rng() % (size + 1);
                memmove(buffer + at + 1, buffer + at, size - at);
                buffer[at] = (uint8_t)rng();
                ++size;
            }
            break;
        case 4:
            if (size) {
                size_t at = rng() % size;
                memmove(buffer + at, buffer + at + 1, size - at - 1);
                --size;
            }
            break;
        case 5:
            if (size)
                size = rng() % (size + 1);
            break;
        default:
            if (size && size * 2 <= MAX_INPUT) {
                size_t from = rng() % size, length = 1 + rng() % (size - from);
                memmove(buffer + size, buffer + from, length);
                size += length;
            }
            break;
        }
    }
    return size;
}

int main(int argc, char **argv)
{
    unsigned long runs = 1000;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            runs = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            rng_state ^= strtoull(argv[++i], NULL, 10) * 0xff51afd7ed558ccdull;
            if (!rng_state)
                rng_state = 1;
        } else if (strcmp(argv[i], "--write-seeds") == 0 && i + 1 < argc) {
            write_dir = argv[++i];
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "fuzz_driver: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    fuzz_seeds(add);
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-' && !load(argv[i])) {
            fprintf(stderr, "fuzz_driver: cannot read %s\n", argv[i]);
            return 2;
        }
        if (strncmp(argv[i], "--runs", 6) == 0 || strncmp(argv[i], "--seed", 6) == 0
            || strncmp(argv[i], "--write", 7) == 0)
            ++i;
    }
    write_dir = NULL;
    for (size_t i = 0; i < corpus_count; ++i)
        LLVMFuzzerTestOneInput(corpus[i], corpus_size[i]);
    uint8_t buffer[MAX_INPUT * 2];
    for (unsigned long run = 0; run < runs; ++run) {
        size_t pick = corpus_count ? rng() % corpus_count : 0;
        size_t size = corpus_count ? corpus_size[pick] : 0;
        if (size)
            memcpy(buffer, corpus[pick], size);
        size = mutate(buffer, size);
        if (size > MAX_INPUT)
            size = MAX_INPUT;
        LLVMFuzzerTestOneInput(buffer, size);
    }
    printf("fuzz_driver: corpus=%zu runs=%lu ok\n", corpus_count, runs);
    return 0;
}
