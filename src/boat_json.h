#ifndef PROTEUS_BOAT_JSON_H
#define PROTEUS_BOAT_JSON_H

// Strict, bounded reading of small JSON documents over the vendored jsmn tokenizer: the server
// directory and plugin manifests. Text is printable ASCII; integers are non-negative.
#include <stdbool.h>
#include <stddef.h>

#ifndef BOAT_JSON_IMPLEMENTATION
#define JSMN_HEADER
#endif
#include "../third_party/jsmn/jsmn.h"

// Validate the whole document's syntax (jsmn does not enforce separators), then tokenize it into
// TOKENS. Returns the token count, or -1 unless it is exactly one object without embedded NULs.
int boat_json_parse(const char *json, size_t length, jsmntok_t *tokens, int capacity);
// The token after INDEX and all of its children.
int boat_json_next(const jsmntok_t *tokens, int count, int index);
// The value token of OBJECT's KEY: -1 when missing, -2 when duplicated or malformed.
int boat_json_member(
    const char *json, const jsmntok_t *tokens, int count, int object, const char *key);
// A non-empty printable-ASCII string, unescaped into OUT (\u00XX only).
bool boat_json_text(const char *json, const jsmntok_t *t, char *out, size_t capacity);
// A non-negative integer of at most nine digits.
bool boat_json_int(const char *json, const jsmntok_t *t, int *out);

#endif
