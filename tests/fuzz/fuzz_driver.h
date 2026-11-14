#ifndef PROTEUS_FUZZ_DRIVER_H
#define PROTEUS_FUZZ_DRIVER_H

#include <stddef.h>
#include <stdint.h>

// H3: every harness is a libFuzzer entry point, `LLVMFuzzerTestOneInput`. A libFuzzer build
// (PROTEUS_FUZZ=ON with Clang) links it directly. Every other build links fuzz_driver.c, which
// replays a corpus and then a fixed, seeded number of mutations, so the same harness is a
// deterministic regression test under CTest, with or without the sanitizers.
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// Each harness lists its starting inputs (valid encodings) through ADD.
typedef void (*FuzzAdd)(const uint8_t *data, size_t size);
void fuzz_seeds(FuzzAdd add);

#endif
